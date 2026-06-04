#!/bin/sh
# Build RELIC.EXE for Win95: src/core/* + src/plat/win32 + bearssl.lib.
# Requires /cache/bearssl.lib (run build_bearssl.sh first).
# Copy sources off the bind mount into /tmp first to dodge Docker Desktop's
# file-sync race (recently-edited files read back truncated otherwise).
set -e
cd /tmp
rm -rf c w && mkdir c w
cp -r /work/src /work/third_party /tmp/w/
W=/tmp/w
B=$W/third_party/bearssl
cd c
DAYS=$(( $(date +%s) / 86400 ))
INC="-i=$B/inc -i=$W/src -i=$W/third_party"
CFLAGS="-bt=nt -3r -os -s -zq -fi=$W/src/compat/ow_compat.h $INC -DBUILD_UNIX_DAYS=$DAYS"

for f in $W/src/main.c \
         $W/src/core/util.c $W/src/core/jsonp.c \
         $W/src/core/http.c $W/src/core/json_write.c $W/src/core/anth.c \
         $W/src/core/conv.c $W/src/core/tools.c $W/src/core/scroll.c \
         $W/src/core/slash.c $W/src/core/sess.c $W/src/core/xport.c $W/src/core/agent.c $W/src/core/ui.c \
         $W/src/core/tls_client.c $W/src/core/netcfg.c \
         $W/src/plat/plat_cfg.c $W/src/plat/win32/plat_win.c; do
  wcc386 $CFLAGS "$f"
done

wlink system nt option quiet option eliminate name RELIC.EXE \
  file { *.o } \
  library /cache/bearssl.lib library wsock32

# Bind the ghost icon into the PE's .rsrc section. Copy the .rc/.ico next
# to the exe first so wrc needs no include-path gymnastics. Resources add
# no imports, so the Win95 import audit is unaffected.
cp $W/src/plat/win32/relic.rc $W/src/plat/win32/relic.ico .
wrc -q -bt=nt relic.rc RELIC.EXE

cp RELIC.EXE /out/
echo "--- imports ---"
wdump /out/RELIC.EXE | grep -E 'DLL name|subsystem .*version'
echo "--- size ---"
ls -la /out/RELIC.EXE
