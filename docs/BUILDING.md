# Building Relic

Six build targets share the same `src/core/` C99 code:

| Target | Toolchain | Command | Output |
|---|---|---|---|
| posix (dev loop) | system clang | `make -C build/posix` | `dist/posix/relic` |
| win32 | Open Watcom v2 (via Docker) | `make -C build/win32` | `dist/win32/RELIC.EXE` |
| win16 (Windows 3.x, 1992) | Open Watcom v2 Win386 (via Docker) | `make -C build/win16` | `dist/win16/RELIC.EXE` |
| classic Mac PPC | Retro68 | `make -C build/macppc` | `dist/macppc/Relic.bin` |
| xbox (original Xbox, 2001) | nxdk (via Docker) | `make -C build/xbox` | `dist/xbox/default.xbe` |
| wii (Nintendo Wii, 2006) | devkitPPC (via Docker) | `make -C build/wii` | `dist/wii/relic.dol` |

## POSIX (macOS/Linux)

    make -C build/posix
    ./dist/posix/relic

### Tests

    make test           # unit suites (c89 + ASan/UBSan) + offline e2e — no network/key needed
    make e2e            # ptyrun-driven REPL smoke tests against the live API (needs ANTHROPIC_API_KEY)
    make test-valgrind  # unit suites under valgrind --leak-check=full (Linux)

Unit suites live in `test/test_*.c` and use the tiny `test/t.h` harness — one
line per check, `file:line` + got/want on FAIL, runs past failures. Suites are
auto-discovered from the filename; if `test_foo.c` needs more than
`src/core/foo.c` to link, add a `TEST_foo_SRC` override in
`build/posix/Makefile`. Set `T_TAP=1` for TAP output.

## Windows 95 — Open Watcom v2

OW2 ships no macOS binary, so the build runs in a `linux/amd64` Docker
container (works on Apple Silicon via Rosetta emulation). One-time:

    # ensure Docker Desktop is running
    make -C build/win32 image     # ~150 MB download, builds image once

Then:

    make -C build/win32           # → dist/win32/RELIC.EXE

The image pins the OW2 "Current-build" snapshot. `make -C build/win32 shell`
drops you into the container with `wcl386` on PATH for experiments.

On a Linux host you can skip Docker: install OW2 natively, `export WATCOM=...`,
and run the `wcl386` line from the Makefile directly.

## Windows 3.x — Open Watcom v2 + Win386

The win16 target shares the relic-ow2 Docker image with win32. `wcc386
-bt=windows` compiles `core/` as ordinary 32-bit flat code (so `int` stays
32 bits and BearSSL builds unchanged), `wlink` emits a Phar Lap REX, and
`wbind` staples Watcom's Win386 supervisor on front to produce a real NE
executable that Windows 3.1+ in 386 enhanced mode loads natively. Winsock
is called through runtime thunks into the 16-bit WINSOCK.DLL (see
`src/plat/win16/plat_win16.c`), so the EXE starts fine on machines with no
TCP/IP stack installed.

    make -C build/win16 image     # only if relic-ow2 isn't built yet
    make -C build/win16           # → dist/win16/RELIC.EXE
    tools/pack_win16.sh           # → dist/win16/relic.img (1.44 MB floppy)

The module audit asserts the NE references only KERNEL / USER / GDI /
KEYBOARD / SOUND (`tools/win16-allowlist.txt`).

### Running under emulation

There is no Windows 3.11 image in the dev loop, but Win95 runs Win386
binaries natively (same loader, same 16-bit Winsock ABI), so the win16
build smoke-tests in the same guest as win32:

    make run-win16                # rebuilds, packs relic16.iso, boots QEMU
    make reload-win16             # hot-swap D: in the running guest

Inside the guest run `D:\INSTALL.BAT` (installs to `C:\RELIC16`), then
launch `C:\RELIC16\RELIC.EXE`. On real Windows 3.x hardware: copy
`RELIC.EXE` + `RELIC.CFG` from the floppy image, and run the EXE from
Program Manager or File Manager (the console is its own window — there is
no console subsystem on 3.x).

## Xbox (2001) — nxdk

