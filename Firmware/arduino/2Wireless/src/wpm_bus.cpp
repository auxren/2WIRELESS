#include "wpm_bus.h"

#include <stdlib.h>
#include <string.h>

#include "wpm_bus_scan.h"
#include "wpm_json.h"

// Defined in 2Wireless.ino.
extern bool v2version;
bool wpm_bus_master_query(uint8_t addr, bool v2);
void writeHeader(WiFiClient client, String statusString, String contentType, int contentLength);
bool wpm_bus_master_polldone();
uint8_t framRead(int addr);

// Stock display cache written by setDisplayMessage(0, ...) on every 0x13 frame: [addr][7 chars].
static const int DISPLAY_CACHE_ADDR = 0x02C0FE;

static const uint32_t BUS_QUIET_MS = 50;

static WpmBusScan scan;
// ISR-written, main-loop-read (32-bit / byte accesses are atomic on the SAMD21).
static volatile uint32_t last_frame_ms = 0;
static volatile bool frame_seen = false;
// v2version is recomputed per HTTP request (any /v2/ request flips it), so latch it at scan start.
static bool scan_v2 = false;

// Not reached while a /getpresets capture caches I2C to FRAM or while i2c_guard is set, so
// bus_quiet can read true during a capture.
void wpm_bus_note_frame() {
  last_frame_ms = millis();
  frame_seen = true;
}

void wpm_bus_on_version_frame(uint8_t addr, const uint8_t ver[7]) {
  scan.on_version_frame(addr, ver, 7);
}

void wpm_bus_on_query_reply(uint8_t addr) { scan.on_query_reply(addr); }

bool wpm_bus_scan_active() { return scan.state() == WPM_SCAN_SCANNING; }

void wpm_bus_loop() {
  if (scan.step(millis()) == WPM_SCAN_ACT_QUERY) {
    if (!wpm_bus_master_query(scan.current_addr(), scan_v2)) scan.query_failed(millis());
  }
}

// Value after "key=" within the query part of the URL; false when the key is absent.
static bool queryValue(const String& url, const char* key, String* out) {
  int q = url.indexOf('?');
  if (q < 0) return false;
  int k = url.indexOf(key, q);
  if (k < 0) return false;
  int s = k + strlen(key);
  int e = s;
  while (e < (int)url.length() && url[e] != '&' && url[e] != ' ') e++;
  *out = url.substring(s, e);
  return true;
}

static int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Exactly two hex digits, either case. Absent key -> def. Malformed -> false.
static bool parseAddr(const String& url, const char* key, uint8_t def, uint8_t* out) {
  String v;
  if (!queryValue(url, key, &v)) {
    *out = def;
    return true;
  }
  if (v.length() != 2) return false;
  int hi = hexNibble(v[0]), lo = hexNibble(v[1]);
  if (hi < 0 || lo < 0) return false;
  *out = (uint8_t)(hi * 16 + lo);
  return true;
}

void wpm_bus_handle_scan(WiFiClient& client, const String& urlString) {
  if (v2version) {
    wpm_send_json(client, wpm_json_err("unsupported"));
    return;
  }
  if (wpm_bus_scan_active()) {
    scan.set_error(WPM_BUS_ERR_BUSY);
    wpm_send_json(client, wpm_json_err("busy"));
    return;
  }
  uint8_t first = WPM_SCAN_ADDR_MIN, last = WPM_SCAN_ADDR_MAX;
  uint32_t window = WPM_SCAN_REPLY_MS;
  bool ok = parseAddr(urlString, "first=", WPM_SCAN_ADDR_MIN, &first) &&
            parseAddr(urlString, "last=", WPM_SCAN_ADDR_MAX, &last);
  if (ok) {
    String w;
    if (queryValue(urlString, "window=", &w)) {
      if (w.length() == 0 || w.length() > 6) {
        ok = false;
      } else {
        for (unsigned i = 0; i < w.length(); i++)
          if (w[i] < '0' || w[i] > '9') ok = false;
        if (ok) {
          window = (uint32_t)atol(w.c_str());
          if (window < 100) window = 100;
          if (window > 2000) window = 2000;
        }
      }
    }
  }
  if (!ok) {
    scan.set_error(WPM_BUS_ERR_ARGS);
    wpm_send_json(client, wpm_json_err("args"));
    return;
  }
  switch (scan.start(first, last, millis(), window)) {
    case WPM_BUS_ERR_NONE:
      scan_v2 = v2version;
      wpm_send_json(client, "{\"ok\":true}");
      break;
    case WPM_BUS_ERR_BUSY: wpm_send_json(client, wpm_json_err("busy")); break;
    default: wpm_send_json(client, wpm_json_err("args")); break;
  }
}

// Streamed in chunks (chunked transfer, like /getpresets): WiFi101 rejects a single send over
// 1400 bytes, which a full sweep would exceed. Heap use stays under ~500 bytes.
static void sendChunk(WiFiClient& client, String& chunk) {
  client.println(chunk.length(), HEX);
  client.println(chunk);
  chunk = "";
}

void wpm_bus_handle_scan_status(WiFiClient& client) {
  writeHeader(client, "HTTP/1.1 200 OK", "Content-type:application/json", -1);
  char buf[96];
  wpm_scan_status_head_json(scan.state(), scan.idx(), scan.count(), buf, sizeof buf);
  String chunk = buf;
  size_t n = scan.count();
  for (size_t i = 0; i < n; i++) {
    if (i) chunk += ",";
    char e[112];
    WpmScanFound f = scan.found()[i];
    if (!f.ver_valid) {  // fall back to the version the stock display cache last saw for this module
      uint8_t c[1 + WPM_SCAN_VER_LEN];
      bool any = false;
      for (size_t k = 0; k < sizeof c; k++) c[k] = framRead(DISPLAY_CACHE_ADDR + (int)k);
      for (size_t k = 1; k < sizeof c; k++)
        if (c[k]) any = true;
      if (c[0] == f.addr && any) {
        for (size_t k = 0; k < WPM_SCAN_VER_LEN; k++) f.ver[k] = (char)c[1 + k];
        f.ver_valid = true;
        f.ver_src = WPM_VER_SRC_CACHE;
      }
    }
    if (wpm_scan_entry_json(f, e, sizeof e)) chunk += e;
    if (chunk.length() >= 400) sendChunk(client, chunk);
  }
  chunk += "]}";
  sendChunk(client, chunk);
  client.println("0");
  client.println();
}

// last_error mirrors the scan's last_error(): start() sets it (busy/args) and clears it on
// success, query_failed() sets 4, and /bus/polldone overrides it with 4 via set_error().
void wpm_bus_handle_status(WiFiClient& client) {
  bool seen = frame_seen;
  bool quiet = !seen || (uint32_t)(millis() - last_frame_ms) >= BUS_QUIET_MS;
  int dialect = seen ? (v2version ? 2 : 1) : 0;
  String body = "{\"wpm_present\":true,\"bus_quiet\":";
  body += quiet ? "true" : "false";
  body += ",\"dialect\":";
  body += dialect;
  body += ",\"last_error\":";
  body += (int)scan.last_error();
  body += "}";
  wpm_send_json(client, body);
}

void wpm_bus_handle_polldone(WiFiClient& client) {
  if (wpm_bus_scan_active()) {
    wpm_send_json(client, wpm_json_err("busy"));
    return;
  }
  if (!wpm_bus_master_polldone()) {
    scan.set_error(WPM_BUS_ERR_SEND_TIMEOUT);
    wpm_send_json(client, wpm_json_err("send_timeout"));
    return;
  }
  wpm_send_json(client, "{\"ok\":true}");
}
