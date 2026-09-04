#!/usr/bin/env python3
"""
i18n-fill.py — add or correct translations by naming the English, not a line.

    tools/i18n-fill.py fr < fr.tsv

Each line of the input is:

    <english prefix><TAB><translation>

The prefix is matched against the English strings the script actually contains.
It must match EXACTLY ONE of them; anything that matches none, or more than one,
is reported and the whole file is refused.

⚠ THIS EXISTS BECAUSE i18n-gen.py PAIRS BY POSITION. That is fine for one
generated pass and wrong for everything after it: adding a single English
string shifts every later line, and a translator working from a list of 370
numbers has no way to notice. Naming the sentence cannot shift.

`\\n` in either column is a newline. An empty translation removes the entry, so
the English comes back.

Merges into an existing lang/<code>.sh rather than replacing it, so a file can
be filled in over several passes.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
"""
import pathlib
import re
import sys


def load_keys(root):
    """Every key a catalog may hold — BOTH installers'.

    ⚠ THE WINDOW'S SENTENCES COUNT TOO. One catalog serves syn-install.sh and
    syn-install-gui.qml, so a filler that knew only the script's would refuse
    every line of a translation for the graphical installer with "0 matches",
    which reads as the translation being wrong rather than the tool being
    half-sighted. i18n-extract.py's main() already joins the two; this uses the
    same two calls so the lists cannot come apart.
    """
    spec = root / "tools" / "i18n-extract.py"
    ns = {"__name__": "extract", "__file__": str(spec)}
    exec(compile(spec.read_text(encoding="utf-8"), str(spec), "exec"), ns)
    keys = ns["strings"]((root / "syn-install.sh").read_text(encoding="utf-8"))
    seen = set(keys)
    for k in ns["gui_keys"](root):
        if k not in seen:
            seen.add(k)
            keys.append(k)
    return keys


def unesc(s):
    return (s.replace('\\"', '"').replace("\\$", "$")
             .replace("\\`", "`").replace("\\\\", "\\"))


def esc(s):
    return (s.replace("\\", "\\\\").replace('"', '\\"')
             .replace("$", "\\$").replace("`", "\\`"))


def read_catalog(path):
    if not path.exists():
        return {}
    text = path.read_text(encoding="utf-8")
    out = {}
    for m in re.finditer(r'^\s*\["((?:[^"\\]|\\.)*)"\]="((?:[^"\\]|\\.)*)"$',
                         text, re.M | re.S):
        out[unesc(m.group(1))] = unesc(m.group(2))
    return out


HEADER = """\
# %s — syn-install's own words.
#
# ⚠ THE KEY IS THE ENGLISH SENTENCE. A translation whose key matches nothing in
# syn-install.sh is dead text that will never appear again, and it is the only
# evidence that an English string was edited — tests/i18n_test.sh fails on one.
#
# An entry left out prints the English. That is not a failure state: an
# installer that says one screen in English is usable, and one that says a
# screen wrong is not.
#
# ⚠ THE ANSWER KEYS STAY [Y/n], [y/N] AND 'yes'. The prompts around them are
# translated; the letters are not, because the script compares against y/n and
# the literal word yes. A translated key would be a question whose own answer
# does not work.
#
# Edit by hand, or with tools/i18n-fill.py, which matches on the English rather
# than on a line number.
#
# SPDX-License-Identifier: GPL-2.0-or-later
"""

NAMES = {
    "de": "Deutsch (de)", "fr": "Français (fr)", "es": "Español (es)",
    "pt": "Português (pt)", "it": "Italiano (it)", "nl": "Nederlands (nl)",
    "pl": "Polski (pl)", "ru": "Русский (ru)", "ja": "日本語 (ja)",
    "zh": "中文 (zh)", "ko": "한국어 (ko)", "hi": "हिन्दी (hi)", "ar": "العربية (ar)",
}


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    code = sys.argv[1]
    root = pathlib.Path(__file__).resolve().parent.parent
    keys = load_keys(root)
    dest = root / ("lang-%s.sh" % code)
    cat = read_catalog(dest)

    problems, added = [], 0
    for lineno, raw in enumerate(sys.stdin.read().split("\n"), 1):
        if not raw.strip() or "\t" not in raw:
            continue
        prefix, value = raw.split("\t", 1)
        # ⚠ ONLY THE VALUE HAS ITS \n TURNED INTO A NEWLINE. Half the English
        # strings are printf formats that carry a LITERAL backslash-n — printf
        # expands it later — so converting it in the prefix too made those keys
        # unmatchable by the very text they are written as.
        value = value.replace("\\n", "\n")
        hits = [k for k in keys if k.startswith(prefix)]
        if len(hits) != 1:
            exact = [k for k in hits if k == prefix]
            if len(exact) == 1:
                hits = exact
            else:
                problems.append("line %d: %d matches for %r"
                                % (lineno, len(hits), prefix[:60]))
                continue
        if value.strip():
            cat[hits[0]] = value
        else:
            cat.pop(hits[0], None)
        added += 1

    if problems:
        print("REFUSED — %d line(s) did not name exactly one string:" % len(problems),
              file=sys.stderr)
        for p in problems:
            print("  " + p, file=sys.stderr)
        return 1

    order = {k: i for i, k in enumerate(keys)}
    body = [HEADER % NAMES.get(code, code), "declare -gA SYN_T=("]
    for k in sorted(cat, key=lambda k: order.get(k, 1 << 30)):
        body.append('  ["%s"]="%s"' % (esc(k), esc(cat[k])))
    body.append(")")
    
    dest.write_text("\n".join(body) + "\n", encoding="utf-8")
    print("%s: %d entries (%d from this pass), %d strings in the script"
          % (dest.name, len(cat), added, len(keys)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
