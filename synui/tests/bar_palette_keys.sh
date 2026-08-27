#!/bin/sh
# bar_palette_keys.sh — every `pal.<key>` a bar module reads is a key the bar
# palette actually defines.
#
# THE BUG THIS EXISTS FOR shipped for months and nothing anywhere reported it.
# Updates.qml asked for `pal.yellow` for its error state. `yellow` is a THEME
# property; the per-strip object these modules read is a different thing with
# its own vocabulary, and it has never had that key. So on the errored branch —
# and only there, which is why nobody hit it — the binding evaluated to
# undefined.
#
# ⚠ AND UNDEFINED DOES NOT BLANK A QML PROPERTY, IT LEAVES THE OLD VALUE. The
# badge kept BarModule's default ink, so "the update check is failing" was drawn
# exactly like "an update is waiting". Not missing, not obviously wrong: right
# in every way except the one thing the colour was there to say. The only trace
# was `Unable to assign [undefined] to QColor` in a log, once per monitor.
#
# That is unreachable by any pixel test — it needs a machine whose update check
# is failing — so the guard is mechanical instead: the palette literals in
# Theme.qml are the vocabulary, and every reader has to stay inside it.
#
# ⚠ BOTH LITERALS, AND SEPARATELY. `barPalette()` and `barPaletteInked()` build
# the same shape by two different routes, and a key added to one and not the
# other is a colour that exists on a clear bar and not on an opaque one — a
# bug that would show on some desktops and not others, which is worse to chase
# than one that shows nowhere.
#
# Reads files. No compositor, no shell, no GPU: it never skips.
#
# Usage: bar_palette_keys.sh /path/to/quickshell-tree
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

TREE=${1:?usage: bar_palette_keys.sh /path/to/quickshell-tree}

python3 - "$TREE" <<'ENDPY'
import os, re, sys

tree = sys.argv[1]
theme = open(os.path.join(tree, "Theme.qml"), encoding="utf-8").read()

def literal_keys(fn):
    """The keys of the object literal `fn` returns."""
    i = theme.index("function " + fn)
    j = theme.index("return {", i)
    # The literal's closing brace, at the indentation `return {` opened from.
    k = theme.index("\n        }", j)
    return set(re.findall(r"^\s{12}([A-Za-z][A-Za-z0-9_]*)\s*:", theme[j:k], re.M))

pals = {fn: literal_keys(fn) for fn in ("barPaletteInked", "barPalette(")}
for fn, keys in pals.items():
    if not keys:
        print(f"FAIL: found no keys in {fn} — this test's parser has gone stale,")
        print("      which is worse than the bug it guards: it would pass forever.")
        sys.exit(1)

names = list(pals)
a, b = pals[names[0]], pals[names[1]]
if a != b:
    print(f"FAIL: the two palette literals define different keys.")
    print(f"  only in {names[0]}: {sorted(a - b)}")
    print(f"  only in {names[1]}: {sorted(b - a)}")
    print("  A key in one and not the other is a colour that exists on a clear")
    print("  bar and not on an opaque one, or the reverse.")
    sys.exit(1)

defined = a
print(f"  palette vocabulary ({len(defined)}): {' '.join(sorted(defined))}")

# Everything that reads one. Bar.qml and the modules are the consumers; the
# components define `pal` itself and read it too.
roots = [os.path.join(tree, "modules"), os.path.join(tree, "components")]
files = [os.path.join(tree, "Bar.qml")]
for r in roots:
    for f in sorted(os.listdir(r)):
        if f.endswith(".qml"):
            files.append(os.path.join(r, f))

bad = []
seen = set()
for path in files:
    for n, line in enumerate(open(path, encoding="utf-8"), 1):
        # Comments explain the keys at length; only code is a reader.
        code = line.split("//")[0]
        if code.lstrip().startswith("*"):
            continue
        for key in re.findall(r"\bpal\.([A-Za-z][A-Za-z0-9_]*)", code):
            seen.add(key)
            if key not in defined:
                bad.append((os.path.relpath(path, tree), n, key))

print(f"  read by the bar ({len(seen)}): {' '.join(sorted(seen))}")

unread = sorted(defined - seen)
if unread:
    # A note, not a failure: a slot nobody reads yet is a palette that is ready
    # for something, and `ink`/`scrim`/`clear` are read by Theme itself.
    print(f"  note: defined and unread here: {' '.join(unread)}")

if bad:
    for path, n, key in bad:
        print(f"  FAIL  {path}:{n}  pal.{key} is not a palette key")
    print()
    print("  A QML binding to a missing key evaluates to undefined, and an")
    print("  undefined assignment LEAVES THE PREVIOUS VALUE — so this draws the")
    print("  wrong colour rather than no colour, and only on whichever branch")
    print("  asked for it. Pick a key from the vocabulary above.")
    sys.exit(1)

print("  ok    every pal.<key> the bar reads is one the palette defines")
ENDPY
