# Offline one-shot chat against the mock-TLS binary; CI smoke test.
[ -n "${RELIC_MOCK:-}" ] || exit 77
export ANTHROPIC_API_KEY=sk-ant-mock
export RELIC_MOCK_RESPONSES="$FIXTURES/mock_end_turn.txt"
export RELIC_MOCK_SENT="$WORK/sent"
"$RELIC" -c -p 'say RELIC_E2E_OK' >out 2>err
rc=$?
cat out; cat err >&2
[ $rc -eq 0 ] || { echo "exit=$rc"; exit 1; }
grep -q RELIC_E2E_OK out || { echo "marker not in output"; exit 1; }
grep -q 'POST /v1/messages HTTP/1.1' sent || { echo "no request captured"; exit 1; }
grep -q '"say RELIC_E2E_OK"' sent || { echo "prompt not in body"; exit 1; }
