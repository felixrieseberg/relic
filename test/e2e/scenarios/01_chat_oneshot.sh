# One-shot chat: real API round-trip, no tools, no tty.
[ -n "${ANTHROPIC_API_KEY:-}" ] || exit 77
"$RELIC" -c -p 'Reply with exactly the token RELIC_E2E_OK and nothing else.' >out 2>err
rc=$?
cat out; cat err >&2
[ $rc -eq 0 ] || { echo "exit=$rc"; exit 1; }
grep -q RELIC_E2E_OK out || { echo "marker not found"; exit 1; }
