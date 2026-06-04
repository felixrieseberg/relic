# Open Watcom v2 cross-build for Win95 (i386 PE, subsystem 4.0, static CRT).
# Runs the Watcom toolchain + Wine inside Docker (linux/amd64).
#
# NOTE: build scripts are piped via stdin (sh -s <) to dodge a Docker Desktop
# file-sync race that corrupts recently-edited files on the volume mount.

ROOT    := ../..
OUT     := $(ROOT)/dist/win32
IMAGE   := relic-ow2
CACHE   := $(abspath .cache)
MOUNTS  := -v $(abspath $(ROOT)):/work:ro -v $(abspath $(OUT)):/out -v $(CACHE):/cache
DOCKER  := docker run --rm --platform linux/amd64 $(MOUNTS) $(IMAGE)
DOCKERI := docker run --rm -i --platform linux/amd64 $(MOUNTS) $(IMAGE)

.PHONY: all image bearssl relic run audit floppy shell clean

all: relic audit floppy

floppy: relic
	$(ROOT)/tools/pack_win32.sh

image:
	docker build --platform linux/amd64 -t $(IMAGE) .

$(OUT):
	mkdir -p $(OUT)
# Create .cache on the host BEFORE docker mounts it: on Linux the daemon
# would otherwise create it root-owned and the host-side audit redirects
# into it would fail. (Mirrors build/xbox and build/wii.)
$(CACHE):
	mkdir -p $(CACHE)

bearssl: image | $(OUT) $(CACHE)
	sync; $(DOCKERI) sh -s < build_bearssl.sh

relic: bearssl
	sync; $(DOCKERI) sh -s < build_relic.sh

run: relic
	docker run --rm --platform linux/amd64 -e ANTHROPIC_API_KEY \
	  -v $(abspath $(ROOT)):/work:ro -v $(abspath $(OUT)):/out $(IMAGE) \
	  sh -c 'wine /out/RELIC.EXE -p "$(P)"'

# Import-table audit: fail the build if any function outside the Win95
# allowlist is imported.
audit: relic
	@$(DOCKER) sh -c 'wdump /out/RELIC.EXE' 2>/dev/null \
	  | grep -E "^\s+[0-9A-F]+\s+[0-9]+\s+[A-Za-z]" | awk '{print $$3}' | sort -u \
	  > $(CACHE)/imports.txt
	@if grep -Ev -f $(ROOT)/tools/win32-allowlist.txt $(CACHE)/imports.txt > $(CACHE)/bad-imports.txt; then \
	  echo "*** Win95-unsafe imports:"; cat $(CACHE)/bad-imports.txt; exit 1; \
	else echo "Win95 import audit: OK ($$(wc -l < $(CACHE)/imports.txt) imports)"; fi

shell: image | $(OUT)
	docker run --rm -it --platform linux/amd64 \
	  -v $(abspath $(ROOT)):/work \
	  -v $(abspath $(OUT)):/out \
	  -e ANTHROPIC_API_KEY \
	  -w /work $(IMAGE) bash

clean:
	rm -rf $(OUT)
