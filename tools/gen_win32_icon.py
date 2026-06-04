#!/usr/bin/env python3
"""Compile src/plat/win32/relic_icon.txt into src/plat/win32/relic.ico.

Host-side asset generator (not part of the Docker build): run it once
whenever relic_icon.txt changes and commit the resulting relic.ico.

    python3 tools/gen_win32_icon.py

The .ico is Win95-vintage on purpose: 32x32 + 16x16 entries, 4-bpp
BMP-encoded (BITMAPINFOHEADER + 16-color palette + XOR/AND masks).
No PNG entries, no 256px entries -- those are Vista-era.

relic_icon.txt holds text pixel maps ("ICON32" / "ICON16" blocks) using
the classic Windows 16-color palette; see LEGEND in that file. `.` is
transparent. After writing the file, this script re-parses it and
round-trips every pixel as a self-check.
"""

import os
import re
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC = os.path.join(ROOT, "src", "plat", "win32", "relic_icon.txt")
OUT = os.path.join(ROOT, "src", "plat", "win32", "relic.ico")

# Standard Windows 16-color palette, in palette-index order.
PALETTE = [
    (0x00, 0x00, 0x00),  # 0  black
    (0x80, 0x00, 0x00),  # 1  maroon
    (0x00, 0x80, 0x00),  # 2  green
    (0x80, 0x80, 0x00),  # 3  olive
    (0x00, 0x00, 0x80),  # 4  navy
    (0x80, 0x00, 0x80),  # 5  purple
    (0x00, 0x80, 0x80),  # 6  teal
    (0xC0, 0xC0, 0xC0),  # 7  silver
    (0x80, 0x80, 0x80),  # 8  gray
    (0xFF, 0x00, 0x00),  # 9  red
    (0x00, 0xFF, 0x00),  # 10 lime
    (0xFF, 0xFF, 0x00),  # 11 yellow
    (0x00, 0x00, 0xFF),  # 12 blue
    (0xFF, 0x00, 0xFF),  # 13 fuchsia
    (0x00, 0xFF, 0xFF),  # 14 cyan
    (0xFF, 0xFF, 0xFF),  # 15 white
]

# Legend character -> palette index ('.' handled via the AND mask).
CHAR_INDEX = {
    "#": 0, "N": 4, "T": 6, "S": 7, "G": 8, "R": 9,
    "Y": 11, "B": 12, "M": 13, "C": 14, "W": 15,
}


def parse_grid(text, name, size):
    m = re.search(name + r"\n((?:[.#A-Z]+\n)+)", text)
    if not m:
        sys.exit("%s: block %s not found" % (SRC, name))
    rows = m.group(1).splitlines()
    if len(rows) != size:
        sys.exit("%s: %d rows, want %d" % (name, len(rows), size))
    for y, row in enumerate(rows):
        if len(row) != size:
            sys.exit("%s row %d: %d cols, want %d" % (name, y, len(row), size))
        for c in row:
            if c != "." and c not in CHAR_INDEX:
                sys.exit("%s row %d: unknown char %r" % (name, y, c))
    return rows


def encode_image(rows):
    """One ICO image: BITMAPINFOHEADER + palette + 4bpp XOR + 1bpp AND."""
    size = len(rows)
    xor_stride = ((size * 4 + 31) // 32) * 4   # 4bpp row, DWORD-aligned
    and_stride = ((size + 31) // 32) * 4       # 1bpp row, DWORD-aligned

    hdr = struct.pack(
        "<IiiHHIIiiII",
        40,                # biSize
        size,              # biWidth
        size * 2,          # biHeight: XOR + AND stacked
        1,                 # biPlanes
        4,                 # biBitCount
        0,                 # biCompression = BI_RGB
        xor_stride * size + and_stride * size,  # biSizeImage
        0, 0,              # biXPelsPerMeter, biYPelsPerMeter
        16,                # biClrUsed
        0,                 # biClrImportant
    )
    pal = b"".join(struct.pack("<BBBB", b, g, r, 0) for (r, g, b) in PALETTE)

    xor = bytearray()
    for row in reversed(rows):                 # DIBs are bottom-up
        line = bytearray(xor_stride)
        for x, c in enumerate(row):
            idx = CHAR_INDEX.get(c, 0)         # '.' -> 0, masked out anyway
            if x % 2 == 0:
                line[x // 2] |= idx << 4
            else:
                line[x // 2] |= idx
        xor += line

    mask = bytearray()
    for row in reversed(rows):
        line = bytearray(and_stride)
        for x, c in enumerate(row):
            if c == ".":                       # 1 = transparent
                line[x // 8] |= 0x80 >> (x % 8)
        mask += line

    return hdr + pal + bytes(xor) + bytes(mask)


def decode_image(blob, size):
    """Inverse of encode_image, for the round-trip self-check."""
    xor_stride = ((size * 4 + 31) // 32) * 4
    and_stride = ((size + 31) // 32) * 4
    xor_off = 40 + 64
    and_off = xor_off + xor_stride * size
    rows = []
    rev = {v: k for k, v in CHAR_INDEX.items()}
    for y in range(size):
        src_y = size - 1 - y
        line = []
        for x in range(size):
            mbyte = blob[and_off + src_y * and_stride + x // 8]
            if mbyte & (0x80 >> (x % 8)):
                line.append(".")
                continue
            pbyte = blob[xor_off + src_y * xor_stride + x // 2]
            idx = (pbyte >> 4) if x % 2 == 0 else (pbyte & 0x0F)
            line.append(rev[idx])
        rows.append("".join(line))
    return rows


def main():
    text = open(SRC).read()
    grids = [parse_grid(text, "ICON32", 32), parse_grid(text, "ICON16", 16)]
    images = [encode_image(g) for g in grids]

    count = len(images)
    out = struct.pack("<HHH", 0, 1, count)     # ICONDIR
    offset = 6 + 16 * count
    for grid, img in zip(grids, images):
        size = len(grid)
        out += struct.pack(
            "<BBBBHHII",
            size & 0xFF, size & 0xFF,          # width, height
            16, 0,                             # colors, reserved
            1, 4,                              # planes, bpp
            len(img), offset,
        )
        offset += len(img)
    for img in images:
        out += img

    with open(OUT, "wb") as f:
        f.write(out)

    # Round-trip self-check: decode what we wrote, compare to the source.
    blob = open(OUT, "rb").read()
    for i, grid in enumerate(grids):
        size = len(grid)
        (_, _, _, _, _, _, length, off) = struct.unpack_from("<BBBBHHII", blob, 6 + 16 * i)
        got = decode_image(blob[off:off + length], size)
        if got != list(grid):
            sys.exit("round-trip mismatch in %dx%d image" % (size, size))

    print("wrote %s (%d bytes, %d images, round-trip OK)" % (
        os.path.relpath(OUT, ROOT), len(out), count))


if __name__ == "__main__":
    main()
