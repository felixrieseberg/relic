# Linux-hosted GCC cross-toolchain for PowerPC Mac OS X ("osxppc"):
# cctools-port's ppc-restored as/ld64 plus FSF GCC 14 targeting
# powerpc-apple-darwin8. Two Apple SDKs are baked in: MacOSX10.4u as the
# toolchain sysroot (GCC's libgcc is built against it -- the proven
# recipe), and MacOSX10.1.5 as the *app* sysroot, so the relic binary
# itself links against 10.1-era libSystem/crt1.o and runs on every
# PowerPC OS X release from 10.1 Puma to 10.5 Leopard.
#
# Apple's modern toolchain cannot emit ppc Mach-O anymore, so this image
# rebuilds the path the retro-PPC community keeps alive:
#   - cctools-port branch 877.8-ld64-253.9-ppc (Un1q32's PowerPC revival,
#     merged upstream into tpoechtrager/cctools-port) for as/ld/ar/otool
#   - VariantXYZ's gcc-14 branch with Darwin/PPC cross-build fixes
#   - crt1.o rebuilt from Apple's open-source Csu (the SDK's stock crt1.o
#     trips modern GCC/ld64; this is the documented fix)
# Recipe after github.com/VariantXYZ/gcc-powerpc-apple-darwin8 (see
# vxyz.me/00003.html), trimmed to C-only and pinned by commit.
#
# Both SDKs are fetched from a public mirror (sha256-pinned) and baked
# into the image. The image embeds whole Apple SDKs, so keep it local --
# don't push it to a registry. The binaries built against it ship in
# releases like every other target (docs/BUILDING.md).
FROM ubuntu:24.04

ARG TARGET=powerpc-apple-darwin8
ARG SDK=MacOSX10.4u
# DEPLOY here is the *toolchain/libgcc* floor, paired with the 10.4u
# sysroot. The app's floor is APP_DEPLOY=10.1 (end of file), which is what
# the build scripts see as $DEPLOY at container runtime.
ARG DEPLOY=10.4
ARG SDK_URL=https://github.com/phracker/MacOSX-SDKs/releases/download/11.3/MacOSX10.4u.sdk.tar.xz
ARG SDK_SHA256=d28276ce6b0f14af30f3bfdc171f8ef4033652da593578568f8be189506c1021
# Pinned commits: cctools-port merge of the 877.8-ld64-253.9-ppc fixes,
# tpoechtrager's xar (ld64 dependency), VariantXYZ's gcc-14 + Csu forks.
ARG CCTOOLS_COMMIT=6694f27d56923e64e6190c8d3eb149413768e9b7
ARG XAR_COMMIT=5fa4675419cfec60ac19a9c7f7c2d0e7c831a497
ARG GCC_COMMIT=67aa1b48dd9a0651a2c2e3b4a1bf6d12e2363fb8
ARG CSU_COMMIT=06967ab403a7a04e30e2f32479285d68152cedd2

