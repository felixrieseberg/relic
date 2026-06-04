#!/usr/bin/env bash
# Cross-compile relic as a static linux/amd64 musl binary via Docker.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
HERE="$ROOT/src/plat/relicos"
docker run --rm --platform linux/amd64 \
  -v "$ROOT":/work -w /work \
  alpine:3.22 sh -euc '
    apk add --no-cache build-base >/dev/null
    make -C third_party/bearssl BUILD=build-linux CC=cc >/dev/null
    DAYS=$(( $(date +%s) / 86400 ))
    mkdir -p src/plat/relicos/out
    cc -static -Os -s -fsigned-char -DBUILD_UNIX_DAYS=$DAYS \
       -Ithird_party/bearssl/inc -Isrc \
       src/main.c \
       src/core/util.c src/core/jsonp.c src/core/http.c src/core/json_write.c \
       src/core/anth.c src/core/conv.c src/core/netcfg.c src/core/ui.c \
       src/core/tools.c src/core/scroll.c src/core/slash.c src/core/sess.c src/core/xport.c src/core/agent.c src/core/tls_client.c \
       src/plat/posix/plat_posix.c src/plat/plat_cfg.c \
       third_party/bearssl/build-linux/libbearssl.a \
       -o src/plat/relicos/out/relic
  '
cp "$HERE/out/relic" "$HERE/rootfs/bin/relic"
ls -l "$HERE/out/relic"
