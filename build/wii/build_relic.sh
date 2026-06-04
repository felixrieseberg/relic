#!/bin/bash
# Build relic.dol for the Nintendo Wii: src/core/* + src/plat/wii +
# libbearssl.a + liblua.a + libogc. Driven by an in-tree devkitPro project
# Makefile (project.mk) so we get wii_rules' elf2dol step for free.
# Run inside the relic-dkp image.
set -e

# Copy sources off the RO bind mount: the project Makefile writes
# intermediate .o files under build/, which fails on a read-only tree.
rm -rf /tmp/build && mkdir -p /tmp/build
cp -r /work/src /work/third_party /tmp/build/

cp /work/build/wii/project.mk /tmp/build/Makefile

DAYS=$(( $(date +%s) / 86400 ))

cd /tmp/build
make -j$(nproc) \
    BEARSSL=/cache/libbearssl.a \
    BEARSSL_INC=/tmp/build/third_party/bearssl/inc \
    LUA=/cache/liblua.a \
    LUA_INC=/cache/lua/src \
    SRC_ROOT=/tmp/build/src \
    THIRD_PARTY=/tmp/build/third_party \
    BUILD_UNIX_DAYS=$DAYS \
    V=1

cp -v relic.elf relic.dol /out/

# Homebrew Channel banner (128x48). Generated on the host by
# tools/gen_wii_icon.py from src/plat/wii/relic_banner.txt and committed.
cp -v /work/src/plat/wii/icon.png /out/

# Homebrew Channel app layout. meta.xml is static; RELIC.CFG is staged by
# tools/run_wii.sh per launch.
cat > /out/meta.xml <<'EOF'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<app version="1">
  <name>Relic</name>
  <coder>Felix Rieseberg</coder>
  <short_description>Coding agent on the Wii</short_description>
  <long_description>Portable C coding agent for the Anthropic API. USB keyboard required.</long_description>
</app>
EOF

echo "--- .dol size ---"
ls -la /out/relic.dol
