#!/usr/bin/env python3
"""
qml-xgettext.py — pull I18n.tr() / I18n.trn() strings out of QML into a .pot.

    tools/qml-xgettext.py --root quickshell --files po-bar/POTFILES -o po-bar/synui-bar.pot

⛔ THIS EXISTS BECAUSE xgettext CANNOT READ QML. There is no --language=QML, and
--language=JavaScript chokes on the object syntax — `Item { id: foo }` is not an
expression, so the lexer resynchronises somewhere unpredictable and silently
extracts a subset. A partial .pot is the worst outcome available: it looks like
a successful run, and the strings it missed are simply English forever.

⚠ THE ARGUMENT MUST BE A PLAIN LITERAL. This reads the source, not the running
program, so `I18n.tr(someVariable)` extracts nothing and translates nothing —
the QML shape of the N_() trap src/i18n.h documents. A tr() whose argument is
not a literal is an ERROR here, not a skip, because the alternative is a call
site that looks marked in review and is English in all thirteen languages.
tests/i18n_bar.sh runs this with --strict for exactly that.

Adjacent literals concatenate the way the QML engine would ("a" "b" and
"a" + "b" are both one string), because a long sentence has to be able to wrap.
A concatenation with anything that is not a literal is the error above.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import pathlib
import re
import sys

# One string literal, single or double quoted, with escapes. Non-greedy body,
# and \\ before the closing quote is handled by the escape alternative.
_STR = r'"(?:[^"\\]|\\.)*"|\'(?:[^\'\\]|\\.)*\''

# A call to the marker, captured up to its closing paren. The body is parsed
# properly below; this only finds the call and its extent cheaply.
_CALL = re.compile(r'\bI18n\.(tr|trn)\s*\(')

# Comments, so a marked string inside one is not extracted. QML takes both
# forms, and a // inside a string literal must not start a comment — hence the
# literal alternative first, which consumes it.
_SKIP = re.compile(r'(' + _STR + r')|(/\*.*?\*/)|(//[^\n]*)', re.S)


def strip_comments(text):
    """Blank out comments, keeping every byte offset and line break intact."""
    out = []
    pos = 0
    for m in _SKIP.finditer(text):
        out.append(text[pos:m.start()])
        chunk = m.group(0)
        if m.group(1) is not None:          # a string literal: keep it
            out.append(chunk)
        else:                               # a comment: keep only its newlines
            out.append(re.sub(r'[^\n]', ' ', chunk))
        pos = m.end()
    out.append(text[pos:])
    return "".join(out)


def js_unescape(literal):
    """The value of a QML string literal, quotes included in the input."""
    body = literal[1:-1]
    out = []
    i = 0
    simple = {'n': '\n', 't': '\t', 'r': '\r', 'b': '\b', 'f': '\f',
              'v': '\v', '0': '\0', '\\': '\\', '"': '"', "'": "'", '\n': ''}
    while i < len(body):
        c = body[i]
        if c != '\\':
            out.append(c)
            i += 1
            continue
        nxt = body[i + 1]
        if nxt == 'u':
            if body[i + 2] == '{':
                end = body.index('}', i)
                out.append(chr(int(body[i + 3:end], 16)))
                i = end + 1
            else:
                out.append(chr(int(body[i + 2:i + 6], 16)))
                i += 6
        elif nxt == 'x':
            out.append(chr(int(body[i + 2:i + 4], 16)))
            i += 4
        else:
            out.append(simple.get(nxt, nxt))
            i += 2
    return "".join(out)


def split_args(body):
    """Top-level comma split of a call's argument list."""
    args, depth, cur, i = [], 0, [], 0
    while i < len(body):
        c = body[i]
        if c in '"\'':
            m = re.compile(_STR).match(body, i)
            if not m:
                raise ValueError("unterminated string literal")
            cur.append(m.group(0))
            i = m.end()
            continue
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
        if c == ',' and depth == 0:
            args.append("".join(cur))
            cur = []
        else:
            cur.append(c)
        i += 1
    args.append("".join(cur))
    return [a.strip() for a in args]


def literal_value(arg):
    """The string an argument denotes, or None if it is not a literal.

    Adjacent and +-joined literals fold, so a sentence can wrap across lines.
    Anything else in the expression — a variable, a call, a ternary — makes the
    whole argument non-literal, which is the error this is here to detect.
    """
    arg = arg.strip()
    # Whatever is left after removing every literal and the joiners between
    # them has to be nothing at all.
    if re.sub(r'[\s+]', '', re.sub(_STR, '', arg)) != '':
        return None
    pieces = re.findall(_STR, arg)
    if not pieces:
        return None
    return "".join(js_unescape(p) for p in pieces)


