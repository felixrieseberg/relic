# -p without permissions: tool call must be auto-denied with the hint message.
[ -n "${ANTHROPIC_API_KEY:-}" ] || exit 77
"$RELIC" -p 'Use the Write tool to create deny.txt containing X.' >out 2>err
cat out; cat err >&2
grep -q 'non-interactive: denied' out || { echo "no denial banner"; exit 1; }
[ ! -e deny.txt ] || { echo "deny.txt should not exist"; exit 1; }
