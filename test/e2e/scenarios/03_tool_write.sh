# Agent loop with tools auto-approved: model writes a file via the Write tool.
[ -n "${ANTHROPIC_API_KEY:-}" ] || exit 77
"$RELIC" --dangerously-skip-permissions \
  -p 'Use the Write tool to create a file named marker.txt whose entire content is E2E_WRITE_OK. Then stop.' \
  >out 2>err
rc=$?
cat out; cat err >&2
[ $rc -eq 0 ] || { echo "exit=$rc"; exit 1; }
[ -f marker.txt ] || { echo "marker.txt not created"; ls -la; exit 1; }
grep -q E2E_WRITE_OK marker.txt || { echo "wrong content:"; cat marker.txt; exit 1; }
