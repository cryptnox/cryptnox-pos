#!/usr/bin/env python3
"""Render the sale screens for colour review.

Photographing the panel does not reproduce its colours — the TFT's own gamma
and the camera's white balance both get in the way. This draws the screens
instead, from the constants in main/ui.cpp, so the fills can be judged at the
size they are actually drawn at.

Two things it does that a photo cannot:

  * Every colour goes through q565() first. The CYD is a 16-bit panel, so it
    cannot show an arbitrary 8-bit hex at all — what reaches the glass is the
    nearest RGB565 value, and that is what these images contain.
  * colour-swatches.png puts the asked-for colour beside the one the panel can
    actually produce, which is where the difference is visible.

Geometry is scaled by K and drawn at that size, rather than drawn small and
enlarged, so edges and text are properly rendered. Flat fills stay exactly the
quantised colour either way.

The coin marks are stand-ins for the real bitmaps and the face is a stand-in
for Montserrat; everything else is the firmware's own numbers.

    python mockups/mockup.py
"""
import glob
import os
from PIL import Image, ImageDraw, ImageFont

OUT = os.path.dirname(os.path.abspath(__file__))
K = 4                                   # render scale


# ---- panel gamut -----------------------------------------------------------
def q565(c):
    """The colour a 16-bit panel actually shows for an 8-bit triple."""
    r, g, b = c
    r5, g6, b5 = r >> 3, g >> 2, b >> 3
    return (r5 << 3 | r5 >> 2, g6 << 2 | g6 >> 4, b5 << 3 | b5 >> 2)


def h(x):
    return ((x >> 16) & 255, (x >> 8) & 255, x & 255)


def Q(x):
    return q565(h(x))


# ---- palette, from main/ui.cpp --------------------------------------------
BG          = Q(0xFFFFFF)   # COL_BG
SURFACE     = Q(0xF2F2F2)   # COL_SURFACE
TEXT        = Q(0x000000)   # COL_TEXT
DIM         = Q(0x9A9A9A)   # COL_DIM
TITLE       = Q(0x424242)   # COL_TITLE
ACCENT      = Q(0x22303D)   # COL_ACCENT       - chrome
BORDER      = Q(0xE0E0E0)   # COL_BORDER       - hairlines
# The page ramp, head into foot: COL_PAGE / COL_PAGE_GRAD as the firmware has
# them, and the grey pair they replaced, kept so the two can be compared at
# size. `python mockups/mockup.py --grey` renders the grey one into
# *-grey.png instead of overwriting the firmware-true files.
RAMP      = (0xE2F5FF, 0xA8D8F0)
RAMP_GREY = (0xE0E0E0, 0xC4C4C4)
ACTION      = Q(0x101F2E)   # COL_ACTION       - charcoal blue: Charge, Confirm
ACTION_SOFT = BG            # Cancel: the card's own white, hairline only
USDC        = Q(0x2775CA)   # Circle brand blue, stand-in mark
ETH         = Q(0x627EEA)   # COL_ETH, the network chip

# ---- geometry, from main/ui.cpp, at render scale --------------------------
SCR_W, SCR_H = 240 * K, 320 * K
CARD_X, CARD_Y = 8 * K, 28 * K
CARD_W, CARD_H, CARD_PAD = 224 * K, 262 * K, 10 * K
CARD_R = 14 * K
CARD_BTN_H = 44 * K
CARD_BTN_W = CARD_W - 2 * CARD_PAD
CARD_BTN_TOP = CARD_H - CARD_BTN_H - 10 * K      # CARD_BTN_Y = -10
BTN_R = 6 * K
CLOCK_X, CLOCK_Y, CLOCK_W = 10 * K, 4 * K, 42 * K
HAIRLINE = 1 * K


def font(px):
    for name in ("Montserrat-Regular", "arial"):
        for p in glob.glob(r"C:\Windows\Fonts\%s*.ttf" % name):
            try:
                return ImageFont.truetype(p, px)
            except OSError:
                pass
    return ImageFont.load_default()


F14, F20, F28 = font(14 * K), font(20 * K), font(28 * K)


def text(d, xy, s, fill, f, anchor="la"):
    d.text(xy, s, fill=fill, font=f, anchor=anchor)


