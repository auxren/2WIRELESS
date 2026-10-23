#pragma once
// /dump, /dump/status and /dump/abort routes: byte-counting preset capture. A BACKUP is mastered
// to a module; the module writes its bank to the card address (0x50) as [off_hi][off_lo][data...]
// lines, which the I2C receive ISR stores straight into the FRAM scratch area (0..0x2BD18) while
// the state machine (libraries/WpmCommon/src/wpm_dump.h) counts bytes and decides the end.
//
// /dump?addr=XX[&len=N][&first_wait_ms=M][&silence_ms=S]: addr is 2 hex digits (required), len is
// the expected byte count (default 0 = unknown), the waits are decimal and clamped to the class
// limits. Replies {"ok":true}, or {"ok":false,"error":"busy"|"args"}. A BACKUP that could not be
// mastered shows up in /dump/status as state "failed", error 4.
// /dump/status: the Task 1 JSON, then one more key, always last:
//   "addr_log":"50 50 00"
// DIAGNOSTIC, present in 1.21: the matched slave addresses (7-bit hex, space separated) of the
// first 8 transactions the dump ISR handler saw during the current job, "" when none. 0x50 is the
// card address, 00 a general call, ff when WPM_DUMP_ADDR_FILTER is 0. Clients must not rely on it.
#include <Arduino.h>
#include <WiFi101.h>

// Experiment (Task 2): the vendored Wire exposes the address that matched for the current
// transaction (WireByte.cpp, Wire.lastAddress()), and the dump handler then applies data lines only
// from transactions addressed to 0x50. If the bench shows the DATA register does not hold the
// address, build with -DWPM_DUMP_ADDR_FILTER=0 (or flip this) to fall back to shape parsing.
#ifndef WPM_DUMP_ADDR_FILTER
#define WPM_DUMP_ADDR_FILTER 1
#endif

bool wpm_dump_active();  // job state SENDING, WAITING or TRANSFERRING
void wpm_dump_loop();    // call once per loop() pass (main loop only), after wpm_bus_loop()
// ISR: receiveEvent calls this instead of the stock paths while wpm_dump_active(). Consumes every
// byte of the transaction.
void wpm_dump_isr_frame(int howMany);

// urlString: "...GET /dump?addr=28&len=990 ...".
void wpm_dump_handle_start(WiFiClient& client, const String& urlString);
void wpm_dump_handle_status(WiFiClient& client);
void wpm_dump_handle_abort(WiFiClient& client);
