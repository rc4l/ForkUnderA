#!/usr/bin/env python
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] Assembles an IWAD from a manifest. The tool knows lump formats and drawing primitives;
# it knows nothing about any particular game. What goes in a given IWAD is the manifest's business.
#
# An IWAD here is the minimum a total-conversion pk3 needs underneath it: the engine chrome the
# pk3 does not carry (status bar face, pause, intermission), placeholder patches so stock texture
# names resolve, and one map so the engine can identify the file at all.
#
# Art is written as PNG lumps rather than Doom patches on purpose. A patch is palette indexed, and
# the palette at runtime belongs to whichever pk3 is loaded on top, so patch bytes authored against
# a different palette come out miscoloured. PNG carries its own colours and the engine converts.

import io
import json
import os
import struct
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))

# ---------------------------------------------------------------------------------------------
# WAD container
# ---------------------------------------------------------------------------------------------


def write_wad(path, lumps, signature=b"IWAD"):
    """lumps is a list of (name, bytes). Zero length entries are markers."""
    body = io.BytesIO()
    body.write(b"\0" * 12)
    table = []
    for name, data in lumps:
        if len(name) > 8:
            raise ValueError("lump name over 8 characters: %r" % name)
        table.append((body.tell(), len(data), name.upper().encode("ascii")))
        body.write(data)
    directory = body.tell()
    for offset, size, name in table:
        body.write(struct.pack("<ii8s", offset, size, name.ljust(8, b"\0")))
    blob = body.getvalue()
    blob = signature + struct.pack("<ii", len(table), directory) + blob[12:]
    with open(path, "wb") as fh:
        fh.write(blob)
    return len(blob)


def as_png(image):
    out = io.BytesIO()
    image.save(out, "PNG", optimize=True)
    return out.getvalue()


# ---------------------------------------------------------------------------------------------
# A 5x7 bitmap font, drawn here so the file owes nothing to anyone else's glyphs
# ---------------------------------------------------------------------------------------------

GLYPHS = {
    "A": ".###.|#...#|#...#|#####|#...#|#...#|#...#",
    "B": "####.|#...#|#...#|####.|#...#|#...#|####.",
    "C": ".###.|#...#|#....|#....|#....|#...#|.###.",
    "D": "####.|#...#|#...#|#...#|#...#|#...#|####.",
    "E": "#####|#....|#....|####.|#....|#....|#####",
    "F": "#####|#....|#....|####.|#....|#....|#....",
    "G": ".###.|#...#|#....|#.###|#...#|#...#|.###.",
    "H": "#...#|#...#|#...#|#####|#...#|#...#|#...#",
    "I": ".###.|..#..|..#..|..#..|..#..|..#..|.###.",
    "J": "..###|...#.|...#.|...#.|...#.|#..#.|.##..",
    "K": "#...#|#..#.|#.#..|##...|#.#..|#..#.|#...#",
    "L": "#....|#....|#....|#....|#....|#....|#####",
    "M": "#...#|##.##|#.#.#|#...#|#...#|#...#|#...#",
    "N": "#...#|##..#|#.#.#|#..##|#...#|#...#|#...#",
    "O": ".###.|#...#|#...#|#...#|#...#|#...#|.###.",
    "P": "####.|#...#|#...#|####.|#....|#....|#....",
    "Q": ".###.|#...#|#...#|#...#|#.#.#|#..#.|.##.#",
    "R": "####.|#...#|#...#|####.|#.#..|#..#.|#...#",
    "S": ".####|#....|#....|.###.|....#|....#|####.",
    "T": "#####|..#..|..#..|..#..|..#..|..#..|..#..",
    "U": "#...#|#...#|#...#|#...#|#...#|#...#|.###.",
    "V": "#...#|#...#|#...#|#...#|#...#|.#.#.|..#..",
    "W": "#...#|#...#|#...#|#...#|#.#.#|##.##|#...#",
    "X": "#...#|#...#|.#.#.|..#..|.#.#.|#...#|#...#",
    "Y": "#...#|#...#|.#.#.|..#..|..#..|..#..|..#..",
    "Z": "#####|....#|...#.|..#..|.#...|#....|#####",
    "0": ".###.|#...#|#..##|#.#.#|##..#|#...#|.###.",
    "1": "..#..|.##..|..#..|..#..|..#..|..#..|.###.",
    "2": ".###.|#...#|....#|...#.|..#..|.#...|#####",
    "3": "#####|...#.|..#..|...#.|....#|#...#|.###.",
    "4": "...#.|..##.|.#.#.|#..#.|#####|...#.|...#.",
    "5": "#####|#....|####.|....#|....#|#...#|.###.",
    "6": "..##.|.#...|#....|####.|#...#|#...#|.###.",
    "7": "#####|....#|...#.|..#..|.#...|.#...|.#...",
    "8": ".###.|#...#|#...#|.###.|#...#|#...#|.###.",
    "9": ".###.|#...#|#...#|.####|....#|...#.|.##..",
    ":": ".....|..#..|..#..|.....|..#..|..#..|.....",
    "/": "....#|....#|...#.|..#..|.#...|#....|#....",
    "%": "##..#|##..#|...#.|..#..|.#...|#..##|#..##",
    "-": ".....|.....|.....|#####|.....|.....|.....",
    ".": ".....|.....|.....|.....|.....|.##..|.##..",
    "!": "..#..|..#..|..#..|..#..|..#..|.....|..#..",
    "'": "..#..|..#..|.....|.....|.....|.....|.....",
    " ": ".....|.....|.....|.....|.....|.....|.....",
}

