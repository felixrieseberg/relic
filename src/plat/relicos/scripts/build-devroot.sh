#!/usr/bin/env bash
# Harvest tcc + musl/linux headers from Alpine into .cache/devroot/ so
# the agent can compile C inside the guest. Run once; output is gitignored.
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
DEV="$HERE/.cache/devroot"
rm -rf "$DEV"; mkdir -p "$DEV"
docker run --rm --platform linux/amd64 -v "$DEV":/out alpine:3.22 sh -euc '
  apk add --no-cache tcc tcc-dev tcc-libs-static musl-dev linux-headers >/dev/null
  # tcc binary + its lib + crt/obj
  mkdir -p /out/usr/bin /out/usr/lib /out/usr/include /out/lib
  cp /usr/bin/tcc /out/usr/bin/
  cp -a /usr/lib/tcc /out/usr/lib/
  cp /usr/lib/libtcc.so* /out/usr/lib/ 2>/dev/null || true
  ls -la /usr/lib/libtcc* /usr/bin/tcc
  # musl crt + libc.a for static linking, libc.so for dynamic
  cp /usr/lib/crt*.o /usr/lib/libc.a /out/usr/lib/ 2>/dev/null || true
  cp /lib/ld-musl-*.so.1 /lib/libc.musl-*.so.1 /out/lib/ 2>/dev/null || true
  # headers
  cp -a /usr/include/. /out/usr/include/
  # quick self-test inside the build container
  echo "int main(){return 42;}" >/tmp/t.c
  /usr/bin/tcc -run /tmp/t.c || [ $? -eq 42 ]
'
du -sh "$DEV" "$DEV"/usr/include "$DEV"/usr/lib 2>/dev/null
