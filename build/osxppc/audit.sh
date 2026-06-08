#!/bin/bash
# Mach-O audit for /out/relic -- the analogue of win32's import-table
# audit. Asserts the binary looks like a period Mac OS X 10.1 executable:
#   - cputype ppc, cpusubtype ALL, MH_EXECUTE
#   - LC_UNIXTHREAD entry point (old dyld predates LC_MAIN)
#   - LC_VERSION_MIN_MACOSX matches the $DEPLOY floor
#   - no dylib imports beyond /usr/lib/libSystem.B.dylib
#   - every undefined symbol present in the $DEPLOY-era libSystem export
#     table (the SDK ships the real dylib, so it is ground truth; catches
#     e.g. a stray $LDBL128 printf, which is 10.4+)
# Run inside the relic-osxppc image, after build_relic.sh.
set -e -o pipefail

OTOOL=$TARGET-otool
NM=$TARGET-nm

hdr=$($OTOOL -hv /out/relic)
echo "$hdr"
echo "$hdr" | grep -q ' PPC '       || { echo "!! cputype is not ppc"; exit 1; }
echo "$hdr" | grep -Eq ' PPC +ALL ' || { echo "!! cpusubtype is not ALL"; exit 1; }
echo "$hdr" | grep -q ' EXECUTE '   || { echo "!! not MH_EXECUTE"; exit 1; }

lc=$($OTOOL -l /out/relic)
echo "$lc" | grep -q 'cmd LC_UNIXTHREAD' \
  || { echo "!! no LC_UNIXTHREAD entry point (old dyld needs it)"; exit 1; }
if echo "$lc" | grep -q 'cmd LC_MAIN'; then
  echo "!! LC_MAIN present (10.8+ only)"; exit 1
fi
echo "$lc" | grep -A2 'LC_VERSION_MIN_MACOSX' | grep -q "version $DEPLOY\$" \
  || { echo "!! LC_VERSION_MIN_MACOSX is not $DEPLOY (floor regressed?)"; exit 1; }
echo "version-min: $DEPLOY"

dylibs=$($OTOOL -L /out/relic)
echo "$dylibs"
extra=$(echo "$dylibs" | tail -n +2 \
        | grep -v '/usr/lib/libSystem.B.dylib' || true)
[ -z "$extra" ] || { echo "!! unexpected dylib imports:$extra"; exit 1; }

"$NM" -u /out/relic | sort > /tmp/need
"$NM" -g $APPSDK/usr/lib/libSystem.B.dylib \
  | awk '$2~/^[TDSCIA]$/{print $3}' | sort -u > /tmp/have
need_n=$(wc -l < /tmp/need | tr -d ' ')
[ "$need_n" -gt 0 ] || { echo "!! audit saw zero imports -- nm broken?"; exit 1; }
missing=$(comm -23 /tmp/need /tmp/have | tr '\n' ' ')
[ -z "$missing" ] \
  || { echo "!! imports missing from $DEPLOY libSystem: $missing"; exit 1; }
echo "imports: $need_n symbols, all in $DEPLOY libSystem"
echo "osxppc: audit OK"
