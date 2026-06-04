# RelicOS: bootable OS target running the Relic UI. Self-contained; does not touch build/*.
# Dev loop: prebuilt Alpine kernel + static busybox + hand-rolled initramfs.
# All artifacts under .cache/ (downloads) and out/ (built); both gitignored.

ALPINE_VER  := 3.22
ALPINE_BASE := https://dl-cdn.alpinelinux.org/alpine/v$(ALPINE_VER)/releases/x86_64
NETBOOT_TAR := alpine-netboot-$(ALPINE_VER).0-x86_64.tar.gz
BUSYBOX_URL := https://busybox.net/downloads/binaries/1.35.0-x86_64-linux-musl/busybox

CACHE   := .cache
OUT     := out
KERNEL  := $(CACHE)/vmlinuz-virt
BUSYBOX := rootfs/bin/busybox
INITRD  := $(OUT)/initramfs.cpio.gz
RELIC   := rootfs/bin/relic
RELICOS := rootfs/bin/relicos

QEMU      ?= qemu-system-x86_64
QEMU_MEM  ?= 512M
DATA_IMG  := $(OUT)/data.qcow2
QEMU_BASE  = $(QEMU) -m $(QEMU_MEM) -kernel $(KERNEL) -initrd $(INITRD) \
             -netdev user,id=n0 -device virtio-net-pci,netdev=n0 -no-reboot
QEMU_DISK  = -drive file=$(DATA_IMG),if=none,id=d0,format=qcow2 -device virtio-blk-pci,drive=d0

$(DATA_IMG):
	@mkdir -p $(OUT)
	qemu-img create -f qcow2 $@ 256M >/dev/null
QEMU_TTY   = -nographic -append "console=ttyS0 quiet $(EXTRA)"
QEMU_GFX   = -vga none -device virtio-gpu -device virtio-keyboard -device virtio-mouse -serial stdio

.PHONY: all fetch initramfs shot run run-gfx ui shell test test-net test-relic test-fb test-cc test-data test-input test-agent test-all screenshot relic relicos clean distclean

# Worktrees: seed heavy gitignored artifacts from the main checkout so we don't
# re-download/rebuild. Runs at parse time so make sees the files before deciding
# what's out of date. APFS clonefile (-c) makes the .cache copy ~free.
MAIN_OS := $(shell git worktree list --porcelain 2>/dev/null | sed -n '1s/^worktree //p')/src/plat/relicos
SEEDS   := $(CACHE) $(BUSYBOX) $(RELIC) $(RELICOS) ../../../third_party/bearssl/build-linux
ifneq ($(abspath $(MAIN_OS)),$(CURDIR))
  $(foreach f,$(SEEDS),$(if $(wildcard $(f)),,$(if $(wildcard $(MAIN_OS)/$(f)),\
    $(info seed: $(f) <- $(MAIN_OS)/$(f))\
    $(shell mkdir -p "$(dir $(f))" && { cp -Rpc "$(MAIN_OS)/$(f)" "$(f)" 2>/dev/null || cp -Rp "$(MAIN_OS)/$(f)" "$(f)"; }))))
endif

all: $(INITRD)

fetch: $(KERNEL) $(BUSYBOX)

$(KERNEL):
	@mkdir -p $(CACHE)/avirt
	curl -fL --progress-bar $(ALPINE_BASE)/$(NETBOOT_TAR) -o $(CACHE)/netboot.tar.gz
	tar -xzf $(CACHE)/netboot.tar.gz -C $(CACHE) --strip-components=1 \
	  boot/vmlinuz-virt boot/initramfs-virt boot/modloop-virt
	cd $(CACHE)/avirt && gzip -dc ../initramfs-virt | cpio -idm 2>/dev/null
	docker run --rm --platform linux/amd64 -v "$(CURDIR)/$(CACHE)":/m alpine:3.22 sh -c \
	  'apk add --no-cache squashfs-tools >/dev/null && unsquashfs -q -f -d /m/avirt/lib /m/modloop-virt \
	   "modules/*/kernel/drivers/input/evdev.ko*" "modules/*/kernel/drivers/input/mousedev.ko*" \
	   "modules/*/kernel/drivers/firmware/qemu_fw_cfg.ko*" \
	   "modules/*/kernel/fs/ext4/*" "modules/*/kernel/fs/jbd2/*" "modules/*/kernel/fs/mbcache.ko*" \
	   "modules/*/kernel/lib/crc16.ko*" "modules/*/kernel/crypto/crc32c*" "modules/*/kernel/arch/x86/crypto/crc32c*"'

