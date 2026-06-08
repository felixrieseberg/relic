# Open Watcom v2 cross-build for Windows 3.x (Win386: 32-bit flat code bound
# into an NE executable; needs a 386 in enhanced mode + Winsock 1.1).
# Shares the relic-ow2 Docker image with build/win32 (same toolchain).
#
# NOTE: build scripts are piped via stdin (sh -s <) to dodge a Docker Desktop
# file-sync race that corrupts recently-edited files on the volume mount.

ROOT    := ../..
OUT     := $(ROOT)/dist/win16
IMAGE   := relic-ow2
CACHE   := $(abspath .cache)
MOUNTS  := -v $(abspath $(ROOT)):/work:ro -v $(abspath $(OUT)):/out -v $(CACHE):/cache
DOCKER  := docker run --rm --platform linux/amd64 $(MOUNTS) $(IMAGE)
DOCKERI := docker run --rm -i --platform linux/amd64 $(MOUNTS) $(IMAGE)

.PHONY: all image bearssl relic audit floppy shell clean

all: relic audit floppy

floppy: relic
	$(ROOT)/tools/pack_win16.sh

# The image is defined once in build/win32; build it from there.
image:
	docker build --platform linux/amd64 -t $(IMAGE) ../win32

$(OUT):
	mkdir -p $(OUT)
# Create .cache on the host BEFORE docker mounts it: on Linux the daemon
# would otherwise create it root-owned and the host-side audit redirects
# into it would fail. (Mirrors build/win32.)
$(CACHE):
	mkdir -p $(CACHE)

bearssl: image | $(OUT) $(CACHE)
	sync; $(DOCKERI) sh -s < build_bearssl.sh

relic: bearssl
	sync; $(DOCKERI) sh -s < build_relic.sh

# Import audit: a bound Win386 NE may reference only the modules every
# Windows 3.1 install has (WINSOCK.DLL is resolved at runtime on purpose,
# so it never appears here and the app can start without a TCP/IP stack).
# Module granularity only -- NE imports are module+ordinal, so an ordinal
# added after 3.1 would still pass; per-function 3.1 availability relies
# on review (the win386 supervisor itself targets Windows 3.0).
audit: relic
	@$(DOCKER) sh -c 'wdump /out/RELIC.EXE' 2>/dev/null \
	  | sed -n '/Module Reference Table/,/^ *$$/p' | grep -E '^[A-Z][A-Z0-9_.]*$$' \
	  | sort -u > $(CACHE)/modules.txt
	@if ! [ -s $(CACHE)/modules.txt ]; then \
	  echo "*** module audit matched nothing (wdump format change?)"; exit 1; \
	elif grep -Ev -f $(ROOT)/tools/win16-allowlist.txt $(CACHE)/modules.txt > $(CACHE)/bad-modules.txt; then \
	  echo "*** Win3.x-unsafe module references:"; cat $(CACHE)/bad-modules.txt; exit 1; \
	else echo "Win3.x module audit: OK ($$(wc -l < $(CACHE)/modules.txt) modules)"; fi

shell: | $(OUT)
	docker run --rm -it --platform linux/amd64 \
	  -v $(abspath $(ROOT)):/work \
	  -v $(abspath $(OUT)):/out \
	  -v $(CACHE):/cache \
	  -e INCLUDE=/opt/watcom/h:/opt/watcom/h/win \
	  -w /work $(IMAGE) bash

clean:
	rm -rf $(OUT)
