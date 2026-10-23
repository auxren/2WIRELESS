#include "wpm_dump_dev.h"

#include <stdio.h>
#include <string.h>

#include "WireByte.h"
#include "wpm_bus.h"
#include "wpm_dump.h"
#include "wpm_json.h"

// Defined in 2Wireless.ino.
bool wpm_dump_master_backup(uint8_t addr);
void framWrite(int addr, uint8_t value);
void wpm_stock_decode_frame(const uint8_t* f, int n);  // the stock >4-byte general-call decoder

static const uint32_t READ_IDLE_MS = 50;    // no SEND while a module read is this recent (as the scan)
static const uint32_t READ_BUSY_MS = 1000;  // /dump refuses within this of a module read
// The card's I2C slave address: 0x50 | CARD_ADDRESS, and CARD_ADDRESS is 0x00 in the sketch.
static const uint8_t CARD_I2C_ADDR = 0x50;

static WpmDumpJob job;
// Diagnostic: first 8 matched addresses seen by the ISR handler in this job. Cleared by the main
// loop right after a successful start (no ISR traffic before the BACKUP is sent).
static const uint8_t ADDR_LOG_MAX = 8;
static volatile uint8_t addr_log[ADDR_LOG_MAX];
static volatile uint8_t addr_log_n = 0;

bool wpm_dump_active() {
  WpmDumpState s = job.state();
  return s == WPM_DUMP_SENDING || s == WPM_DUMP_WAITING || s == WPM_DUMP_TRANSFERRING;
}

void wpm_dump_loop() {
  // Modules read presets from the WPM with interrupts off and no timeouts: do not master the bus
  // right after a read. The hold is capped at the job's first_wait_ms (measured from start): after
  // that the BACKUP is reported as not sent (error 4) instead of holding forever.
  if (job.state() == WPM_DUMP_SENDING && wpm_bus_read_recent(READ_IDLE_MS)) {
    if (job.elapsed_ms(millis()) >= job.first_wait_ms()) job.send_failed(millis());
    return;
  }
  if (job.step(millis()) == WPM_DUMP_ACT_SEND_BACKUP) {
    if (!wpm_dump_master_backup(job.addr())) job.send_failed(millis());
  }
}

// A data line [off_hi][off_lo][data...] of n bytes. drop_past_expected is the shape-parsing
// fallback (no address filter): a line at or beyond the expected length is counted, not written.
static void data_line(const uint8_t* f, int n, bool drop_past_expected) {
  if (n < 3) return;  // too short to be a line
  uint32_t offset = ((uint32_t)f[0] << 8) | f[1];
  uint32_t cnt = (uint32_t)(n - 2);
  if (drop_past_expected && job.expected() != 0 && offset >= job.expected()) {
    job.on_dropped(cnt);
    return;
  }
  uint32_t written = 0;
  for (uint32_t i = 0; i < cnt; i++) {
    uint32_t pos = offset + i;
    // Defensive: a u16 offset plus a 255-byte line stays below WPM_DUMP_SCRATCH_END (0x2BD18), so
    // this never triggers today; it keeps a future wider offset away from the settings and OTA record.
    if (pos >= WPM_DUMP_SCRATCH_END) break;
    framWrite((int)pos, f[2 + i]);
    written++;
  }
  if (written < cnt) job.on_dropped(cnt - written);
  if (written) job.on_data(offset, written);
}

static bool is_complete_frame(const uint8_t* f, int n) { return n == 5 && f[0] == 0x04 && f[3] == 0x0A; }

static void log_addr(uint8_t a) {
  if (addr_log_n < ADDR_LOG_MAX) {
    addr_log[addr_log_n] = a;
    addr_log_n = addr_log_n + 1;
  }
}

// Reads the whole pending transaction (at most 256 bytes, the ring buffer size).
static int drain(uint8_t* f, int cap) {
  int n = 0;
  while (Wire.available() > 0) {
    int b = Wire.read();
    if (n < cap) f[n++] = (uint8_t)b;
  }
  return n;
}

bool wpm_dump_isr_frame(int howMany) {
  (void)howMany;
  uint8_t f[256];
  const uint8_t a = Wire.lastAddress();
#if WPM_DUMP_ADDR_FILTER
  if (a == CARD_I2C_ADDR) {
    int n = drain(f, sizeof f);
    if (n > 0) log_addr(a);
    data_line(f, n, false);
    return true;
  }
  if (a == 0x00 && Wire.available() == 5) {
    // A 5-byte general-call frame: the completion frame, or MIDI clock / a 0x13 display frame
    // that must keep working. Peek is not available, so read it and forward it to the stock decoder.
    int n = drain(f, sizeof f);
    log_addr(a);
    if (is_complete_frame(f, n)) {
      wpm_bus_note_frame();
      job.on_complete_frame(f[2]);
    } else {
      wpm_stock_decode_frame(f, n);
    }
    return true;
  }
  if (Wire.available() > 0) log_addr(a);
  return false;  // any other general-call frame: the stock path reads it unchanged
#else
  int n = drain(f, sizeof f);
  if (n > 0) log_addr(a);
  if (is_complete_frame(f, n)) job.on_complete_frame(f[2]);
  else data_line(f, n, true);
  return true;
#endif
}