GLYPH_W, GLYPH_H = 5, 7
TRACKING = 1


def text_size(s, scale):
    n = len(s)
    w = n * GLYPH_W + max(0, n - 1) * TRACKING
    return w * scale, GLYPH_H * scale


def draw_text(s, scale=2, fill=(255, 255, 255, 255), shadow=(0, 0, 0, 255)):
    """Renders uppercase text. A one pixel shadow keeps it legible on any background."""
    s = s.upper()
    for ch in s:
        if ch not in GLYPHS:
            raise ValueError("no glyph for %r (text %r)" % (ch, s))
    w, h = text_size(s, scale)
    pad = scale if shadow else 0
    img = Image.new("RGBA", (w + pad, h + pad), (0, 0, 0, 0))
    px = img.load()

    def stamp(ox, oy, colour):
        for i, ch in enumerate(s):
            rows = GLYPHS[ch].split("|")
            gx = (i * (GLYPH_W + TRACKING)) * scale
            for ry, row in enumerate(rows):
                for rx, cell in enumerate(row):
                    if cell != "#":
                        continue
                    for dy in range(scale):
                        for dx in range(scale):
                            px[ox + gx + rx * scale + dx, oy + ry * scale + dy] = colour

    if shadow:
        stamp(pad, pad, shadow)
    stamp(0, 0, fill)
    return img


# ---------------------------------------------------------------------------------------------
# Generators. Each takes (spec) and returns lump bytes.
# ---------------------------------------------------------------------------------------------

def gen_text(spec):
    return as_png(draw_text(spec["text"],
                            scale=spec.get("scale", 2),
                            fill=tuple(spec.get("fill", (255, 255, 255, 255)))))


def gen_border(spec):
    """One tile of the view border. `edge` picks which sides get the bevel, so it faces inward."""
    edge = spec["edge"]
    w, h = spec.get("width", 8), spec.get("height", 8)
    light, mid, dark = (150, 170, 205, 255), (86, 104, 140, 255), (40, 50, 74, 255)
    img = Image.new("RGBA", (w, h), mid)
    d = ImageDraw.Draw(img)
    last = 0
    if "t" in edge:
        d.line([(0, 0), (w - 1, 0)], fill=light)
    if "b" in edge:
        d.line([(0, h - 1), (w - 1, h - 1)], fill=dark)
    if "l" in edge:
        d.line([(0, 0), (0, h - 1)], fill=light)
    if "r" in edge:
        d.line([(w - 1, 0), (w - 1, h - 1)], fill=dark)
    return as_png(img)


def gen_blank(spec):
    """A one pixel patch with no posts: the name resolves, and nothing is drawn.

    This is how you switch a piece of engine chrome off. A total conversion draws its own status
    bar and intermission, so the stock mugshot and word graphics have no place to go; leaving them
    blank is the deliberate answer, and inventing art for them puts a stranger on screen.
    """
    return struct.pack("<hhhh", 1, 1, 0, 0) + struct.pack("<i", 16) + b"\xff"