def find_call_end(text, start):
    """Index just past the ) that closes the call opened at `start`."""
    depth, i = 0, start
    while i < len(text):
        c = text[i]
        if c in '"\'':
            m = re.compile(_STR).match(text, i)
            if not m:
                raise ValueError("unterminated string literal")
            i = m.end()
            continue
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise ValueError("unbalanced call")


def scan(path, rel):
    """[(msgid, msgid_plural or None, line)] plus a list of complaints."""
    text = strip_comments(path.read_text(encoding='utf-8'))
    hits, bad = [], []
    for m in _CALL.finditer(text):
        kind = m.group(1)
        open_paren = m.end() - 1
        try:
            close = find_call_end(text, open_paren)
        except ValueError as e:
            bad.append(f"{rel}:{text[:m.start()].count(chr(10)) + 1}: {e}")
            continue
        line = text[:m.start()].count('\n') + 1
        args = split_args(text[open_paren + 1:close])
        want = 1 if kind == 'tr' else 3
        if len(args) < want:
            bad.append(f"{rel}:{line}: I18n.{kind}() takes {want} argument(s), got {len(args)}")
            continue
        single = literal_value(args[0])
        if single is None:
            bad.append(f"{rel}:{line}: I18n.{kind}()'s first argument is not a string literal "
                       f"— it extracts nothing and stays English: {args[0][:60]}")
            continue
        plural = None
        if kind == 'trn':
            plural = literal_value(args[1])
            if plural is None:
                bad.append(f"{rel}:{line}: I18n.trn()'s plural form is not a string literal: "
                           f"{args[1][:60]}")
                continue
        hits.append((single, plural, line))
    return hits, bad


def po_escape(s):
    return (s.replace('\\', '\\\\').replace('"', '\\"')
             .replace('\n', '\\n').replace('\t', '\\t'))


HEADER = '''# SynapseOS — the bar's own words.
#
# ⚠ GENERATED by tools/qml-xgettext.py. Do not edit; run it again.
#
# SPDX-License-Identifier: GPL-2.0-or-later
msgid ""
msgstr ""
"Project-Id-Version: synfiles\\n"
"Report-Msgid-Bugs-To: \\n"
"MIME-Version: 1.0\\n"
"Content-Type: text/plain; charset=UTF-8\\n"
"Content-Transfer-Encoding: 8bit\\n"
"Plural-Forms: nplurals=2; plural=(n != 1);\\n"
"PO-Revision-Date: \\n"
"Last-Translator: \\n"
"Language-Team: \\n"
"Language: \\n"

'''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', required=True)
    ap.add_argument('--files', required=True, help='a POTFILES list, paths relative to --root')
    ap.add_argument('-o', '--output', required=True)
    ap.add_argument('--strict', action='store_true',
                    help='exit non-zero on a non-literal argument')
    args = ap.parse_args()

    root = pathlib.Path(args.root)
    listed = [l.strip() for l in pathlib.Path(args.files).read_text().splitlines()]
    listed = [l for l in listed if l and not l.startswith('#')]

    entries, complaints = {}, []
    for rel in listed:
        p = root / rel
        if not p.is_file():
            complaints.append(f"{rel}: listed in POTFILES and not on disk")
            continue
        hits, bad = scan(p, rel)
        complaints.extend(bad)
        for msgid, plural, line in hits:
            e = entries.setdefault(msgid, {'plural': plural, 'refs': []})
            e['refs'].append(f"{rel}:{line}")
            # ⚠ A msgid used BOTH ways is a bug the catalogs cannot express:
            # one entry cannot be singular-only in one place and plural in
            # another, and msgfmt would take whichever won.
            if plural and e['plural'] and plural != e['plural']:
                complaints.append(f"{rel}:{line}: \"{msgid}\" has two different plural forms")
            e['plural'] = e['plural'] or plural

    out = [HEADER]
    for msgid in sorted(entries):
        e = entries[msgid]
        for ref in e['refs']:
            out.append(f"#: {ref}\n")
        out.append(f'msgid "{po_escape(msgid)}"\n')
        if e['plural']:
            out.append(f'msgid_plural "{po_escape(e["plural"])}"\n')
            out.append('msgstr[0] ""\n')
            out.append('msgstr[1] ""\n')
        else:
            out.append('msgstr ""\n')
        out.append('\n')

    for c in complaints:
        print(c, file=sys.stderr)

    if complaints and args.strict:
        return 1

    pathlib.Path(args.output).write_text("".join(out), encoding='utf-8')
    print(f"qml-xgettext: {len(entries)} msgids from {len(listed)} files -> {args.output}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
