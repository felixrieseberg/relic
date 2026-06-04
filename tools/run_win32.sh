#!/bin/sh
# Run RELIC.EXE under Wine-in-Docker with stdin/stdout connected.
# This is the autonomous-debug entrypoint: pipe REPL input on stdin, get
# trace + output on stdout/stderr.
#
#   echo "/nettest" | tools/run_win32.sh
#   tools/run_win32.sh -c -p "say hi"
#   ANTHROPIC_API_KEY=sk-... tools/run_win32.sh -v          # interactive
#   tools/run_win32.sh --build                              # rebuild EXE first
#
# Exit code = RELIC.EXE's exit code.
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)
IMAGE=relic-ow2
OUT="$ROOT/dist/win32"

BUILD=0
if [ "$1" = "--build" ]; then BUILD=1; shift; fi

# Ensure image + EXE exist.
docker image inspect "$IMAGE" >/dev/null 2>&1 || make -C build/win32 image
CACHE="$ROOT/build/win32/.cache"
mkdir -p "$OUT" "$CACHE"
if [ "$BUILD" = 1 ] || [ ! -f "$OUT/RELIC.EXE" ]; then
  sync
  [ -f "$CACHE/bearssl.lib" ] || docker run --rm -i --platform linux/amd64 \
    -v "$ROOT":/work:ro -v "$OUT":/out -v "$CACHE":/cache "$IMAGE" \
    sh -s < build/win32/build_bearssl.sh >&2
  docker run --rm -i --platform linux/amd64 \
    -v "$ROOT":/work:ro -v "$OUT":/out -v "$CACHE":/cache "$IMAGE" \
    sh -s < build/win32/build_relic.sh >&2
fi

# -i (no -t): stdin pipeable, stdout/stderr stream back uncooked.
exec docker run --rm -i --platform linux/amd64 \
  -e ANTHROPIC_API_KEY \
  -e WINEDEBUG=-all \
  -v "$OUT":/out \
  -w /out \
  "$IMAGE" wine /out/RELIC.EXE "$@"
