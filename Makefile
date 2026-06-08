# Top-level convenience targets.

# Developer-local env (git-ignored). Plain KEY=value lines; exported to recipes.
-include .env
-include .env.local
export

.PHONY: all posix win32 win16 macppc osxppc xbox wii relicos run-macppc setup-macppc run-win95 reload-win95 setup-win95 run-win16 reload-win16 run-xbox setup-xbox run-wii setup-wii test test-valgrind e2e clean format format-check release

FMT_SRC := $(shell find src test \( -path src/plat/relicos -prune \) -o \( -name '*.c' -o -name '*.h' \) -print)

all: posix win32

posix:
	$(MAKE) -C build/posix relic

win32:
	$(MAKE) -C build/win32 all

win16:
	$(MAKE) -C build/win16 all

macppc:
	$(MAKE) -C build/macppc relic

osxppc:
	$(MAKE) -C build/osxppc all

xbox:
	$(MAKE) -C build/xbox all

wii:
	$(MAKE) -C build/wii all

relicos:
	$(MAKE) -C src/plat/relicos all

# Build + launch in SheepShaver.
run-macppc:
	tools/run_macppc.sh $(ARGS)

setup-macppc:
	tools/setup_macppc.sh $(ISO) $(ROM)

# Build + boot under real Windows 95 in QEMU. ARGS=--no-build|--fresh.
run-win95:
	tools/run_win95.sh $(ARGS)

# Rebuild + hot-swap D: in an already-running run-win95 guest.
reload-win95:
	tools/run_win95.sh --reload $(ARGS)

setup-win95:
	tools/setup_win95.sh $(IMG)

# Build + boot the Windows 3.x target inside the Win95 guest (Win386 NE
# binaries run there natively). ARGS=--no-build|--fresh.
run-win16:
	tools/run_win16.sh $(ARGS)

reload-win16:
	tools/run_win16.sh --reload $(ARGS)

# Build + boot under xemu. ARGS=--no-build|--fresh.
run-xbox:
	tools/run_xbox.sh $(ARGS)

setup-xbox:
	tools/setup_xbox.sh $(BIOS) $(HDD)

# Build + boot under Dolphin. ARGS=--no-build|--headless.
run-wii:
	tools/run_wii.sh $(ARGS)

setup-wii:
	tools/setup_wii.sh

# Everything that runs on the host with no network, API key, or emulator:
# unit suites (-std=c89), the same suites under ASan/UBSan, then offline e2e.
test:
	$(MAKE) -C build/posix test
	$(MAKE) -C build/posix test-san
	$(MAKE) -C build/posix e2e-mock

test-valgrind:
	$(MAKE) -C build/posix test-valgrind

# End-to-end: real binary, real API (set ANTHROPIC_API_KEY in .env).
# Optionally narrow with E2E='10_*.sh'.
e2e:
	$(MAKE) -C build/posix e2e

# Clean release archives for every target into dist/release/ (Docker required
# for the cross targets). VERSION=x.y.z overrides the stamp; CI runs the same
# script per-target from .github/workflows/release.yml on v* tags.
release:
	tools/build_release.sh

format:
	clang-format -i $(FMT_SRC)

format-check:
	clang-format --dry-run -Werror $(FMT_SRC)

clean:
	$(MAKE) -C build/posix clean
	$(MAKE) -C build/win32 clean
	$(MAKE) -C build/win16 clean
	$(MAKE) -C build/macppc clean
	$(MAKE) -C build/osxppc clean
	$(MAKE) -C build/xbox clean
	$(MAKE) -C build/wii clean
