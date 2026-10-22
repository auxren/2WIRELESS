# OTA robustness bench: power cuts, corruption, recovery (Task 10)

Run with the owner at the case. Everything below uses `W=Firmware/arduino/bench/wpm.sh` (curl bound to en1; `$W get <path>`, `$W post <path> <body>`);
set it once: `W=Firmware/arduino/bench/wpm.sh`.

## Facts that govern the procedure

- The WPM is bus-powered from the 200e case. A USB data cable back-feeds the MKR1000, so a case power
  cycle only resets it with **USB unplugged**. Every bossac flash ends in a software reset.
- After any reset the Mac must join BUCHLA200E once: `bash Firmware/arduino/bench/wifi_rejoin.sh`
  (60 s wait, join, polls up to 90 s). "Up" below means this script exited 0.
- LED during a loader run: solid on from the moment the loader starts the copy until it jumps to the
  app (about 1 s after reset plus the copy). 5 Hz blink means no valid app (loader refuses to jump).
  Slow loader copy time = 445 rows x 50 ms, about 22 s. The CRC passes and the record writes are
  milliseconds each.
- OTA record: 18 bytes at FRAM 0x02BE50 (`"OTA1"`, len LE32, crc32 LE32, flags, last_result, app_crc32 LE32).
- Variants: `A=Firmware/arduino/build/variants/a/2Wireless.ota.bin` (build tag `a`),
  `B=.../b/2Wireless.ota.bin` (tag `b`). Roundtrip timings: push 47 s, verify 19 s, commit reply 2 s.

## Step 0: set up (no owner needed)

```
bash Firmware/arduino/build.sh FramLoader --slow 50      # prints the bench-artefact warning
bash Firmware/arduino/build.sh 2Wireless --profile wpm --tag a   # prints "embedded loader: 8104 bytes crc32 431B705E"
# USB plugged in, no serial monitor open:
bash Firmware/arduino/flash.sh Firmware/arduino/build/2Wireless/2Wireless.ino.bin
bash Firmware/arduino/bench/wifi_rejoin.sh; $W get /version        # build "a"
git checkout Firmware/arduino/2Wireless/src/wpm_loader_blob.cpp    # never commit the slow blob
```

Then unplug the USB data cable. Check with `$W get /ota/status` that it answers
(`{"pending":false,"last":...,"app_crc32":...}`).

Helper used throughout (print the commit URL; ota_push never commits):

```
stage() { python3 Firmware/arduino/bench/ota_push.py "$1" --iface en1; }   # last line: commit URL
```

## Step 1: mid-copy power cut (3 repetitions: 5 s, 10 s, 20 s)

Per repetition, with T the variant not currently running (rep 1: A running, stage B):

1. `stage <T.ota.bin>`; `curl --interface en1 '<commit URL>'`. The reply (`ok`) is "t=0".
2. Owner starts a stopwatch at the reply. LED goes solid within about 1-2 s. At the cut time (5 / 10 /
   20 s) owner switches the case off for 5 s, then back on.
3. Owner watches the LED after power-on: expect solid again, for the full ~22 s (copy restarts from
   row 0), then off (app up). Note the duration.
4. `wifi_rejoin.sh`, then `$W get /ota/status` and `$W get /version`.

Expected: `last:"ok"`, `pending:false`, `/version` build = T's tag, `app_crc32` = T's CRC (B 57429505,
A 661CCA5F per RESULTS.md B6). Record anything else (blinking = the failure we are looking for).

## Step 2: cut after the copy, before the record is cleared

The post-copy window (flash CRC pass, record write, magic clear) is milliseconds, so it cannot be hit
by eye. Repeat Step 1 with cut times 21, 22 and 23 s (the last row finishes at about 22 s from loader
start, ~23-24 s after the reply), 3 attempts, and report the LED duration of the post-cut run:
about 22 s means the record was still pending (re-copy); a short LED / immediate app means the magic
had already been cleared. Expected either way: app T runs, `last:"ok"`, no blink.

## Step 3: corrupt staged image

```
stage <B.ota.bin>                       # note len N and crc C (original) from the commit URL
# flip one byte at FRAM 0x1000 (first byte of the 64-byte element):
D=$($W get '/readmemory?addr=0x1000&length=64' | python3 -c 'import sys,json,re;t=sys.stdin.read();d=re.findall(r"\"data\":\s*\"([0-9a-fA-F]+)\"",t)[0][:128];print("%02x"%(int(d[:2],16)^0xff)+d[2:])')
$W post /writememory "[{\"addr\":\"1000\",\"data\":\"$D\"}]"
curl --interface en1 "http://192.168.0.1/ota/commit?len=N&crc32=C"     # commit with the ORIGINAL CRC
```

