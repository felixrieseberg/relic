#!/bin/bash
# Fetch + compile Lua with nxdk's clang into /cache/liblua.a.
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

CC=/opt/nxdk/bin/nxdk-cc
AR=llvm-ar

mkdir -p /tmp/lobj && cd /tmp/lobj && rm -f *.o *.a

# Generic ANSI build. nxdk-cc predefines _WIN32 (it targets i386-pc-win32),
# which makes luaconf.h set LUA_USE_WINDOWS -> LUA_DL_DLL and pulls in
# LoadLibrary/_popen/_fseeki64 that nxdk's Win32 shim doesn't have. -U_WIN32
# drops Lua to its pure-C fallbacks: dynamic C loaders error "not enabled"
# (require for .lua still works via the Lua searcher), io.popen errors,
# fseek/ftell are the ANSI ones. pdclib (platform/xbox) supplies math.h (x87
# impls), setjmp, strtod, locale, signal.h, time, clock, system (stub). The
# two main()s are excluded so liblua.a is purely the embed lib.
CFLAGS="-c -O2 -fno-stack-protector -fsigned-char -I$SRC -U_WIN32"

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
