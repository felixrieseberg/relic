#!/usr/bin/env bash
# Cross-compile relicos (chat shell + fb log + agent loop) as a static
# linux/amd64 musl binary via Docker. No litehtml.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
HERE="$ROOT/src/plat/relicos"
IMG=relicos-builder:3.22
docker image inspect "$IMG" >/dev/null 2>&1 || {
  echo "[0/3] building $IMG (one-time)"
  printf 'FROM alpine:3.22\nRUN apk add --no-cache g++ make linux-headers\n' \
    | docker build --platform linux/amd64 -q -t "$IMG" - >/dev/null
}
docker run --rm --platform linux/amd64 \
  -v "$ROOT":/work -w /work/src/plat/relicos \
  "$IMG" sh -euc '
    BEAR=../../../third_party/bearssl
    DAYS=$(( $(date +%s) / 86400 ))
    INC="-Irelicos -I../.. -I$BEAR/inc"
    CXXFLAGS="-std=c++17 -O2 -DNDEBUG $INC"
    CFLAGS="-O2 -DNDEBUG -DBUILD_UNIX_DAYS=$DAYS $INC"
    mkdir -p out/obj
    # Incremental: an .o is fresh if newer than its source AND .hdrs (a stamp
    # touched to the newest header mtime -- crude but correct for this size).
    H=out/obj/.hdrs; touch -r "$(ls -t relicos/*.h ../../core/*.h ../*.h | head -1)" $H
    printf "%s" "$CFLAGS$CXXFLAGS" | cmp -s - out/obj/.flags || { printf "%s" "$CFLAGS$CXXFLAGS" > out/obj/.flags; touch $H; }
    objof() { echo out/obj/$(echo "$1" | tr "/." __).o; }
    fresh() { [ -f "$2" ] && [ "$2" -nt "$1" ] && [ "$2" -nt $H ]; }
    cc1()   { o=$(objof "$1"); fresh "$1" "$o" || gcc $CFLAGS  -c "$1" -o "$o"; }
    cxx1()  { o=$(objof "$1"); fresh "$1" "$o" || g++ $CXXFLAGS -c "$1" -o "$o"; }
    CORE_C="../../core/util.c ../../core/jsonp.c ../../core/http.c \
            ../../core/json_write.c ../../core/anth.c ../../core/conv.c \
            ../../core/netcfg.c ../../core/tls_client.c \
            ../../core/tools.c ../../core/xport.c \
            ../../core/scroll.c ../../core/slash.c ../../core/ui.c \
            ../posix/plat_posix.c ../plat_cfg.c \
            relicos/core_stubs.c"
    SRCS_CXX="relicos/main.cpp relicos/init.cpp relicos/ui.cpp relicos/agent_ui.cpp \
              relicos/evdev.cpp relicos/fb.cpp relicos/textfb.cpp relicos/logview.cpp"
    echo "[1/3] bearssl"
    [ -f $BEAR/build-linux/libbearssl.a ] || make -C $BEAR BUILD=build-linux CC=gcc >/dev/null
    echo "[2/3] core + relicos"
    OBJS=""
    for c in $CORE_C;   do OBJS="$OBJS $(objof "$c")"; cc1  "$c" & done
    for c in $SRCS_CXX; do OBJS="$OBJS $(objof "$c")"; cxx1 "$c" & done
    for j in $(jobs -p); do wait "$j" || exit 1; done
    echo "[3/3] link"
    g++ -static -s $OBJS $BEAR/build-linux/libbearssl.a -o out/relicos
  '
mkdir -p "$HERE/rootfs/bin"
cp "$HERE/out/relicos" "$HERE/rootfs/bin/relicos"
ls -l "$HERE/out/relicos"
file "$HERE/out/relicos"
