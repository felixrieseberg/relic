#!/bin/sh
# One-time QEMU setup for the win95 target.
# Populates emu/win95/ with a qcow2 overlay backed by the user's existing
# Windows 95 disk image, so iteration never touches the original. Nothing
# here is committed (see emu/.gitignore).
#
#   tools/setup_win95.sh [path/to/windows95.img]
#
# Arg is optional if emu/win95/base.img already exists, or if WIN95_IMG is set
# (e.g. in .env). Other env: QEMU_IMG (binary path).
set -e
cd "$(dirname "$0")/.."
EMU=emu/win95
mkdir -p "$EMU"

IMG_ARG=${1:-${WIN95_IMG:-}}
QEMU_IMG=${QEMU_IMG:-qemu-img}

say()  { printf '  %s\n' "$*"; }
ok()   { printf '  \033[32m✓\033[0m %s\n' "$*"; }
warn() { printf '  \033[33m!\033[0m %s\n' "$*"; }
die()  { printf '  \033[31m✗\033[0m %s\n' "$*"; exit 1; }

echo "Setting up Windows 95 QEMU assets in $EMU/"

# --- base disk image --------------------------------------------------------
[ -z "$IMG_ARG" ] && [ -f "$EMU/base.img" ] && IMG_ARG=$EMU/base.img
[ -n "$IMG_ARG" ] || die "no Windows 95 image — pass path as arg 1 or set WIN95_IMG= in .env"
[ -f "$IMG_ARG" ] || die "image not found: $IMG_ARG"
IMG_ARG=$(cd "$(dirname "$IMG_ARG")" && pwd)/$(basename "$IMG_ARG")

if [ "$IMG_ARG" = "$(pwd)/$EMU/base.img" ]; then
  ok "base.img     (already present)"
else
  # Copy, don't symlink: a live qemu on the source would block our overlay's
  # read lock and feed us a moving target. APFS clonefile makes this ~free.
  rm -f "$EMU/base.img"
  cp -c "$IMG_ARG" "$EMU/base.img" 2>/dev/null || cp "$IMG_ARG" "$EMU/base.img"
  ok "base.img     ← $IMG_ARG"
fi

# --- qcow2 overlay ----------------------------------------------------------
command -v "$QEMU_IMG" >/dev/null || die "qemu-img not found — brew install qemu"
if [ -f "$EMU/hda.qcow2" ]; then
  ok "hda.qcow2    (already present, leaving untouched — delete to recreate)"
else
  "$QEMU_IMG" create -q -f qcow2 -b base.img -F raw "$EMU/hda.qcow2"
  ok "hda.qcow2    (overlay; writes never touch base.img)"
fi

# --- qemu binary ------------------------------------------------------------
if command -v qemu-system-i386 >/dev/null; then
  ok "qemu         $(command -v qemu-system-i386)"
else
  warn "qemu         qemu-system-i386 not found — brew install qemu"
fi

echo
echo "Ready. Next:"
echo "    make run-win95         # build RELIC.EXE, repack D: ISO, boot Windows 95"
echo "    make reload-win95      # rebuild + hot-swap D: in the running guest"
echo "Inside the guest: run D:\\INSTALL.BAT, then C:\\RELIC\\RELIC.EXE."
