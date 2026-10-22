# WPM firmware (Arduino MKR1000)

Firmware for the Buchla 200e Wi-Fi preset manager (WPM): an Arduino MKR1000 (SAMD21) with a 256 KB SPI
FRAM. This tree builds the app from source, serves `/version`, and can update itself over Wi-Fi (OTA).

Flash layout (flash addresses):

| Range | Contents |
| --- | --- |
| 0x0000-0x1FFF | SAM-BA bootloader, stock ("v2.0 [Arduino:XYZ] Dec 20 2016"); kept for USB recovery |
| 0x2000-0x5FFF | FRAM loader (0x4000-byte blob, built from `FramLoader` with the `wpm_loader` profile and embedded in the app build) |
| 0x6000- | the app |

## Build

Needs `arduino-cli` and `python3`. `build.sh` installs the pinned core `arduino:samd@1.8.14` and
`WiFi101@0.16.1`, and copies `boards.local.txt` next to the core's `boards.txt`.

    ./build.sh 2Wireless --profile wpm        # -> build/2Wireless/2Wireless.ino.bin and 2Wireless.ota.bin
    ./build.sh FramLoader                      # only when the loader changes; regenerates src/wpm_loader_blob.cpp
    ./build.sh 2Wireless --profile wpm --tag b # same, with FW_BUILD_TAG "b" (reported as "build" by /version)

