FROM --platform=linux/amd64 debian:bookworm-slim
RUN dpkg --add-architecture i386 && apt-get update && \
    apt-get install -y --no-install-recommends curl xz-utils ca-certificates make wine wine32 mtools && \
    rm -rf /var/lib/apt/lists/*
ENV WINEDEBUG=-all WINEPREFIX=/root/.wine
RUN wine wineboot --init 2>/dev/null || true
# Pinned monthly snapshot — "Current-build" is a moving tag and would make the
# Win95 import-table audit non-reproducible.
ARG WATCOM_TAG=2026-04-01-Build
RUN mkdir -p /opt/watcom && \
    curl -fsSL https://github.com/open-watcom/open-watcom-v2/releases/download/${WATCOM_TAG}/ow-snapshot.tar.xz \
    | tar -xJ -C /opt/watcom
ENV WATCOM=/opt/watcom
ENV PATH=$WATCOM/binl64:$PATH
ENV INCLUDE=$WATCOM/h:$WATCOM/h/nt
ENV LIB=$WATCOM/lib386:$WATCOM/lib386/nt
WORKDIR /work
