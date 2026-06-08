#!/bin/sh
# Build RELIC.EXE for Windows 3.x: src/core/* + src/plat/win16 + bearssl16.lib.
# Requires /cache/bearssl16.lib (run build_bearssl.sh first).
#
# Pipeline: wcc386 -bt=windows compiles 32-bit flat objects, wlink emits a
# Phar Lap REX, and wbind staples Watcom's Win386 supervisor (win386.ext)
# on front to make a real NE executable that Windows 3.1+ loads. wrc then
# adds the icon to the NE resource table.
#
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
# Win16 API headers, not the NT set the image's INCLUDE defaults to.
export INCLUDE=$WATCOM/h:$WATCOM/h/win
# -bw selects the default-windowing runtime: stdio gets a scrollable text
# window (Win 3.x has no console subsystem).
CFLAGS="-bt=windows -bw -3r -os -s -zq -fi=$W/src/compat/ow_compat.h $INC -DBUILD_UNIX_DAYS=$DAYS"

for f in $W/src/main.c \
         $W/src/core/util.c $W/src/core/jsonp.c \
         $W/src/core/http.c $W/src/core/json_write.c $W/src/core/anth.c \
         $W/src/core/conv.c $W/src/core/tools.c $W/src/core/scroll.c \
         $W/src/core/slash.c $W/src/core/sess.c $W/src/core/xport.c $W/src/core/agent.c $W/src/core/ui.c \
         $W/src/core/tls_client.c $W/src/core/netcfg.c \
         $W/src/plat/plat_cfg.c $W/src/plat/win16/plat_win16.c; do
  wcc386 $CFLAGS "$f"
done

# lowercase .rex: wbind composes the filename itself and the Linux fs is
# case-sensitive.
wlink system win386 option quiet option eliminate option stack=64k \
  name relic.rex file { *.o } \
  library /cache/bearssl16.lib

# Bind the supervisor + ghost icon in one step: wbind runs wrc itself (-R)
# so the resource table lands in the stub NE without disturbing the
# appended REX (a post-hoc wrc pass shifts it and the supervisor then
# reports "Invalid EXE").
# (wbind derives every filename from the first argument, so the .rc must
# be named relic.rc here.)
cp $W/src/plat/win16/relic16.rc relic.rc
cp $W/src/plat/win32/relic.ico .
wbind relic -R -q -bt=windows relic.rc
mv -f relic.exe RELIC.EXE

cp RELIC.EXE /out/
echo "--- modules ---"
wdump /out/RELIC.EXE | grep -B2 -A20 'Module Reference Table' || true
echo "--- size ---"
ls -la /out/RELIC.EXE
