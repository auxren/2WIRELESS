# Bench results

## Task 1 baseline (2026-10-21)

Untouched sketch, `wpm` profile, flashed to Oren's WPM (in its 200e case).

    ./build.sh 2Wireless --profile mkr1000   -> size 109924 crc32 5D0CB530  (soft target 108,612 +-3%: +1.2%)
    ./build.sh 2Wireless --profile wpm       -> size 109940 crc32 AC31B388
    strings build/2Wireless/2Wireless.ino.bin | grep -c "Studio H Software USB Midi"   -> 1
    ./flash.sh build/2Wireless/2Wireless.ino.bin
        (touch on /dev/cu.usbmodem83344203, bootloader /dev/cu.usbmodem83344201; "Verify successful", CPU reset)

bossac log (H19 reference): `Firmware/arduino/bench/bossac-20261021-225213.log`

After reset: the first `networksetup -setairportnetwork en1 BUCHLA200E BUCHLA200E` at 60 s printed
"Could not find network" (no association made); a second attempt about 90 s later joined.

    curl --interface en1 http://192.168.0.1/currentpreset   -> {"currentpreset": "1"}
    curl --interface en1 http://192.168.0.1/ssid            -> {"ssid": "BUCHLA200E"}
    curl --interface en1 'http://192.168.0.1/getpresets?addr=76' -> "module_address": "0x76", "data": ""
    ioreg: "Studio H Software USB Midi" present (VID 4617 = 0x1209), CDC port /dev/cu.usbmodem83344203

## Task 2 (WireByte), 2026-10-21

    ./build.sh 2Wireless --profile wpm --verbose  -> size 109940 crc32 C494FF32 (stock-source build: 109940 AC31B388)
    grep -c "libraries/Wire/" <verbose log>        -> 0 ; WireByte.cpp compiled and linked, no duplicate symbols

Bench, new build (module 259e, addr 0x28, joined after ~60 s):
- /getpresets?addr=28 returned 1069 bytes of data (2138 hex chars); /readmemory?addr=0x0&length=1069 returned data.
- /setpresets?addr=28 returned an empty body; after 10 s a second /getpresets differed from the first:
  second capture has extra leading bytes (`00000000fff06ea0...` vs `0000fff06ea0...`), i.e. shifted, 1483 of
  2138 hex chars differ. NOT byte-identical. Not yet known whether stock v1.18 behaves the same.
- Reference run with stock v1.18 NOT completed: after flashing it the AP was never joinable within the
  3-join limit (en1 stayed on the 192.168.0.x LAN, whose 192.168.0.1 answers "Not Found" to everything).
  The new build re-flashed afterwards was also not joinable within 3 joins (60 s + 3 attempts).
  Needs Oren: AP check / case power cycle, then rerun the stock reference and the new build.
Final device state: new build (crc C494FF32) flashed.
