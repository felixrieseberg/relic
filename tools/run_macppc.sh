#!/bin/sh
# Build the macppc target and launch it in SheepShaver with the fresh .dsk
# mounted. One command, every iteration:
#
#   tools/run_macppc.sh                # build 'relic', launch
#   tools/run_macppc.sh --no-build     # skip rebuild, just relaunch
#   SHEEPSHAVER=/path/... tools/run_macppc.sh
#
# Prereq: tools/setup_macppc.sh has populated emu/macppc/ (ROM + ISO + boot.dsk).
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)
EMU=$ROOT/emu/macppc
OUT=$ROOT/dist/macppc

BUILD=1
[ "$1" = "--no-build" ] && { BUILD=0; shift; }
TARGET=relic
DSK=$OUT/Relic.dsk

[ -f "$EMU/rom" ]      || { echo "missing $EMU/rom — run tools/setup_macppc.sh first"; exit 1; }
[ -f "$EMU/boot.dsk" ] || { echo "missing $EMU/boot.dsk — run tools/setup_macppc.sh first"; exit 1; }

if [ "$BUILD" = 1 ]; then
  # Dev convenience (mirrors win32): source .env.local for ANTHROPIC_API_KEY
  # and bake it into RELIC.CFG on the .dsk so the guest doesn't need editing.
  # The generated file lives under dist/ (git-ignored) and is for testing only.
  [ -f "$ROOT/.env.local" ] && . "$ROOT/.env.local"
  mkdir -p "$OUT"
  (
    umask 077   # RELIC.CFG (and the .dsk built from it) carry a real key
    if [ -n "${ANTHROPIC_API_KEY:-}" ]; then
      sed "s|^api_key=.*|api_key=$ANTHROPIC_API_KEY|" \
          "$ROOT/RELIC.CFG.example" > "$OUT/RELIC.CFG"
      echo ">> staged dev RELIC.CFG (api_key from env/.env.local)"
    else
      rm -f "$OUT/RELIC.CFG"
    fi
  )
  echo ">> building macppc/$TARGET"
  make -C "$ROOT/build/macppc" "$TARGET"
fi
[ -f "$DSK" ] || { echo "build did not produce $DSK"; exit 1; }

# Locate SheepShaver.
SS=${SHEEPSHAVER:-}
[ -z "$SS" ] && for c in \
    "$EMU/SheepShaver.app/Contents/MacOS/SheepShaver" \
    "/Applications/SheepShaver.app/Contents/MacOS/SheepShaver" \
    "$HOME/Applications/SheepShaver.app/Contents/MacOS/SheepShaver" \
    "$(command -v SheepShaver 2>/dev/null || true)" \
    "$(command -v sheepshaver 2>/dev/null || true)"; do
  [ -n "$c" ] && [ -x "$c" ] && SS=$c && break
done
[ -n "$SS" ] || { echo "SheepShaver not found — set SHEEPSHAVER= or see tools/setup_macppc.sh"; exit 1; }

# Regenerate prefs every launch so the just-built .dsk is always mounted.
# We point HOME at emu/macppc/ so SheepShaver reads/writes its prefs + nvram
# there instead of polluting the real ~/.
mkdir -p "$EMU/shared"
{
  echo "rom $EMU/rom"
  echo "disk $EMU/boot.dsk"
  echo "disk $DSK"
  [ -f "$EMU/mpw.dsk" ]     && echo "disk $EMU/mpw.dsk"
  [ -f "$EMU/install.iso" ] && echo "cdrom $EMU/install.iso"
  echo "extfs $EMU/shared"
  echo "ramsize 134217728"
  echo "screen win/1024/768"
  echo "frameskip 0"
  echo "ether slirp"
  echo "bootdriver 0"
  echo "nosound true"
  echo "ignoresegv true"
  echo "idlewait true"
  echo "jit true"
} > "$EMU/.sheepshaver_prefs"
# Cocoa builds look here instead of ~/.sheepshaver_prefs — cover both.
mkdir -p "$EMU/Library/Preferences"
cp "$EMU/.sheepshaver_prefs" "$EMU/Library/Preferences/SheepShaver Preferences"

echo ">> launching SheepShaver ($TARGET → $(basename "$DSK"))"
exec env HOME="$EMU" "$SS"
