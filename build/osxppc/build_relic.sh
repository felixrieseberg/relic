#!/bin/bash
# Build /out/relic for PowerPC Mac OS X 10.1+: src/core/* + src/plat/posix
# + plat_cfg.c + main.c, linked with /cache/libbearssl.a against the
# 10.1.5 SDK. audit.sh then asserts the Mach-O headers and imports look
# like a period binary.
# Run inside the relic-osxppc image.
set -e -o pipefail

CC=$TARGET-gcc

SRC=/work/src
BEAR=/work/third_party/bearssl
DAYS=$(( $(date +%s) / 86400 ))

# Same flag story as build/posix: -fsigned-char because char defaults to
# unsigned on ppc; -static-libgcc because old OS X ships no libgcc_s a
# modern GCC could use. -mcpu=750 keeps G3s in, -force_cpusubtype_ALL
# keeps the header at cpusubtype ALL regardless, and -isysroot $APPSDK +
# -mlong-double-64 pin the 10.1-era ABI (matches build_bearssl.sh).
$CC -O2 -mcpu=750 -force_cpusubtype_ALL \
  -isysroot $APPSDK -mmacosx-version-min=$DEPLOY -mlong-double-64 \
  -Wall -Wextra -fsigned-char -fno-stack-protector \
  -DBUILD_UNIX_DAYS=$DAYS \
  -I$BEAR/inc -I$SRC \
  $SRC/main.c $SRC/core/*.c \
  $SRC/plat/posix/plat_posix.c $SRC/plat/plat_cfg.c \
  /cache/libbearssl.a \
  -static-libgcc -Wl,-force_cpusubtype_ALL -o /out/relic

file /out/relic || true
ls -la /out/relic
