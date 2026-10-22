# PR 0: buildable firmware tree, `/version`, server fix, FRAM-staged OTA

## Summary

- The WPM firmware (Arduino MKR1000 sketch) now builds reproducibly from source with
  `Firmware/arduino/build.sh` (arduino-cli, core `arduino:samd@1.8.14`, `WiFi101@0.16.1`, vendored
  MIDIUSB and USB_Host_Library_SAMD, a `boards.local.txt` overlay for the Studio H USB identity), and
  `flash.sh` uploads over USB with bossac.
- Vendored `WireByte` (per-byte I2C slave serving); `/getpresets` capture data is served from FRAM per
  byte. Equivalence to the shipped v1.18 behaviour is not yet established (bench B1); the one recorded
  device run differed on a second capture, see `Firmware/arduino/bench/RESULTS.md`.
- New `GET /version` capability route. `FW_VERSION` is now "1.19".
- `server.begin()` is called once in `setup()` instead of per association (stock left port 80 closed
  after association churn). The capture counter is reset before each `sendBackupPresets`.
- Wi-Fi bring-up retries three times (`WiFi.end()`, 500 ms, reconfigure) before the stock halt, so a
  software reset with USB attached comes back on Wi-Fi.
- FRAM-staged OTA: a small loader at flash 0x2000 copies a staged, CRC-checked image from FRAM into the
  app region at 0x6000. New routes `/ota/status`, `/ota/abort`, `/ota/commit`.

Flash layout: SAM-BA bootloader 0x0000-0x1FFF (stock, kept for USB recovery); FRAM loader 0x2000-0x5FFF
(0x4000-byte blob built from `FramLoader`, embedded in the app build); app from 0x6000. USB flashing
uses the full `.ino.bin`; OTA uses the app-only `.ota.bin`.

Stock v1.18 units need one USB upload of the full 1.19 `.ino.bin` to get OTA; after that, updates can go
over Wi-Fi. The shipped v1.18 binary `Firmware/arduino/2Wireless/bin/2Wireless.ino.bin` is the recovery
image and is not modified.

## Compatibility contract

Nothing existing changes:

- All stock routes keep URL, parameter order, JSON shapes and default behaviour; stock pages work
  unchanged.
- FRAM map untouched. New state only in the reserved range 0x02BE4D-0x02BE7F (the OTA record is 18 bytes
  at 0x02BE50, magic "OTA1"). Staged OTA data goes through the existing `/writememory` from FRAM address
  0, so staging overwrites the preset transfer buffer but never the stored settings.
- USB identity 0x1209:0xFBA0 "Studio H Software USB Midi", AP BUCHLA200E and the bus protocol unchanged.
- New behaviour only through new routes; refusals are HTTP 200 with
  `{"ok":false,"error":"len|image|crc|busy"}`.
- `FW_VERSION` is "1.19".

## Known limitation

The loader's header check validates only the stack pointer and reset vector ranges (SP in SRAM, reset
vector odd and in [0x6000, 0x40000)). It cannot tell a 0x2000-linked binary (for example the stock
v1.18 `bin/2Wireless.ino.bin`) from a 0x6000-linked one. Only `.ota.bin` files produced by `build.sh`
are valid OTA images; committing anything else may brick the app until a USB reflash. A future PR may
add an image signature.

The 1200-baud touch (done by `flash.sh` and arduino-cli/bossac uploads) erases the flash row at 0x2000
before resetting into SAM-BA; SAM-BA then stays resident and the upload rewrites the whole image, so USB
recovery is unchanged. Never do the touch without an upload following it.

## What was bench-tested

To be updated after the bench pass. Tasks 1-9 have desktop tests (`make -C Firmware/arduino/test`) and
compile clean. The device bench pass is PENDING: the WPM needs a case power cycle that only the owner can
do. See [`Firmware/arduino/bench/RESULTS.md`](../../Firmware/arduino/bench/RESULTS.md) for everything
actually recorded (a Task 1 baseline flash, and an incomplete Task 2 WireByte run whose second capture
after `/setpresets` was not byte-identical to the first; the stock v1.18 reference run was not completed).

Pending device items:

- B1: 259e restore reference capture
- B2: first loader flash and reject-to-jump
- B3: `/version` and Setup page v1.19 and Wi-Fi churn
- B4: OTA refusals
- B5: software-reset bring-up x3
- B6: OTA round trip A to B to A, with timings
- Task 10: power-cut, corruption and recovery

No device result is claimed for any of these.

## How to reproduce

    cd Firmware/arduino
    make -C test                                  # desktop tests (CRC32, OTA record, loader logic, /version)
    ./build.sh FramLoader                         # loader blob (only when the loader changes)
    ./build.sh 2Wireless --profile wpm            # build/2Wireless/2Wireless.ino.bin + 2Wireless.ota.bin
    ./flash.sh build/2Wireless/2Wireless.ino.bin  # USB, full image (first time on a stock unit)

OTA, with the Mac joined to the WPM AP (BUCHLA200E):

    cd bench
    ./wpm.sh get version
    python3 ./ota_push.py ../build/2Wireless/2Wireless.ota.bin   # stages via /writememory, verifies, prints the commit URL
    ./wpm.sh get 'ota/commit?len=<N>&crc32=<HEX>'
    ./wifi_rejoin.sh
    ./wpm.sh get version
    ./wpm.sh get ota/status

The full A to B to A procedure is in `Firmware/arduino/bench/RESULTS.md` (Task 9).

🤖 Generated with [Claude Code](https://claude.com/claude-code)

https://claude.ai/code/session_01TT8DVnxoAbrr5uYq3D89kJ
