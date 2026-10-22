#!/bin/bash
# Reproducible arduino-cli build for the WPM firmware.
# Usage: build.sh [2Wireless|FramLoader] [--profile wpm|wpm_loader|mkr1000] [--verbose]
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SKETCH="2Wireless"
PROFILE=""
VERBOSE=""
while [ $# -gt 0 ]; do
  case "$1" in
    --profile) PROFILE="$2"; shift 2 ;;
    --verbose) VERBOSE="-v"; shift ;;
    2Wireless|FramLoader) SKETCH="$1"; shift ;;
    *) echo "usage: $0 [2Wireless|FramLoader] [--profile wpm|wpm_loader|mkr1000] [--verbose]" >&2; exit 2 ;;
  esac
done

if [ -z "$PROFILE" ]; then
  if [ "$SKETCH" = "FramLoader" ]; then PROFILE="wpm_loader"; else PROFILE="wpm"; fi
fi

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

if [ "$SKETCH" = "FramLoader" ]; then
  # The loader must fit the 0x4000-byte .sketch_boot slot; embed it in the app.
  python3 "$HERE/tools/blob2c.py" "$BIN" "$HERE/2Wireless/src/wpm_loader_blob.cpp"
fi

if [ "$SKETCH" = "2Wireless" ] && [ "$PROFILE" = "wpm" ]; then
  # Full image = loader blob (0x2000..0x5FFF) + app (0x6000..). Check both
  # vector tables, then emit the app-only OTA image (bin minus first 0x4000).
  python3 - "$BIN" "$OUT/2Wireless.ota.bin" "$HERE/build/FramLoader/FramLoader.ino.bin" <<'PYEOF'
import struct, sys, zlib
d = open(sys.argv[1], "rb").read()
if len(d) <= 0x4000 + 8:
    sys.exit("error: bin too small to contain loader + app")
def vec(off):
    return struct.unpack_from("<II", d, off)
sp, rst = vec(0)
if not (0x20000000 < sp <= 0x20008000) or not (0x2000 <= (rst & ~1) < 0x6000) or not rst & 1:
    sys.exit("error: loader vectors invalid at offset 0: sp=%08X reset=%08X" % (sp, rst))
asp, arst = vec(0x4000)
if not (0x20000000 < asp <= 0x20008000) or not (0x6000 <= (arst & ~1) < 0x40000) or not arst & 1:
    sys.exit("error: app vectors invalid at offset 0x4000: sp=%08X reset=%08X" % (asp, arst))
ldr = open(sys.argv[3], "rb").read()
if d[:0x4000] != ldr + b"\xff" * (0x4000 - len(ldr)):
    sys.exit("error: stale loader blob: run build.sh FramLoader")
ota = d[0x4000:]
if len(ota) % 2 or len(ota) > 0x2BD18:
    sys.exit("error: OTA image length %d must be even and <= 0x2BD18" % len(ota))
open(sys.argv[2], "wb").write(ota)
print("vectors ok: loader sp=%08X reset=%08X, app sp=%08X reset=%08X" % (sp, rst, asp, arst))
print("ota size %d crc32 %08X -> %s" % (len(ota), zlib.crc32(ota) & 0xffffffff, sys.argv[2]))
PYEOF
fi
