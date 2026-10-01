#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (c) 2026 Cryptnox SA
"""Draw the chain/asset marks as LVGL images: USDC, TRON, Tether, Ethereum, Polygon.

TRON, Tether, Ethereum and Polygon are geometry, not artwork files — simple
enough to draw (a faceted triangle, a crossed stem, a stretched octahedron, a
hexagon ring) that shipping source beats vendoring four PNGs. USDC keeps Circle's
own artwork. Everything is rendered at SS x supersampling and downsampled, so the
16px chips still have clean edges.

Output is LV_IMG_CF_TRUE_COLOR_ALPHA (RGB565 LE + 1 alpha byte). The alpha is
the point: these discs sit on a grey pill AND on the white amount screen, and
the network chips overlap the coin beneath them. USDC used to be generated
flattened onto white (tools/gen_usdc_logo.py, now removed) and showed as a white
square on the pill — hence one generator and one format for all four.

Run from repo root:  python tools/gen_chain_icons.py [--preview]
"""

import math
import sys

from PIL import Image, ImageDraw

USDC_SRC = "assets/circle_usdc_logo.svg.png"

OUT_C = "main/chain_icons.c"
OUT_H = "main/chain_icons.h"
SS    = 8                      # supersampling factor

TRON_RED  = (0xE7, 0x39, 0x2E)
USDT_GRN  = (0x26, 0xA1, 0x7B)
ETH_BLUE  = (0x62, 0x7E, 0xEA)
POLY_PURP = (0x82, 0x47, 0xE5)   # Polygon brand purple
WHITE     = (0xFF, 0xFF, 0xFF)

COIN = 30      # asset mark in a pill's icon slot
CHIP = 16      # network badge on the coin's corner


def _canvas(px):
    img = Image.new("RGBA", (px * SS, px * SS), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img)


def _disc(d, px, colour, ring):
    """Filled circle, optionally inside a white ring (the chip's separator)."""
    n = px * SS
    if ring:
        d.ellipse([0, 0, n - 1, n - 1], fill=WHITE + (255,))
        pad = int(1.6 * SS)
        d.ellipse([pad, pad, n - 1 - pad, n - 1 - pad], fill=colour + (255,))
    else:
        d.ellipse([0, 0, n - 1, n - 1], fill=colour + (255,))


def _fit(pts, k):
    """Scale a mark about the disc centre. Chips carry a ring and only ~12px of
    usable width, so their marks have to be pulled well inside the edge."""
    return [(0.5 + (x - 0.5) * k, 0.5 + (y - 0.5) * k) for x, y in pts]


def _poly(d, px, pts, fill):
    n = px * SS
    d.polygon([(x * n, y * n) for x, y in pts], fill=fill)


def tron(px, ring=False, facets=True, k=1.0):
    """TRON: a triangle split into three facets by lines meeting off-centre."""
    img, d = _canvas(px)
    _disc(d, px, TRON_RED, ring)

    a, b, c = _fit([(0.16, 0.28), (0.86, 0.37), (0.45, 0.84)], k)
    _poly(d, px, [a, b, c], WHITE + (255,))
    if facets:
        # Re-cut the facet seams in the disc colour. Below ~20px these close up
        # into a solid triangle, which is why the chip asks for facets=False.
        n = px * SS
        m = _fit([(0.58, 0.44)], k)[0]                    # interior meet point
        w = max(1, int(0.9 * SS))
        for p in (a, b, c):
            d.line([(m[0] * n, m[1] * n), (p[0] * n, p[1] * n)],
                   fill=TRON_RED + (255,), width=w)
    return img


def usdt(px, ring=False, k=1.0):
    """Tether: the crossed stem of the currency sign, on the brand green."""
    img, d = _canvas(px)
    _disc(d, px, USDT_GRN, ring)
    n = px * SS
    def bar(x0, y0, x1, y1):
        (x0, y0), (x1, y1) = _fit([(x0, y0), (x1, y1)], k)
        d.rectangle([x0 * n, y0 * n, x1 * n, y1 * n], fill=WHITE + (255,))
    bar(0.22, 0.18, 0.78, 0.31)     # head
    bar(0.42, 0.18, 0.58, 0.84)     # stem
    bar(0.29, 0.40, 0.71, 0.51)     # the cross stroke that makes it a currency
    return img


def eth(px, ring=False, k=1.0):
    """Ethereum: the octahedron, upper diamond over the lower pyramid."""
    img, d = _canvas(px)
    _disc(d, px, ETH_BLUE, ring)
    _poly(d, px, _fit([(0.50, 0.10), (0.80, 0.50), (0.50, 0.65), (0.20, 0.50)], k),
          WHITE + (255,))
    _poly(d, px, _fit([(0.22, 0.58), (0.50, 0.72), (0.78, 0.58), (0.50, 0.90)], k),
          WHITE + (255,))
    return img


