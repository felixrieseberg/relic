#!/bin/bash
# Fetch + compile Lua with devkitPPC's gcc into /cache/liblua.a.
# Mirrors build_bearssl.sh: /work is RO, /cache is RW. Nothing is committed
# to the repo -- the tarball + headers + .a all live under /cache.
# Re-run with FORCE=1 to wipe the build (but keep the tarball).
set -e

VER=5.4.7
SHA=9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30
TGZ=/cache/lua-$VER.tar.gz
SRC=/cache/lua-$VER/src
OUT=/cache/liblua.a

if [ -f "$OUT" ] && [ -d "$SRC" ] && [ -z "$FORCE" ]; then
  echo "liblua.a cached; set FORCE=1 to rebuild"
  ln -sfn "lua-$VER" /cache/lua
  exit 0
fi

[ -f "$TGZ" ] || curl -fL "https://www.lua.org/ftp/lua-$VER.tar.gz" -o "$TGZ"
echo "$SHA  $TGZ" | sha256sum -c -
rm -rf /cache/lua-$VER
tar -xzf "$TGZ" -C /cache

CC=$DEVKITPPC/bin/powerpc-eabi-gcc
AR=$DEVKITPPC/bin/powerpc-eabi-ar

mkdir -p /tmp/lobj && cd /tmp/lobj && rm -f *.o *.a

# Generic ANSI build. devkitPPC's newlib lacks dlopen/popen, so leave
# LUA_USE_LINUX/POSIX undefined; the pure-C fallbacks (no dynamic C loaders,
# io.popen errors, ANSI fseek/ftell) match what the embed needs. The two
# main()s are excluded so liblua.a is purely the embed lib.
MACHDEP="-DGEKKO -mrvl -mcpu=750 -meabi -mhard-float"
CFLAGS="-c -O2 $MACHDEP -I$SRC"

ok=0
for f in "$SRC"/*.c; do
  base=$(basename "$f" .c)
  case "$base" in lua|luac) continue ;; esac
  "$CC" $CFLAGS "$f" -o "$base.o"
  ok=$((ok+1))
done
echo "compiled: $ok"

"$AR" rcs liblua.a *.o
cp liblua.a /cache/
ln -sfn "lua-$VER" /cache/lua
ls -la /cache/liblua.a
