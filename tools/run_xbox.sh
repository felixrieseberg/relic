#!/bin/sh
# Build the xbox target and boot it inside xemu.
#   tools/run_xbox.sh                 # rebuild .xbe, stage CFG, boot xemu
#   tools/run_xbox.sh --no-build      # skip rebuild
#   tools/run_xbox.sh --headless      # off-screen (no window; tail the serial log)
#   tools/run_xbox.sh --restore       # restore the user's pre-setup xemu.toml
# Prereq: tools/setup_xbox.sh populated emu/xbox/ + xemu.toml.
# Note: kills a previously-launched copy of this repo's xemu first so the
# qcow2 HDD isn't lock-held.
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)
EMU=$ROOT/emu/xbox
OUT=$ROOT/dist/xbox
XEMU=$EMU/xemu.app/Contents/MacOS/xemu
XEMU_DIR="$HOME/Library/Application Support/xemu/xemu"
TOML="$XEMU_DIR/xemu.toml"

# Mirrors run_win95 / run_macppc: source .env[.local] so ANTHROPIC_API_KEY
# lands in RELIC.CFG without the user exporting. From a worktree the main
# repo owns the real .env.local; source its path FIRST so a worktree-local
# .env.local can still override for per-branch experiments.
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
      if [ -f "$TOML.relic-bak" ]; then
        mv "$TOML.relic-bak" "$TOML"
        echo "restored $TOML from backup"
      else
        echo "no backup at $TOML.relic-bak; nothing to restore"
      fi
      exit 0
      ;;
    *) echo "unknown arg '$a'"; exit 2 ;;
  esac
done

# Kill a stale instance of *this repo's* xemu (see header) so the qcow2 HDD
# isn't lock-held. Other xemu installs are left alone.
for pid in $(pgrep -f "/xemu\.app/Contents/MacOS/xemu" 2>/dev/null || true); do
  case "$(ps -o args= -p "$pid" 2>/dev/null)" in
    "$XEMU"*) kill "$pid" 2>/dev/null || true ;;
  esac
done

[ -x "$XEMU" ]        || { echo "missing $XEMU — run tools/setup_xbox.sh"; exit 1; }
[ -f "$TOML" ]        || { echo "missing $TOML — run tools/setup_xbox.sh"; exit 1; }
[ -e "$EMU/bios.bin" ]  || { echo "missing $EMU/bios.bin (MCPX)"; exit 1; }
[ -e "$EMU/flash.bin" ] || { echo "missing $EMU/flash.bin (1 MB BIOS)"; exit 1; }
[ -e "$EMU/hdd.qcow2" ] || { echo "missing $EMU/hdd.qcow2"; exit 1; }

need_iso=0
if [ "$BUILD" = 1 ]; then
  echo ">> building xbox"
  make -C "$ROOT" xbox
  # build_relic.sh always emits a fresh Relic.iso containing ONLY default.xbe,
  # so we must rewrap to add RELIC.CFG regardless of timestamps.
  need_iso=1
fi
[ -f "$OUT/default.xbe" ] || { echo "build did not produce $OUT/default.xbe"; exit 1; }
[ -f "$OUT/Relic.iso" ]   || need_iso=1

# Stage RELIC.CFG next to the .xbe. xemu mounts the .xbe's dir as D:, so
# sibling files are readable in-guest.
old_umask=$(umask); umask 077
if [ -n "$ANTHROPIC_API_KEY" ]; then
  sed "s|^api_key=.*|api_key=$ANTHROPIC_API_KEY|" RELIC.CFG.example > "$OUT/RELIC.CFG.tmp"
else
  echo "   (ANTHROPIC_API_KEY not set; staging example CFG — set it in .env.local)"
  cp RELIC.CFG.example "$OUT/RELIC.CFG.tmp"
fi
[ "$VERBOSE" != 0 ] && printf 'verbose=%s\n' "$VERBOSE" >> "$OUT/RELIC.CFG.tmp"
# Only replace when content actually changed, so --no-build iterations can
# skip the ~1.5 s docker rewrap below. `test -nt` on macOS /bin/sh (bash 3.2)
# is whole-second and races the build step; cmp is exact.
if cmp -s "$OUT/RELIC.CFG.tmp" "$OUT/RELIC.CFG" 2>/dev/null; then
  rm -f "$OUT/RELIC.CFG.tmp"
else
  mv "$OUT/RELIC.CFG.tmp" "$OUT/RELIC.CFG"
  need_iso=1
fi
umask "$old_umask"

if [ $need_iso = 1 ]; then
  echo ">> wrapping Relic.iso"
  make -C "$ROOT/build/xbox" iso >/dev/null
fi

# xemu mounts dvd_path as the virtual DVD; it MUST be an XISO -- a bare
# .xbe boots the dashboard's "Please insert an Xbox disc" prompt.
NEW_DVD="$OUT/Relic.iso"
sed -i.tmp "s|^dvd_path *=.*|dvd_path      = '${NEW_DVD}'|" "$TOML"
rm -f "$TOML.tmp"

echo ">> booting xemu (DVD = $NEW_DVD)"
LOG=${RELIC_XEMU_LOG:-$OUT/xemu-serial.log}
: > "$LOG"
echo "   (serial/debug log: $LOG)"

# xemu's Input menu binds host keys to an Xbox *controller* (XID protocol,
# not HID) -- useless for typing. For the REPL we plug QEMU's usb-kbd via
# the legacy -usbdevice syntax, which auto-attaches to the first free hub
# port instead of a specific address (`-device usb-kbd,port=...` gets
# silently dropped by the Xbox machine because its OHCI topology is
# pre-wired via the internal hub). libusbohci then enumerates it and our
# HID connect callback fires.
KBD="-device usb-kbd"

# Headless: xemu's QEMU fork drops the VNC backend, so -vnc is rejected;
# -monitor stdio is also useless once we're backgrounded with no tty. Just
# suppress the window -- output goes to the serial log either way.
set -- -serial file:"$LOG" $KBD
[ "$HEADLESS" = 1 ] && set -- "$@" -display none
exec "$XEMU" "$@"
