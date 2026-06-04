# devkitPPC cross-build for the Nintendo Wii (PowerPC 750CL "Broadway").
# Runs the devkitPro toolchain inside the official devkitpro/devkitppc Docker
# image (multi-arch; native on Apple Silicon).
#
# Output: dist/wii/relic.dol  (the runnable Wii executable)
#         dist/wii/relic.elf  (debug-symbol ELF; Dolphin can boot either)
#
# Mirrors build/xbox/Makefile's ergonomics: `make -C build/wii` from a clean
# host produces the .dol via two `make` invocations (image, then build).

ROOT    := ../..
OUT     := $(ROOT)/dist/wii
IMAGE   := relic-dkp
CACHE   := $(abspath .cache)
MOUNTS  := -v $(abspath $(ROOT)):/work:ro -v $(abspath $(OUT)):/out -v $(CACHE):/cache
DOCKER  := docker run --rm $(MOUNTS) $(IMAGE)
DOCKERI := docker run --rm -i $(MOUNTS) $(IMAGE)

.PHONY: all image bearssl lua relic sd shell clean

all: relic

image:
	docker build -t $(IMAGE) .

$(OUT):
	mkdir -p $(OUT)
$(CACHE):
	mkdir -p $(CACHE)

# BearSSL as a static archive, compiled with powerpc-eabi-gcc against the Wii
# target. Cached under .cache/libbearssl.a; delete to rebuild.
bearssl: image | $(OUT) $(CACHE)
	sync; $(DOCKERI) bash -s < build_bearssl.sh

# Lua: tarball + headers + liblua.a all live under .cache/ -- nothing
# vendored. Same caching contract as BearSSL.
lua: image | $(OUT) $(CACHE)
	sync; $(DOCKERI) bash -s < build_lua.sh

# Main build: compiles core/*.c + plat/wii/*.c + plat_cfg.c + main.c, links
# with libbearssl.a + liblua.a + libogc + libfat, produces relic.elf via
# elf2dol -> relic.dol.
relic: bearssl lua
	sync; $(DOCKERI) bash -s < build_relic.sh

# Stage dist/wii/{boot.dol, meta.xml, RELIC.CFG} into dist/wii/sd/apps/relic/
# WITHOUT recompiling. tools/run_wii.sh calls this after staging a fresh
# RELIC.CFG so every Dolphin launch sees the current API key on the virtual
# SD card.
sd: | $(OUT)
	mkdir -p $(abspath $(OUT))/sd/apps/relic
	cp $(OUT)/relic.dol $(OUT)/sd/apps/relic/boot.dol
	cp $(OUT)/meta.xml $(OUT)/sd/apps/relic/meta.xml
	cp $(OUT)/icon.png $(OUT)/sd/apps/relic/icon.png
	[ -f $(OUT)/RELIC.CFG ] && cp $(OUT)/RELIC.CFG $(OUT)/sd/apps/relic/ || true

# Drop into the build container with /work R/W (useful for experiments).
shell: image | $(OUT) $(CACHE)
	docker run --rm -it \
	  -v $(abspath $(ROOT)):/work \
	  -v $(abspath $(OUT)):/out \
	  -v $(CACHE):/cache \
	  -e ANTHROPIC_API_KEY \
	  -w /work $(IMAGE) bash

clean:
	rm -rf $(OUT) $(CACHE)
