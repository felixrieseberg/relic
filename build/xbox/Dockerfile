FROM --platform=linux/amd64 debian:bookworm-slim

# nxdk needs: clang/lld (LLVM toolchain), make, cmake, bison/flex (for cxbe
# generation tools in nxdk's tree), python3, git, bc. We install a native
# LLVM 14+ from Debian stable -- newer is fine, nxdk supports 11..18.
RUN apt-get update && apt-get install -y --no-install-recommends \
      git ca-certificates curl make cmake bc \
      clang lld llvm \
      bison flex \
      python3 \
      xxd \
      && rm -rf /var/lib/apt/lists/*

# nxdk has no tagged releases, so pin to a known-good commit on master.
# Override with `--build-arg NXDK_REF=<sha>` to bump.
ARG NXDK_REF=d0ae96acc926a6c3f6254b289a1a9a3c1db182f1
RUN git clone https://github.com/XboxDev/nxdk.git /opt/nxdk \
    && cd /opt/nxdk && git checkout $NXDK_REF \
    && git submodule update --init --recursive

# Pre-build nxdk's own tools (cxbe, extract-xiso) and its SDK libs so every
# project build reuses them. NXDK_ONLY=y keeps make from trying to build a
# sample project. Enable NXDK_NET so lwIP is baked into libnxdk_net.lib.
ENV NXDK_DIR=/opt/nxdk
RUN cd /opt/nxdk && /bin/bash -c "eval \$(./bin/activate -s) && \
    make -j\$(nproc) NXDK_ONLY=y NXDK_NET=y cxbe extract-xiso || true"

# cxbe (the host-native Xbox .xbe generator) is built with g++, not clang.
# Install it AFTER the heavy nxdk clone so a future g++ bump doesn't
# re-invalidate the multi-minute git-clone layer. apt still caches within
# the build.
RUN apt-get update && apt-get install -y --no-install-recommends g++ \
    && rm -rf /var/lib/apt/lists/*

# Sanity: the activate script sets CC=nxdk-cc etc; keep PATH so it's reachable
# from non-interactive sh. nxdk's activate is bash-specific, so our build
# script sources it explicitly rather than relying on the dockerfile ENV.
ENV PATH=/opt/nxdk/bin:$PATH

WORKDIR /work
