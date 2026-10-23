#include "wpm_dump.h"

#include <cstdio>

WpmBusError WpmDumpJob::start(uint8_t addr, uint32_t expected, uint32_t first_wait_ms, uint32_t silence_ms,
                              uint32_t now_ms) {
  if (running()) return WPM_BUS_ERR_BUSY;
  if (addr < WPM_SCAN_ADDR_MIN || addr > WPM_SCAN_ADDR_MAX || expected > WPM_DUMP_SCRATCH_END ||
      first_wait_ms < WPM_DUMP_FIRST_WAIT_MIN_MS || first_wait_ms > WPM_DUMP_FIRST_WAIT_MAX_MS ||
      silence_ms < WPM_DUMP_SILENCE_MIN_MS || silence_ms > WPM_DUMP_SILENCE_MAX_MS)
    return WPM_BUS_ERR_ARGS;
  addr_ = addr;
  expected_ = expected;
  first_wait_ = first_wait_ms;
  silence_ = silence_ms;
  started_ = true;
  t_begin_ = t_start_ = t_last_ = t_end_ = now_ms;
  seen_ = 0;
  err_ = WPM_BUS_ERR_NONE;
  received_ = 0;
  extent_ = 0;
  lines_ = 0;
  dropped_ = 0;
  stray_ = 0;
  complete_ = false;
  state_ = WPM_DUMP_SENDING;
  return WPM_BUS_ERR_NONE;
}

void WpmDumpJob::finish(WpmDumpState s, WpmBusError e, uint32_t now_ms) {
  state_ = s;
  err_ = e;
  t_end_ = now_ms;
}

WpmDumpAction WpmDumpJob::step(uint32_t now_ms) {
  const uint32_t rx = received_;  // one snapshot per step
  const bool done_frame = complete_;
  switch (state_) {
    case WPM_DUMP_SENDING:
      t_start_ = now_ms;
      seen_ = rx;
      state_ = WPM_DUMP_WAITING;
      return WPM_DUMP_ACT_SEND_BACKUP;
    case WPM_DUMP_WAITING:
      if (done_frame) {
        finish(WPM_DUMP_DONE, WPM_BUS_ERR_NONE, now_ms);
      } else if (rx != seen_) {
        seen_ = rx;
        t_last_ = now_ms;
        state_ = WPM_DUMP_TRANSFERRING;
      } else if (now_ms - t_start_ >= first_wait_) {
        finish(WPM_DUMP_FAILED, WPM_BUS_ERR_NO_RESPONSE, now_ms);
      }
      break;
    case WPM_DUMP_TRANSFERRING:
      if (done_frame || (expected_ && rx >= expected_)) {
        finish(WPM_DUMP_DONE, WPM_BUS_ERR_NONE, now_ms);
      } else if (rx != seen_) {
        seen_ = rx;
        t_last_ = now_ms;
      } else if (now_ms - t_last_ >= silence_) {
        finish(WPM_DUMP_DONE, WPM_BUS_ERR_NONE, now_ms);
      }
      break;
    default:
      break;
  }
  return WPM_DUMP_ACT_NONE;
}

void WpmDumpJob::send_failed(uint32_t now_ms) {
  if (state_ == WPM_DUMP_SENDING || state_ == WPM_DUMP_WAITING)
    finish(WPM_DUMP_FAILED, WPM_BUS_ERR_SEND_TIMEOUT, now_ms);
}

void WpmDumpJob::abort(uint32_t now_ms) {
  if (running()) t_end_ = now_ms;
  state_ = WPM_DUMP_IDLE;
}

void WpmDumpJob::on_data(uint32_t offset, uint32_t n) {
  const uint32_t end = offset + n;
  if (end > extent_) extent_ = end;
  received_ = received_ + n;
  lines_ = lines_ + 1;
}

void WpmDumpJob::on_dropped(uint32_t n) { dropped_ = dropped_ + n; }

void WpmDumpJob::on_complete_frame(uint8_t from_addr) {
  if (from_addr == addr_)
    complete_ = true;
  else
    stray_ = stray_ + 1;
}

uint32_t WpmDumpJob::elapsed_ms(uint32_t now_ms) const {
  if (!started_) return 0;
  return (running() ? now_ms : t_end_) - t_begin_;
}

const char* wpm_dump_state_str(WpmDumpState s) {
  switch (s) {
    case WPM_DUMP_IDLE: return "idle";
    case WPM_DUMP_SENDING: return "sending";
    case WPM_DUMP_WAITING: return "waiting";
    case WPM_DUMP_TRANSFERRING: return "transferring";
    case WPM_DUMP_DONE: return "done";
    case WPM_DUMP_FAILED: return "failed";
  }
  return "idle";
}

size_t wpm_dump_status_json(const WpmDumpJob& j, uint32_t now_ms, char* buf, size_t n) {
  if (!buf || n == 0) return 0;
  int w = std::snprintf(buf, n,
                        "{\"state\":\"%s\",\"addr\":\"%02x\",\"expected\":%lu,\"received\":%lu,"
                        "\"extent\":%lu,\"lines\":%lu,\"dropped\":%lu,\"complete_frame\":%s,"
                        "\"error\":%d,\"elapsed_ms\":%lu}",
                        wpm_dump_state_str(j.state()), static_cast<unsigned>(j.addr()),
                        static_cast<unsigned long>(j.expected()), static_cast<unsigned long>(j.received()),
                        static_cast<unsigned long>(j.extent()), static_cast<unsigned long>(j.lines()),
                        static_cast<unsigned long>(j.dropped()), j.complete_frame() ? "true" : "false",
                        static_cast<int>(j.error()), static_cast<unsigned long>(j.elapsed_ms(now_ms)));
  if (w < 0 || static_cast<size_t>(w) >= n) {
    buf[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(w);
}
