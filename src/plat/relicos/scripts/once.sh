#!/usr/bin/env bash
# once.sh "prompt text" [outname]
# Boot, send the prompt to Claude once, screendump the result.
set -euo pipefail
cd "$(dirname "$0")/.."
PROMPT="${1:-hello}"; NAME="${2:-once}"
: "${ANTHROPIC_API_KEY:?set ANTHROPIC_API_KEY}"
OUT=out; MON=$OUT/mon.sock; LOG=$OUT/serial.log; KEY=$OUT/.key; PRM=$OUT/.prompt
rm -f "$MON" "$LOG" "$KEY" "$PRM" "$OUT/$NAME.ppm" "$OUT/$NAME.png"
trap 'rm -f "$KEY" "$PRM"' EXIT
umask 077; printf '%s' "$ANTHROPIC_API_KEY" > "$KEY"
printf '%s' "$PROMPT" > "$PRM"

[ -f "$OUT/data.qcow2" ] || qemu-img create -f qcow2 "$OUT/data.qcow2" 256M >/dev/null
qemu-system-x86_64 -m 512M -kernel .cache/vmlinuz-virt -initrd $OUT/initramfs.cpio.gz \
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 -no-reboot \
  -vga none -device virtio-gpu -display none \
  -drive file=$OUT/data.qcow2,if=none,id=d0,format=qcow2 -device virtio-blk-pci,drive=d0 \
  -serial file:"$LOG" -monitor unix:"$MON",server,nowait \
  -fw_cfg name=opt/relicos.key,file="$KEY" \
  -fw_cfg name=opt/relicos.prompt,file="$PRM" \
  -append "console=ttyS0 quiet relicos.run=once relicos.model=${RELICOS_MODEL:-claude-sonnet-4-6} panic=10" &
pid=$!
trap '[ -n "${pid:-}" ] && kill $pid 2>/dev/null; wait 2>/dev/null; rm -f "$KEY" "$PRM"' EXIT  # re-arm with pid

mon() { printf '%s\n' "$1" | nc -U "$MON" >/dev/null || true; }
for _ in $(seq 600); do grep -q 'RELICOS_ONCE_' "$LOG" 2>/dev/null && break; sleep 1; done
grep -q 'RELICOS_ONCE_PASS' "$LOG" || { echo "FAIL"; tail -40 "$LOG"; exit 1; }
mon "screendump $OUT/$NAME.ppm"; sleep 0.5; mon "quit"; wait "$pid" 2>/dev/null || true; pid=
command -v sips >/dev/null && sips -s format png "$OUT/$NAME.ppm" --out "$OUT/$NAME.png" >/dev/null
ls -l "$OUT/$NAME.png" 2>/dev/null || ls -l "$OUT/$NAME.ppm"
