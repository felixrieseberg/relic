# Raw-mode line editor across a soft-wrap boundary. With a 20-col pty the
# 18th typed char fills the prompt line; relic must force the deferred wrap
# (" \b") so the next backspace can emit the cursor-up/erase sequence and
# walk back onto line 1. Ends with /quit so no API call is made.
[ -n "${ANTHROPIC_API_KEY:-}" ] || export ANTHROPIC_API_KEY=sk-ant-e2e-dummy
{
  # 18 chars fill cols 2..19, 5 more land on the wrapped line
  printf 'xxxxxxxxxxxxxxxxxxxxxxx'
  # backspace through the wrap and all the way to an empty buffer
  printf '\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b'
  printf '/quit\r'
} | "$PTYRUN" -t 20 -c 20 -- "$RELIC" >out 2>&1
rc=$?
cat -v out
[ $rc -eq 0 ] || { echo "exit=$rc"; exit 1; }
# forced-wrap nudge after the line-filling char
grep -qF "$(printf 'x \bx')" out || { echo "no forced wrap"; exit 1; }
# cursor-up + goto-last-col + erase-EOL when backspace crosses the wrap
grep -qF "$(printf '\033[A\033[20G\033[K')" out || { echo "no backwrap"; exit 1; }