$(BUSYBOX):
	@mkdir -p rootfs/bin
	curl -fL --progress-bar $(BUSYBOX_URL) -o $@
	chmod +x $@

ROOTFS_SRC := rootfs/etc/udhcpc.script $(BUSYBOX) \
              $(wildcard rootfs/bin/relic) $(wildcard rootfs/bin/relicos) \
              $(wildcard rootfs/usr/include/*) $(wildcard rootfs/RELIC.CFG)
KMOD_SRC   := $(CACHE)/avirt/lib
DEVROOT    := $(CACHE)/devroot

$(DEVROOT)/usr/bin/tcc:
	./scripts/build-devroot.sh

# initramfs is two concatenated cpio archives: a fat BASE (busybox + kernel
# modules + tcc/headers) that rarely changes, and a thin TOP (init + relicos)
# that we repack on every build. The kernel unpacks them in order, TOP last,
# so files in TOP override BASE -- which is also why init lives in TOP.
INITRD_BASE := $(OUT)/initramfs.base.cpio
INITRD_TOP  := $(OUT)/initramfs.top.cpio

# Uncompressed: arm64-host TCG makes in-guest gunzip the slowest boot phase.
$(INITRD_BASE): $(BUSYBOX) $(KERNEL) $(DEVROOT)/usr/bin/tcc
	@rm -rf $(OUT)/stage.base && mkdir -p $(OUT)/stage.base/bin
	cp -a $(KMOD_SRC) $(OUT)/stage.base/
	cp -a $(DEVROOT)/. $(OUT)/stage.base/
	cp -a $(BUSYBOX) $(OUT)/stage.base/bin/
	( cd $(OUT)/stage.base && find . -print0 | cpio -0 -o -H newc 2>/dev/null ) > $@
	@ls -l $@

$(INITRD_TOP): $(ROOTFS_SRC) $(RELICOS)
	@rm -rf $(OUT)/stage.top && mkdir -p $(OUT)/stage.top
	cp -a rootfs/. $(OUT)/stage.top/
	ln -f $(OUT)/stage.top/bin/relicos $(OUT)/stage.top/init   # relicos *is* PID 1; hardlink so cpio stores it once
	chmod +x $(OUT)/stage.top/init $(OUT)/stage.top/etc/udhcpc.script
	( cd $(OUT)/stage.top && find . -print0 | cpio -0 -o -H newc 2>/dev/null ) > $@

$(INITRD): $(INITRD_BASE) $(INITRD_TOP)
	cat $(INITRD_BASE) $(INITRD_TOP) > $@
	@ls -l $@

initramfs: $(INITRD)

# Dev loop: edit relicos/*.cpp -> `make shot` -> out/screenshot.png. ~4s warm.
shot: $(RELICOS) $(INITRD)
	./scripts/screenshot.sh

run: $(KERNEL) $(INITRD)
	$(QEMU_BASE) $(QEMU_TTY)

run-gfx: $(KERNEL) $(INITRD)
	$(QEMU_BASE) $(QEMU_GFX) -append "console=ttyS0 quiet $(EXTRA)"

# Interactive: opens a window, types go to the prompt, Enter -> Claude.
# Reads ANTHROPIC_API_KEY from your host env and passes it on the kernel
# cmdline (dev only; for a real image use rootfs/RELIC.CFG instead).
ui: $(KERNEL) $(RELICOS) initramfs $(DATA_IMG)
	@[ -f ../../../.env.local ] && . ../../../.env.local; \
	  [ -n "$$ANTHROPIC_API_KEY" ] || echo "warning: ANTHROPIC_API_KEY not set; UI will be offline"; \
	  umask 077; printf '%s' "$$ANTHROPIC_API_KEY" > $(OUT)/.key; \
	  trap 'rm -f $(OUT)/.key' EXIT; \
	  $(QEMU_BASE) $(QEMU_GFX) $(QEMU_DISK) \
	    -fw_cfg name=opt/relicos.key,file=$(OUT)/.key \
	    -append "console=ttyS0 quiet relicos.run=ui $(EXTRA)"

shell: ; $(MAKE) run EXTRA=""

# Headless self-tests: await.sh owns the qemu process so timeouts can't leak it.
QEMU_HEADLESS = $(QEMU_BASE) -display none -serial file:$(OUT)/serial.log

test: $(KERNEL) $(INITRD)
	./scripts/await.sh $(OUT)/serial.log RELICOS_SMOKE_PASS 30 -- \
	  $(QEMU_HEADLESS) -append "console=ttyS0 quiet relicos.test=smoke panic=5"
	@echo "PASS: smoke"

test-net: $(KERNEL) $(INITRD)
	./scripts/await.sh $(OUT)/serial.log RELICOS_NET_PASS 45 -- \
	  $(QEMU_HEADLESS) -append "console=ttyS0 quiet relicos.test=net panic=5"
	@echo "PASS: net"

test-relic: $(KERNEL) $(RELIC) initramfs
	./scripts/await.sh $(OUT)/serial.log RELICOS_RELIC_PASS 150 -- \
	  $(QEMU_HEADLESS) -append "console=ttyS0 quiet relicos.test=relic panic=5"
	@echo "PASS: relic TLS"

test-fb: $(KERNEL) $(RELICOS) initramfs
	./scripts/await.sh $(OUT)/serial.log RELICOS_FB_PASS 60 -- \
	  $(QEMU_HEADLESS) -vga none -device virtio-gpu \
	  -append "console=ttyS0 quiet relicos.test=fb panic=5"
	@echo "PASS: fb"

test-cc: $(KERNEL) $(INITRD)
	./scripts/await.sh $(OUT)/serial.log RELICOS_CC_PASS 30 -- \
	  $(QEMU_HEADLESS) -append "console=ttyS0 quiet relicos.test=cc panic=5"
	@echo "PASS: cc"

test-data: $(KERNEL) $(INITRD) $(DATA_IMG)
	./scripts/await.sh $(OUT)/serial.log RELICOS_DATA_PASS 30 -- \
	  $(QEMU_HEADLESS) $(QEMU_DISK) -append "console=ttyS0 quiet relicos.test=data panic=5"
	@grep RELICOS_DATA $(OUT)/serial.log
	@echo "PASS: data"

test-input: $(KERNEL) $(RELICOS) initramfs
	./scripts/ui-test.sh
	@echo "PASS: input"

test-agent: $(KERNEL) $(RELICOS) initramfs
	./scripts/agent-test.sh
	@echo "PASS: agent"

test-fast: test test-cc test-input
test-all:  test test-net test-cc test-data test-fb test-input test-relic

screenshot: $(KERNEL) $(RELICOS) initramfs
	./scripts/screenshot.sh

CORE_SRCS := $(wildcard ../../core/*.c) $(wildcard ../posix/*.c) ../../main.c
$(RELIC): $(CORE_SRCS)
	./scripts/build-relic-linux.sh
	@rm -f $(INITRD)

relic:
	./scripts/build-relic-linux.sh
	@rm -f $(INITRD)
	$(MAKE) initramfs

UI_SRCS := $(wildcard relicos/*.cpp) $(wildcard relicos/*.h)
$(RELICOS): $(UI_SRCS)
	./scripts/build-relicos.sh
	@rm -f $(INITRD)

relicos:
	./scripts/build-relicos.sh
	@rm -f $(INITRD)
	$(MAKE) initramfs

clean:
	rm -rf $(OUT) rootfs/bin/relic rootfs/bin/relicos

distclean: clean
	rm -rf $(CACHE) rootfs/bin/busybox
