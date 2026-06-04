#!/usr/bin/env bash
# await.sh LOGFILE PATTERN TIMEOUT_SECS -- QEMU_CMD...
# Launches QEMU_CMD, polls LOGFILE for PATTERN, always reaps qemu on exit.
set -uo pipefail
log="$1"; pat="$2"; timeout="$3"; shift 3
[ "${1:-}" = "--" ] && shift
rm -f "$log"
"$@" & pid=$!
trap '[ -n "${pid:-}" ] && kill "$pid" 2>/dev/null; wait 2>/dev/null' EXIT
for _ in $(seq "$timeout"); do
  kill -0 "$pid" 2>/dev/null || { pid=; break; }
  [ -f "$log" ] && grep -q "$pat" "$log" && { wait "$pid" 2>/dev/null; pid=; exit 0; }
  sleep 1
done
[ -f "$log" ] && grep -q "$pat" "$log" && exit 0
echo "FAIL: '$pat' not found in $log after ${timeout}s" >&2
[ -f "$log" ] && tail -n 40 "$log" >&2
exit 1
