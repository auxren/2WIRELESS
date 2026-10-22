#!/bin/bash
# Reproducible arduino-cli build for the WPM firmware.
# Usage: build.sh [2Wireless|FramLoader] [--profile wpm|wpm_loader|mkr1000] [--verbose]
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SKETCH="2Wireless"
PROFILE="wpm"
VERBOSE=""
while [ $# -gt 0 ]; do
  case "$1" in
    --profile) PROFILE="$2"; shift 2 ;;
    --verbose) VERBOSE="-v"; shift ;;
    2Wireless|FramLoader) SKETCH="$1"; shift ;;
    *) echo "usage: $0 [2Wireless|FramLoader] [--profile wpm|wpm_loader|mkr1000] [--verbose]" >&2; exit 2 ;;
  esac
done

# Pinned dependencies.
CORE="arduino:samd@1.8.14"
CORE_VER="1.8.14"
WIFI101="WiFi101@0.16.1"
# MIDIUSB and USB_Host_Library_SAMD are vendored (patched) in libraries/; see their WPM-CHANGES.md.

arduino-cli core install "$CORE"
arduino-cli lib install "$WIFI101"

# Board profiles (wpm, wpm_loader) live next to the core's boards.txt.
CORE_DIR="$(arduino-cli config get directories.data)/packages/arduino/hardware/samd/${CORE_VER}"
cp "$HERE/boards.local.txt" "$CORE_DIR/boards.local.txt"

OUT="$HERE/build/$SKETCH"
arduino-cli compile --fqbn "arduino:samd:$PROFILE" --libraries "$HERE/libraries" \
  $VERBOSE --output-dir "$OUT" --warnings default "$HERE/$SKETCH"

BIN="$OUT/$SKETCH.ino.bin"
python3 -c 'import sys,zlib;d=open(sys.argv[1],"rb").read();print("size %d crc32 %08X"%(len(d),zlib.crc32(d)&0xffffffff))' "$BIN"