ENV CROSS=/opt/cross
ENV PATH=$CROSS/bin:$PATH
ENV LD_LIBRARY_PATH=/usr/local/lib

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential clang git ca-certificates curl xz-utils file \
      autoconf automake libtool-bin flex bison texinfo python3 \
      zlib1g-dev libssl-dev libxml2-dev \
    && rm -rf /var/lib/apt/lists/*

# shallow-fetch a pinned commit (clone --depth can't take a raw sha)
RUN printf '%s\n' '#!/bin/sh' 'set -e; mkdir -p "$3"; cd "$3"' \
      'git init -q; git remote add origin "$1"' \
      'git fetch -q --depth 1 origin "$2"; git checkout -q FETCH_HEAD' \
      > /usr/local/bin/fetch-commit && chmod +x /usr/local/bin/fetch-commit

# xar: archive library ld64 links against.
RUN fetch-commit https://github.com/tpoechtrager/xar.git $XAR_COMMIT /tmp/xar \
    && cd /tmp/xar/xar \
    && CC=clang CXX=clang++ ./configure >/dev/null \
    && make -j"$(nproc)" >/dev/null && make install >/dev/null && ldconfig \
    && rm -rf /tmp/xar

# cctools + ld64 with PowerPC support: as, ld, ar, ranlib, nm, otool, lipo...
RUN fetch-commit https://github.com/tpoechtrager/cctools-port.git $CCTOOLS_COMMIT /tmp/cctools-port \
    && cd /tmp/cctools-port/cctools && ./autogen.sh \
    && ./configure CC=clang CXX=clang++ --prefix=$CROSS --target=$TARGET >/dev/null \
    && make -j"$(nproc)" >/dev/null && make install >/dev/null \
    && mkdir -p $CROSS/$TARGET/bin \
    && for t in ld ar as nm ranlib strip lipo otool; do \
         ln -s $CROSS/bin/$TARGET-$t $CROSS/$TARGET/bin/$t; done \
    && rm -rf /tmp/cctools-port
# dsymutil shim: only ever invoked for -g link steps, which we don't do;
# a no-op spares us a full LLVM build.
RUN printf '%s\n' '#!/bin/sh' 'exit 0' > $CROSS/bin/$TARGET-dsymutil \
    && chmod +x $CROSS/bin/$TARGET-dsymutil \
    && ln -s $CROSS/bin/$TARGET-dsymutil $CROSS/$TARGET/bin/dsymutil

# MacOSX10.4u SDK (sha256-pinned), thinned to the ppc slice: this ld64 can
# read fat archives, but single-arch inputs keep every link unambiguous.
# The arch is picked word-anchored from lipo's "are:" list -- a substring
# grep would match 'ppc' inside 'ppc64' or inside the file's own name.
RUN curl -fsSL "$SDK_URL" -o /tmp/sdk.tar.xz \
    && echo "$SDK_SHA256  /tmp/sdk.tar.xz" | sha256sum -c - \
    && tar xJf /tmp/sdk.tar.xz -C $CROSS && rm /tmp/sdk.tar.xz \
    && cd $CROSS/$SDK.sdk/usr/lib \
    && find . -type f | while read -r f; do \
         case "$(file -b "$f")" in *universal*) ;; *) continue;; esac; \
         archs=" $($CROSS/bin/$TARGET-lipo -info "$f" | sed 's/.* are: //') "; \
         case "$archs" in \
           *" ppc "*)     arch=ppc;; \
           *" ppc7400 "*) arch=ppc7400;; \
           *) continue;; \
         esac; \
         mv "$f" "$f.fat"; \
         if $CROSS/bin/$TARGET-lipo -thin "$arch" "$f.fat" -output "$f"; then \
           rm -f "$f.fat"; \
         else \
           echo "!! lipo -thin $arch failed for $f; keeping fat copy"; \
           mv "$f.fat" "$f"; \
         fi; done

# GCC 14, C only, hosted on Linux, targeting powerpc-apple-darwin8 with the
# SDK as sysroot. config.site entries disable as/ld feature probes that the
# old cctools would fail. Built with clang (the published recipe notes gcc
# OOMs the container). One RUN on purpose: the ~2 GB source + build trees
# must be deleted in the layer that created them or they stay in the image
# forever. Mid-build, crt1.o in the toolchain sysroot is replaced with a
# rebuild from Apple's open-source Csu -- the 10.4u SDK's stock object
# doesn't link with modern GCC, and libgcc's configure links against it.
# (The app itself links the 10.1.5 SDK's stock crt1.o; see below.)
RUN fetch-commit https://github.com/VariantXYZ/gcc-14-branch.git $GCC_COMMIT /tmp/gcc \
    && cd /tmp/gcc && ./contrib/download_prerequisites >/dev/null \
    && fetch-commit https://github.com/VariantXYZ/Csu.git $CSU_COMMIT /tmp/csu \
    && mkdir -p /tmp/gcc-build && cd /tmp/gcc-build \
    && { echo gcc_cv_as_mmacosx_version_min=no; \
         echo gcc_cv_as_darwin_build_version=no; \
         echo gcc_cv_ld64_demangle=0; \
         echo gcc_cv_ld64_platform_version=0; } > config.site-$TARGET \
    && CONFIG_SITE=/tmp/gcc-build/config.site-$TARGET /tmp/gcc/configure \
         CC=clang CXX=clang++ \
         --prefix=$CROSS --target=$TARGET \
         --with-sysroot=$CROSS/$SDK.sdk \
         --enable-languages=c --disable-bootstrap --disable-nls \
         --disable-multilib --with-dwarf2 \
         CFLAGS_FOR_TARGET="-O2 -mmacosx-version-min=$DEPLOY" \
         LDFLAGS_FOR_TARGET="-mmacosx-version-min=$DEPLOY" >/dev/null \
    && make -j"$(nproc)" all-gcc >/dev/null \
    && make install-gcc >/dev/null \
    && cd /tmp/csu \
    && make CC=$CROSS/bin/$TARGET-gcc \
         ARCH_CFLAGS="-arch ppc -D__private_extern__= -isysroot $CROSS/$SDK.sdk" \
         ./crt1.v1.o \
    && mv ./crt1.v1.o $CROSS/$SDK.sdk/usr/lib/crt1.o \
    && cd /tmp/gcc-build \
    && make -j"$(nproc)" >/dev/null && make install >/dev/null \
    && rm -rf /tmp/gcc /tmp/gcc-build /tmp/csu

# App sysroot: the 10.1.5 SDK. Its libs are plain ppc (pre-universal), so
# no thinning. Linking relic against this with -mmacosx-version-min=10.1
# yields a binary whose every import exists back to Puma's libSystem
# (55.1.0); 10.0 is unreachable regardless -- it never had an SDK, and
# xnu-123.5 has no /dev/random device for TLS entropy.
ARG APPSDK=MacOSX10.1.5
ARG APPSDK_URL=https://github.com/phracker/MacOSX-SDKs/releases/download/11.3/MacOSX10.1.5.sdk.tar.xz
ARG APPSDK_SHA256=cf939f34716cbf5a6046c650a54cf26ec67d102b856155e7f1de111d314e8f8a
ARG APP_DEPLOY=10.1
RUN curl -fsSL "$APPSDK_URL" -o /tmp/appsdk.tar.xz \
    && echo "$APPSDK_SHA256  /tmp/appsdk.tar.xz" | sha256sum -c - \
    && tar xJf /tmp/appsdk.tar.xz -C $CROSS && rm /tmp/appsdk.tar.xz

ENV TARGET=$TARGET SDKPATH=$CROSS/$SDK.sdk \
    APPSDK=$CROSS/$APPSDK.sdk DEPLOY=$APP_DEPLOY
WORKDIR /work
