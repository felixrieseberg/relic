# Interactive permission prompt: REPL on a tty, model issues a Write,
# we answer 'a' (auto-accept edits) at the [y/n/a/v] prompt.
[ -n "${ANTHROPIC_API_KEY:-}" ] || exit 77
prompt='Use the Write tool exactly once to create perm.txt containing E2E_PERM. Do not output any other text.'
printf '%s\ra\r/quit\r' "$prompt" | "$PTYRUN" -t 90 -- "$RELIC" >out 2>&1
rc=$?
cat -v out
[ $rc -eq 0 ] || { echo "exit=$rc"; exit 1; }
grep -qF 'Allow? [y]es' out || { echo "permission prompt never shown"; exit 1; }
[ -f perm.txt ] || { echo "perm.txt not written"; ls -la; exit 1; }
grep -q E2E_PERM perm.txt || { echo "wrong content"; cat perm.txt; exit 1; }
