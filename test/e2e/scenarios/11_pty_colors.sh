# ANSI attributes: plat_con_attr only emits escapes when stdout is a tty.
[ -n "${ANTHROPIC_API_KEY:-}" ] || export ANTHROPIC_API_KEY=sk-ant-e2e-dummy
printf '\004' | "$PTYRUN" -t 20 -- "$RELIC" >out 2>&1
rc=$?
cat -v out
[ $rc -eq 0 ] || { echo "exit=$rc"; exit 1; }
esc=$(printf '\033')
grep -qF "${esc}[1m" out || { echo "no bold-on escape"; exit 1; }
grep -qF "${esc}[0m" out || { echo "no reset escape"; exit 1; }
