#!/usr/bin/env python3
"""Compile src/plat/wii/relic_banner.txt into src/plat/wii/icon.png.

Host-side asset generator: run once whenever relic_banner.txt changes and
commit the resulting icon.png.

    python3 tools/gen_wii_icon.py

icon.png is the 128x48 banner the Wii Homebrew Channel draws on the app's
card (staged into sd/apps/relic/ by `make -C build/wii sd`). RGBA with
transparency; HBC composites it onto a light glassy card.
"""

import os

import pixmap

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC = os.path.join(ROOT, "src", "plat", "wii", "relic_banner.txt")
OUT = os.path.join(ROOT, "src", "plat", "wii", "icon.png")


def main():
    rows = pixmap.parse_block(SRC, "BANNER128x48", 128, 48)
    pixmap.write_png(OUT, rows)
    pixmap.check_png_roundtrip(OUT, rows)
    print("wrote %s (%d bytes, 128x48, round-trip OK)"
          % (os.path.relpath(OUT, ROOT), os.path.getsize(OUT)))


if __name__ == "__main__":
    main()
