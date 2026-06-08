# Cross-build for PowerPC Mac OS X 10.1-10.5, via a locally built Docker
# image holding a Linux-hosted powerpc-apple-darwin8 toolchain
# (cctools-port + GCC 14 + Apple SDKs; see Dockerfile).
#
# Output: dist/osxppc/relic  (Mach-O ppc executable, Mac OS X 10.1+;
#         also runs on Intel 10.4-10.6 under Rosetta)
#
# Mirrors build/wii/Makefile's ergonomics: `make -C build/osxppc` from
# a clean host produces the binary. First `make image` compiles the cross
# toolchain and bakes in Apple's 10.4u and 10.1.5 SDKs fetched from a
# public mirror -- the image embeds whole Apple SDKs, so don't push it to
# a registry.

ROOT    := ../..
OUT     := $(ROOT)/dist/osxppc
IMAGE   := relic-osxppc
CACHE   := $(abspath .cache)
MOUNTS  := -v $(abspath $(ROOT)):/work:ro -v $(abspath $(OUT)):/out -v $(CACHE):/cache
DOCKERI := docker run --rm -i -e FORCE $(MOUNTS) $(IMAGE)

.PHONY: all image bearssl relic audit shell clean

all: relic audit

image:
	docker build -t $(IMAGE) .

$(OUT):
	mkdir -p $(OUT)
$(CACHE):
	mkdir -p $(CACHE)

# BearSSL as a static archive, compiled with powerpc-apple-darwin8-gcc.
# Cached under .cache/libbearssl.a; delete (or FORCE=1) to rebuild.
bearssl: image | $(OUT) $(CACHE)
	sync; $(DOCKERI) bash -s < build_bearssl.sh

# Main build: compiles core/*.c + plat/posix + plat_cfg.c + main.c, links
# with libbearssl.a.
relic: bearssl
	sync; $(DOCKERI) bash -s < build_relic.sh

# Mach-O header + import audit (win32's import-table analogue): cputype
# ppc, LC_UNIXTHREAD, version-min, libSystem-only imports, every symbol
# present in 10.1's libSystem. Fails the build on any mismatch.
audit: relic
	sync; $(DOCKERI) bash -s < audit.sh

# Drop into the build container with /work R/W (useful for experiments).
shell: image | $(OUT) $(CACHE)
	docker run --rm -it \
	  -v $(abspath $(ROOT)):/work \
	  -v $(abspath $(OUT)):/out \
	  -v $(CACHE):/cache \
	  -w /work $(IMAGE) bash

clean:
	rm -rf $(OUT) $(CACHE)
