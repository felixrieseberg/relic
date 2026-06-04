# Official devkitPro PPC image: GCC cross-toolchain + libogc + portlibs
# (libfat, libwiikeyboard, libwiiuse, libbte) preinstalled. Multi-arch, so
# Apple Silicon hosts run it natively -- no Rosetta tax unlike nxdk.
FROM devkitpro/devkitppc:latest

# curl + ca-certificates for build_lua.sh's tarball fetch. The base image is
# Debian-derived; keep the layer thin.
RUN apt-get update && apt-get install -y --no-install-recommends \
      curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

ENV DEVKITPRO=/opt/devkitpro
ENV DEVKITPPC=/opt/devkitpro/devkitPPC
ENV PATH=$DEVKITPPC/bin:$DEVKITPRO/tools/bin:$PATH

WORKDIR /work
