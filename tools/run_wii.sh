#!/bin/sh
# Build the wii target and boot it inside Dolphin.
#   tools/run_wii.sh                  # rebuild .dol, stage SD, boot Dolphin
#   tools/run_wii.sh --no-build       # skip rebuild
#   tools/run_wii.sh --headless       # -b (batch / no GUI chrome)
#   tools/run_wii.sh --restore        # restore the user's pre-setup Dolphin.ini
# Prereq: tools/setup_wii.sh populated emu/wii/ + Dolphin.ini.
# Note: kills a previously-launched copy of this repo's Dolphin first so the
# SD-sync folder and config aren't held by a stale session.
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)
EMU=$ROOT/emu/wii
OUT=$ROOT/dist/wii
DOLPHIN=$EMU/Dolphin.app/Contents/MacOS/Dolphin
DOLPHIN_DIR="$HOME/Library/Application Support/Dolphin"
INI="$DOLPHIN_DIR/Config/Dolphin.ini"

REAL_ROOT=$(git -C "$ROOT" rev-parse --git-common-dir 2>/dev/null \
             | xargs -I{} dirname {} 2>/dev/null)
for src in "${REAL_ROOT:-$ROOT}/.env" "${REAL_ROOT:-$ROOT}/.env.local" \
           "$ROOT/.env" "$ROOT/.env.local"; do
  [ -f "$src" ] && . "$src"
done

BUILD=1
HEADLESS=0
VERBOSE=${VERBOSE:-0}
for a in "$@"; do
  case "$a" in
    --no-build) BUILD=0 ;;
    --headless) HEADLESS=1 ;;
    --restore)
      if [ -f "$INI.relic-bak" ]; then
        mv "$INI.relic-bak" "$INI"
        echo "restored $INI from backup"
      else
        echo "no backup at $INI.relic-bak; nothing to restore"
      fi
      exit 0
      ;;
    *) echo "unknown arg '$a'"; exit 2 ;;
  esac
done

# Kill a stale instance of *this repo's* Dolphin (see header) so the SD-sync
# folder isn't in use. Other Dolphin installs are left alone.
for pid in $(pgrep -f "/Dolphin\.app/Contents/MacOS/Dolphin" 2>/dev/null || true); do
  case "$(ps -o args= -p "$pid" 2>/dev/null)" in
    "$DOLPHIN"*) kill "$pid" 2>/dev/null || true ;;
  esac
done

[ -x "$DOLPHIN" ] || { echo "missing $DOLPHIN — run tools/setup_wii.sh"; exit 1; }
[ -f "$INI" ]     || { echo "missing $INI — run tools/setup_wii.sh"; exit 1; }

if [ "$BUILD" = 1 ]; then
  echo ">> building wii"
  make -C "$ROOT" wii
fi
[ -f "$OUT/relic.dol" ] || { echo "build did not produce $OUT/relic.dol"; exit 1; }

# Stage RELIC.CFG next to the .dol on the virtual SD. Dolphin's folder-sync
# mirrors dist/wii/sd/ onto sd:/ at boot.
(
  umask 077
  if [ -n "$ANTHROPIC_API_KEY" ]; then
    sed "s|^api_key=.*|api_key=$ANTHROPIC_API_KEY|" RELIC.CFG.example > "$OUT/RELIC.CFG.tmp"
  else
    echo "   (ANTHROPIC_API_KEY not set; staging example CFG — set it in .env.local)"
    cp RELIC.CFG.example "$OUT/RELIC.CFG.tmp"
  fi
  [ "$VERBOSE" != 0 ] && printf 'verbose=%s\n' "$VERBOSE" >> "$OUT/RELIC.CFG.tmp"
  mv "$OUT/RELIC.CFG.tmp" "$OUT/RELIC.CFG"
)

echo ">> staging SD card"
make -C "$ROOT/build/wii" sd >/dev/null

# Re-point the SD sync folder at this worktree's dist (the toml may have been
# written from a different worktree).
sed -i.tmp "s|^WiiSDCardSyncFolder *=.*|WiiSDCardSyncFolder = $OUT/sd/|" "$INI"
rm -f "$INI.tmp"

echo ">> booting Dolphin (ELF = $OUT/relic.elf)"
# Dolphin's macOS Qt argv parser drops `-e <file>` with a space; use the
# long --exec= form. -b (batch) skips the game-library window so each
# iteration boots straight into the render view.
#
# SIDevice0=7 plugs an emulated GameCube keyboard into port 1. Dolphin's
# /dev/usb/kbd HLE is Windows-only, so the SI keyboard is the input path
# that works on macOS/Linux; Relic's wii_kbd.c reads it directly.
set -- -b --exec="$OUT/relic.elf" \
       -C Dolphin.Core.SIDevice0=7 \
       -C Dolphin.Core.WiiKeyboard=True
[ "$HEADLESS" = 1 ] && set -- "$@" -C Dolphin.Display.RenderToMain=True
exec "$DOLPHIN" "$@"
