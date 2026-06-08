#!/bin/sh
# Compile all of BearSSL with Open Watcom for the Win386 target into a
# static lib. Run inside the relic-ow2 container with /work mounted
# read-only. Identical to build/win32/build_bearssl.sh except -bt=windows
# (32-bit flat code either way; only the runtime/binding differs).
set -e
if [ -f /cache/bearssl16.lib ] && [ -z "$FORCE" ]; then
  echo "bearssl16.lib exists; set FORCE=1 to rebuild"
  exit 0
fi
cd /tmp
rm -rf bobj && mkdir bobj && cd bobj

SRC=/work/third_party/bearssl
CFLAGS="-bt=windows -3r -os -s -zq -fi=/work/src/compat/ow_compat.h -i=$SRC/inc -i=$SRC/src"

ok=0; fail=0; failed=""
for f in $SRC/src/*.c $SRC/src/*/*.c; do
  if wcc386 $CFLAGS "$f" 2>err.txt; then
    ok=$((ok+1))
  else
    fail=$((fail+1))
    failed="$failed $f"
    echo "=== FAIL: $f ==="
    cat err.txt
  fi
done
echo "compiled: $ok ok, $fail failed"
[ -n "$failed" ] && echo "failed files:$failed"

[ $fail -eq 0 ] || exit 1
rm -f bearssl16.lib
ls *.o | sed 's/^/+/' | xargs -n40 wlib -q -b bearssl16.lib
cp bearssl16.lib /cache/ && stat -c '%n %s bytes' /cache/bearssl16.lib
