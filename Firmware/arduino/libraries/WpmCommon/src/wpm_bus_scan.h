#pragma once
// I2C bus scan state machine (one QUERY per address, listen for the 0x13 version frame) and
// the JSON helpers for its status. Pure C++17, no Arduino dependencies, so it runs on the
// desktop. on_version_frame() is ISR-safe (plain/volatile field writes only); everything else
// runs from the main loop.
#include <cstddef>
#include <cstdint>
#include <cstring>

constexpr uint8_t WPM_SCAN_ADDR_MIN = 0x08;
constexpr uint8_t WPM_SCAN_ADDR_MAX = 0x77;
constexpr size_t WPM_SCAN_MAX_FOUND = 112;  // 0x08..0x77 inclusive
constexpr uint32_t WPM_SCAN_REPLY_MS = 300;
constexpr uint32_t WPM_SCAN_GAP_MS = 150;
constexpr size_t WPM_SCAN_VER_LEN = 7;

enum WpmScanState { WPM_SCAN_IDLE, WPM_SCAN_SCANNING, WPM_SCAN_DONE };
enum WpmScanAction { WPM_SCAN_ACT_NONE, WPM_SCAN_ACT_QUERY };  // QUERY: master a QUERY to current_addr()
enum WpmBusError {
  WPM_BUS_ERR_NONE = 0,
  WPM_BUS_ERR_BUSY = 1,
  WPM_BUS_ERR_ARGS = 2,
  WPM_BUS_ERR_SEND_TIMEOUT = 4,
  WPM_BUS_ERR_NO_RESPONSE = 5
};

struct WpmScanFound {
  uint8_t addr;
  bool ver_valid;
  char ver[WPM_SCAN_VER_LEN];
};

class WpmBusScan {
 public:
  // Refuses with BUSY while scanning, ARGS if either bound is outside 0x08..0x77.
  // first > last is accepted and ends DONE at the first step() with no entries.
  // reply_ms is the per-address listen window (not clamped here).
  WpmBusError start(uint8_t first, uint8_t last, uint32_t now_ms,
                    uint32_t reply_ms = WPM_SCAN_REPLY_MS);
  // Returns ACT_QUERY exactly once per address; otherwise NONE.
  WpmScanAction step(uint32_t now_ms);
  void query_failed(uint32_t now_ms);  // last_error SEND_TIMEOUT, skip to the gap
  // ISR-side: writes a one-slot mailbox only; step() consumes it. A frame for another address,
  // or outside a window, is counted in stray_replies() and otherwise ignored.
  void on_version_frame(uint8_t from_addr, const uint8_t* ver, size_t n);
  WpmScanState state() const { return state_; }
  uint8_t current_addr() const { return addr_; }
  size_t idx() const { return idx_; }
  size_t count() const { return count_; }
  const WpmScanFound* found() const { return found_; }
  WpmBusError last_error() const { return err_; }
  void clear_error() { err_ = WPM_BUS_ERR_NONE; }
  uint32_t stray_replies() const { return stray_; }
  uint32_t reply_ms() const { return reply_ms_; }

 private:
  enum Phase : uint8_t { PH_SEND, PH_WAIT, PH_GAP };
  WpmScanState state_ = WPM_SCAN_IDLE;
  Phase phase_ = PH_SEND;
  uint8_t first_ = 0, last_ = 0, addr_ = 0;
  size_t idx_ = 0, count_ = 0;
  uint32_t phase_start_ = 0, reply_ms_ = WPM_SCAN_REPLY_MS;
  WpmBusError err_ = WPM_BUS_ERR_NONE;
  WpmScanFound found_[WPM_SCAN_MAX_FOUND] = {};
  // ISR-shared.
  volatile bool window_ = false;  // true only during the WAIT phase
  volatile uint8_t mb_addr = 0;
  volatile uint8_t mb_len = 0;
  volatile uint8_t mb_ver[WPM_SCAN_VER_LEN] = {};
  volatile bool mb_full = false;
  volatile uint32_t stray_ = 0;
};

const char* wpm_scan_state_str(WpmScanState s);  // "idle" | "scanning" | "done"
// One found entry: {"addr":"28","ver_valid":true,"version":"31.0   "}. The 7 version bytes are
// escaped: '"' and '\\' get a backslash, bytes < 0x20 or >= 0x7F become '.'. Returns the length
// written, or 0 if n is too small (buf >= 64 is always enough).
size_t wpm_scan_entry_json(const WpmScanFound& f, char* buf, size_t n);
// Header up to and including "found":[  : {"state":"scanning","idx":3,"count":1,"found":[
size_t wpm_scan_status_head_json(WpmScanState s, size_t idx, size_t count, char* buf, size_t n);
// Index of the longest candidate occurring in `line`, or -1.
int wpm_bus_route_match(const char* line, const char* const* candidates, size_t n);