- Profiles (`boards.local.txt`): `wpm` (Studio H USB identity 0x1209/0xFBA0, "Studio H Software USB
  Midi"), `wpm_loader` (no USB stack, used for `FramLoader`), `mkr1000` (stock core entry).
- `build.sh` prints size and CRC32 of the `.ino.bin`. For the `wpm` profile it also checks both vector
  tables (loader at offset 0, app at offset 0x4000), checks the embedded loader blob is current, stamps the app marker (below) into the `.ino.bin`, removes the unstamped `2Wireless.ino.hex`, and
  writes `2Wireless.ota.bin`: the `.ino.bin` without its first 0x4000 bytes (the app-only image,
  at most 0x2BD18 bytes, even length).
- MIDIUSB and USB_Host_Library_SAMD are vendored in `libraries/` with small patches; see each
  library's `WPM-CHANGES.md`. `libraries/WpmCommon` holds the CRC32, OTA record and `/version`
  formatter shared by the app, the loader and the desktop tests (`make -C test`).
- `2Wireless/src/WireByte.{h,cpp}` is the vendored per-byte I2C slave (class `TwoWireByte`, global
  `Wire`); `WireByte.diff` shows the change against the core's `Wire.cpp`. `/getpresets` capture data is
  served from FRAM per byte through it. Equivalence to the shipped v1.18 behaviour is not yet
  established (bench B1; see Bench results).

## Flash over USB

- Put the WPM in USB Device mode and use a data cable (not charge-only). No double-tap of reset is
  needed (by design, not bench-verified: the touch resets into SAM-BA).
- Flash the FULL `build/2Wireless/2Wireless.ino.bin` (loader + app). The `.ota.bin` is only for OTA.

      ./flash.sh build/2Wireless/2Wireless.ino.bin [port] [--dry-run]

- `flash.sh` does the Arduino 1200-baud touch (bossac itself does not) only when it is about to upload,
  then runs bossac with verify and reset, and logs the full debug output to
  `bench/bossac-<timestamp>.log`.
- The 1200-baud touch (done by `flash.sh` and by `arduino-cli upload`) erases the flash row at 0x2000
  before resetting into SAM-BA (the Arduino core's `Reset.cpp`). Under this layout that row is the
  loader's first row. By design, SAM-BA then stays resident on the next reset because the app vector
  is invalid, and the upload rewrites the whole image, so USB recovery is unchanged. This is by design
  and not bench-verified. Never do the touch without an upload following it.
- The bootloader identifies as "v2.0 [Arduino:XYZ] Dec 20 2016".
- Recovery: the shipped v1.18 binary `2Wireless/bin/2Wireless.ino.bin` is the recovery image and is
  never modified. Put the board in the bootloader and
  `arduino-cli upload -b arduino:samd:mkr1000 -p <port> -i 2Wireless/bin/2Wireless.ino.bin`.
  (That image is linked at 0x2000; it overwrites the loader and the unit is a stock v1.18 unit again.)

## OTA

Stock v1.18 units need one USB upload (of the full 1.19 `.ino.bin`) to get OTA. After that, updates go
over Wi-Fi.

How it works: the image is `build/2Wireless/2Wireless.ota.bin`. Stage it in FRAM with the existing
`/writememory` route starting at FRAM address 0 (the capture/transfer area), then call `/ota/commit`.
Staging overwrites the preset transfer buffer but never the stored settings. The commit writes a pending
record to FRAM `0x02BE50` (18 bytes, magic "OTA1", inside the range 0x02BE4D-0x02BE7F that is
reserved for new state) and resets the WPM. On the next boot the loader at 0x2000 sees the record,
checks `len` and the staged head (vectors plus app marker), checks CRC32 over FRAM `[0..len)`, erases and writes flash from 0x6000 in 256-byte rows, verifies the
flash CRC, clears the magic, records the result and jumps to the app. A bad CRC (`last_result` 2) or a refused len/head (3, `image`) clears the magic and
boots the existing app. The loader never erases itself, so a power cut re-runs the copy on the next boot;
if the flash verify fails, the copy is retried at most 3 times. If flash holds no plausible app (the
vector check fails), the loader blinks the LED at 5 Hz for 3 s and then hands the unit to the SAM-BA
bootloader (double-tap magic + reset), so the "Arduino MKR1000" USB port appears and one `flash.sh`
upload recovers it without reaching the reset button; each power-up repeats the blink and hand-off.

`bench/ota_push.py <file.ota.bin>` stages and read-back-verifies an image and prints the commit URL; it
never calls `/ota/commit`.

App image marker: every valid app image carries a 16-byte marker at app-image offset 0x1C
(vector-table entries 7..10, reserved on ARMv6-M and never fetched, so the Arduino core leaves
them zero): `"WPMAPP1\0"`, link base LE32 0x6000, image length LE32 (the `.ota.bin` length).
In the full `.ino.bin` it sits at file offset 0x401C. `build.sh` stamps it and refuses to build
if the slot is not empty. The "head" is the first 44 bytes of an app image; `/ota/commit` and the
loader both check it (vectors plus marker, marker length == record len), so a stock or full image
is refused before anything is erased. `build.sh` also removes `2Wireless.ino.hex` (it is unstamped;
`flash.sh` takes the `.bin`).
`bench/ota_push.py` refuses a file without a valid marker before any network call.

Route contract (all replies HTTP 200 with a JSON body):

- `/ota/commit?len=N&crc32=HEX` checks in order job running (`"error":"busy"`), `len` (0, odd, or
  greater than 0x2BD18 gives `"error":"len"`), image head (word 0 in 0x20000000..0x20008000, word 1
  odd and inside 0x6000..0x40000, marker at 0x1C with base 0x6000 and length == `len` gives `"error":"image"`), CRC over FRAM (`"error":"crc"`). `busy` is
  never returned by this firmware today (`wpm_ota_busy()` is hard-coded false; no bus jobs exist yet).
  On success it writes the record, replies `{"ok":true,"pending":{"len":N,"crc32":"HEX"}}`,
  then calls `NVIC_SystemReset()` after the reply is flushed.
- `/ota/status` replies `{"loader":"fram-loader-1","pending":false,"last":"ok"|"crc_mismatch"|"image"|"none","app_crc32":"HEX"}`.
  `last` persists until the next commit or abort; a commit resets it to none so a stale ok can never
  be read as the new commit's outcome before the loader reports. `/ota/abort` clears the magic and
  sets `last` to none. `app_crc32` is kept in the record but only reported while `last` is "ok".
  `loader` is absent when the loader section is not present; `app_crc32` is `"unknown"` unless `last` is `"ok"`.
- `/ota/abort` replies `{"ok":true}`.
- Refusals are HTTP 200 with `{"ok":false,"error":"len|image|crc|busy"}`, never 4xx/5xx.

Other changes in this PR: Wi-Fi bring-up retries three times (`WiFi.end()`, 500 ms, reconfigure) before
the stock halt, so a software reset with USB attached comes back on Wi-Fi. `server.begin()` is called
once in `setup()` instead of per association (stock left port 80 closed after association churn). The
capture counter is reset before each `sendBackupPresets`.

## /version

`GET /version` returns (one line; shape from `wpm_version_format`):

    {"version":"1.19","board":"mkr1000","loader":"fram-loader-1","capabilities":["version","ota"],
     "getpresets_wait":false,"usbMode":1,"pollOnStart":0,"reset_cause":"POR","uptime_s":123,"rssi":-45,
     "tcp_sockets_free":null,"app_crc32":"HEX"}

| Field | Meaning |
| --- | --- |
| `version` | `FW_VERSION`, now "1.19". Every PR bumps it; clients key on `capabilities`, not the number |
| `build` | present only in builds made with `build.sh --tag`; placed right after `version` |
| `board` | always "mkr1000" |
| `loader` | "fram-loader-1" when a plausible loader is at flash 0x2000; key omitted otherwise (dev builds) |
| `capabilities` | currently `["version","ota"]`; later PRs append |
| `getpresets_wait` | false |
| `usbMode` | the stored USB mode (1 = Device, 0 = Host) |
| `pollOnStart` | the stored "poll modules on startup" setting (0/1) |
| `reset_cause` | "POR", "BOD", "EXT", "WDT", "SYST" or "UNKNOWN", from the reset-cause register |
| `uptime_s` | seconds since boot |
| `rssi` | `WiFi.RSSI()` |
| `tcp_sockets_free` | currently always `null` (not measured in PR 0) |
| `app_crc32` | CRC32 of the last OTA-installed image as 8 hex digits when the last OTA result is ok; otherwise "unknown" |

Per the design spec, stock units answer `/version` with the HTML home page (HTTP 200), which clients
treat as "stock" (a spec statement, not yet checked on a device).

## Compatibility contract

Nothing existing changes (design spec section 3):

- Every existing route keeps its URL, parameter order, reply shape and default behaviour. New parameters
  are optional and trailing. The stock pages (PresetManager, GetPresets, SetPresets, WriteFirmware, Setup,
  home page) keep working; the Setup page shows "Firmware v1.19".
- The FRAM layout (`Firmware/fram_map.txt`) is untouched. New state lives only in the reserved range
  0x02BE4D-0x02BE7F and is zero on stock units.
- USB identity (0x1209:0xFBA0 "Studio H Software USB Midi"), AP name BUCHLA200E and the I2C bus
  protocol are unchanged by design. A `v2/` path prefix keeps its stock meaning on every route (spec
  statement, not yet checked on a device).
- Boot timing unchanged and "the loader adds well under 1 ms when no OTA is pending" are design targets
  from the spec, not measurements; B2 and B6 record the real timings.
- New behaviour arrives only through new routes (`/version`, `/ota/*`). Refusals on new routes are
  HTTP 200 with `{"ok":false,"error":"<code>"}`.
- `FW_VERSION` is "1.19".

## Bench results

To be updated after the bench pass. Tasks 1-9 have desktop tests (`make -C test`) and compile clean,
but the device bench pass is PENDING (the WPM needs a case power cycle). Recorded so far in
[`bench/RESULTS.md`](bench/RESULTS.md): the Task 1 baseline build flashed and answering, and a Task 2
(WireByte) run in which `/getpresets` returned data but a second capture after `/setpresets` differed
from the first (shifted, not byte-identical; whether stock v1.18 does the same is not yet known). The
stock v1.18 reference run was not completed. Pending, each to be recorded in
[`bench/RESULTS.md`](bench/RESULTS.md) (it has no entries for them yet):

- B1: 259e restore reference capture
- B2: first loader flash and reject-to-jump
- B3: `/version`, Setup page v1.19 and Wi-Fi churn
- B4: OTA refusals
- B5: software-reset bring-up x3
- B6: OTA round trip A to B to A, with timings
- Task 10: power-cut, corruption and recovery
