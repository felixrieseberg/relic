"""Shared helpers for the host-side icon generators (gen_wii_icon.py,
gen_xbox_icons.py, gen_macppc_icons.py).

Text pixel maps live next to each platform's sources (relic_icon.txt,
relic_banner.txt, relic_logo.txt) using a common legend: `.` is
transparent/background, letters are colors from the classic Windows
16-color palette. parse_block() validates dimensions and charset hard --
a malformed map should fail the generator, never produce a broken asset.

gen_win32_icon.py predates this module and is self-contained; it shares
the legend but not the code.
"""

import re
import struct
import zlib

# Legend character -> RGB. Same legend across all pixel-map files.
RGB = {
    "#": (0x00, 0x00, 0x00),
    "W": (0xFF, 0xFF, 0xFF),
    "S": (0xC0, 0xC0, 0xC0),
    "G": (0x80, 0x80, 0x80),
    "N": (0x00, 0x00, 0x80),
    "B": (0x00, 0x00, 0xFF),
    "C": (0x00, 0xFF, 0xFF),
    "T": (0x00, 0x80, 0x80),
    "M": (0xFF, 0x00, 0xFF),
    "R": (0xFF, 0x00, 0x00),
    "Y": (0xFF, 0xFF, 0x00),
}


def parse_block(path, name, width, height, charset=None):
    """Read block `name` from a pixel-map file; return list of row strings."""
    text = open(path).read()
    m = re.search(name + r"\n((?:[^\n]+\n)+)", text)
    if not m:
        raise SystemExit("%s: block %s not found" % (path, name))
    rows = m.group(1).splitlines()
    if len(rows) != height:
        raise SystemExit("%s %s: %d rows, want %d" % (path, name, len(rows), height))
    allowed = set(charset if charset is not None else "." + "".join(RGB))
    for y, row in enumerate(rows):
        if len(row) != width:
            raise SystemExit("%s %s row %d: %d cols, want %d"
                             % (path, name, y, len(row), width))
        bad = set(row) - allowed
        if bad:
            raise SystemExit("%s %s row %d: unknown chars %s"
                             % (path, name, y, sorted(bad)))
    return rows


def scale(rows, factor):
    """Nearest-neighbor upscale of a pixel map."""
    out = []
    for row in rows:
        wide = "".join(c * factor for c in row)
        out.extend([wide] * factor)
    return out


def _chunk(tag, payload):
    return (struct.pack(">I", len(payload)) + tag + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))


def write_png(path, rows):
    """Write a pixel map as an 8-bit RGBA PNG ('.' -> fully transparent)."""
    h, w = len(rows), len(rows[0])
    raw = bytearray()
    for row in rows:
        raw.append(0)                       # filter: None
        for c in row:
            if c == ".":
                raw += b"\x00\x00\x00\x00"
            else:
                raw += bytes(RGB[c]) + b"\xff"
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", ihdr)
           + _chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + _chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def read_png(path):
    """Decode a PNG written by write_png (filter-0 RGBA only); return rows
    of (r, g, b, a) tuples. Used for round-trip self-checks."""
    blob = open(path, "rb").read()
    assert blob[:8] == b"\x89PNG\r\n\x1a\n"
    pos, w, h, idat = 8, 0, 0, b""
    while pos < len(blob):
        ln, tag = struct.unpack_from(">I4s", blob, pos)
        body = blob[pos + 8:pos + 8 + ln]
        if tag == b"IHDR":
            w, h, depth, ctype = struct.unpack_from(">IIBB", body)
            assert (depth, ctype) == (8, 6), "not 8-bit RGBA"
        elif tag == b"IDAT":
            idat += body
        pos += 12 + ln
    raw = zlib.decompress(idat)
    stride = 1 + w * 4
    rows = []
    for y in range(h):
        line = raw[y * stride:(y + 1) * stride]
        assert line[0] == 0, "unexpected PNG filter"
        rows.append([tuple(line[1 + x * 4:5 + x * 4]) for x in range(w)])
    return rows


def check_png_roundtrip(path, rows):
    got = read_png(path)
    for y, row in enumerate(rows):
        for x, c in enumerate(row):
            want = (0, 0, 0, 0) if c == "." else RGB[c] + (0xFF,)
            if got[y][x] != want:
                raise SystemExit("%s: pixel (%d,%d) mismatch" % (path, x, y))