Expected: `"error":"crc"` (commit-time catch), `/ota/status` pending:false. Then bypass commit by writing the
record by hand with the original (now wrong for the corrupted FRAM) CRC:

```
python3 - N C <<'PY'     # N decimal, C hex; prints the 18-byte record as hex
import struct,sys
n,c=int(sys.argv[1]),int(sys.argv[2],16)
print((b"OTA1"+struct.pack("<II",n,c)+bytes(2)+bytes(4)).hex())
PY
$W post /writememory '[{"addr":"2be50","data":"<hex printed above>"}]'
```

`$W get /ota/status` should now show `pending:true`. Owner power-cycles (5 s off, USB out), LED
solid for under 1 s (reject before copy), then app up; `wifi_rejoin.sh`.
Expected: `last:"crc_mismatch"`, `pending:false`, `/version` build = the previously running tag
(old app still runs), flash not touched. Then `curl --interface en1 http://192.168.0.1/ota/abort`.

## Step 4: oversize and broken vectors, after a real reboot

Power-cycle first (owner, 5 s off, USB out), `wifi_rejoin.sh`, then
`curl --interface en1 'http://192.168.0.1/ota/commit?len=L&crc32=00000000'` for each:
`len=0`, `len=1001`, `len=179482` (expected `"error":"len"`); `len=1000` with FRAM 0 holding the preset
bank (nothing staged) (expected `"error":"image"`); stage a valid image and commit with `crc32=DEADBEEF`
(expected `"error":"crc"`); `/ota/abort` (expected `ok`). After each, `/ota/status` must show
`pending:false`; the app keeps running (build tag unchanged), no reboot.

## Step 5: recovery drill (brick and reflash)

Record for len=0x1000 over an all-zero image: CRC32 of 4096 zero bytes = `C71C0011`.

```
python3 - <<'PY'     # 64 zero elements of 64 B at FRAM 0..0xFFF, in one POST
import json;print(json.dumps([{"addr":"%x"%a,"data":"00"*64} for a in range(0,0x1000,64)],separators=(",",":")))
PY
```
Post that JSON with `$W post /writememory '<json>'` (4 KB body: well under the 15 KB part limit),
verify with `$W get '/readmemory?addr=0x0&length=128'` (all zeros), then write the record:

```
$W post /writememory '[{"addr":"2be50","data":"4f5441310010000011001cc7000000000000"}]'
```
(`OTA1`, len 0x1000, crc C71C0011 LE, flags 0, last 0, app_crc 0.) Check with `$W get /ota/status` (pending:true).

Owner power-cycles (USB out). Expected: loader copies 4 KB of zeros (LED solid about 1 s), verifies
the flash CRC (ok), writes `last:"ok"`, resets; then `flash_app_ok()` fails (zero vectors) so the
LED **blinks at 5 Hz forever** and HTTP never answers. Owner confirms the blink.
Recover: plug USB data in, close any serial reader, then
`bash Firmware/arduino/flash.sh Firmware/arduino/2Wireless/bin/2Wireless.ino.bin` (shipped v1.18) or the
branch's `build/2Wireless/2Wireless.ino.bin` (this rewrites the loader slot as well, with the slow loader
if that is what is in `build/`). Expected: back in one upload, `wifi_rejoin.sh` OK, `/version` correct.

## Results (controller fills in)

| Step | Rep | Cut time (s after commit reply) | LED observed | last / app_crc32 / build | Pass/fail |
|------|-----|---------------------------------|--------------|--------------------------|-----------|
| 1 | 1 | 5 | | | |
| 1 | 2 | 10 | | | |
| 1 | 3 | 20 | | | |
| 2 | 1 | 21 | | | |
| 2 | 2 | 22 | | | |
| 2 | 3 | 23 | | | |
| 3 | commit | n/a | | | |
| 3 | hand record | power cycle | | | |
| 4 | len=0 / 1001 / 179482 | n/a | | | |
| 4 | len=1000 image / DEADBEEF crc | n/a | | | |
| 5 | brick | power cycle | | | |
| 5 | recovery | n/a | | | |
