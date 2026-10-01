#!/usr/bin/env python3
"""Extract the config portal's page from portal_page.h and check it parses.

The page is a pair of C string literals compiled into the firmware, so the
compiler proves the C is valid and nothing proves the HTML or the JavaScript is.
A typo in there is a wizard that cannot be completed, on a device with no console
to read — which is a worse failure than anything the C compiler catches.

So: pull the literals out, un-escape them, and hand the <script> body to
`node --check`. Also asserts every id the script reaches for exists in the markup,
which is the other half of the same class of bug.

    python tools/check_portal_page.py

Exit status 0 and "portal page OK", or a diagnosis. No dependencies beyond node.
"""

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "main" / "portal_page.h"


def literal_after(text, name):
    """Concatenate the adjacent C string literals of `static const char *const name`.

    Comments are stripped first: a /* ... */ between two literals is legal C and
    portal_page.h uses them to annotate the script, so a naive scan for quotes
    would pick up any quote inside the prose.
    """
    start = text.index(name)
    # The terminating semicolon, found by walking rather than by index(";"): the
    # JavaScript in these literals is full of semicolons, and every one of them
    # would end the declaration early.
    body = re.sub(r"/\*.*?\*/", "", text[start:], flags=re.S)
    i, in_str, end = 0, False, None
    while i < len(body):
        c = body[i]
        if in_str:
            if c == "\\":
                i += 1
            elif c == '"':
                in_str = False
        elif c == '"':
            in_str = True
        elif c == ";":
            end = i
            break
        i += 1
    if end is None:
        raise SystemExit(f"no terminating ; for {name}")
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', body[:end])
    if not parts:
        raise SystemExit(f"no string literals found for {name}")
    joined = "".join(parts)
    # Only quoted literals are collected, so a macro spliced between two of them
    # (OTA_MANIFEST_URL) comes out as an empty string. That is fine for a syntax
    # check — it still leaves a valid string literal where the URL would be — but
    # do not read the extracted script as what the device actually serves.
    #
    # The escapes portal_page.h actually uses. \\uXXXX is a JS escape that has to
    # survive into the output, so it is left alone.
    return joined.replace('\\"', '"').replace("\\n", "\n").replace("\\\\", "\\")


def main():
    if shutil.which("node") is None:
        raise SystemExit("node not found - cannot check the page's JavaScript")

    text = SRC.read_text(encoding="utf-8")
    page = literal_after(text, "PAGE_HTML") + literal_after(text, "PAGE_JS")

    script = re.search(r"<script>(.*)</script>", page, re.S)
    if script is None:
        raise SystemExit("no <script> block in the page")

    with tempfile.TemporaryDirectory() as d:
        js = Path(d) / "page.js"
        js.write_text(script.group(1), encoding="utf-8")
        r = subprocess.run(["node", "--check", str(js)],
                           capture_output=True, text=True)
        if r.returncode != 0:
            sys.stderr.write(r.stderr)
            raise SystemExit("the page's JavaScript does not parse")

    # Every $('x') must name an element the markup defines, or the page throws at
    # the first render and shows nothing at all.
    js = script.group(1)
    wanted = set(re.findall(r"\$\('([^']+)'\)", js))
    have = set(re.findall(r"\bid=([A-Za-z_][\w-]*)", page))
    missing = sorted(wanted - have)
    if missing:
        raise SystemExit(f"script reaches for ids the markup does not define: {missing}")

    # And the reverse: an id nothing mentions is usually a section renamed on one
    # side only. Matched against any quoted string in the script, not just $('x'),
    # because show() and propose() are handed ids as arguments.
    named = set(re.findall(r"'([A-Za-z_][\w-]*)'", js))
    # An id the markup references itself — <use href=#cnx> for the Cryptnox mark,
    # drawn in the header and again on the card illustration — is wired even
    # though no JavaScript ever names it.
    named |= set(re.findall(r"href=['\"]?#([A-Za-z_][\w-]*)", page))
    unused = sorted(have - named)
    if unused:
        raise SystemExit(f"markup defines ids the script never names: {unused}")

    # --emit-js writes the extracted script and the id list for the render test
    # (tools/test_portal_render.js) to consume, so there is one extractor and not
    # two that could disagree about what the firmware actually serves.
    if "--emit-js" in sys.argv:
        out = Path(sys.argv[sys.argv.index("--emit-js") + 1])
        out.write_text(js, encoding="utf-8")
        out.with_suffix(".ids").write_text("\n".join(sorted(have)), encoding="utf-8")

    print(f"portal page OK ({len(page)} bytes, {len(have)} ids, all wired)")


if __name__ == "__main__":
    main()
