# Bare ESC at the prompt must not swallow the next keystroke.
# plat_getkey() probes one byte after ESC for a CSI/SS3 introducer; if it
# isn't '[' or 'O' that byte is pushed back, not dropped. So ESC '/' 'q'...
# yields "/quit" (clean exit), not "quit" sent to the API (timeout).
[ -n "${ANTHROPIC_API_KEY:-}" ] || export ANTHROPIC_API_KEY=sk-ant-e2e-dummy
printf '\033/quit\r' | "$PTYRUN" -t 10 -- "$RELIC" --ip 127.0.0.1 >out 2>&1
rc=$?
cat -v out
[ $rc -eq 0 ] || { echo "exit=$rc"; exit 1; }
grep -qF "> /quit" out || { echo "leading char after ESC was dropped"; exit 1; }
