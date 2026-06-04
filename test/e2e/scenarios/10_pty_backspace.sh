# Raw-mode line editor: type "/quix", backspace, "t", Enter -> dispatches /quit.
# Asserts the "\b \b" erase sequence relic emits for backspace (plat_getkey path).
[ -n "${ANTHROPIC_API_KEY:-}" ] || export ANTHROPIC_API_KEY=sk-ant-e2e-dummy
printf '/quix\bt\r' | "$PTYRUN" -t 20 -- "$RELIC" >out 2>&1
rc=$?
cat -v out
[ $rc -eq 0 ] || { echo "exit=$rc"; exit 1; }
bs=$(printf '\b \b')
grep -qF "$bs" out || { echo "no backspace erase sequence"; exit 1; }
