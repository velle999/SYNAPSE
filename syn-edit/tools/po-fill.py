#!/usr/bin/env python3
"""
po-fill.py — fill a .po from `msgid<TAB>msgstr` lines.

    tools/po-fill.py de < de.tsv                 # po/de.po
    tools/po-fill.py de --dir=po-bar < de.tsv    # po-bar/de.po

⚠ MATCHED ON THE msgid, NEVER ON POSITION. A translation list paired by line
number slipped once already, in syn-install's Italian catalog: sixteen labels
landed on their neighbours, every count still matched, and nothing complained.
A msgid that names no string in the .po is refused and the file is not written.

`\\n` in either column is a newline. An empty msgstr leaves the entry alone, so
a partial pass is safe and repeatable.

⚠ A PLURAL ENTRY TAKES ONE COLUMN PER FORM, tab-separated, in the order the
catalog's own Plural-Forms header defines — two for German, three for Polish
and Russian, one for Japanese, six for Arabic. Give it the wrong number and the
file is refused; give a plural entry a single column and it is refused too.
That check exists because the substitution below matches `msgstr ` and a plural
block has `msgstr[0]`: without it, every plural line would be accepted, written
nowhere, and reported as filled.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
"""
import pathlib
import re
import sys


def parse_po(text):
    """[(kind, payload)] where kind is 'entry' (msgid, [lines]) or 'raw'."""
    return re.split(r'\n\n', text)


def po_escape(s):
    return (s.replace('\\', '\\\\').replace('"', '\\"')
             .replace('\n', '\\n').replace('\t', '\\t'))


def po_unescape(s):
    return (s.replace('\\n', '\n').replace('\\t', '\t')
             .replace('\\"', '"').replace('\\\\', '\\'))


def msgid_of(block):
    """The msgid of a .po block, joined across its continuation lines."""
    m = re.search(r'^msgid ((?:"(?:[^"\\]|\\.)*"\s*)+)', block, re.M)
    if not m:
        return None
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
    return po_unescape("".join(parts))


def msgstr_of(block):
    """The msgstr of a .po block, joined across its continuation lines.

    Answers for a plural block too, where the strings are msgstr[0], msgstr[1]
    and so on: any form with content counts as translated.
    """
    out = []
    for m in re.finditer(r'^msgstr(?:\[\d+\])? ((?:"(?:[^"\\]|\\.)*"\s*)+)',
                         block, re.M):
        out.append(po_unescape("".join(
            re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1)))))
    return "".join(out)


def is_plural(block):
    return re.search(r'^msgid_plural ', block, re.M) is not None


