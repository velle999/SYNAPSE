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


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    code = sys.argv[1]
    root = pathlib.Path(__file__).resolve().parent.parent
    po = root / "po" / ("%s.po" % code)
    text = po.read_text(encoding="utf-8")
    blocks = parse_po(text)

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
        if msgstr.strip():
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
        # Replace the whole msgstr, however many continuation lines it had.
        new = re.sub(r'^msgstr (?:"(?:[^"\\]|\\.)*"\s*)+', 'msgstr "%s"\n' % po_escape(msgstr),
                     b, count=1, flags=re.M)
        # A fuzzy marker outlives the string it was guessed for.
        new = re.sub(r'^#, fuzzy\n', '', new, flags=re.M)
        blocks[i] = new.rstrip('\n')

    po.write_text("\n\n".join(blocks).rstrip('\n') + "\n", encoding="utf-8")
    done = sum(1 for b in blocks if msgid_of(b) and
               not re.search(r'^msgstr ""\s*$', b, re.M))
    total = sum(1 for b in blocks if msgid_of(b))
    print("%s: %d/%d translated (%d from this pass)" % (po.name, done, total, len(want)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
