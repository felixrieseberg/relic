#!/bin/sh
# Build clean, distributable release archives into dist/release/.
#
#   tools/build_release.sh [target ...]    # default: all seven targets
#   VERSION=0.2.0 tools/build_release.sh   # override the version stamp
#
# macppc fetches Apple's Open Transport glue on first build (its Makefile
# runs tools/fetch_otglue.sh); osxppc compiles its cross toolchain into a
# local Docker image on first run (build/osxppc/Dockerfile).
#
# Each target's dist/<target>/ is wiped and rebuilt from scratch so a
# developer RELIC.CFG holding a real API key (see docs/BUILDING.md) can never
# end up inside an archive; every archive ships the example config only, and
# the script aborts if anything staged contains a real-looking key.
# Cross targets need Docker. CI runs this per-target from
# .github/workflows/release.yml.
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)
REL=$ROOT/dist/release

ALL="posix win32 win16 macppc osxppc xbox wii"
TARGETS=${*:-$ALL}

# Version stamp: $VERSION, else the nearest git tag (or short sha), with any
# leading "v" dropped.
VERSION=${VERSION:-$(git describe --tags --always 2>/dev/null || echo dev)}
VERSION=${VERSION#v}

# Warn when the stamp disagrees with the version baked into the binaries.
SRC_VER=$(sed -n 's/^#define RELIC_VERSION "\(.*\)"$/\1/p' src/main.c)
case $VERSION in
  "$SRC_VER" | "$SRC_VER".* | "$SRC_VER"-*) ;;
  *) echo "** packaging $VERSION but src/main.c has RELIC_VERSION \"$SRC_VER\"" >&2 ;;
esac

need_docker() {
  docker info >/dev/null 2>&1 \
    || { echo "target '$1' needs Docker; start it and retry" >&2; exit 1; }
}

# Abort if anything under $1 contains what looks like a real Anthropic API
# key: a long key body after "sk-ant-". The committed placeholders
# (sk-ant-api03-..., sk-ant-REPLACE-ME) never match. -a scans binaries too,
# so keys inside relic.img / Relic.dsk / Relic.iso are caught.
key_check() {
  if LC_ALL=C grep -rqaE 'sk-ant-[A-Za-z0-9_-]{20,}' "$1"; then
    echo "!! real-looking API key found in staged release files -- aborting:" >&2
    LC_ALL=C grep -rlaE 'sk-ant-[A-Za-z0-9_-]{20,}' "$1" >&2
    exit 1
  fi
}

stage_init() {
  STAGE=$REL/.stage-$1
  rm -rf "$STAGE"
  mkdir -p "$STAGE"
}

# finish_zip STAGE ARCHIVE -- key-check, zip the staged tree, clean up.
finish_zip() {
  key_check "$1"
  rm -f "$REL/$2"
  (cd "$1" && zip -q -r -X "$REL/$2" .)
  rm -rf "$1"
  echo "  -> dist/release/$2"
}

# finish_targz STAGE NAME -- key-check, tar staged $NAME/ tree, clean up.
finish_targz() {
  key_check "$1"
  rm -f "$REL/$2.tar.gz"
  COPYFILE_DISABLE=1 tar -czf "$REL/$2.tar.gz" -C "$1" "$2"
  rm -rf "$1"
  echo "  -> dist/release/$2.tar.gz"
}

checksum() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$@"
  else shasum -a 256 "$@"; fi
}

