#!/usr/bin/env bash
# Headless input test: boot --ui, inject keystrokes via QEMU monitor,
# assert the prompt echoed them and Enter dispatched.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=out; MON=$OUT/mon.sock; LOG=$OUT/serial.log; PPM=$OUT/ui.ppm
rm -f "$MON" "$LOG" "$PPM"

qemu-system-x86_64 -m 256M -kernel .cache/vmlinuz-virt -initrd $OUT/initramfs.cpio.gz \
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 -no-reboot \
  -vga none -device virtio-gpu -device virtio-keyboard -display none \
  -serial file:"$LOG" -monitor unix:"$MON",server,nowait \
  -append "console=ttyS0 quiet relicos.run=ui panic=10" &
pid=$!
trap '[ -n "${pid:-}" ] && kill $pid 2>/dev/null; wait 2>/dev/null' EXIT

mon() { printf '%s\n' "$1" | nc -U "$MON" >/dev/null || true; }
await() { for _ in $(seq "$2"); do grep -q "$1" "$LOG" 2>/dev/null && return 0; sleep 1; done; return 1; }

await 'kbd:' 30 || { echo "FAIL: keyboard not found"; tail -20 "$LOG"; exit 1; }
await 'blit ok' 30 || { echo "FAIL: no initial blit"; tail -20 "$LOG"; exit 1; }

for k in h i shift-1 ret; do mon "sendkey $k"; sleep 0.3; done

await 'submit: hi!' 60 || { echo "FAIL: submit not seen"; tail -30 "$LOG"; exit 1; }

# console toggle: ` hides, ` shows. Sleep before each dump so virtio-gpu
# defio has flushed mmap writes to the scanout.
mon "sendkey grave_accent"
await 'console: hidden' 10 || { echo "FAIL: console did not hide"; tail -30 "$LOG"; exit 1; }
sleep 0.5; mon "screendump $OUT/ui-hidden.ppm"
# keys typed while hidden must not reach the input buffer (focus released)
for k in x y z; do mon "sendkey $k"; sleep 0.2; done
mon "sendkey grave_accent"
await 'console: shown' 10 || { echo "FAIL: console did not reopen"; tail -30 "$LOG"; exit 1; }
for k in o k ret; do mon "sendkey $k"; sleep 0.3; done
await 'submit: ok' 10 || { echo "FAIL: hidden keys leaked into buffer"; tail -30 "$LOG"; exit 1; }
sleep 0.5; mon "screendump $PPM"
sleep 0.5
mon "quit"; wait "$pid" 2>/dev/null || true; pid=

[ -s "$PPM" ] || { echo "FAIL: no screendump"; exit 1; }
echo "RELICOS_INPUT_PASS"
command -v sips >/dev/null && sips -s format png "$PPM" --out "$OUT/ui.png" >/dev/null
