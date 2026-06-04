#!/bin/bash
# Build default.xbe for the Microsoft Xbox: src/core/* + src/plat/xbox +
# libbearssl.a + nxdk libs. Driven by a small in-tree nxdk project Makefile
# (project.mk) so we get nxdk's linker rules (cxbe, .xbe generation) for
# free. Run inside the relic-nxdk image.
set -e

# Copy sources off the RO bind mount: nxdk's Makefile writes intermediate
# .o files next to the .c, which fails on a read-only tree.
rm -rf /tmp/build && mkdir -p /tmp/build
cp -r /work/src /work/third_party /tmp/build/

# Stage the nxdk-project Makefile at the build root.
cp /work/build/xbox/project.mk /tmp/build/Makefile

DAYS=$(( $(date +%s) / 86400 ))

cd /tmp/build
make -j$(nproc) \
    NXDK_DIR=/opt/nxdk \
    BEARSSL=/cache/libbearssl.a \
    BEARSSL_INC=/tmp/build/third_party/bearssl/inc \
    LUA=/cache/liblua.a \
    LUA_INC=/cache/lua/src \
    SRC_ROOT=/tmp/build/src \
    THIRD_PARTY=/tmp/build/third_party \
    BUILD_UNIX_DAYS=$DAYS \
    V=1

# Re-pack default.xbe with the boot-logo strip (the grayscale mark the
# console tints green at boot). nxdk's default.xbe recipe never passes
# -LOGO, so run cxbe again on main.exe with it. logo.pgm is generated on
# the host by tools/gen_xbox_icons.py and committed. -TITLE must match
# XBE_TITLE in project.mk.
/opt/nxdk/tools/cxbe/cxbe -OUT:bin/default.xbe -TITLE:Relic \
  -LOGO:src/plat/xbox/logo.pgm main.exe

# nxdk drops default.xbe under bin/. Copy out before we wrap it in an ISO
# (which would consume the bin/ dir in a subsequent step).
cp -v bin/default.xbe /out/
# XBMC-family dashboards show default.tbn as the thumbnail next to the xbe.
cp -v src/plat/xbox/default.tbn /out/
[ -f main.exe ] && cp -v main.exe /out/ || true

# Optional bootable ISO. Keep default.xbe intact by staging into a fresh
# dir instead of letting extract-xiso eat bin/.
if [ -x /opt/nxdk/tools/extract-xiso/build/extract-xiso ]; then
  mkdir -p /tmp/iso
  cp bin/default.xbe /tmp/iso/
  cp src/plat/xbox/default.tbn /tmp/iso/
  /opt/nxdk/tools/extract-xiso/build/extract-xiso -c /tmp/iso /out/Relic.iso 2>&1 | tail -3 || true
fi

echo "--- .xbe size ---"
ls -la /out/default.xbe
