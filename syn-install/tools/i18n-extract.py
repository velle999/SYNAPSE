#!/usr/bin/env python3
"""
i18n-extract.py — every string syn-install shows a person, in catalog order.

⚠ THE CATALOG IS KEYED BY THE ENGLISH SENTENCE, so this is the only thing that
knows what the valid keys ARE. Without it, editing an English string silently
orphans its fourteen translations: the new sentence has no entry, so it prints
English, and the old entry sits in every catalog matching nothing. Neither half
of that says anything at runtime.

  tools/i18n-extract.py --list                one key per line
  tools/i18n-extract.py --template            an empty catalog to fill in
  tools/i18n-extract.py --check lang/de.sh    keys in that file matching nothing

The strings come from the five places a translatable string can appear:

    step/success/fail/warn/die/prompt "…"   translated inside the helper
    say "…"                                 a line of prose
    $(t '…')                                a fragment inside a bigger line
    tf '…' args                             a sentence with something in it

⚠ AND A double-quoted STRING CAN SPAN LINES. The careful multi-line warnings —
the ones most worth translating — are written across three or four source
lines, and an extractor that read line by line would emit four keys that match
nothing instead of the one that does.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
"""
import re
import sys
import pathlib

HELPERS = ("step", "success", "fail", "warn", "die", "prompt", "say")


def strings(src: str):
    """Yield each translatable literal, in source order, deduplicated."""
    seen, out = set(), []

    def add(s):
        if not s or not s.strip():
            return
        # A key with nothing to translate — a bare path, a device name, a
        # number — is noise in fourteen files. One word of three letters is the
        # bar.
        #
        # ⚠ IT USED TO BE TWO WORDS, and that silently dropped "Password:" —
        # a one-word prompt, on the screen where somebody types their password,
        # left in English in every language. A rule about how much text a
        # string has is not a rule about whether it is worth translating.
        if not re.search(r"[A-Za-z]{3,}", s):
            return
        if s in seen:
            return
        seen.add(s)
        out.append(s)

    # ── the helpers, whose argument may span lines ──
    call = re.compile(r"(?:^|[;&|(]|\bthen\b|\belse\b|\bdo\b)\s*(%s)\s+\"" %
                      "|".join(HELPERS), re.M)
    for m in call.finditer(src):
        i = m.end()               # first character inside the quotes
        buf = []
        while i < len(src):
            c = src[i]
            if c == "\\" and i + 1 < len(src):
                buf.append(src[i:i + 2])
                i += 2
                continue
            if c == '"':
                break
            buf.append(c)
            i += 1
        lit = "".join(buf)
        # An interpolated helper argument is not a key: its text changes per
        # run. Those call sites use tf() instead, and are collected below.
        if "$" in lit or "`" in lit:
            continue
        add(lit)

    # ── $(t '…') and tf '…' ──
    for m in re.finditer(r"\$\(t '((?:[^'\\]|\\.)*)'\)", src):
        add(m.group(1).replace("'\\''", "'"))
    for m in re.finditer(r"\btf '((?:[^'\\]|\\.)*)'", src):
        add(m.group(1))
    for m in re.finditer(r'\btf "((?:[^"\\]|\\.)*)"', src):
        add(m.group(1))

    return out


def bash_quote(s: str) -> str:
    """Inside a double-quoted bash word."""
    return s.replace("\\", "\\\\").replace('"', '\\"').replace("$", "\\$").replace("`", "\\`")


def gui_keys(root):
    """The sentences the GRAPHICAL installer marks, from tools/gui-strings.py.

    ⚠ ONE CATALOG SERVES BOTH INSTALLERS, so both files' keys are what a
    catalog is checked against. Read the script's alone and every string the
    window added reads as an ORPHAN — the check that exists to find dead
    translations would condemn the live ones instead.

    ⚠ IMPORTED, NOT REIMPLEMENTED: the folding of concatenated literals is
    exactly the part that is easy to get subtly wrong, and two copies of it
    would disagree the first time a sentence was wrapped differently.
    """
    spec = root / "tools" / "gui-strings.py"
    qml = root / "syn-install-gui.qml"
    if not spec.exists() or not qml.exists():
        return []
    ns = {"__name__": "gui_strings", "__file__": str(spec)}
    exec(compile(spec.read_text(encoding="utf-8"), str(spec), "exec"), ns)
    keys, _bad = ns["scan"](qml)
    return keys


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    src = (root / "syn-install.sh").read_text(encoding="utf-8")
    keys = strings(src)
    seen = set(keys)
    for k in gui_keys(root):
        if k not in seen:
            seen.add(k)
            keys.append(k)

    if "--list" in sys.argv:
        for k in keys:
            print(k.replace("\n", "\\n"))
        return 0

    if "--template" in sys.argv:
        print("declare -gA SYN_T=(")
        for k in keys:
            print('  ["%s"]=""' % bash_quote(k))
        print(")")
        return 0

    if "--check" in sys.argv:
        path = pathlib.Path(sys.argv[sys.argv.index("--check") + 1])
        cat = path.read_text(encoding="utf-8")
        # The keys a catalog declares, read the same way bash would.
        have = re.findall(r'^\s*\["((?:[^"\\]|\\.)*)"\]=', cat, re.M)
        unesc = lambda s: (s.replace('\\"', '"').replace("\\$", "$")
                            .replace("\\`", "`").replace("\\\\", "\\"))
        known = set(keys)
        orphans = [h for h in have if unesc(h) not in known]
        # ⚠ COUNT THE KEYS, NOT THE LINES. A translated paragraph holds real
        # newlines, so its entry spans several lines of the catalog and a
        # line-anchored count reported a third of the file missing.
        filled = len(have)
        print("%s: %d/%d translated, %d orphan(s)" %
              (path.name, filled, len(keys), len(orphans)))
        for o in orphans:
            print("  ORPHAN: %s" % o[:90])
        return 1 if orphans else 0

    print("total translatable strings: %d" % len(keys))
    print("total characters: %d" % sum(len(k) for k in keys))
    return 0


if __name__ == "__main__":
    sys.exit(main())
