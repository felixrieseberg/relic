# REPL over a pipe (fgets fallback path). Keyless: only slash commands.
[ -n "${ANTHROPIC_API_KEY:-}" ] || export ANTHROPIC_API_KEY=sk-ant-e2e-dummy
printf '/status\n/help\n/quit\n' | "$RELIC" >out 2>err
rc=$?
cat out
[ $rc -eq 0 ] || { echo "exit=$rc"; exit 1; }
grep -q 'Relic v' out || { echo "no banner"; exit 1; }
grep -q 'API key' out || { echo "no /status output"; exit 1; }
