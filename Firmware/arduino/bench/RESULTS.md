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
