#!/bin/sh
# Build the win16 target and boot it inside real Windows 95 under QEMU.
# There is no Windows 3.11 image in the dev loop, but Win95 runs Win386 NE
# binaries natively (same loader, same 16-bit WINSOCK.DLL ABI), so it is the
# practical smoke-test environment; reuses the emu/win95/ assets and machine
# shape from tools/run_win95.sh.
#
#   tools/run_win16.sh                 # rebuild EXE, repack D:, boot
#   tools/run_win16.sh --no-build      # skip rebuild, just repack + boot
#   tools/run_win16.sh --fresh         # discard overlay, start from clean OS
#   tools/run_win16.sh --reload        # rebuild + hot-swap D: in running guest
#
# Inside the guest, run D:\INSTALL.BAT (installs to C:\RELIC16), then launch
# C:\RELIC16\RELIC.EXE — on Windows 3.x a DOS box cannot start a Windows app,
# so the batch does not try.
#
# Prereq: tools/setup_win95.sh has populated emu/win95/.
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)
EMU=$ROOT/emu/win95
OUT=$ROOT/dist/win16
MON=$EMU/mon.sock
QEMU=${QEMU:-qemu-system-i386}

BUILD=1
RELOAD=0
for a in "$@"; do
  case "$a" in
    --no-build) BUILD=0 ;;
    --fresh)    rm -f "$EMU/hda.qcow2" ;;
    --reload)   RELOAD=1 ;;
    *) echo "unknown arg '$a'"; exit 2 ;;
  esac
done

[ -L "$EMU/base.img" ] || [ -f "$EMU/base.img" ] \
  || { echo "missing $EMU/base.img — run tools/setup_win95.sh first"; exit 1; }
[ -f "$EMU/hda.qcow2" ] \
  || qemu-img create -q -f qcow2 -b base.img -F raw "$EMU/hda.qcow2"

if [ "$BUILD" = 1 ]; then
  echo ">> building win16"
  make -C "$ROOT" win16
fi
[ -f "$OUT/RELIC.EXE" ] || { echo "build did not produce $OUT/RELIC.EXE"; exit 1; }

# --- pack D: ----------------------------------------------------------------
echo ">> packing $EMU/relic16.iso"
(
umask 077   # RELIC.CFG + relic16.iso may carry a real key
if [ -n "$ANTHROPIC_API_KEY" ]; then
  sed "s|^api_key=.*|api_key=$ANTHROPIC_API_KEY|" RELIC.CFG.example \
    | sed 's/$/\r/' > "$OUT/RELIC.CFG"
else
  echo "   (ANTHROPIC_API_KEY not set; disc gets the example config)"
  sed 's/$/\r/' RELIC.CFG.example > "$OUT/RELIC.CFG"
fi
printf '@ECHO OFF\r
IF NOT EXIST C:\\RELIC16\\NUL MD C:\\RELIC16\r
COPY /Y D:\\RELIC.EXE  C:\\RELIC16\r
COPY /Y D:\\RELIC.CFG C:\\RELIC16\r
ECHO Installed. Now run C:\\RELIC16\\RELIC.EXE (File ^> Run).\r
' > "$OUT/INSTALL.BAT"
STAGE="$OUT/iso"
rm -rf "$STAGE"; mkdir -p "$STAGE"
cp "$OUT/RELIC.EXE" "$OUT/RELIC.CFG" "$OUT/INSTALL.BAT" tools/win95/INSTALL.PIF "$STAGE/"
rm -f "$EMU/relic16.iso"
if command -v hdiutil >/dev/null; then
  hdiutil makehybrid -quiet -iso -joliet -default-volume-name RELIC \
    -o "$EMU/relic16.iso" "$STAGE"
elif command -v xorriso >/dev/null; then
  xorriso -as mkisofs -quiet -iso-level 3 -J -V RELIC -o "$EMU/relic16.iso" "$STAGE"
else
  echo "need hdiutil (macOS) or xorriso to build relic16.iso"; exit 1
fi
rm -rf "$STAGE"
)

# --- reload -----------------------------------------------------------------
if [ "$RELOAD" = 1 ]; then
  [ -S "$MON" ] || { echo "no running guest at $MON — start one with 'make run-win16'"; exit 1; }
  mon() { printf '%s\n' "$1" | nc -w 1 -U "$MON" >/dev/null 2>&1 || true; }
  echo ">> swapping D: in running guest"
  mon "eject -f ide1-cd0"
  mon "change ide1-cd0 \"$EMU/relic16.iso\" raw"
  echo "   done — re-run D:\\INSTALL.BAT inside the guest"
  exit 0
fi

# --- boot -------------------------------------------------------------------
echo ">> booting Windows 95 (D: = fresh RELIC disc; run D:\\INSTALL.BAT)"
exec "$QEMU" \
  -M pc,acpi=off,hpet=off -cpu pentium -m 128 \
  -vga cirrus \
  -drive file="$EMU/hda.qcow2",format=qcow2 \
  -cdrom "$EMU/relic16.iso" \
  -boot order=c \
  -device sb16 \
  -netdev user,id=n0 -device ne2k_isa,netdev=n0,iobase=0x300,irq=10 \
  -rtc base=localtime \
  -monitor unix:"$MON",server,nowait \
  $QEMU_EXTRA