def nplurals_of(text):
    """How many forms this catalog declares. 2 if it declares nothing."""
    m = re.search(r'Plural-Forms:[^"\\]*nplurals\s*=\s*(\d+)', text)
    return int(m.group(1)) if m else 2


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    # ⚠ A DIRECTORY, BECAUSE THERE ARE TWO CATALOG SETS NOW. po/ is the
    # compositor's, po-bar/ is the bar's — different domains and different last
    # miles (.mo read by libintl vs JSON read by a FileView), but identical .po
    # files, so this tool serves both. A second copy of it would drift, and the
    # thing it protects against is a translation landing on its neighbour.
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    opts = [a for a in sys.argv[1:] if a.startswith("--")]
    code = args[0]
    subdir = "po"
    for o in opts:
        if o.startswith("--dir="):
            subdir = o.split("=", 1)[1]
    root = pathlib.Path(__file__).resolve().parent.parent
    po = root / subdir / ("%s.po" % code)
    text = po.read_text(encoding="utf-8")
    blocks = parse_po(text)
    nplurals = nplurals_of(text)

    index = {}
    for i, b in enumerate(blocks):
        mid = msgid_of(b)
        if mid:
            index[mid] = i

    want, problems = {}, []
    for lineno, raw in enumerate(sys.stdin.read().split("\n"), 1):
        if not raw.strip() or "\t" not in raw:
            continue
        mid, msgstr = raw.split("\t", 1)
        mid = mid.replace("\\n", "\n")
        if mid not in index:
            problems.append("line %d: no such msgid %r" % (lineno, mid[:60]))
            continue
        if not msgstr.strip():
            continue
        if is_plural(blocks[index[mid]]):
            forms = [f.replace("\\n", "\n") for f in msgstr.split("\t")]
            if len(forms) != nplurals:
                problems.append(
                    "line %d: %r is a plural entry — %s declares nplurals=%d, "
                    "got %d column(s)"
                    % (lineno, mid[:40], po.name, nplurals, len(forms)))
                continue
            want[mid] = forms
        else:
            want[mid] = msgstr.replace("\\n", "\n")

    if problems:
        print("REFUSED — %d line(s) name no string in %s:" % (len(problems), po.name),
              file=sys.stderr)
        for p in problems:
            print("  " + p, file=sys.stderr)
        return 1

    for mid, msgstr in want.items():
        i = index[mid]
        b = blocks[i]
        if isinstance(msgstr, list):
            # One msgstr[N] per form, replacing however many the block had.
            body = "".join('msgstr[%d] "%s"\n' % (k, po_escape(f))
                           for k, f in enumerate(msgstr))
            new = re.sub(r'^msgstr\[\d+\] (?:"(?:[^"\\]|\\.)*"\s*)+', '',
                         b, flags=re.M).rstrip('\n') + '\n' + body
        else:
            # Replace the whole msgstr, however many continuation lines it had.
            #
            # ⛔ A LAMBDA, NOT A REPLACEMENT STRING. re.sub() processes escapes in
            # its replacement template — \n becomes a newline, \1 becomes a
            # group — and po_escape() exists precisely to PRODUCE backslash
            # sequences. Passed as a string, every "\n" this tool had just
            # written was turned straight back into a literal newline, which
            # ends the quoted string and makes the file unparseable: msgfmt says
            # "end-of-line within string" and then "keyword \"Klicken\" unknown"
            # on the German for the second half of the sentence.
            #
            # It went unseen because the compositor's own catalogs were filled
            # before any multi-line msgid reached them; it broke all thirteen bar
            # catalogs at once on 2026-09-01, and tests/i18n_bar.sh caught it.
            # A function replacement is used verbatim.
            new = re.sub(r'^msgstr (?:"(?:[^"\\]|\\.)*"\s*)+',
                         lambda _m: 'msgstr "%s"\n' % po_escape(msgstr),
                         b, count=1, flags=re.M)
        # A fuzzy marker outlives the string it was guessed for.
        #
        # ⛔ AND IT IS NOT ALONE ON ITS LINE. gettext writes one comma-separated
        # flag line: `#, fuzzy, c-format`. A regex for `^#, fuzzy\n` leaves that
        # untouched, and a fuzzy entry is one msgfmt DOES NOT USE — so five
        # freshly filled c-format strings sat in the catalog, correct, and
        # shipped English. Drop just the flag; keep whatever else is on the line.
        def _unfuzz(m):
            flags = [f.strip() for f in m.group(1).split(',') if f.strip() != 'fuzzy']
            return ('#, ' + ', '.join(flags) + '\n') if flags else ''
        new = re.sub(r'^#,[ \t]*(.*fuzzy.*)\n', _unfuzz, new, flags=re.M)
        blocks[i] = new.rstrip('\n')

    po.write_text("\n\n".join(blocks).rstrip('\n') + "\n", encoding="utf-8")
    # ⚠ A msgstr CAN SPAN LINES, and a long one always does — msgmerge wraps it
    # as `msgstr ""` followed by continuation strings. Testing for a line that
    # is exactly `msgstr ""` therefore calls every wrapped translation missing,
    # which is how a fully translated catalog once reported 69 strings short.
    # Join the whole run and ask whether it has any content.
    done = sum(1 for b in blocks if msgid_of(b) and msgstr_of(b))
    total = sum(1 for b in blocks if msgid_of(b))
    print("%s: %d/%d translated (%d from this pass)" % (po.name, done, total, len(want)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