do_posix() {
  rm -rf dist/posix
  OS=$(uname -s | tr '[:upper:]' '[:lower:]')
  if [ "$OS" = darwin ]; then
    # Universal 2-way binary: x86_64 (macOS 10.6+, i.e. any 64-bit Intel Mac
    # on Snow Leopard or later) and arm64 (macOS 11+; clang clamps the arm64
    # slice's min version). 10.6 is the floor for the x86_64 slice: the
    # linker emits the old LC_UNIXTHREAD startup for pre-10.8 targets, but
    # dyld_stub_binder only exists from 10.6, and every undefined symbol in
    # the binary ($INODE64/$1050 variants, __stack_chk_*, __*_chk) is 10.5+.
    # Older Macs than that are 32-bit Intel or PPC and need other slices
    # entirely. BearSSL must be rebuilt fat first so both slices have
    # something to link against; the posix Makefile then sees the .a and
    # skips its own bearssl rule.
    MACFLAGS="-arch x86_64 -arch arm64 -mmacosx-version-min=10.6"
    rm -rf third_party/bearssl/build
    $MAKE -C third_party/bearssl lib CFLAGS="-W -Wall -Os -fPIC $MACFLAGS"
    $MAKE -C build/posix relic CC="cc $MACFLAGS"
    ARCH=universal
    REQUIRES="macOS 10.6+ (64-bit Intel) or 11+ (Apple Silicon)"
  else
    $MAKE -C build/posix relic
    ARCH=$(uname -m)
    REQUIRES="a libc + BSD sockets"
  fi
  NAME=relic-$VERSION-$OS-$ARCH
  stage_init posix
  mkdir -p "$STAGE/$NAME"
  cp dist/posix/relic "$STAGE/$NAME/"
  cp RELIC.CFG.example "$STAGE/$NAME/"
  cp LICENSE NOTICES "$STAGE/$NAME/"
  cat > "$STAGE/$NAME/README" <<EOF
Relic $VERSION ($OS-$ARCH)
--------------------------
1. Copy RELIC.CFG.example to RELIC.CFG (here or in \$HOME) and put your
   real key on the api_key= line, or export ANTHROPIC_API_KEY=sk-ant-...
2. ./relic -p "hello"      (one-shot)
   ./relic                 (chat; /quit to exit)
Requires $REQUIRES.
EOF
  finish_targz "$STAGE" "$NAME"
}

do_win32() {
  need_docker win32
  rm -rf dist/win32
  $MAKE -C build/win32 all
  NAME=relic-$VERSION-win32
  stage_init win32
  cp dist/win32/RELIC.EXE dist/win32/RELIC.CFG dist/win32/README.TXT \
     dist/win32/relic.img "$STAGE/"
  sed 's/$/\r/' LICENSE > "$STAGE/LICENSE.TXT"
  sed 's/$/\r/' NOTICES > "$STAGE/NOTICES.TXT"
  finish_zip "$STAGE" "$NAME.zip"
}

do_win16() {
  need_docker win16
  rm -rf dist/win16
  $MAKE -C build/win16 all
  tools/pack_win16.sh
  NAME=relic-$VERSION-win16
  stage_init win16
  cp dist/win16/RELIC.EXE dist/win16/RELIC.CFG dist/win16/README.TXT \
     dist/win16/relic.img "$STAGE/"
  sed 's/$/\r/' LICENSE > "$STAGE/LICENSE.TXT"
  sed 's/$/\r/' NOTICES > "$STAGE/NOTICES.TXT"
  finish_zip "$STAGE" "$NAME.zip"
}

do_macppc() {
  need_docker macppc
  rm -rf dist/macppc
  # clean-dsk: never reuse a cached .dsk whose freed HFS blocks may hold a
  # dev API key (see build/macppc/Makefile, which also fetches the OT glue).
  $MAKE -C build/macppc clean-dsk relic
  NAME=relic-$VERSION-macppc
  stage_init macppc
  cp dist/macppc/Relic.bin dist/macppc/Relic.dsk "$STAGE/"
  cp RELIC.CFG.example "$STAGE/RELIC.CFG"
  cp LICENSE "$STAGE/LICENSE.txt"
  cp NOTICES "$STAGE/NOTICES.txt"
  cat > "$STAGE/README.txt" <<EOF
Relic $VERSION for classic Mac OS (PowerPC)
-------------------------------------------
Relic.dsk  HFS disk image: mount it (or use it in SheepShaver) -- the app
           and a sample RELIC.CFG are inside.
Relic.bin  MacBinary-encoded application, for transferring to a real Mac
           (decode with StuffIt Expander).
Put your real key on the api_key= line of RELIC.CFG before launching.
Requires Mac OS 8.1+ with Open Transport TCP/IP.
EOF
  finish_zip "$STAGE" "$NAME.zip"
}

