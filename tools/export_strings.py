#!/usr/bin/env python3
"""Export the firmware's user-facing strings to strings.xlsx for review/translation.

Columns: "file:line" | string | (empty, for the reviewer's comments)

Scans main/*.cpp and main/*.h. Adjacent C literals are concatenated the way the
compiler does, so a sentence split over source lines exports as one sentence,
credited to the line it starts on. HTML/CSS/JS blobs (the config page) are
reduced to their text nodes and the handful of strings the page's JS shows.

ponytail: heuristic filters, not a parser. Log lines, NVS/JSON keys, URLs and
format-only fragments are dropped -- see SKIP. Anything misjudged shows up as a
missing or junk row in the sheet; adjust the rules here rather than hand-editing
the output, which is regenerated.
"""
import html
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = sorted(ROOT.glob("main/*.cpp")) + sorted(ROOT.glob("main/*.h"))

# A C string literal, escapes included. Comments are stripped before this runs.
LIT = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
def strip_comments(text):
    """Blank out // and /* */ comments, preserving line count and offsets."""
    out = []
    i, n = 0, len(text)
    while i < n:
        two = text[i:i + 2]
        if two == '//':
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif two == '/*':
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r'[^\n]', ' ', text[i:j]))
            i = j
        elif two[0] == '"':
            m = LIT.match(text, i)
            if m:
                out.append(m.group(0))
                i = m.end()
            else:
                out.append('"')
                i += 1
        else:
            out.append(text[i])
            i += 1
    return ''.join(out)


def squeeze(s):
    """Collapse the whitespace the tag-blanking leaves behind, decode entities."""
    s = html.unescape(re.sub(r'\s+', ' ', s)).strip()
    # `"<A href='" URL "'>Cryptnox setup"`: the tag opened in a different literal,
    # so its tail rides along on the text. Shed it.
    return re.sub(r'^[\'"]?>\s*', '', s)


def unescape(s):
    return (s.replace('\\n', '\n').replace('\\t', '\t').replace('\\"', '"')
             .replace("\\'", "'").replace('\\\\', '\\'))


def runs(text):
    """Yield (line, joined_string, [(offset_in_joined, line)]) per concatenation."""
    for m in re.finditer(r'"(?:[^"\\\n]|\\.)*"(?:\s*"(?:[^"\\\n]|\\.)*")*', text):
        why = None
        ctx = NOT_UI.search(stmt_prefix(text, m.start()))
        if ctx:
            why = 'context: ' + ctx.group(0).strip()
        # `{ "gas price too low", "Fee too low - raise it..." }`: the first half of
        # a table pair, lowercase, is a string the node sent us to match on -- wire
        # protocol, not prose. Translating it would break the error mapping.
        elif (stmt_prefix(text, m.start()).strip() == ''
                and text[m.end():m.end() + 40].lstrip().startswith(',')
                and re.match(r'"[a-z]', m.group(0))):
            why = 'wire needle'
        marks, buf = [], ''
        for lit in LIT.finditer(m.group(0)):
            line = text.count('\n', 0, m.start() + lit.start()) + 1
            piece = unescape(lit.group(1))
            marks.append((len(buf), line))
            buf += piece
        if buf:
            yield text.count('\n', 0, m.start()) + 1, buf, marks, why


def line_of(marks, offset):
    line = marks[0][1]
    for start, ln in marks:
        if start <= offset:
            line = ln
        else:
            break
    return line


# Where a literal sits tells us more than how it looks: a key, a header name,
# a log line and an include are all plain English at a glance.
# set_header, not _header: build_header("Send") is a screen title.
NOT_UI = re.compile(r'ESP_LOG|cJSON_GetObject|set_header|_hdr|strcasecmp|strcmp|'
                    r'rpc_err_contains|#\s*include|dual_get|dual_set|nvs_|'
                    r'static_assert|pos_handle_anomaly|'
                    r'#\s*define\s+\w*(?:VERSION|TAG)\b')


def stmt_prefix(text, start):
    """The enclosing statement up to `start` -- what this literal is an argument to."""
    cut = max(text.rfind(c, 0, start) for c in ';{}')
    return text[cut + 1:start]


