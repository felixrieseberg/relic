#!/bin/sh
# Compile all of BearSSL with Open Watcom into a static lib.
# Run inside the relic-ow2 container with /work mounted read-only.
# The minimal TLS profile in tls_client.c means wlink only pulls the .o
# files we actually reference, so building the whole tree here is fine.
set -e
if [ -f /cache/bearssl.lib ] && [ -z "$FORCE" ]; then
  echo "bearssl.lib exists; set FORCE=1 to rebuild"
  exit 0
fi
cd /tmp
rm -rf bobj && mkdir bobj && cd bobj

SRC=/work/third_party/bearssl
CFLAGS="-bt=nt -3r -os -s -zq -fi=/work/src/compat/ow_compat.h -i=$SRC/inc -i=$SRC/src"

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
rm -f bearssl.lib
ls *.o | sed 's/^/+/' | xargs -n40 wlib -q -b bearssl.lib
cp bearssl.lib /cache/ && stat -c '%n %s bytes' /cache/bearssl.lib
