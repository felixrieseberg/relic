#!/usr/bin/env python3
"""Generate the Xbox icon assets, both committed:

  src/plat/xbox/default.tbn  64x64 PNG thumbnail shown by XBMC-family
                             dashboards next to default.xbe. A 2x
                             nearest-neighbor upscale of the win32 ghost
                             (src/plat/win32/relic_icon.txt, ICON32).
  src/plat/xbox/logo.pgm     100x17 binary PGM (maxval 255) consumed by
                             nxdk's cxbe -LOGO: the grayscale boot-logo
                             strip the console tints green at boot.
                             From src/plat/xbox/relic_logo.txt.

Host-side: run once whenever either pixel map changes and commit.

    python3 tools/gen_xbox_icons.py
"""

import os

import pixmap

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
ICON = os.path.join(ROOT, "src", "plat", "win32", "relic_icon.txt")
LOGO = os.path.join(ROOT, "src", "plat", "xbox", "relic_logo.txt")
TBN = os.path.join(ROOT, "src", "plat", "xbox", "default.tbn")
PGM = os.path.join(ROOT, "src", "plat", "xbox", "logo.pgm")

GRAY = {".": 0, "1": 64, "2": 128, "3": 192, "W": 255}


def main():
    rows = pixmap.scale(pixmap.parse_block(ICON, "ICON32", 32, 32), 2)
    pixmap.write_png(TBN, rows)
    pixmap.check_png_roundtrip(TBN, rows)
    print("wrote %s (%d bytes, 64x64, round-trip OK)"
          % (os.path.relpath(TBN, ROOT), os.path.getsize(TBN)))

    logo = pixmap.parse_block(LOGO, "LOGO100x17", 100, 17, charset=GRAY)
    body = bytes(GRAY[c] for row in logo for c in row)
    with open(PGM, "wb") as f:
        f.write(b"P5\n100 17\n255\n" + body)
    print("wrote %s (%d bytes, P5 100x17 maxval 255)"
          % (os.path.relpath(PGM, ROOT), os.path.getsize(PGM)))


if __name__ == "__main__":
    main()