def _hexagon(r):
    """Pointy-top regular hexagon of radius r about the disc centre."""
    return [(0.5 + r * math.cos(math.radians(a)),
             0.5 + r * math.sin(math.radians(a))) for a in range(30, 360, 60)]


def polygon(px, ring=False, hollow=True, k=1.0):
    """Polygon: a hexagon ring on the brand purple — the shape the name is.

    The chip asks for hollow=False for the same reason TRON's asks for
    facets=False: at 16px inside a ring the cut-out closes up into a blurred
    donut, and a solid hexagon is the shape that survives the downsample."""
    img, d = _canvas(px)
    _disc(d, px, POLY_PURP, ring)
    _poly(d, px, _fit(_hexagon(0.34), k), WHITE + (255,))
    if hollow:
        _poly(d, px, _fit(_hexagon(0.20), k), POLY_PURP + (255,))
    return img


def usdc(px):
    """Circle's own mark, alpha preserved. Already a filled disc, so no ring."""
    img = Image.open(USDC_SRC).convert("RGBA")
    return img.resize((px * SS, px * SS), Image.LANCZOS)


def encode(img, px):
    """RGB565 little-endian + one alpha byte per pixel."""
    img = img.resize((px, px), Image.LANCZOS)
    out = bytearray()
    for y in range(px):
        for x in range(px):
            r, g, b, a = img.getpixel((x, y))
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            out += bytes((v & 0xFF, (v >> 8) & 0xFF, a))
    return bytes(out), img


ICONS = [
    ("icon_usdc", COIN, lambda: usdc(COIN)),
    ("icon_tron", COIN, lambda: tron(COIN)),
    ("icon_usdt", COIN, lambda: usdt(COIN)),
    ("icon_eth",  COIN, lambda: eth(COIN)),
    ("icon_poly", COIN, lambda: polygon(COIN)),
    ("chip_tron", CHIP, lambda: tron(CHIP, ring=True, facets=False, k=0.60)),
    ("chip_eth",  CHIP, lambda: eth(CHIP, ring=True, k=0.62)),
    ("chip_poly", CHIP, lambda: polygon(CHIP, ring=True, hollow=False, k=0.66)),
]

HDR = ("/*\n * SPDX-License-Identifier: LGPL-3.0-or-later\n"
       " * Copyright (c) 2026 Cryptnox SA\n */\n"
       "/* Auto-generated by tools/gen_chain_icons.py - do not edit. */\n\n")

preview = "--preview" in sys.argv
blobs   = []
for name, px, draw in ICONS:
    data, small = encode(draw(), px)
    blobs.append((name, px, data))
    if preview:
        # Composite onto the grey pill colour so the preview shows what the
        # antialiased edges actually land on.
        bg = Image.new("RGBA", (px, px), (0xF2, 0xF2, 0xF2, 255))
        Image.alpha_composite(bg, small).resize(
            (px * 6, px * 6), Image.NEAREST).save("build/preview_%s.png" % name)

with open(OUT_C, "w", newline="\n") as f:
    f.write(HDR)
    f.write('#include "lvgl.h"\n\n')
    for name, px, data in blobs:
        f.write("static const uint8_t %s_map[] = {\n" % name)
        for i in range(0, len(data), 15):
            f.write("    " + ",".join("0x%02X" % b for b in data[i:i + 15]) + ",\n")
        f.write("};\n\n")
        f.write("const lv_img_dsc_t %s = {\n" % name)
        f.write("    .header = {\n"
                "        .cf = LV_IMG_CF_TRUE_COLOR_ALPHA,\n"
                "        .always_zero = 0,\n"
                "        .reserved = 0,\n")
        f.write("        .w = %d,\n        .h = %d,\n    },\n" % (px, px))
        f.write("    .data_size = %d,\n" % len(data))
        f.write("    .data = %s_map,\n};\n\n" % name)

with open(OUT_H, "w", newline="\n") as f:
    f.write(HDR)
    f.write("#ifndef CHAIN_ICONS_H\n#define CHAIN_ICONS_H\n\n")
    f.write('#include "lvgl.h"\n\n')
    for name, _, _ in blobs:
        f.write("extern const lv_img_dsc_t %s;\n" % name)
    f.write("\n#endif /* CHAIN_ICONS_H */\n")

print("Wrote %s: %s" % (OUT_C, ", ".join(
    "%s %dx%d (%d B)" % (n, p, p, len(d)) for n, p, d in blobs)))