def gen_sky(spec):
    w, h = spec.get("width", 256), spec.get("height", 128)
    img = Image.new("RGBA", (w, h))
    d = ImageDraw.Draw(img)
    top, bottom = (8, 10, 34, 255), (60, 44, 110, 255)
    for y in range(h):
        t = y / float(h - 1)
        d.line([(0, y), (w, y)],
               fill=tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,))
    # Stars from a fixed seed rather than the system generator, so the file is byte reproducible.
    # Multiplying the index by a constant is what NOT to do here: it lays them out on a lattice.
    seed = spec.get("seed", 20250807)
    for _ in range(90):
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        x = (seed >> 7) % w
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        y = (seed >> 7) % (h * 2 // 3)
        v = 150 + ((seed >> 3) % 100)
        d.point((x, y), fill=(v, v, 255, 255))
    return as_png(img)


def gen_solid(spec):
    w, h = spec.get("width", 8), spec.get("height", 8)
    return as_png(Image.new("RGBA", (w, h), tuple(spec.get("fill", (0, 0, 0, 0)))))


def gen_stub(spec):
    """A one pixel Doom patch. Stock texture names resolve to this instead of erroring."""
    return struct.pack("<hhhh", 1, 1, 0, 0) + struct.pack("<i", 16) + b"\x00\x01\x00\x00\x00\xff"


def gen_endoom(spec):
    """The 80x25 text screen: one character byte and one attribute byte per cell."""
    lines = spec.get("lines", [])
    attr = spec.get("attribute", 0x1F)
    cells = []
    for row in range(25):
        text = lines[row] if row < len(lines) else ""
        text = text[:80].ljust(80)
        for ch in text:
            cells.append(ord(ch) if ord(ch) < 256 else ord("?"))
            cells.append(attr)
    return bytes(bytearray(cells))


def gen_text_lump(spec):
    """A plain text lump. The licence travels inside the file, not only in the repo."""
    return ("\n".join(spec.get("lines", [])) + "\n").encode("ascii")


def gen_marker(spec):
    return b""


def gen_map(spec):
    """A single square sector with one player start: enough for the engine to load something."""
    half = spec.get("half", 128)
    floor, ceiling = spec.get("floor", 0), spec.get("ceiling", 128)
    flat = spec.get("flat", "F_SKY1")[:8].ljust(8, "\0").encode("ascii")
    tex = spec.get("texture", "-")[:8].ljust(8, "\0").encode("ascii")

    verts = [(-half, -half), (half, -half), (half, half), (-half, half)]
    things = struct.pack("<hhhhh", 0, 0, 0, 1, 7)
    vertexes = b"".join(struct.pack("<hh", x, y) for x, y in verts)
    linedefs = b"".join(struct.pack("<hhhhhhh", a, b, 1, 0, 0, i, -1)
                        for i, (a, b) in enumerate([(0, 1), (1, 2), (2, 3), (3, 0)]))
    sidedefs = b"".join(struct.pack("<hh8s8s8sh", 0, 0, tex, tex, tex, 0) for _ in range(4))
    sectors = struct.pack("<hh8s8shhh", floor, ceiling, flat, flat, 192, 0, 0)
    return {
        "THINGS": things,
        "LINEDEFS": linedefs,
        "SIDEDEFS": sidedefs,
        "VERTEXES": vertexes,
        "SEGS": b"",
        "SSECTORS": b"",
        "NODES": b"",
        "SECTORS": sectors,
        "REJECT": b"\x00",
        "BLOCKMAP": struct.pack("<hhhh", -half, -half, 1, 1) + struct.pack("<hh", 4, 0),
    }


GENERATORS = {
    "text": gen_text,
    "border": gen_border,
    "blank": gen_blank,
    "sky": gen_sky,
    "solid": gen_solid,
    "stub": gen_stub,
    "endoom": gen_endoom,
    "textlump": gen_text_lump,
    "marker": gen_marker,
}


# ---------------------------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------------------------

def build(manifest_path, out_path=None):
    with io.open(manifest_path, encoding="utf-8") as fh:
        manifest = json.load(fh)

    lumps = []
    seen = set()

    def add(name, data):
        key = name.upper()
        if key in seen:
            raise ValueError("duplicate lump %s" % key)
        seen.add(key)
        lumps.append((key, data))

    # One ordered list, so which namespace a lump lands in is stated rather than inferred.
    for spec in manifest.get("lumps", []):
        if spec.get("gen") == "map":
            add(spec["name"], b"")
            for name, data in gen_map(spec).items():
                add(name, data)
            continue
        gen = GENERATORS.get(spec.get("gen"))
        if gen is None:
            raise ValueError("unknown generator %r for %s" % (spec.get("gen"), spec.get("name")))
        add(spec["name"], gen(spec))

    out = out_path or os.path.join(HERE, manifest["output"])
    size = write_wad(out, lumps)
    return out, len(lumps), size


def main(argv):
    if len(argv) < 2:
        print("usage: mkiwad.py <manifest.json> [output.wad]")
        return 2
    out, count, size = build(argv[1], argv[2] if len(argv) > 2 else None)
    # The digest is printed rather than left to be looked up: it has to be copied into
    # config/iwadallowlist.txt, and PNG bytes can shift with the zlib the build happens to use.
    import hashlib
    digest = hashlib.sha256(io.open(out, "rb").read()).hexdigest()
    print("wrote %s: %d lumps, %d bytes" % (out, count, size))
    print("sha256 %s" % digest)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
