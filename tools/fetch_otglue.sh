#!/bin/sh
# Fetch Apple's MPW-GM disk image (public mirror) and:
#   - extract Open Transport static glue into third_party/otglue/
#   - stage the converted disk image at emu/macppc/mpw.dsk so SheepShaver can
#     mount it as a second volume (one-stop install source for MPW Shell +
#     the PPC compilers/linker, giving the guest a real build environment).
# Neither artifact is redistributed in this repo.
#
# Requires: curl, docker (uses the Retro68 image's ConvertDiskImage + hfsutils).
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)
OUT=$ROOT/third_party/otglue
TMP=$ROOT/scratch/otglue
EMU=$ROOT/emu/macppc
mkdir -p "$OUT" "$TMP" "$EMU"

URL=${MPW_GM_URL:-"http://ftpmirror.your.org/pub/misc/apple/ftp.apple.com/developer/Tool_Chest/Core_Mac_OS_Tools/MPW_etc./MPW-GM_Images/MPW-GM.img.bin"}
SHA=99bbfa95bb9800c8ffc572fce6d72e561f012331c5c623fa45f732502b6fa872

have_glue() { [ -f "$OUT/OpenTransportAppPPC.o" ] && [ -f "$OUT/OpenTptInetPPC.o" ]; }
have_mpw()  { [ -f "$EMU/mpw.dsk" ]; }

if have_glue && have_mpw; then
  echo "otglue + mpw.dsk: already present"
  exit 0
fi

if [ ! -f "$TMP/MPW-GM.img.bin" ]; then
  echo "otglue: downloading MPW-GM (~24 MB) from $URL"
  curl -fL "$URL" -o "$TMP/MPW-GM.img.bin"
fi
echo "$SHA  $TMP/MPW-GM.img.bin" | shasum -a 256 -c -

echo "otglue: extracting via Retro68 docker (ConvertDiskImage + hfsutils)"
IMAGE=${RETRO68_IMAGE:-$(awk -F'= ' '/^IMAGE *\?=/{print $2}' "$ROOT/build/macppc/Makefile")}
docker run --rm --platform linux/amd64 \
  -v "$TMP:/tmp/mpw" -v "$OUT:/out" \
  "$IMAGE" bash -c '
    set -e
    H=/Retro68-build/toolchain/bin
    [ -f /tmp/mpw/MPW-GM.dsk ] || $H/ConvertDiskImage /tmp/mpw/MPW-GM.img.bin /tmp/mpw/MPW-GM.dsk >/dev/null
    $H/hmount /tmp/mpw/MPW-GM.dsk >/dev/null
    P=":MPW-GM:Interfaces&Libraries:Libraries:PPCLibraries:"
    $H/hcopy -r "${P}OpenTransportAppPPC.o" /out/OpenTransportAppPPC.o
    $H/hcopy -r "${P}OpenTptInetPPC.o"      /out/OpenTptInetPPC.o
  '

if ! have_mpw; then
  cp "$TMP/MPW-GM.dsk" "$EMU/mpw.dsk"
  echo "otglue: staged MPW disk image at $EMU/mpw.dsk"
fi
ls -l "$OUT" "$EMU/mpw.dsk"
echo "otglue: done"
