# WPM firmware (Arduino MKR1000)

Build and flash with arduino-cli (core arduino:samd@1.8.14).

    ./build.sh 2Wireless --profile wpm        # -> build/2Wireless/2Wireless.ino.bin, prints size + CRC32
    ./flash.sh build/2Wireless/2Wireless.ino.bin [port] [--dry-run]

- Profiles (boards.local.txt, installed by build.sh next to the core's boards.txt): `wpm` (Studio H USB
  identity 0x1209/0xFBA0), `wpm_loader` (no USB stack), `mkr1000` (stock core entry).
- WiFi101 is installed from the Library Manager at a pinned version. MIDIUSB and USB_Host_Library_SAMD
  are vendored in `libraries/` with small patches; see each library's `WPM-CHANGES.md`.
- flash.sh does the 1200-baud touch (which erases the app's first flash row) only when it is about to
  upload, and logs the full bossac debug output to `bench/bossac-<timestamp>.log`.
- Recovery: put the board in the bootloader and `arduino-cli upload -b arduino:samd:mkr1000 -p <port>
  -i 2Wireless/bin/2Wireless.ino.bin` (the shipped v1.18 binary).
- Bench results: `bench/RESULTS.md`.
