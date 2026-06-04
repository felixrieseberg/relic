# ESC during an in-flight request stops it (without exiting the REPL).
# Input is queued upfront: prompt<CR> ESC /quit<CR>. plat_esc_poll() sees
# ESC on the first poll tick (during the TLS handshake) and aborts; /quit
# then exits cleanly. Only needs to reach api.anthropic.com:443, not auth.
[ -n "${RELIC_MOCK:-}" ] && exit 77
[ -n "${ANTHROPIC_API_KEY:-}" ] || export ANTHROPIC_API_KEY=sk-ant-e2e-dummy
printf 'say hello\r\033/quit\r' | "$PTYRUN" -t 60 -- "$RELIC" >out 2>&1
rc=$?
cat -v out
[ $rc -eq 0 ] || { echo "exit=$rc (should return to prompt, not exit)"; exit 1; }
grep -qF '(interrupted)' out || { echo "no (interrupted) marker"; exit 1; }
