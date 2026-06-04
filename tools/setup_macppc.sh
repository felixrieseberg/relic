#!/bin/sh
# One-time SheepShaver setup for the macppc target.
# Populates emu/macppc/ with the user-supplied ROM + install ISO and a blank
# boot disk. Nothing here is committed (see emu/.gitignore).
#
#   tools/setup_macppc.sh [path/to/MacOS.iso] [path/to/rom]
#
# Both args are optional if the files are already in emu/macppc/. Env overrides:
#   MACPPC_ISO, MACPPC_ROM, MACPPC_BOOT_MB (default 500), SHEEPSHAVER (binary path)
set -e
cd "$(dirname "$0")/.."
EMU=emu/macppc
mkdir -p "$EMU/shared"

ISO_ARG=${1:-${MACPPC_ISO:-}}
ROM_ARG=${2:-${MACPPC_ROM:-}}
BOOT_MB=${MACPPC_BOOT_MB:-500}

say()  { printf '  %s\n' "$*"; }
ok()   { printf '  \033[32m✓\033[0m %s\n' "$*"; }
warn() { printf '  \033[33m!\033[0m %s\n' "$*"; }
die()  { printf '  \033[31m✗\033[0m %s\n' "$*"; exit 1; }

echo "Setting up SheepShaver assets in $EMU/"

# --- install ISO ------------------------------------------------------------
if [ -n "$ISO_ARG" ]; then
  [ -f "$ISO_ARG" ] || die "ISO not found: $ISO_ARG"
  cp -f "$ISO_ARG" "$EMU/install.iso"
  ok "install.iso  ← $ISO_ARG"
elif [ -f "$EMU/install.iso" ]; then
  ok "install.iso  (already present)"
else
  warn "install.iso  missing — pass a Mac OS 8.1–9.0.4 CD image as arg 1"
fi

# --- ROM --------------------------------------------------------------------
if [ -n "$ROM_ARG" ]; then
  [ -f "$ROM_ARG" ] || die "ROM not found: $ROM_ARG"
  cp -f "$ROM_ARG" "$EMU/rom"
  ok "rom          ← $ROM_ARG"
elif [ -f "$EMU/rom" ]; then
  ok "rom          (already present)"
else
  warn "rom          missing — pass a New World Mac ROM as arg 2, or drop it at $EMU/rom"
fi

# --- blank boot disk --------------------------------------------------------
if [ -f "$EMU/boot.dsk" ]; then
  ok "boot.dsk     (already present, leaving untouched)"
else
  say "boot.dsk     creating ${BOOT_MB} MB blank volume…"
  dd if=/dev/zero of="$EMU/boot.dsk" bs=1048576 count="$BOOT_MB" status=none 2>/dev/null \
    || dd if=/dev/zero of="$EMU/boot.dsk" bs=1048576 count="$BOOT_MB" 2>/dev/null
  ok "boot.dsk     (${BOOT_MB} MB — Mac OS installer will format it on first boot)"
fi

# --- Open Transport glue + MPW disk image (Apple SDK; not redistributed) ----
# fetch_otglue.sh extracts the OT static glue AND stages emu/macppc/mpw.dsk,
# which SheepShaver mounts as a second volume so you can install MPW Shell +
# PPC compilers onto your boot disk on first run.
if [ -f third_party/otglue/OpenTransportAppPPC.o ] && [ -f "$EMU/mpw.dsk" ]; then
  ok "otglue       (already present)"
  ok "mpw.dsk      (already present — mounted as 'MPW-GM' on next boot)"
else
  say "otglue       downloading Apple's MPW-GM image (~24 MB; URL and SHA-256 in tools/fetch_otglue.sh)…"
  if tools/fetch_otglue.sh >/dev/null 2>&1; then
    ok "otglue       fetched (third_party/otglue/)"
    ok "mpw.dsk      staged ($EMU/mpw.dsk — drag the MPW folder onto your boot disk)"
  else
    warn "otglue       fetch failed — run tools/fetch_otglue.sh manually (networking won't link without it)"
  fi
fi

# --- SheepShaver binary -----------------------------------------------------
SS=${SHEEPSHAVER:-}
[ -z "$SS" ] && for c in \
    "$EMU/SheepShaver.app/Contents/MacOS/SheepShaver" \
    "/Applications/SheepShaver.app/Contents/MacOS/SheepShaver" \
    "$HOME/Applications/SheepShaver.app/Contents/MacOS/SheepShaver" \
    "$(command -v SheepShaver 2>/dev/null || true)" \
    "$(command -v sheepshaver 2>/dev/null || true)"; do
  [ -n "$c" ] && [ -x "$c" ] && SS=$c && break
done
if [ -n "$SS" ]; then
  ok "SheepShaver  $SS"
else
  warn "SheepShaver  not found — install the kanjitalk755 build:"
  say  "             https://github.com/kanjitalk755/macemu/releases"
  say  "             (or set SHEEPSHAVER=/path/to/binary)"
fi

echo
if [ -f "$EMU/rom" ] && [ -f "$EMU/install.iso" ]; then
  echo "Ready. Next:"
  echo "    make run-macppc        # builds + launches; first boot runs from CD"
  echo "On first boot, run the installer onto the empty 'MacOS' disk, then reboot."
else
  echo "Drop the missing files into $EMU/ and re-run this script."
fi