do_osxppc() {
  need_docker osxppc
  rm -rf dist/osxppc
  # FORCE=1 rebuilds the cached libbearssl.a -- releases must never link a
  # BearSSL archive left over from older sources or different flags.
  # `all` = relic + Mach-O audit.
  $MAKE -C build/osxppc all FORCE=1
  NAME=relic-$VERSION-osxppc
  stage_init osxppc
  mkdir -p "$STAGE/$NAME"
  cp dist/osxppc/relic "$STAGE/$NAME/"
  cp RELIC.CFG.example "$STAGE/$NAME/"
  cp LICENSE NOTICES "$STAGE/$NAME/"
  cat > "$STAGE/$NAME/README" <<EOF
Relic $VERSION for Mac OS X on PowerPC (10.1 Puma through 10.5 Leopard)
-----------------------------------------------------------------------
1. Copy RELIC.CFG.example to RELIC.CFG (here or in \$HOME) and put your
   real key on the api_key= line, or export ANTHROPIC_API_KEY=sk-ant-...
2. ./relic -p "hello"      (one-shot)
   ./relic                 (chat; /quit to exit)
Run it from Terminal.app. Requires Mac OS X 10.1 or later on PowerPC
(G3 or later); also runs on Intel 10.4-10.6 under Rosetta.
EOF
  finish_targz "$STAGE" "$NAME"
}

do_xbox() {
  need_docker xbox
  rm -rf dist/xbox
  $MAKE -C build/xbox all
  NAME=relic-$VERSION-xbox
  stage_init xbox
  cp dist/xbox/default.xbe dist/xbox/Relic.iso "$STAGE/"
  cp RELIC.CFG.example "$STAGE/RELIC.CFG"
  cp LICENSE "$STAGE/LICENSE.txt"
  cp NOTICES "$STAGE/NOTICES.txt"
  cat > "$STAGE/README.txt" <<EOF
Relic $VERSION for the original Xbox
------------------------------------
Recommended: FTP default.xbe and RELIC.CFG (with your real api_key= filled
in) to your modded Xbox, e.g. E:\\Apps\\Relic\\, and launch from the
dashboard. Relic.iso is a bootable DVD image of the bare .xbe; it carries no
config, so the HDD route is the practical one.
Requires the ability to run unsigned XBEs and a USB keyboard.
EOF
  finish_zip "$STAGE" "$NAME.zip"
}

do_wii() {
  need_docker wii
  rm -rf dist/wii
  $MAKE -C build/wii all
  NAME=relic-$VERSION-wii
  stage_init wii
  # Homebrew Channel layout: extract the zip onto the SD card root.
  mkdir -p "$STAGE/apps/relic"
  cp dist/wii/relic.dol "$STAGE/apps/relic/boot.dol"
  cp dist/wii/meta.xml "$STAGE/apps/relic/"
  cp RELIC.CFG.example "$STAGE/apps/relic/RELIC.CFG"
  cp LICENSE "$STAGE/apps/relic/LICENSE.txt"
  cp NOTICES "$STAGE/apps/relic/NOTICES.txt"
  cat > "$STAGE/apps/relic/README.txt" <<EOF
Relic $VERSION for the Nintendo Wii
-----------------------------------
1. Extract this zip onto the root of your SD card (it creates apps/relic/).
2. Put your real key on the api_key= line of apps/relic/RELIC.CFG.
3. Launch Relic from the Homebrew Channel.
Requires the Homebrew Channel and a USB or GameCube keyboard.
EOF
  finish_zip "$STAGE" "$NAME.zip"
}

MAKE=${MAKE:-make}
mkdir -p "$REL"

for t in $TARGETS; do
  echo "=== $t ==="
  case $t in
    posix)  do_posix ;;
    win32)  do_win32 ;;
    win16)  do_win16 ;;
    macppc) do_macppc ;;
    osxppc) do_osxppc ;;
    xbox)   do_xbox ;;
    wii)    do_wii ;;
    *) echo "unknown target '$t' (expected: $ALL)" >&2; exit 1 ;;
  esac
done

# Checksums over this version's archives only -- stale archives from older
# versions/runs may still sit in dist/release/ and must not enter the
# manifest.
(cd "$REL" && rm -f SHA256SUMS && checksum relic-"$VERSION"-* > SHA256SUMS)
echo "=== done ==="
cat "$REL/SHA256SUMS"
