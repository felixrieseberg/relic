#!/bin/bash
# Compile BearSSL with nxdk's clang into /cache/libbearssl.a.
# Run inside the relic-nxdk image. /work is RO, /cache is writable.
# Re-run from scratch with FORCE=1.
set -e

if [ -f /cache/libbearssl.a ] && [ -z "$FORCE" ]; then
  echo "libbearssl.a cached; set FORCE=1 to rebuild"
  exit 0
fi

# nxdk's activate just exports PATH=$NXDK_DIR/bin:$PATH; the compiler is
# nxdk-cc (a clang wrapper that sets the i386-pc-win32-elf target etc.).
# We invoke the wrappers by absolute path so a stale PATH/sourcing failure
# can't silently zero $CC.
source /opt/nxdk/bin/activate
CC=/opt/nxdk/bin/nxdk-cc
# nxdk-lib wraps llvm-lib (MS LIB.EXE syntax); use llvm-ar directly with
# the traditional `rcs` flags. The .a is linked by nxdk-link which accepts
# either format.
AR=llvm-ar

BEAR=/work/third_party/bearssl
COMPAT=/work/src/compat/xbox_compat.h

mkdir -p /tmp/bobj && cd /tmp/bobj
rm -f *.o *.a

CFLAGS="-c -O2 -fno-stack-protector -fsigned-char \
  -include $COMPAT \
  -I$BEAR/inc -I$BEAR/src"

# Skip x86-intrinsic translation units. BearSSL's inner.h auto-defines
# BR_AES_X86NI/BR_CHACHA20_SSE2/etc. when __i386__ is set (which nxdk-cc
# does). Those .c files then #include <wmmintrin.h>/<x86intrin.h>/<intrin.h>,
# which need a real Linux/Win64 sysroot -- nxdk-cc's i386-pc-win32-elf
# target doesn't ship them. The portable fallbacks (aes_ct, aes_small,
# chacha20_ct, hmac_drbg) cover the same functionality and are what the
# linker picks anyway via tls_client.c's minimal-profile vtables.
SKIP_RE='(aes_x86ni|chacha20_sse2|ghash_pclmul|sysrng)'

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
echo "skipped (x86 intrinsics, not used in our minimal TLS profile): $skip"
echo "compiled: $ok ok, $fail failed"
[ -n "$failed" ] && echo "failed files:$failed"
[ $fail -eq 0 ] || exit 1

"$AR" rcs libbearssl.a *.o
cp libbearssl.a /cache/
ls -la /cache/libbearssl.a