Builds an `.xbe` for the original Microsoft Xbox (Pentium III, 64 MB, built-in
Ethernet) using [nxdk](https://github.com/XboxDev/nxdk), an LLVM-based SDK.
Runs in Docker on macOS or Linux; no Windows host required. One-time:

    make -C build/xbox image     # builds the nxdk container (~1.7 GB)

Then:

    make -C build/xbox           # → dist/xbox/default.xbe + Relic.iso

### Running in xemu

One-time — drop xemu.app plus your own BIOS/HDD dumps into `emu/xbox/`
(xemu's [getting-started guide](https://xemu.app/docs/getting-started/) walks
through dumping; nothing copyrighted is committed):

    XBOX_BIOS=~/Downloads/flash.bin \
    XBOX_MCPX=~/Downloads/mcpx.bin \
    XBOX_HDD=~/Downloads/xbox_hdd.qcow2 \
      make setup-xbox

Then, every iteration:

    make run-xbox                 # rebuilds .xbe + ISO, stages RELIC.CFG, boots xemu

`run-xbox` sources `.env.local` so `ANTHROPIC_API_KEY` is baked into the
RELIC.CFG written onto the virtual DVD. Relic boots into the interactive
REPL driven by a USB keyboard (xemu attaches a `usb-kbd` device; on hardware
plug a USB keyboard via a controller-port adapter).

## Wii — devkitPPC

Builds a `.dol` for the Nintendo Wii (PowerPC 750CL "Broadway", 88 MB,
built-in WiFi/Ethernet) using [devkitPPC](https://devkitpro.org/) + libogc.
Runs in the official `devkitpro/devkitppc` Docker image (multi-arch — native
on Apple Silicon). One-time:

    make -C build/wii image       # pulls devkitpro/devkitppc (~2 GB)

Then:

    make -C build/wii             # → dist/wii/relic.dol + relic.elf

### Running in Dolphin

Dolphin needs no BIOS to boot homebrew. One-time — drop `Dolphin.app` into
`emu/wii/` and let setup wire the config:

    make setup-wii

Then, every iteration:

    make run-wii                  # rebuilds .dol, stages SD card, boots Dolphin

`run-wii` sources `.env.local` so `ANTHROPIC_API_KEY` is baked into the
RELIC.CFG staged onto the virtual SD card at `sd:/apps/relic/`. Input
comes from an emulated GameCube keyboard on controller port 1 (Dolphin's
Wii-USB-keyboard HLE is Windows-only, so the SI path is what works on
macOS/Linux); on real hardware plug any USB keyboard into a back port and
launch from the Homebrew Channel.

## Classic Mac OS — Retro68

Retro68 is a modern GCC cross-compiler for 68k/PPC classic Mac. The build
runs via the official prebuilt Docker image `ghcr.io/autc04/retro68`:

    make -C build/macppc             # pulls image on first run
    make -C build/macppc shell       # interactive toolchain shell

To build the toolchain natively instead (e.g., for IDE integration):

    git clone --depth 1 https://github.com/autc04/Retro68.git ~/Retro68
    cd ~/Retro68 && mkdir build && cd build && ../build-toolchain.bash

### Running in SheepShaver

One-time setup — drop your own Mac OS install ISO and ROM into `emu/macppc/`
(git-ignored, never committed):

    tools/setup_macppc.sh ~/Downloads/MacOS8_1.iso [~/path/to/rom]

Then, every iteration:

    make run-macppc                 # rebuilds relic, launches SheepShaver
    make run-macppc ARGS=--no-build # launch without rebuilding

The script regenerates the SheepShaver prefs on each run so the freshly built
`.dsk` is always mounted as a "Relic" volume next to your boot disk. First
launch boots from the CD — install onto the blank `boot.dsk`, reboot, then the
app is on the desktop. See `emu/macppc/README.md` for details.

## Releases

`tools/build_release.sh` rebuilds targets from a wiped `dist/<target>/` and
packs distributable archives into `dist/release/`:

    make release                        # posix win32 xbox wii (Docker required)
    tools/build_release.sh win32 wii    # just some targets
    VERSION=0.2.0 make release          # override the version stamp

Every archive carries only the example `RELIC.CFG`, plus `LICENSE` and
`NOTICES`. The script greps the staged files (including the disk images) for
anything that looks like a real API key and aborts if it finds one, so a dev
config can't slip into a release. It also warns when the version stamp
doesn't match `RELIC_VERSION` in `src/main.c`.

The macppc target is left out of the default set (and out of CI releases)
on purpose: its binaries statically link Apple's Open Transport glue (see
`tools/fetch_otglue.sh`), so the resulting archive is for personal use
only. `tools/build_release.sh macppc` builds it explicitly.

On macOS the posix archive is a universal binary: x86_64 (runs on 64-bit
Intel Macs back to macOS 10.6 Snow Leopard) plus arm64 (Apple Silicon),
built by rebuilding BearSSL fat and passing both `-arch` flags to clang.
32-bit Intel and PowerPC OS X are not covered; build from source there.

In CI, pushing a `v*` tag runs `.github/workflows/release.yml`: it runs the
test suite, builds every target (x86_64 + aarch64 Linux and universal macOS
binaries for posix; Docker cross-builds for the rest), and attaches the
archives plus `SHA256SUMS` to a **draft** GitHub release for review.
`workflow_dispatch` runs the same builds without creating a release.

## Dev artifacts contain your API key

The `run-*` / `setup-*` flows are for local iteration: they read
`ANTHROPIC_API_KEY` from `.env` / `.env.local` and bake it into the `RELIC.CFG`
packed into the images they boot — `dist/macppc/Relic.dsk`,
`dist/xbox/Relic.iso`, `dist/wii/sd/`, `emu/win95/relic.iso`, and
`dist/win32/relic.img` if you have put a real key into `dist/win32/RELIC.CFG`.
Everything under `dist/` and `emu/` is git-ignored, but treat those images like
secrets: never attach them to releases, issues, or screenshots. Anything meant
for distribution should be packed from a clean `dist/` so it only carries the
example config.

## Verifying the win32 binary

Before booting an emulator, sanity-check the PE on the host:

    docker run --rm -v $(pwd):/work relic-ow2 \
      sh -c 'wdump -q dist/win32/RELIC.EXE | head -40'

Look for: subsystem version ≤ 4.0, imports limited to KERNEL32 / USER32 /
WSOCK32. No MSVCRT.DLL. The build asserts the same against
`tools/win32-allowlist.txt`.
