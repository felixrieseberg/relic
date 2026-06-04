# Retro68 cross-build for classic Mac OS PPC, via the official Docker image.
# The cmake build tree lives in the host-mounted .cache/ so libbearssl.a and
# every unchanged .o survive across docker runs (~1.4s warm rebuild).

ROOT     := ../..
OUT      := $(ROOT)/dist/macppc
CACHE    := $(abspath .cache)
# Pinned by digest (was :latest as of 2026-04). Override with IMAGE=... to bump.
IMAGE    ?= ghcr.io/autc04/retro68@sha256:e8b6cc8ac3c0cf26dcb299d5396cc7055c102b6bc46b67e2df960453af8ae92b
TOOLCHAIN:= /Retro68-build/toolchain
CMAKE_TC := $(TOOLCHAIN)/powerpc-apple-macos/cmake/retroppc.toolchain.cmake
DOCKER   := docker run --rm --platform linux/amd64 \
              -v $(abspath $(ROOT)):/work:ro -v $(abspath $(OUT)):/out -v $(CACHE):/cache $(IMAGE)
BUILD_DAYS := $(shell expr `date +%s` / 86400)
CONFIGURE  := cmake -S /work/build/macppc -B /cache/ppc -DCMAKE_TOOLCHAIN_FILE=$(CMAKE_TC) -DBUILD_UNIX_DAYS=$(BUILD_DAYS) >/dev/null

# EXTRA lets a target append post-build steps (e.g. hcopy RELIC.CFG into the .dsk).
define build_app
	@mkdir -p $(OUT) $(CACHE)
	$(DOCKER) bash -c 'set -e; $(CONFIGURE) && \
	  cmake --build /cache/ppc --target $(1)_APPL -- -j4 && \
	  $(EXTRA) \
	  cp /cache/ppc/$(1).bin /cache/ppc/$(1).APPL /cache/ppc/$(1).dsk /out/'
endef

.PHONY: all relic shell clean

all: relic

# If a dev RELIC.CFG was staged in dist/macppc/ (run_macppc.sh does this from
# .env.local), use it; otherwise fall back to the committed example.
DEV_CFG := $(abspath $(OUT))/RELIC.CFG
CFG_SRC := $(if $(wildcard $(DEV_CFG)),/out/RELIC.CFG,/work/RELIC.CFG.example)
relic: EXTRA = $(TOOLCHAIN)/bin/hmount /cache/ppc/Relic.dsk >/dev/null && \
	  $(TOOLCHAIN)/bin/hcopy -r $(CFG_SRC) :RELIC.CFG && \
	  $(TOOLCHAIN)/bin/hattrib -t TEXT -c ttxt :RELIC.CFG && \
	  $(TOOLCHAIN)/bin/humount &&
relic: ; $(call build_app,Relic)

shell:
	docker run --rm -it --platform linux/amd64 -v $(abspath $(ROOT)):/work -w /work $(IMAGE) bash

clean:
	rm -rf $(OUT) $(CACHE)
