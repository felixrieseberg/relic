#!/bin/sh
# Build the win32 target and boot it inside real Windows 95 under QEMU.
# One command, every iteration:
#
#   tools/run_win95.sh                 # rebuild EXE, repack D:, boot
#   tools/run_win95.sh --no-build      # skip rebuild, just repack + boot
#   tools/run_win95.sh --fresh         # discard overlay, start from clean OS
#   tools/run_win95.sh --reload        # rebuild + hot-swap D: in running guest
#   QEMU_EXTRA='-device sb16,dma=5' tools/run_win95.sh
#
# Prereq: tools/setup_win95.sh has populated emu/win95/.
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)
EMU=$ROOT/emu/win95
OUT=$ROOT/dist/win32
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
  echo ">> building win32"
  make -C "$ROOT" win32
fi
[ -f "$OUT/RELIC.EXE" ] || { echo "build did not produce $OUT/RELIC.EXE"; exit 1; }

# --- pack D: ----------------------------------------------------------------
# Win95's protected-mode floppy driver can't enumerate QEMU's FDC reliably, so
# ship the build on an ISO instead — the guest's CD-ROM driver has no such
# trouble and the hot-swap path works the same way via the monitor.
echo ">> packing $EMU/relic.iso"
(
umask 077   # RELIC.CFG + relic.iso carry a real key
if [ -n "$ANTHROPIC_API_KEY" ]; then
  sed "s|^api_key=.*|api_key=$ANTHROPIC_API_KEY|" RELIC.CFG.example \
    | sed 's/$/\r/' > "$OUT/RELIC.CFG"
else
  echo "   (ANTHROPIC_API_KEY not set; disc gets the example config)"
  sed 's/$/\r/' RELIC.CFG.example > "$OUT/RELIC.CFG"
fi
# Win95 COMMAND.COM rejects 'COPY src C:\RELIC\' with "Invalid directory" — the
# trailing backslash makes it parse an empty filename. Drop it; MD guarantees
# C:\RELIC is a directory so the bare path copies into it. Then cd + launch the
# freshly installed EXE so a reload is one double-click.
printf '@ECHO OFF\r
IF NOT EXIST C:\\RELIC\\NUL MD C:\\RELIC\r
COPY /Y D:\\RELIC.EXE  C:\\RELIC\r
COPY /Y D:\\RELIC.CFG C:\\RELIC\r
ECHO Installed to C:\\RELIC\r
C:\r
CD \\RELIC\r
RELIC.EXE\r
' > "$OUT/INSTALL.BAT"
STAGE="$OUT/iso"
rm -rf "$STAGE"; mkdir -p "$STAGE"
cp "$OUT/RELIC.EXE" "$OUT/RELIC.CFG" "$OUT/INSTALL.BAT" tools/win95/INSTALL.PIF "$STAGE/"
rm -f "$EMU/relic.iso"
if command -v hdiutil >/dev/null; then
  hdiutil makehybrid -quiet -iso -joliet -default-volume-name RELIC \
    -o "$EMU/relic.iso" "$STAGE"
elif command -v xorriso >/dev/null; then
  xorriso -as mkisofs -quiet -iso-level 3 -J -V RELIC -o "$EMU/relic.iso" "$STAGE"
else
  echo "need hdiutil (macOS) or xorriso to build relic.iso"; exit 1
fi
rm -rf "$STAGE"
)

# --- reload -----------------------------------------------------------------
# Hot-swap D: in an already-running guest via the HMP monitor socket. The guest
# sees a disc-change, so Explorer/DOS re-read it instead of serving stale
# cached sectors from the old image.
if [ "$RELOAD" = 1 ]; then
  [ -S "$MON" ] || { echo "no running guest at $MON — start one with 'make run-win95'"; exit 1; }
  mon() { printf '%s\n' "$1" | nc -w 1 -U "$MON" >/dev/null 2>&1 || true; }
  echo ">> swapping D: in running guest"
  mon "eject -f ide1-cd0"
  mon "change ide1-cd0 \"$EMU/relic.iso\" raw"
  echo "   done — re-run D:\\INSTALL.BAT inside the guest"
  exit 0
fi

# --- boot -------------------------------------------------------------------
# Machine shape matches the image's installed drivers: ISA NE2000 at 0x300/IRQ10,
# SB16 audio, Cirrus VGA, no ACPI. SLIRP user networking gives the guest outbound
# TCP. The explicit -vga cirrus matters: modern QEMU defaults to stdvga, and the
# guest's Cirrus driver against that paints vertical stripes instead of a desktop.
echo ">> booting Windows 95 (D: = fresh RELIC disc)"
exec "$QEMU" \
  -M pc,acpi=off,hpet=off -cpu pentium -m 128 \
  -vga cirrus \
  -drive file="$EMU/hda.qcow2",format=qcow2 \
  -cdrom "$EMU/relic.iso" \
  -boot order=c \
  -device sb16 \
  -netdev user,id=n0 -device ne2k_isa,netdev=n0,iobase=0x300,irq=10 \
  -rtc base=localtime \
  -monitor unix:"$MON",server,nowait \
  $QEMU_EXTRA
