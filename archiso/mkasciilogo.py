#!/usr/bin/env python3
"""Generate the SynapseOS console mark -> airootfs/usr/share/synapseos/logo.txt

Run from the repo root after changing anything here:

    python3 archiso/mkasciilogo.py > archiso/airootfs/usr/share/synapseos/logo.txt

The geometry is the dendrite mark from synui/data/logo.svg -- the containing
triangle, the soma at (0,-48), and the five branches -- so the console mark and
the vector mark are the same drawing.

TWO CONSUMERS, AND THE SECOND ONE DICTATES THE STYLE
----------------------------------------------------
  - fastfetch prints this verbatim (logo.type = file-raw).
  - areofyl/fetch copies it to ~/.config/fetch/logo.txt and does NOT print it.
    It turns the art into a HEIGHT MAP -- char_weight_utf8() maps each glyph to
    a Z height -- lights it, and spins it. So the choice of characters is not
    decoration here, it is geometry.

THE WEIGHT BAND IS THE WHOLE TRICK
----------------------------------
fetch's build_points() auto-scales depth: it measures the standard deviation of
the height map and, when stddev < 0.25, multiplies depth by up to 3x to stop
flat logos looking flat. Art therefore has to sit in a NARROW, LOW band to come
out as a smooth solid; anything with a big spread gets no boost and renders as
jagged spikes.

fastfetch's own Arch logo -- the one that looks right in fetch -- uses only:

    `  0.08   .  0.10   /  0.12   -  0.14
    :  0.18   +  0.22   s  0.30   o  0.38

on a FILLED shape. This file uses the same ramp for the same reason. Attempts
that ignored this all failed, and each failed differently:

    half-block Tux (all U+2580)  every cell 0.50 -> zero variance -> flat slab
    luminance relief of Tux      spread 0.40..1.00 -> no boost   -> spiky mush
    the SYN wordmark             block 1.00 vs box-draw 0.20     -> a cliff

So: filled, low, narrow. Not line art, and not the top of the ramp.
"""

import math

W, H = 38, 19

# Straight out of synui/data/logo.svg.
TRIANGLE = [(0, -464), (400, 272), (-400, 272)]
SOMA = (0.0, -48.0)
BRANCHES = [(0, -416), (288, 192), (-288, 192), (176, 200), (-176, 200)]

X0, X1, Y0, Y1 = -430.0, 430.0, -490.0, 300.0

# Cyan, matching the wordmark this replaced and the panel accent.
ACCENT, RESET = "\033[38;5;51m", "\033[0m"


def cell(x, y):
    return ((x - X0) / (X1 - X0) * (W - 1), (y - Y0) / (Y1 - Y0) * (H - 1))


T = [cell(*p) for p in TRIANGLE]
S = cell(*SOMA)
ENDS = [cell(*e) for e in BRANCHES]


def _side(p, a, b):
    return (p[0] - b[0]) * (a[1] - b[1]) - (a[0] - b[0]) * (p[1] - b[1])


def inside(p):
    d = [_side(p, T[0], T[1]), _side(p, T[1], T[2]), _side(p, T[2], T[0])]
    return not (any(v < 0 for v in d) and any(v > 0 for v in d))


def _seg_dist(p, a, b):
    vx, vy = b[0] - a[0], b[1] - a[1]
    wx, wy = p[0] - a[0], p[1] - a[1]
    t = max(0.0, min(1.0, (wx * vx + wy * vy) / (vx * vx + vy * vy)))
    return math.hypot(wx - t * vx, wy - t * vy)


def edge_dist(p):
    # Only the two SLANTED edges. The base is horizontal, so tapering against
    # it put every cell of the bottom row within a fraction of an edge and
    # collapsed the whole row into backticks.
    return min(_seg_dist(p, T[0], T[1]), _seg_dist(p, T[2], T[0]))


def branch_dist(p):
    d = min(_seg_dist(p, S, e) for e in ENDS)
    # The soma is a node, not a line, so it gets its own falloff -- otherwise
    # the branch lines alone leave the centre no denser than its arms.
    return min(d, math.hypot(p[0] - S[0], p[1] - S[1]) * 0.8)


def glyph(p):
    bd, ed = branch_dist(p), edge_dist(p)
    if bd < 0.9:
        return "o"          # the dendrite itself, the raised part
    if bd < 1.6:
        return "s"          # its shoulder, so the arms are not a hard step
    if ed < 0.6:
        return "`"          # ...and the triangle tapers out to its edges
    if ed < 1.1:
        return "."
    if ed < 1.7:
        return ":"
    return "+"


def main():
    rows = []
    for r in range(H):
        row = "".join(glyph((c, r)) if inside((c, r)) else " " for c in range(W))
        rows.append(row.rstrip())

    # Trim blank rows at BOTH ends. The apex lands partway into row 2, so the
    # grid always has a couple of empty rows above it -- and a blank line at the
    # top of this file is not padding, it displaces the whole mark downwards
    # against the info column that fastfetch prints beside it. Leading blanks
    # were the bug; trailing ones only made the block taller than the art.
    while rows and not rows[0]:
        rows.pop(0)
    while rows and not rows[-1]:
        rows.pop()

    print(ACCENT + "\n".join(rows) + RESET)


if __name__ == "__main__":
    main()
