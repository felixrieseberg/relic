# nxdk cross-build for the original Microsoft Xbox (2001 / Pentium III).
# Runs the nxdk toolchain (LLVM clang/lld) inside Docker (linux/amd64 image,
# Apple Silicon hosts pay Rosetta overhead on nxdk's own tool rebuild, not
# on our build).
#
# Output: dist/xbox/default.xbe  (the runnable Xbox executable)
#         dist/xbox/Relic.iso    (bootable DVD image, if extract-xiso built)
#
# Mirrors build/win32/Makefile's ergonomics: `make -C build/xbox` from a
# clean macOS host produces the .xbe via two `make` invocations (image, then
# the build itself).

ROOT    := ../..
OUT     := $(ROOT)/dist/xbox
IMAGE   := relic-nxdk
CACHE   := $(abspath .cache)
MOUNTS  := -v $(abspath $(ROOT)):/work:ro -v $(abspath $(OUT)):/out -v $(CACHE):/cache
DOCKER  := docker run --rm --platform linux/amd64 $(MOUNTS) $(IMAGE)
DOCKERI := docker run --rm -i --platform linux/amd64 $(MOUNTS) $(IMAGE)

.PHONY: all image bearssl lua relic iso shell clean

# build_relic.sh inside the container already produces dist/xbox/default.xbe
# AND dist/xbox/Relic.iso, so `all` and `relic` do the same thing. Mirroring
# build/win32/Makefile's shape (it also aliases all->relic).
all: relic

# Rewrap dist/xbox/{default.xbe,RELIC.CFG,...} into Relic.iso WITHOUT
# recompiling. tools/run_xbox.sh calls this after staging a fresh
# RELIC.CFG so every launch ships the current API key + prompt on the
# virtual DVD (the bare .xbe alone won't carry adjacent files).
iso: | $(OUT)
	docker run --rm --platform linux/amd64 \
	  -v $(abspath $(OUT)):/out $(IMAGE) \
	  bash -c 'mkdir -p /tmp/iso && cp -v /out/default.xbe /tmp/iso/ \
	    && ([ -f /out/RELIC.CFG ] && cp -v /out/RELIC.CFG /tmp/iso/ || true) \
	    && ([ -f /out/default.tbn ] && cp -v /out/default.tbn /tmp/iso/ || true) \
	    && /opt/nxdk/tools/extract-xiso/build/extract-xiso -c /tmp/iso /out/Relic.iso 2>&1 | tail -3'

image:
	docker build --platform linux/amd64 -t $(IMAGE) .

$(OUT):
	mkdir -p $(OUT)
$(CACHE):
	mkdir -p $(CACHE)

# BearSSL as a static archive, compiled with nxdk's clang against the Xbox
# target. Cached under .cache/libbearssl.a; delete to rebuild.
bearssl: image | $(OUT) $(CACHE)
	sync; $(DOCKERI) bash -s < build_bearssl.sh

# Lua: tarball + headers + liblua.a all live under .cache/ -- nothing
# vendored. Same /cache caching contract as BearSSL.
lua: image | $(OUT) $(CACHE)
	sync; $(DOCKERI) bash -s < build_lua.sh

# Main build: compiles core/*.c + plat/xbox/*.c + plat_cfg.c + main.c,
# links with libbearssl.a + liblua.a + nxdk's libs (liblwip, libxboxkrnl,
# libnxdk_hal, libpdclib), produces default.xbe via cxbe.
relic: bearssl lua
	sync; $(DOCKERI) bash -s < build_relic.sh

# Drop into the build container with /work R/W (useful for experiments).
shell: image | $(OUT)
	docker run --rm -it --platform linux/amd64 \
	  -v $(abspath $(ROOT)):/work \
	  -v $(abspath $(OUT)):/out \
	  -v $(CACHE):/cache \
	  -e ANTHROPIC_API_KEY \
	  -w /work $(IMAGE) bash

clean:
	rm -rf $(OUT) $(CACHE)
