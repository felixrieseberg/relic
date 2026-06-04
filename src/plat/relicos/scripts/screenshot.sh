#!/usr/bin/env bash
# Boot relicos.run=ui headless with virtio-gpu, screendump via QEMU monitor.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=out; MON=$OUT/mon.sock; LOG=$OUT/serial.log; PPM=$OUT/screenshot.ppm
rm -f "$MON" "$LOG" "$PPM"
qemu-system-x86_64 -m 256M -kernel .cache/vmlinuz-virt -initrd $OUT/initramfs.cpio.gz \
  -no-reboot -vga none -device virtio-gpu -display none \
  -serial file:"$LOG" -monitor unix:"$MON",server,nowait \
  -append "console=ttyS0 quiet relicos.nonet relicos.run=ui panic=10" &
pid=$!
trap '[ -n "$pid" ] && kill $pid 2>/dev/null; wait 2>/dev/null' EXIT
for _ in $(seq 300); do grep -q 'blit ok' "$LOG" 2>/dev/null && break; sleep 0.05; done
sleep 0.15   # let virtio-gpu defio flush mmap writes to the scanout
printf 'screendump %s\nquit\n' "$PPM" | nc -U "$MON" >/dev/null || true
wait "$pid" 2>/dev/null || true; pid=
ls -l "$PPM"
command -v sips >/dev/null && sips -s format png "$PPM" --out "$OUT/screenshot.png" >/dev/null && ls -l "$OUT/screenshot.png"
