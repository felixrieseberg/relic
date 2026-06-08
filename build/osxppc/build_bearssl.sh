#!/bin/bash
# Compile BearSSL with powerpc-apple-darwin8-gcc into /cache/libbearssl.a.
# Run inside the relic-osxppc image. /work is RO, /cache is writable.
# Re-run from scratch with FORCE=1.
set -e

if [ -f /cache/libbearssl.a ] && [ -z "$FORCE" ]; then
  echo "libbearssl.a cached; set FORCE=1 to rebuild"
  exit 0
fi

CC=$TARGET-gcc
AR=$TARGET-ar
RANLIB=$TARGET-ranlib

BEAR=/work/third_party/bearssl

# -mcpu=750 keeps the code G3-safe (every 10.1-capable Mac is at least a
# G3); every later PowerPC Mac runs 750 code fine. -force_cpusubtype_ALL
# stops the assembler from stamping the objects ppc750 anyway -- period
# binaries are cpusubtype ALL, which is also what Rosetta and the kernel's
# subtype grading like best. -isysroot $APPSDK + -mlong-double-64 pin the
# 10.1-era ABI: the $LDBL128 libSystem symbols only exist on 10.4+.
# Unlike the wii/xbox targets, sysrng.c stays in: BR_USE_URANDOM works
# back to 10.1 (/dev/urandom arrived with xnu-201).
mkdir -p /tmp/bobj && cd /tmp/bobj
rm -f *.o *.a
CFLAGS="-c -O2 -mcpu=750 -force_cpusubtype_ALL \
  -isysroot $APPSDK -mmacosx-version-min=$DEPLOY -mlong-double-64 \
  -fno-stack-protector \
  -I$BEAR/inc -I$BEAR/src"

ok=0; fail=0; failed=""
for f in $BEAR/src/*.c $BEAR/src/*/*.c; do
  base=$(basename "$f" .c)
  sub=$(basename "$(dirname "$f")")
  obj="${sub}__${base}.o"
  if "$CC" $CFLAGS "$f" -o "$obj" 2>err.txt; then
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

# Mach-O archives need a ranlib table of contents or ld64 refuses them.
"$AR" rc libbearssl.a *.o
"$RANLIB" libbearssl.a
cp libbearssl.a /cache/
ls -la /cache/libbearssl.a
