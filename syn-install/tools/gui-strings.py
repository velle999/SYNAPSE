#!/usr/bin/env python3
"""
gui-strings.py — every English sentence syn-install-gui.qml asks to translate.

The window's catalog is syn-install's own (`syn-install --strings` prints it),
keyed by the English sentence — so the keys have to be READ OUT of the QML to
be checked against the catalogs, exactly as tests/i18n_test.sh reads them out
of syn-install.sh.

⚠ ADJACENT AND +-JOINED LITERALS FOLD, because a sentence longer than a line
is written as a concatenation and the key is the whole of it. Anything else
inside the call — a variable, a ternary, a function — makes the argument
non-literal, and a non-literal argument extracts NOTHING while looking exactly
like a marked string. Those are reported as errors rather than skipped: that is
the one failure mode of this whole scheme.

--check also reads the file the other way round: every property that PUTS
WORDS ON THE SCREEN — text, subtext, title, blurb, placeholderText — must get
them from root.t()/root.tf() and not from a bare literal. A missed one is not a
broken build or a wrong key, it is a button that stays English in thirteen
languages while everything around it changes, which is what "Re-check" did
while the paragraph above it told people to press it.

    tools/gui-strings.py            # one key per line, \\n escaped
    tools/gui-strings.py --check    # exit 1 on a non-literal argument
                                    #   or an unmarked visible string

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
"""
import pathlib
import re
import sys

STR = r'"(?:[^"\\]|\\.)*"|\'(?:[^\'\\]|\\.)*\''
CALL = re.compile(r'root\.(t|tf)\(')


def strip_comments(text):
    """Blank out // and /* */ without touching string literals."""
    out, i = [], 0
    pat = re.compile(STR)
    while i < len(text):
        c = text[i]
        if c in '"\'':
            m = pat.match(text, i)
            if m:
                out.append(m.group(0)); i = m.end(); continue
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = len(text) if j < 0 else j
            out.append(" " * (j - i)); i = j; continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = len(text) if j < 0 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:j])); i = j; continue
        out.append(c); i += 1
    return "".join(out)


def unescape(lit):
    body = lit[1:-1]
    out, i = [], 0
    while i < len(body):
        if body[i] == "\\" and i + 1 < len(body):
            nxt = body[i + 1]
            out.append({"n": "\n", "t": "\t", "r": "\r",
                        '"': '"', "'": "'", "\\": "\\"}.get(nxt, nxt))
            i += 2
            continue
        out.append(body[i]); i += 1
    return "".join(out)


def first_arg(text, open_paren):
    """The first argument's source, and the index just past the call."""
    depth, i, start = 0, open_paren, open_paren + 1
    pat = re.compile(STR)
    arg_end = None
    while i < len(text):
        c = text[i]
        if c in '"\'':
            m = pat.match(text, i)
            if not m:
                return None, i
            i = m.end(); continue
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return text[start:arg_end if arg_end is not None else i], i
        elif c == "," and depth == 1 and arg_end is None:
            arg_end = i
        i += 1
    return None, i


def scan(path):
    text = strip_comments(path.read_text(encoding="utf-8"))
    keys, bad = [], []
    for m in CALL.finditer(text):
        arg, _ = first_arg(text, m.end() - 1)
        line = text[: m.start()].count("\n") + 1
        if arg is None:
            bad.append(f"{path.name}:{line}: unbalanced root.{m.group(1)}() call")
            continue
        if re.sub(r"[\s+]", "", re.sub(STR, "", arg)) != "":
            bad.append(f"{path.name}:{line}: root.{m.group(1)}()'s first argument is not a "
                       f"string literal — it reaches no catalog and stays English: "
                       f"{arg.strip()[:60]}")
            continue
        pieces = re.findall(STR, arg)
        if not pieces:
            bad.append(f"{path.name}:{line}: root.{m.group(1)}() with no string at all")
            continue
        keys.append("".join(unescape(p) for p in pieces))
    return keys, bad


# The properties a person reads. Everything else a QML string can be — an id, a
# colour, a path, a record field, an app_id — is not prose and is not this.
TEXT_PROPS = ("text", "subtext", "title", "blurb", "label", "placeholderText")
PROP = re.compile(r"^[ \t]*(%s):[ \t]*(.*)$" % "|".join(TEXT_PROPS), re.M)


def unmarked(path):
    """Visible-text properties whose value contains an unmarked literal."""
    text = strip_comments(path.read_text(encoding="utf-8"))
    out = []
    for m in PROP.finditer(text):
        # A value can run over several lines (a ternary, a folded sentence), so
        # take it to the next property or closing brace rather than to the
        # newline — otherwise a marked multi-line value reads as unmarked.
        val, i, depth = [], m.end(2), 0
        val.append(m.group(2))
        for line in text[i:].split("\n")[1:]:
            if re.match(r"^[ \t]*[A-Za-z_.]+:", line) or re.match(r"^[ \t]*[}\]]", line):
                break
            val.append(line)
            if len(val) > 12:
                break
        joined = " ".join(val)
        # Strip every marked call's argument list, then look at what is left.
        rest = re.sub(r"root\.tf?\(", " (", joined)
        lits = re.findall(STR, joined)
        marked = "root.t(" in joined or "root.tf(" in joined
        human = [l for l in lits if re.search(r"[A-Za-z]{2}", l[1:-1])]
        if not human or marked:
            continue
        line_no = text[: m.start()].count("\n") + 1
        out.append(f"{path.name}:{line_no}: {m.group(1)} is drawn from a bare literal — "
                   f"it stays English in every language: {joined.strip()[:60]}")
    return out


def main():
    here = pathlib.Path(__file__).resolve().parent.parent
    qml = here / "syn-install-gui.qml"
    keys, bad = scan(qml)
    if "--check" in sys.argv:
        bad += unmarked(qml)
    for b in bad:
        print(b, file=sys.stderr)
    if "--check" in sys.argv:
        print(f"{len(keys)} marked string(s), {len(bad)} problem(s)")
        return 1 if bad else 0
    seen = set()
    for k in keys:
        if k in seen:
            continue
        seen.add(k)
        print(k.replace("\\", "\\\\").replace("\n", "\\n").replace("\t", "\\t"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
