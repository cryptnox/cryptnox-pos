#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (c) 2026 Cryptnox SA
"""Convert the TTFs in assets/fonts/ into the LVGL fonts in main/fonts/.

Plus Jakarta Sans is the primary face (titles, buttons, figures), Inter the
secondary (small text). Both are static instances under the SIL Open Font
License 1.1: github.com/tokotype/PlusJakartaSans at commit 18d1cd2, and
github.com/rsms/inter release v4.1 — the licences sit next to the TTFs.

Each text font carries the same glyphs LVGL's built-in Montserrat fonts do:
ASCII, degree sign, bullet (the textarea password character) and the full
LV_SYMBOL_* set merged in from LVGL's own FontAwesome file, so every
LV_SYMBOL_* the UI or a stock widget draws keeps rendering. icons_48 is only
the three status marks — nothing draws text at that size.

Needs node (npx fetches lv_font_conv). Run from the repo root:
    python tools/gen_fonts.py
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TTF = Path("assets/fonts")   # relative: the paths land in each .c header
FA = Path("managed_components/lvgl__lvgl/scripts/built_in_font/"
          "FontAwesome5-Solid+Brands+Regular.woff")
OUT = Path("main/fonts")

TEXT = "0x20-0x7F,0xB0,0x2022"
# Copied from lvgl/scripts/built_in_font/built_in_font_gen.py (8.4).
SYMBOLS = ("61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,"
           "61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,"
           "61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,"
           "61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,"
           "61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,"
           "62099,62212,62189,62810,63426,63650")
STATUS = "61452,61453,61553"   # LV_SYMBOL_OK, _CLOSE, _WARNING

# name, size, face (None = symbols only)
FONTS = [
    ("font_inter_14",        14, "Inter-Regular"),
    ("font_inter_14_medium", 14, "Inter-Medium"),
    ("font_pjs_20_medium",   20, "PlusJakartaSans-Medium"),
    ("font_pjs_20_semibold", 20, "PlusJakartaSans-SemiBold"),
    ("font_pjs_28_semibold", 28, "PlusJakartaSans-SemiBold"),
    ("font_pjs_28_light",    28, "PlusJakartaSans-Light"),
    ("font_icons_48",        48, None),
]


def main() -> int:
    (ROOT / OUT).mkdir(exist_ok=True)
    for name, size, face in FONTS:
        src = []
        if face is not None:
            src += ["--font", (TTF / f"{face}.ttf").as_posix(), "-r", TEXT]
        src += ["--font", FA.as_posix(), "-r", SYMBOLS if face else STATUS]
        cmd = ["npx", "-y", "lv_font_conv@1.5.2", "--no-compress", "--no-prefilter",
               "--bpp", "4", "--size", str(size), "--format", "lvgl",
               "--force-fast-kern-format", "--lv-include", "lvgl.h",
               "-o", (OUT / f"{name}.c").as_posix()] + src
        subprocess.run(cmd, check=True, cwd=ROOT,
                       shell=(sys.platform == "win32"))
        print(f"{name}.c")
    return 0


if __name__ == "__main__":
    sys.exit(main())
