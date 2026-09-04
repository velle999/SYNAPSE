#!/bin/sh
# bar_dim_ink.sh — the strip's dim ink is derived from its OWN ink, never a
# hex literal.
#
# THE BUG THIS EXISTS FOR WAS FIXED ONCE AND KEPT SHIPPING. 492 ("the dim ink
# is measured against the surface, not a grey from memory") replaced
# pick("#3a4a52", "#6b7280") with a measured blend — and did it for
# `Theme.fgDim` only. Both STRIP palettes went on returning the identical pair,
# and the strip is the one surface a theme cannot know anything about, because
# a clear bar is drawn on the WALLPAPER.
#
# What that costs, measured off a screenshot rather than argued:
#
#   · #6b7280 on the pale sky a portrait bar sat over    2.84 : 1
#   · #3a4a52 on the dark green at the same bar's left   1.03 : 1
#
# against 9.9:1 and 9.5:1 for the ink beside it. `pal.dim` is the empty
# workspace digit, the muted volume, the stale weather, the disabled network,
# the paused media — text that is present and cannot be read.
#
# ⚠ IT LOOKED LIKE A PORTRAIT BUG AND WAS NEVER ONE. The portrait panel is
# simply where a light-measuring span and eight empty desktops happen at the
# same time; the dark-strip half of the same literal is worse.
#
# ⚠ AND BOTH LITERALS, SEPARATELY — `barPalette()` and `barPaletteInked()` build
# the same shape by two routes, the same reason bar_palette_keys.sh checks both.
#
# A pixel test cannot reach this: it needs a wallpaper whose measured ink flips,
# an empty workspace, and the monitor it happens on. So the guard is mechanical
# — `dim` must be built out of the ink this palette already chose.
#
# Reads files. No compositor, no shell, no GPU: it never skips.
#
# Usage: bar_dim_ink.sh /path/to/quickshell-tree
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

TREE=${1:?usage: bar_dim_ink.sh /path/to/quickshell-tree}

python3 - "$TREE" <<'ENDPY'
import os, re, sys

tree  = sys.argv[1]
theme = open(os.path.join(tree, "Theme.qml"), encoding="utf-8").read()

def dim_value(fn):
    """The text `dim:` is assigned to inside the literal `fn` returns."""
    i = theme.index("function " + fn)
    j = theme.index("return {", i)
    k = theme.index("\n        }", j)
    m = re.search(r"^\s+dim:\s*(.+?),?\s*$", theme[j:k], re.M)
    return m.group(1).rstrip(",") if m else None

bad = []
for fn in ("barPaletteInked", "barPalette"):
    v = dim_value(fn)
    if v is None:
        bad.append((fn, "(no dim: key at all)"))
        continue
    if re.search(r'"#[0-9a-fA-F]{3,8}"', v):
        bad.append((fn, v))
    elif "wash" not in v and "ink" not in v and "fgDim" not in v:
        bad.append((fn, v))

if bad:
    for fn, v in bad:
        print(f"  FAIL  {fn}: dim is {v}")
    print()
    print("  A hex literal here is a grey chosen against ONE surface, once. The")
    print("  strip's surface is the wallpaper — it changes per monitor, per span")
    print("  and per wallpaper, and the palette carries `inkOnDark` and no colour")
    print("  at all. Build dim out of `wash`/`ink` (alpha lets the compositor")
    print("  blend it against whatever is really there) or, for an opaque strip,")
    print("  take root.fgDim, which was measured against that surface.")
    sys.exit(1)

print("  ok    both strip palettes derive dim from their own ink")
ENDPY
