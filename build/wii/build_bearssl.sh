#!/bin/bash
# Compile BearSSL with devkitPPC's gcc into /cache/libbearssl.a.
# Run inside the relic-dkp image. /work is RO, /cache is writable.
# Re-run from scratch with FORCE=1.
set -e

if [ -f /cache/libbearssl.a ] && [ -z "$FORCE" ]; then
  echo "libbearssl.a cached; set FORCE=1 to rebuild"
  exit 0
fi

CC=$DEVKITPPC/bin/powerpc-eabi-gcc
AR=$DEVKITPPC/bin/powerpc-eabi-ar

BEAR=/work/third_party/bearssl
COMPAT=/work/src/compat/wii_compat.h

mkdir -p /tmp/bobj && cd /tmp/bobj
rm -f *.o *.a

# MACHDEP mirrors wii_rules: tune for the 750CL, hard-float, RVL ABI.
MACHDEP="-DGEKKO -mrvl -mcpu=750 -meabi -mhard-float"
CFLAGS="-c -O2 $MACHDEP -fno-stack-protector \
  -include $COMPAT \
  -I$BEAR/inc -I$BEAR/src"

# Unlike nxdk's i386 target, GCC for PPC doesn't define __i386__/__SSE2__ so
# BearSSL's intrinsic backends compile out cleanly. We still skip sysrng.c:
# it gates on BR_USE_URANDOM (off) and would compile to a no-op anyway, but
# keeping the same exclusion as xbox means src/compat/wii_bearssl.c is the
# single source of br_prng_seeder_system.
SKIP_RE='(sysrng)'

ok=0; fail=0; failed=""; skip=0
for f in $BEAR/src/*.c $BEAR/src/*/*.c; do
  base=$(basename "$f" .c)
  if echo "$base" | grep -Eq "^${SKIP_RE}"; then
    skip=$((skip+1))
    continue
  fi
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
echo "skipped (sysrng, replaced by compat/wii_bearssl.c): $skip"
echo "compiled: $ok ok, $fail failed"
[ -n "$failed" ] && echo "failed files:$failed"
[ $fail -eq 0 ] || exit 1

"$AR" rcs libbearssl.a *.o
cp libbearssl.a /cache/
ls -la /cache/libbearssl.a
