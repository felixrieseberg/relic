# Network self-test: exercises DNS + raw TCP + TLS handshake. Keyless.
[ -n "${RELIC_MOCK:-}" ] && exit 77
"$RELIC" --nettest >out 2>&1
rc=$?
cat out
[ $rc -eq 0 ] || { echo "exit=$rc"; exit 1; }
grep -qi 'api.anthropic.com' out || { echo "no probe line"; exit 1; }
