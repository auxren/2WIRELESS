#include "wpm_bus_scan.h"

#include <cstdio>

WpmBusError WpmBusScan::start(uint8_t first, uint8_t last, uint32_t now_ms, uint32_t reply_ms) {
  if (state_ == WPM_SCAN_SCANNING) {
    err_ = WPM_BUS_ERR_BUSY;
    return WPM_BUS_ERR_BUSY;
  }
  if (first < WPM_SCAN_ADDR_MIN || first > WPM_SCAN_ADDR_MAX || last < WPM_SCAN_ADDR_MIN ||
      last > WPM_SCAN_ADDR_MAX) {
    err_ = WPM_BUS_ERR_ARGS;
    return WPM_BUS_ERR_ARGS;
  }
  window_ = false;
  mb_full = false;
  stray_ = 0;
  err_ = WPM_BUS_ERR_NONE;
  first_ = first;
  last_ = last;
  addr_ = first;
  idx_ = 0;
  count_ = 0;
  reply_ms_ = reply_ms;
  phase_ = PH_SEND;
  phase_start_ = now_ms;
  state_ = WPM_SCAN_SCANNING;
  return WPM_BUS_ERR_NONE;
}

WpmScanAction WpmBusScan::step(uint32_t now_ms) {
  if (state_ != WPM_SCAN_SCANNING) return WPM_SCAN_ACT_NONE;
  if (phase_ == PH_WAIT) {
    if (mb_full) {
      if (count_ < WPM_SCAN_MAX_FOUND) {
        WpmScanFound& f = found_[count_++];
        f.addr = addr_;
        uint8_t n = mb_len;
        if (n > WPM_SCAN_VER_LEN) n = WPM_SCAN_VER_LEN;
        f.ver_valid = (n == WPM_SCAN_VER_LEN);
        for (size_t i = 0; i < WPM_SCAN_VER_LEN; i++) f.ver[i] = i < n ? static_cast<char>(mb_ver[i]) : ' ';
      }
      window_ = false;
      mb_full = false;
      phase_ = PH_GAP;
      phase_start_ = now_ms;
      return WPM_SCAN_ACT_NONE;
    }
    if (now_ms - phase_start_ >= reply_ms_) {
      window_ = false;
      phase_ = PH_GAP;
      phase_start_ = now_ms;
    }
    return WPM_SCAN_ACT_NONE;
  }
  if (phase_ == PH_GAP) {
    if (now_ms - phase_start_ < WPM_SCAN_GAP_MS) return WPM_SCAN_ACT_NONE;
    idx_++;
    addr_++;
    phase_ = PH_SEND;
  }
  // PH_SEND (possibly entered just now from the gap).
  if (addr_ > last_) {
    state_ = WPM_SCAN_DONE;
    return WPM_SCAN_ACT_NONE;
  }
  mb_full = false;
  phase_ = PH_WAIT;
  phase_start_ = now_ms;
  window_ = true;
  return WPM_SCAN_ACT_QUERY;
}

void WpmBusScan::query_failed(uint32_t now_ms) {
  err_ = WPM_BUS_ERR_SEND_TIMEOUT;
  window_ = false;
  phase_ = PH_GAP;
  phase_start_ = now_ms;
}

void WpmBusScan::on_version_frame(uint8_t from_addr, const uint8_t* ver, size_t n) {
  if (!window_ || mb_full || from_addr != addr_) {
    stray_ = stray_ + 1;
    return;
  }
  if (n > WPM_SCAN_VER_LEN) n = WPM_SCAN_VER_LEN;
  mb_addr = from_addr;
  mb_len = static_cast<uint8_t>(n);
  for (size_t i = 0; i < n; i++) mb_ver[i] = ver[i];
  mb_full = true;
}

const char* wpm_scan_state_str(WpmScanState s) {
  switch (s) {
    case WPM_SCAN_SCANNING: return "scanning";
    case WPM_SCAN_DONE: return "done";
    default: return "idle";
  }
}

size_t wpm_scan_entry_json(const WpmScanFound& f, char* buf, size_t n) {
  char tmp[80];
  size_t p = 0;
  int w = std::snprintf(tmp, sizeof tmp, "{\"addr\":\"%02x\",\"ver_valid\":%s,\"version\":\"",
                        static_cast<unsigned>(f.addr), f.ver_valid ? "true" : "false");
  if (w < 0) return 0;
  p = static_cast<size_t>(w);
  for (size_t i = 0; i < WPM_SCAN_VER_LEN; i++) {
    uint8_t c = static_cast<uint8_t>(f.ver[i]);
    if (c == '"' || c == '\\') {
      tmp[p++] = '\\';
      tmp[p++] = static_cast<char>(c);
    } else if (c < 0x20 || c >= 0x7F) {
      tmp[p++] = '.';
    } else {
      tmp[p++] = static_cast<char>(c);
    }
  }
  tmp[p++] = '"';
  tmp[p++] = '}';
  tmp[p] = '\0';
  if (p + 1 > n) return 0;
  std::memcpy(buf, tmp, p + 1);
  return p;
}

size_t wpm_scan_status_head_json(WpmScanState s, size_t idx, size_t count, char* buf, size_t n) {
  int w = std::snprintf(buf, n, "{\"state\":\"%s\",\"idx\":%u,\"count\":%u,\"found\":[",
                        wpm_scan_state_str(s), static_cast<unsigned>(idx), static_cast<unsigned>(count));
  if (w < 0 || static_cast<size_t>(w) >= n) return 0;
  return static_cast<size_t>(w);
}

int wpm_bus_route_match(const char* line, const char* const* candidates, size_t n) {
  int best = -1;
  size_t best_len = 0;
  for (size_t i = 0; i < n; i++) {
    size_t l = std::strlen(candidates[i]);
    if (l > best_len && std::strstr(line, candidates[i]) != nullptr) {
      best = static_cast<int>(i);
      best_len = l;
    }
  }
  return best;
}
