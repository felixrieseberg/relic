#!/bin/sh
# End-to-end test driver.
#
#   test/e2e/run.sh <relic-binary-or-wrapper> [scenario-glob]
#
# Each scenario is a standalone shell script under test/e2e/scenarios/ that
# is executed in a fresh empty work dir with $RELIC, $PTYRUN and $WORK set.
# Exit 0 = pass, 77 = skip (e.g. no API key), anything else = fail.
set -u
RELIC=${1:?usage: run.sh <relic-binary> [glob]}
case "$RELIC" in /*) ;; *) RELIC="$PWD/$RELIC" ;; esac
GLOB=${2:-'*.sh'}

cd "$(dirname "$0")/../.."
ROOT=$(pwd)

PTYRUN="$ROOT/dist/posix/ptyrun"
SCEN="$ROOT/test/e2e/scenarios"
WORKROOT="$ROOT/test/e2e/.work"
FIXTURES="$ROOT/test/fixtures"

[ -x "$RELIC" ] || { echo "e2e: not executable: $RELIC" >&2; exit 1; }
[ -x "$PTYRUN" ] || { echo "e2e: missing $PTYRUN (build with: make -C build/posix ptyrun)" >&2; exit 1; }

rm -rf "$WORKROOT"
mkdir -p "$WORKROOT"

pass=0; fail=0; skip=0; failed=""
for s in "$SCEN"/$GLOB; do
  [ -f "$s" ] || continue
  name=$(basename "$s" .sh)
  WORK="$WORKROOT/$name"
  mkdir -p "$WORK"
  printf '  %-28s ' "$name"
  (
    cd "$WORK"
    RELIC="$RELIC" PTYRUN="$PTYRUN" WORK="$WORK" FIXTURES="$FIXTURES" sh "$s"
  ) >"$WORK/log" 2>&1
  rc=$?
  if [ $rc -eq 0 ]; then
    echo "PASS"; pass=$((pass+1))
  elif [ $rc -eq 77 ]; then
    echo "SKIP"; skip=$((skip+1))
  else
    echo "FAIL (rc=$rc)"; fail=$((fail+1)); failed="$failed $name"
    sed 's/^/      | /' "$WORK/log" | tail -n 40
  fi
done

echo
echo "  e2e: $pass passed, $fail failed, $skip skipped"
[ $fail -eq 0 ] || { echo "  failed:$failed"; exit 1; }
