#!/usr/bin/env bash
# End-to-end: boot --ui with a real API key, type a tiny prompt, assert
# the agent produced a reply and the loop terminated (proves request
# build, TLS, parse, agent loop).
set -euo pipefail
cd "$(dirname "$0")/.."
: "${ANTHROPIC_API_KEY:?set ANTHROPIC_API_KEY to run test-agent}"
OUT=out; MON=$OUT/mon.sock; LOG=$OUT/serial.log; KEY=$OUT/.key
rm -f "$MON" "$LOG" "$KEY"
umask 077; printf '%s' "$ANTHROPIC_API_KEY" > "$KEY"

qemu-system-x86_64 -m 256M -kernel .cache/vmlinuz-virt -initrd $OUT/initramfs.cpio.gz \
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 -no-reboot \
  -vga none -device virtio-gpu -device virtio-keyboard -display none \
  -serial file:"$LOG" -monitor unix:"$MON",server,nowait \
  -fw_cfg name=opt/relicos.key,file="$KEY" \
  -append "console=ttyS0 quiet relicos.run=ui panic=10" &
pid=$!
trap '[ -n "${pid:-}" ] && kill $pid 2>/dev/null; wait 2>/dev/null; rm -f "$KEY"' EXIT

mon() { printf '%s\n' "$1" | nc -U "$MON" >/dev/null || true; }
await() { for _ in $(seq "$2"); do grep -q "$1" "$LOG" 2>/dev/null && return 0; sleep 1; done; return 1; }

await 'kbd:' 30 || { echo "FAIL: no kbd"; tail -20 "$LOG"; exit 1; }
await 'blit ok' 30 || { echo "FAIL: no first blit"; exit 1; }

# type "hi" and submit
for k in h i ret; do mon "sendkey $k"; sleep 0.3; done
await 'submit: hi' 30 || { echo "FAIL: submit not seen"; tail -30 "$LOG"; exit 1; }

# wait for agent to reply and the loop to finish
await 'agent says:' 90 || { echo "FAIL: agent did not reply"; tail -40 "$LOG"; exit 1; }
await 'agent: done' 60 || { echo "FAIL: agent loop did not finish"; tail -40 "$LOG"; exit 1; }

mon "screendump $OUT/agent.ppm"; sleep 0.5; mon "quit"; wait "$pid" 2>/dev/null || true; pid=
echo "RELICOS_AGENT_PASS"
command -v sips >/dev/null && sips -s format png "$OUT/agent.ppm" --out "$OUT/agent.png" >/dev/null
