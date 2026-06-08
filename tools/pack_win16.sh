#!/bin/sh
# Pack dist/win16/ into a 1.44 MB FAT12 floppy image for 86Box / QEMU / real HW.
# Runs mtools inside the existing relic-ow2 Docker image so the host
# needs nothing extra. (Mirrors tools/pack_win32.sh.)
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)

[ -f dist/win16/RELIC.EXE ] || { echo "dist/win16/RELIC.EXE missing — run 'make win16' first"; exit 1; }

# Ensure a sample config exists (no real key committed). Refresh from
# RELIC.CFG.example unless the user has put a real key in already — in that
# case the key is kept (handy for booting real hardware) and ends up INSIDE
# relic.img, so warn loudly: that image must never be shared or released.
if [ ! -f dist/win16/RELIC.CFG ] \
   || grep -Eq 'api_key=sk-ant-(REPLACE-ME|api03-\.\.\.)' dist/win16/RELIC.CFG; then
  sed 's/$/\r/' RELIC.CFG.example > dist/win16/RELIC.CFG
else
  echo "!! dist/win16/RELIC.CFG holds a real api_key — relic.img will contain it." >&2
  echo "!! Keep that image private; delete RELIC.CFG to pack a clean disk."        >&2
fi

# Short README for the disk (CRLF so Notepad renders it).
sed 's/$/\r/' > dist/win16/README.TXT <<'EOF'
Relic for Windows 3.x
---------------------
1. Copy RELIC.EXE and RELIC.CFG into a directory, e.g. C:\RELIC.
2. Edit RELIC.CFG and put your real key on the api_key= line.
3. In Program Manager: File > Run > C:\RELIC\RELIC.EXE
   (or add an icon; set the working directory to C:\RELIC).
Requires Windows 3.1+ in 386 enhanced mode and a Winsock 1.1
TCP/IP stack: Microsoft TCP/IP-32 on Windows for Workgroups 3.11,
or Trumpet Winsock on Windows 3.1.
EOF

docker run --rm --platform linux/amd64 \
  -v "$ROOT/dist/win16":/d \
  relic-ow2 sh -c '
    set -e
    which mformat >/dev/null || { apt-get update -qq && apt-get install -y -qq mtools; } >/dev/null 2>&1
    rm -f /d/relic.img
    mformat -C -f 1440 -v RELIC -i /d/relic.img ::
    mcopy   -i /d/relic.img /d/RELIC.EXE ::RELIC.EXE
    mcopy   -i /d/relic.img /d/RELIC.CFG  ::RELIC.CFG
    mcopy   -i /d/relic.img /d/README.TXT  ::README.TXT
    echo "--- floppy contents ---"
    mdir    -i /d/relic.img ::
  '

ls -la dist/win16/relic.img
echo "Wrote dist/win16/relic.img (1.44 MB FAT12)"