// Decimal, 1..9 digits, nothing else.
static bool parse_dec(const char* s, size_t len, uint32_t* out) {
  if (len == 0 || len > 9) return false;
  uint32_t v = 0;
  for (size_t i = 0; i < len; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
    v = v * 10 + (uint32_t)(s[i] - '0');
  }
  *out = v;
  return true;
}

static bool parse_hex2(const char* s, size_t len, uint8_t* out) {
  if (len != 2) return false;
  uint8_t v = 0;
  for (size_t i = 0; i < 2; i++) {
    char c = s[i];
    uint8_t d;
    if (c >= '0' && c <= '9') d = (uint8_t)(c - '0');
    else if (c >= 'a' && c <= 'f') d = (uint8_t)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') d = (uint8_t)(c - 'A' + 10);
    else return false;
    v = (uint8_t)(v * 16 + d);
  }
  *out = v;
  return true;
}

static uint32_t clamp(uint32_t n, uint32_t lo, uint32_t hi) { return n < lo ? lo : (n > hi ? hi : n); }

// q: "addr=28&len=990&..." (no leading '?', NUL terminated). Unknown keys, empty or malformed
// values, and a missing addr fail. Waits are clamped, len is left for start() to range check.
static bool parse_dump_args(const char* q, uint8_t* addr, uint32_t* len, uint32_t* first_wait,
                            uint32_t* silence) {
  bool have_addr = false;
  *len = 0;
  *first_wait = WPM_DUMP_FIRST_WAIT_DEFAULT_MS;
  *silence = WPM_DUMP_SILENCE_DEFAULT_MS;
  while (*q) {
    const char* amp = strchr(q, '&');
    size_t seg = amp ? (size_t)(amp - q) : strlen(q);
    const char* eq = (const char*)memchr(q, '=', seg);
    if (!eq) return false;
    size_t klen = (size_t)(eq - q);
    const char* v = eq + 1;
    size_t vlen = seg - klen - 1;
    uint32_t n;
    if (klen == 4 && strncmp(q, "addr", 4) == 0) {
      if (!parse_hex2(v, vlen, addr)) return false;
      have_addr = true;
    } else if (klen == 3 && strncmp(q, "len", 3) == 0) {
      if (!parse_dec(v, vlen, len)) return false;
    } else if (klen == 13 && strncmp(q, "first_wait_ms", 13) == 0) {
      if (!parse_dec(v, vlen, &n)) return false;
      *first_wait = clamp(n, WPM_DUMP_FIRST_WAIT_MIN_MS, WPM_DUMP_FIRST_WAIT_MAX_MS);
    } else if (klen == 10 && strncmp(q, "silence_ms", 10) == 0) {
      if (!parse_dec(v, vlen, &n)) return false;
      *silence = clamp(n, WPM_DUMP_SILENCE_MIN_MS, WPM_DUMP_SILENCE_MAX_MS);
    } else {
      return false;
    }
    q = amp ? amp + 1 : q + seg;
  }
  return have_addr;
}

void wpm_dump_handle_start(WiFiClient& client, const String& urlString) {
  if (wpm_bus_scan_active() || wpm_dump_active() || wpm_bus_read_recent(READ_BUSY_MS)) {
    wpm_send_json(client, wpm_json_err("busy"));
    return;
  }
  // Query text: after '?' up to the space before " HTTP".
  String query = "";
  int qpos = urlString.indexOf('?');
  if (qpos >= 0) {
    int e = urlString.indexOf(' ', qpos);
    query = urlString.substring(qpos + 1, e < 0 ? urlString.length() : e);
  }
  uint8_t addr = 0;
  uint32_t len, first_wait, silence;
  if (!parse_dump_args(query.c_str(), &addr, &len, &first_wait, &silence)) {
    wpm_send_json(client, wpm_json_err("args"));
    return;
  }
  switch (job.start(addr, len, first_wait, silence, millis())) {
    case WPM_BUS_ERR_NONE:
      addr_log_n = 0;  // no ISR traffic yet: the BACKUP goes out from the next loop pass
      wpm_send_json(client, "{\"ok\":true}");
      break;
    case WPM_BUS_ERR_BUSY: wpm_send_json(client, wpm_json_err("busy")); break;
    default: wpm_send_json(client, wpm_json_err("args")); break;
  }
}

void wpm_dump_handle_status(WiFiClient& client) {
  char buf[256];
  size_t n = wpm_dump_status_json(job, millis(), buf, sizeof buf);
  if (n < 2) {
    wpm_send_json(client, wpm_json_err("args"));
    return;
  }
  buf[n - 1] = '\0';  // drop the closing '}'
  String body = buf;
  body += ",\"addr_log\":\"";
  uint8_t cnt = addr_log_n;
  if (cnt > ADDR_LOG_MAX) cnt = ADDR_LOG_MAX;
  for (uint8_t i = 0; i < cnt; i++) {
    char h[4];
    snprintf(h, sizeof h, i ? " %02x" : "%02x", (unsigned)addr_log[i]);
    body += h;
  }
  body += "\"}";
  wpm_send_json(client, body);
}

void wpm_dump_handle_abort(WiFiClient& client) {
  job.abort(millis());
  wpm_send_json(client, "{\"ok\":true}");
}