SKIP = (
    ('no letters',      re.compile(r'^[^A-Za-z]*$')),
    ('identifier/key',  re.compile(r'^[a-z0-9_.:/()\-]+$')),
    ('macro constant',  re.compile(r'^[A-Z0-9_]+$')),
    ('url',             re.compile(r'^https?://')),
    ('format only',     re.compile(r'^%[-0-9.]*[a-zA-Z]')),
    ('punct/specifier', re.compile(r'^[\s%sdxfu0-9.\-+*/()]*$')),
    ('mime type',       re.compile(r'^(application|text)/')),
    ('hex blob',        re.compile(r'^[0-9a-fA-F]{8,}$')),
    ('http status',     re.compile(r'^\d{3}\s')),
    # A stray '<' or an attribute is a tag the concatenation cut in half. A bare
    # '>' is not: "Fee too low - raise it on Settings > Tx" is prose.
    ('markup fragment', re.compile(r'.*(?:<|=[\'"])', re.S)),
)


def reject(s):
    """Why this string is not user-facing text, or None to keep it."""
    s = s.strip()
    if len(s) < 2:
        return 'too short'
    for why, pat in SKIP:
        if pat.match(s):
            return why
    # One long unbroken token is an address, a base64 blob or an identifier --
    # never a sentence. Real UI text this long has a space in it.
    if len(s) >= 16 and not re.search(r'\s', s):
        return 'long single token'
    # Needs two consecutive letters somewhere -- filters "0x%02X" style leftovers.
    if not re.search(r'[A-Za-z]{2}', s):
        return 'no word'
    return None


def keep(s):
    return reject(s) is None


TAGS = re.compile(r'<[^>]*>')
# Tags that sit inside a sentence: blanking them keeps the sentence whole, where
# a block tag legitimately ends one.
INLINE = re.compile(r'</?(?:b|i|em|strong|span|code|small|u|a|kbd|abbr|sub|sup)'
                    r'(?:\s[^>]*)?>', re.I)


def blank(m):
    return ' ' * len(m.group(0))


def html_text(blob, marks):
    """Text nodes of an HTML blob, each with the source line it starts on.

    Substitutions keep the original length so offsets still map back to lines.
    """
    blob = re.sub(r'<style\b.*?</style>', blank, blob, flags=re.S)
    blob = re.sub(r'<svg\b.*?</svg>', blank, blob, flags=re.S)
    blob = INLINE.sub(blank, blob)
    blob = TAGS.sub(lambda m: '\n' * len(m.group(0)), blob)
    for m in re.finditer(r'[^\n]+', blob):
        txt = squeeze(m.group(0))
        yield line_of(marks, m.start()), txt, reject(txt)


# The same idea as NOT_UI, for JS: what the string is handed to gives it away.
JS_NOT_UI = re.compile(r'setRequestHeader|getElementById|querySelector|'
                       r'setAttribute|getAttribute|addEventListener|classList|'
                       r'localStorage|\.open\(')


def js_text(blob, marks):
    """Strings the config page's JS puts on screen (single- or double-quoted)."""
    for m in re.finditer(r"'((?:[^'\\\n]|\\.)*)'", blob):
        txt = squeeze(TAGS.sub(' ', unescape(m.group(1))))
        before = blob[:m.start()].rstrip()[-1:]
        after = blob[m.end():].lstrip()[:1]
        if JS_NOT_UI.search(blob[max(0, m.start() - 60):m.start()]):
            why = 'context: js api'
        # `{'X-Prov-Token':T}` is a key; `x?'yes':'no'` is prose. Both are
        # followed by ':' -- what comes before separates them.
        elif after == ':' and before in '{,':
            why = 'js object key'
        elif before == '[' and after == ']':
            why = 'js property'
        elif re.match(r'^[#.\[]', txt):
            why = 'css selector'
        else:
            why = reject(txt)
        yield line_of(marks, m.start()), txt, why


def scan_all(source):
    """(line, string, reason) for every literal -- reason None means it is UI text."""
    text = strip_comments(source)
    for start, blob, marks, why in runs(text):
        if why is not None:                               # dropped on context
            yield start, blob.strip(), why
        elif blob.lstrip().startswith('<script'):         # the config page's JS
            yield from js_text(blob, marks)
        elif '<' in blob and '>' in blob:                 # the config page's HTML
            yield from html_text(blob, marks)
        else:
            yield start, blob.strip(), reject(blob)


def scan(source):
    """(line, string) for every user-facing string in one file's source."""
    for line, txt, why in scan_all(source):
        if why is None:
            yield line, txt


