#!/usr/bin/env python3
"""
po-fill.py — fill a .po from `msgid<TAB>msgstr` lines.

    tools/po-fill.py de < de.tsv

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
    code = sys.argv[1]
    root = pathlib.Path(__file__).resolve().parent.parent
    po = root / "po" / ("%s.po" % code)
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
            new = re.sub(r'^msgstr (?:"(?:[^"\\]|\\.)*"\s*)+', 'msgstr "%s"\n' % po_escape(msgstr),
                         b, count=1, flags=re.M)
        # A fuzzy marker outlives the string it was guessed for.
        new = re.sub(r'^#, fuzzy\n', '', new, flags=re.M)
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
