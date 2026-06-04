# PgUp at empty prompt opens the scrollback pager (full-screen redraw).
[ -n "${ANTHROPIC_API_KEY:-}" ] || export ANTHROPIC_API_KEY=sk-ant-e2e-dummy
# ESC [ 5 ~  = PgUp; 'q' leaves the pager; Ctrl-D exits the REPL.
printf '\033[5~q\004' | "$PTYRUN" -t 20 -r 24 -c 80 -- "$RELIC" >out 2>&1
rc=$?
cat -v out
[ $rc -eq 0 ] || { echo "exit=$rc"; exit 1; }
esc=$(printf '\033')
grep -qF "${esc}[2J" out || { echo "no clear-screen (pager never opened)"; exit 1; }
grep -qF 'PgUp/PgDn' out || { echo "no pager status bar"; exit 1; }