def backspace(d, cx, cy, colour):
    """The outline backspace the firmware draws in amount_kbd_draw_cb()."""
    w, hh, nose, cross, lw = 16 * K, 9 * K, 6 * K, 4 * K, 2 * K
    d.line([(cx - w, cy), (cx - w + nose, cy - hh), (cx + w, cy - hh),
            (cx + w, cy + hh), (cx - w + nose, cy + hh), (cx - w, cy)],
           fill=colour, width=lw, joint="curve")
    kx = cx + nose // 2
    d.line([(kx - cross, cy - cross), (kx + cross, cy + cross)],
           fill=colour, width=lw)
    d.line([(kx + cross, cy - cross), (kx - cross, cy + cross)],
           fill=colour, width=lw)


# ---- shared chrome ---------------------------------------------------------
def page():
    """Page ground, clock, Wi-Fi mark, card, home indicator."""
    img = Image.new("RGB", (SCR_W, SCR_H), Q(RAMP[0]))
    d = ImageDraw.Draw(img)

    # The vertical ramp LVGL fills the screen with. Quantised per row, since
    # that is what the panel does to it — the banding in the PNG is the banding
    # on the glass.
    for y in range(SCR_H):
        t = y / (SCR_H - 1)
        d.line([(0, y), (SCR_W, y)],
               fill=q565(tuple(round(a + (b - a) * t)
                               for a, b in zip(h(RAMP[0]), h(RAMP[1])))))

    text(d, (CLOCK_X, CLOCK_Y), "09:41", TITLE, F14)

    cx, cy = SCR_W - 14 * K - 11 * K, 6 * K + 11 * K
    for i, sweep in enumerate((140, 110, 100)):
        r = 4 * K + i * 3 * K
        d.arc([cx - r, cy - r, cx + r, cy + r], start=270 - sweep // 2,
              end=270 + sweep // 2, fill=TEXT, width=2 * K)
    dot = 2 * K
    d.ellipse([cx - dot, cy - dot, cx + dot, cy + dot], fill=TEXT)

    d.rounded_rectangle([CARD_X, CARD_Y, CARD_X + CARD_W, CARD_Y + CARD_H],
                        radius=CARD_R, fill=BG)
    d.rounded_rectangle([(SCR_W - 84 * K) // 2, SCR_H - 14 * K,
                         (SCR_W + 84 * K) // 2, SCR_H - 9 * K],
                        radius=3 * K, fill=DIM)
    return img, d


def badge(d, cx, cy, chip=True):
    """Stand-in for make_asset_badge: coin mark, network chip on its corner."""
    r = 15 * K
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=USDC)
    text(d, (cx, cy + K), "$", BG, F20, anchor="mm")
    if chip:
        ccx, ccy, cr = cx + 12 * K, cy + 12 * K, 8 * K
        d.ellipse([ccx - cr, ccy - cr, ccx + cr, ccy + cr], fill=BG)
        d.ellipse([ccx - cr + 2 * K, ccy - cr + 2 * K,
                   ccx + cr - 2 * K, ccy + cr - 2 * K], fill=ETH)


# ---- screen one: amount ----------------------------------------------------
def amount():
    img, d = page()
    ox, oy = CARD_X, CARD_Y

    pw, ph = 96 * K, 42 * K
    px, py = ox + CARD_W - 8 * K - pw, oy + 10 * K
    d.rounded_rectangle([px, py, px + pw, py + ph], radius=ph // 2, fill=SURFACE)
    badge(d, px + 26 * K, py + ph // 2)
    text(d, (px + 48 * K, py + ph // 2), "USDC", TEXT, F14, anchor="lm")
    text(d, (ox + 70 * K, py + ph // 2), "12.50", TEXT, F28, anchor="mm")

    keys = [("1", "2", "3"), ("4", "5", "6"), ("7", "8", "9"),
            ("00", "0", None)]
    kx, ky = ox + CARD_PAD, oy + 58 * K
    kw, kh = CARD_BTN_W, 148 * K
    for r, row in enumerate(keys):
        for c, k in enumerate(row):
            cxx = kx + (c * kw) // 3 + kw // 6
            cyy = ky + (r * kh) // 4 + kh // 8
            if k is None:
                backspace(d, cxx, cyy, TEXT)
            else:
                text(d, (cxx, cyy), k, TEXT, F28, anchor="mm")

    by = oy + CARD_BTN_TOP
    d.rounded_rectangle([ox + CARD_PAD, by, ox + CARD_PAD + CARD_BTN_W,
                         by + CARD_BTN_H], radius=BTN_R, fill=ACTION)
    text(d, (ox + CARD_W // 2, by + CARD_BTN_H // 2), "Charge", BG, F20,
         anchor="mm")
    return img


# ---- screen two: review ----------------------------------------------------
def confirm():
    img, d = page()
    ox, oy = CARD_X, CARD_Y

    text(d, (ox + CARD_PAD, oy + 8 * K), "Total", DIM, F14)
    text(d, (ox + CARD_PAD, oy + 24 * K), "12.50", TEXT, F28)
    badge(d, ox + CARD_PAD + 96 * K, oy + 41 * K)
    text(d, (ox + CARD_PAD + 120 * K, oy + 41 * K), "USDC", TEXT, F14,
         anchor="lm")

    text(d, (ox + CARD_PAD, oy + 64 * K), "To", DIM, F14)
    text(d, (ox + CARD_PAD, oy + 82 * K), "0x71C7656EC7ab88b098", TEXT, F14)
    text(d, (ox + CARD_PAD, oy + 98 * K), "defa72249c63b8f0f61", TEXT, F14)

    text(d, (ox + CARD_PAD, oy + 126 * K), "ERC-20 token contract", DIM, F14)
    text(d, (ox + CARD_PAD, oy + 144 * K), "0xA0b86991c6218b36c1", TEXT, F14)
    text(d, (ox + CARD_PAD, oy + 160 * K), "d19D4a2e9Eb0cE3606eB", TEXT, F14)

    half = (CARD_W - 2 * CARD_PAD - 8 * K) // 2
    by = oy + CARD_BTN_TOP
    lx = ox + CARD_PAD
    d.rounded_rectangle([lx, by, lx + half, by + CARD_BTN_H], radius=BTN_R,
                        fill=ACTION_SOFT, outline=BORDER, width=HAIRLINE)
    text(d, (lx + half // 2, by + CARD_BTN_H // 2), "Cancel", TEXT, F20,
         anchor="mm")
    rx = ox + CARD_W - CARD_PAD - half
    d.rounded_rectangle([rx, by, rx + half, by + CARD_BTN_H], radius=BTN_R,
                        fill=ACTION)
    text(d, (rx + half // 2, by + CARD_BTN_H // 2), "Confirm", BG, F20,
         anchor="mm")
    return img


# ---- asked for, versus what the panel can show -----------------------------
def swatches():
    rows = [("Charge / Confirm", 0x101F2E), ("Cancel", 0xFFFFFF),
            ("Page ramp, head", RAMP[0]), ("Page ramp, foot", RAMP[1]),
            ("Chrome", 0x22303D), ("Keypad ink", 0x000000)]
    pad, sw, shh, gap = 16 * K, 150 * K, 60 * K, 30 * K
    img = Image.new("RGB", (pad * 2 + sw * 2 + gap, pad * 2 + 24 * K
                            + len(rows) * (shh + 26 * K)), (255, 255, 255))
    d = ImageDraw.Draw(img)
    text(d, (pad, pad), "asked for", TITLE, F14)
    text(d, (pad + sw + gap, pad), "what the panel shows (RGB565)", TITLE, F14)
    for i, (name, hexv) in enumerate(rows):
        y = pad + 24 * K + i * (shh + 26 * K)
        want, got = h(hexv), q565(h(hexv))
        d.rectangle([pad, y, pad + sw, y + shh], fill=want, outline=BORDER,
                    width=HAIRLINE)
        d.rectangle([pad + sw + gap, y, pad + sw * 2 + gap, y + shh], fill=got,
                    outline=BORDER, width=HAIRLINE)
        text(d, (pad, y + shh + 4 * K), "#%06X  %s" % (hexv, name), TEXT, F14)
        text(d, (pad + sw + gap, y + shh + 4 * K), "#%02X%02X%02X" % got, TEXT,
             F14)
    return img


if __name__ == "__main__":
    import sys
    grey = "--grey" in sys.argv[1:]
    if grey:
        RAMP = RAMP_GREY
    suffix = "-grey" if grey else ""
    for name, im in (("screen-1-amount" + suffix, amount()),
                     ("screen-2-review" + suffix, confirm()),
                     ("colour-swatches" + suffix, swatches())):
        p = os.path.join(OUT, name + ".png")
        im.save(p)
        print("wrote %-22s %s" % (name + ".png", im.size))
    for label, v in (("Charge/Confirm", 0x101F2E), ("Page ramp foot", RAMP[1])):
        print("%-15s asked #%06X   panel #%02X%02X%02X"
              % (label, v, *q565(h(v))))
