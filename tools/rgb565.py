#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (c) 2026 Cryptnox SA
"""Shared RGB565 packing for the LVGL image generators.

The CYD panel is 16-bit (RGB565). Naively truncating 8-bit channels to 5-6-5
bits produces visible banding on smooth gradients and anti-aliased edges. This
packer rounds to the nearest representable colour and applies Floyd-Steinberg
error diffusion, trading banding for fine noise the eye reads as a smooth
gradient.

Output bytes are little-endian (matches lv_color_t with LV_COLOR_16_SWAP=0).
"""


def _clamp8(v):
    return 0.0 if v < 0.0 else (255.0 if v > 255.0 else v)


def rgb565_le_dithered(img):
    """Floyd-Steinberg dither an RGB PIL image to little-endian RGB565 bytes."""
    img = img.convert("RGB")
    w, h = img.size
    src = img.load()
    # Mutable float working buffer; carries diffused error across the image.
    buf = [[list(src[x, y]) for x in range(w)] for y in range(h)]

    data = bytearray()
    for y in range(h):
        for x in range(w):
            r = _clamp8(buf[y][x][0])
            g = _clamp8(buf[y][x][1])
            b = _clamp8(buf[y][x][2])

            r5 = int(r) >> 3
            g6 = int(g) >> 2
            b5 = int(b) >> 3

            # Reconstruct the 8-bit value the panel will actually display, so
            # the diffused error matches what the eye sees (not the raw drop).
            rr = (r5 << 3) | (r5 >> 2)
            gg = (g6 << 2) | (g6 >> 4)
            bb = (b5 << 3) | (b5 >> 2)

            er, eg, eb = r - rr, g - gg, b - bb
            for dx, dy, wgt in ((1, 0, 7), (-1, 1, 3), (0, 1, 5), (1, 1, 1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h:
                    buf[ny][nx][0] += er * wgt / 16.0
                    buf[ny][nx][1] += eg * wgt / 16.0
                    buf[ny][nx][2] += eb * wgt / 16.0

            val = (r5 << 11) | (g6 << 5) | b5
            data.append(val & 0xFF)
            data.append((val >> 8) & 0xFF)
    return data