SAMPLE = r'''
static const char *TAG = "x";                 /* "not this" */
void f(void) {
    ESP_LOGI(TAG, "joined %s", ssid);
    ui_msg("Hold card to reader");
    ui_msg("A sentence split "
           "over two lines.");
    settings_get("wifi_ssid", out, n);
    static const struct { const char *needle; const char *say; } MAP[] = {
        { "nonce too low", "Already submitted" },
    };
}
static const char *const PAGE =
"<p>Tap the <b>merchant's</b> card, not a customer's.</p>"
"<h2>Wi-Fi</h2>";
static const char *const JS = "<script>o.textContent='No networks found';</script>";
'''


def selftest():
    got = dict((t, ln) for ln, t in scan(SAMPLE))
    assert 'Hold card to reader' in got, got
    assert 'A sentence split over two lines.' in got or \
           'A sentence split \nover two lines.' in got, got
    assert "Tap the merchant's card, not a customer's." in got, got
    assert 'Wi-Fi' in got and 'No networks found' in got, got
    assert 'Already submitted' in got, got                 # the message is kept
    for junk in ('x', 'not this', 'joined %s', 'wifi_ssid', 'nonce too low'):
        assert junk not in got, (junk, got)
    # A split sentence is credited to the line it starts on, not the line it ends.
    assert got['Hold card to reader'] == 5, got
    print('selftest ok')


def audit():
    """Print every literal that was NOT exported, and the rule that dropped it.

    The rows in the sheet you can check by eye; a string the filters ate is
    invisible there. This is the other half -- read it and look for prose.
    """
    from collections import Counter
    tally = Counter()
    for path in SRC:
        shown = False
        for line, txt, why in scan_all(path.read_text(encoding='utf-8',
                                                      errors='replace')):
            if why is None:
                continue
            tally[why] += 1
            if '--all' not in sys.argv and why in ('no letters', 'too short',
                                                   'no word', 'identifier/key',
                                                   'macro constant'):
                continue          # never prose; hidden unless you ask for --all
            if not shown:
                print(f'\n--- {path.name}')
                shown = True
            one = ' '.join(txt.split())
            print(f'  {line:>5}  [{why}] {one[:100]}')
    print('\ndropped by rule:')
    for why, n in tally.most_common():
        print(f'  {n:>5}  {why}')


def main():
    if '--selftest' in sys.argv:
        return selftest()
    if '--audit' in sys.argv:
        return audit()
    # Only the sheet needs it: --selftest and --audit run without openpyxl.
    import openpyxl
    import openpyxl.styles

    shown, hidden = [], []
    for path in SRC:
        source = path.read_text(encoding='utf-8', errors='replace')
        for line, txt, why in scan_all(source):
            (hidden if why else shown).append((path.name, line, txt, why))

    def dedup(rows):
        seen, out = set(), []
        for name, line, txt, why in sorted(rows, key=lambda r: (r[0], r[1])):
            key = (name, line, txt)
            if key not in seen:
                seen.add(key)
                out.append((f'{name}:{line}', txt, why))
        return out

    shown, hidden = dedup(shown), dedup(hidden)

    def sheet(ws, title, header, rows, third):
        ws.title = title
        ws.append(header)
        for cell in ws[1]:
            cell.font = openpyxl.styles.Font(bold=True)
        for loc, txt, why in rows:
            ws.append([loc, txt, third(why)])
        ws.column_dimensions['A'].width = 22
        ws.column_dimensions['B'].width = 90
        ws.column_dimensions['C'].width = 40
        ws.freeze_panes = 'A2'
        wrap = openpyxl.styles.Alignment(wrap_text=True, vertical='top')
        for row in ws.iter_rows(min_row=2, min_col=2, max_col=3):
            for cell in row:
                cell.alignment = wrap

    wb = openpyxl.Workbook()
    sheet(wb.active, 'strings', ['file:line', 'string', 'comment'],
          shown, lambda why: '')
    # The reviewer cannot challenge a filter they cannot see. Second sheet, so
    # the review list stays exactly the three columns that were asked for and
    # nothing is hidden by a heuristic in this file.
    sheet(wb.create_sheet(), 'excluded',
          ['file:line', 'string', 'why it was excluded'],
          hidden, lambda why: why or '')

    dest = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'strings.xlsx'
    wb.save(dest)
    print(f'{len(shown)} strings + {len(hidden)} excluded -> {dest}')


if __name__ == '__main__':
    main()
