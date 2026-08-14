#!/usr/bin/env python3
"""theme_contrast.py — every colour has to be legible on the surface it lands on

Two palettes are checked, both parsed from the source that ships them, because
the bug this exists for is a colour that was *chosen* against one background and
*drawn* on another. Nothing here needs a compositor: these are numbers in a table
and the arithmetic is WCAG's.

1. panel_accent, against its own theme's panel.

   synui's menus, the control panel, the calendar and the overlays all draw
   their headers, selections and rules in panel_accent, on a surface derived
   from the theme's base colour. Every preset's value was picked against a DARK
   panel — the comments said so, in as many words — and the three presets whose
   scheme is "light" kept that assumption. Win95 shipped a light blue on #C0C0C0
   silver at 1.53:1, which is the calendar nobody could read.

2. kitty's light ANSI sixteen, against the surface it is composited onto.

   These were measured against solid #C0C0C0 and are drawn on #C0C0C0 at the
   0.90 alpha floor synui-glass imposes on a light scheme — over whatever the
   wallpaper is. Black is the worst case and the one the floor was designed
   around, so black is what they are measured over.

   The bright half is held to a lower bar than the normal half on purpose: a
   terminal needs sixteen DISTINGUISHABLE colours, and demanding 4.5:1 of all of
   them collapses the two halves into each other.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
"""
import re
import sys

# WCAG 2.1 relative luminance and contrast ratio.
def _chan(v):
    return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4

def lum(rgb):
    r, g, b = (_chan(c) for c in rgb)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b

def contrast(a, b):
    la, lb = lum(a), lum(b)
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)

def hexrgb(s):
    s = s.lstrip('#')
    return [int(s[i:i + 2], 16) / 255 for i in (0, 2, 4)]

fails = []
passes = 0

def check(desc, got, floor):
    global passes
    if got >= floor:
        print('  ok    %-58s %5.2f:1' % (desc, got))
        passes += 1
    else:
        print('  FAIL  %-58s %5.2f:1 (needs %.1f)' % (desc, got, floor))
        fails.append(desc)

# ── 1. panel_accent on its own panel ────────────────────────────────────────
# 4.5:1 is WCAG AA for body text, and panel_accent is used for text — the
# calendar's weekday row and its selected day, the control panel's section
# headers. Not a decorative rule that could take 3:1.
ACCENT_FLOOR = 4.5

def check_presets(path):
    src = open(path, encoding='utf-8').read()
    blocks = re.findall(r'\[(SYN_THEME_\w+)\]\s*=\s*\{(.*?)\n    \},', src, re.S)
    if not blocks:
        print('  FAIL  parsed no presets out of %s' % path)
        fails.append('preset parse')
        return
    print('panel_accent, on the panel its own theme paints (>= %.1f:1)' % ACCENT_FLOOR)
    for name, body in blocks:
        acc = re.search(r'\.panel_accent\s*=\s*\{([^}]*)\}', body)
        base = re.search(r'\.base_r\s*=\s*(\d+),\s*\.base_g\s*=\s*(\d+),'
                         r'\s*\.base_b\s*=\s*(\d+)', body)
        scheme = re.search(r'\.scheme\s*=\s*"(\w+)"', body)
        if not (acc and base and scheme):
            continue
        a = [float(x.strip().rstrip('f')) for x in acc.group(1).split(',')[:3]]
        b = [int(x) / 255 for x in base.groups()]
        short = name.replace('SYN_THEME_', '').lower()
        check('%-12s (%s)' % (short, scheme.group(1)), contrast(a, b), ACCENT_FLOOR)

# ── 2. kitty's light sixteen, where they are actually drawn ─────────────────
# #C0C0C0 is the darkest light base any shipped preset uses (95's silver; XP's
# beige and bubblegum's pink are paler and only gain). 0.90 is synui-glass's
# light-scheme floor, and black is the worst wallpaper under it.
SILVER = hexrgb('c0c0c0')
FLOOR_ALPHA = 0.90
COMPOSITED = [FLOOR_ALPHA * c for c in SILVER]     # over black

NORMAL_FLOOR = 3.5    # after compositing; these clear 4.5 on the solid surface
BRIGHT_FLOOR = 3.5

def check_kitty(path):
    src = open(path, encoding='utf-8').read()
    # The light set is the `else` arm of the scheme test — take the LAST
    # ansi="..." assignment, which is that one. Anchored on the assignment so a
    # stray colorN elsewhere in the file cannot be picked up.
    arms = re.findall(r'ansi="(.*?)"', src, re.S)
    if len(arms) < 2:
        print('  FAIL  parsed %d ansi palettes out of %s (expected 2)'
              % (len(arms), path))
        fails.append('ansi parse')
        return
    cols = dict(re.findall(r'color(\d+)\s+(#[0-9a-fA-F]{6})', arms[-1]))
    if len(cols) != 16:
        print('  FAIL  light palette has %d colours, expected 16' % len(cols))
        fails.append('ansi count')
        return

    print()
    print('kitty light ANSI, on #C0C0C0 at the %.2f glass floor over black'
          % FLOOR_ALPHA)
    for n in range(16):
        c = cols.get(str(n))
        floor = BRIGHT_FLOOR if 8 <= n <= 14 else NORMAL_FLOOR
        check('color%-2d %s' % (n, c), contrast(hexrgb(c), COMPOSITED), floor)

    # The rule the comment states, asserted rather than described. A bright
    # colour LIGHTER than its normal counterpart is the wrong direction on a
    # pale surface, and every one of the seven used to be.
    #
    # color0/color8 is exempt and is the only exemption: black has nowhere
    # deeper to go, and 8 has to stay distinguishable from 0.
    print()
    print('the bright half is DEEPER than the normal half (pale surface)')
    global passes
    for n in range(1, 7):
        ln, lb = lum(hexrgb(cols[str(n)])), lum(hexrgb(cols[str(n + 8)]))
        desc = 'color%d is deeper than color%d' % (n + 8, n)
        if lb < ln:
            print('  ok    %s' % desc)
            passes += 1
        else:
            print('  FAIL  %s — bright is LIGHTER (%.4f vs %.4f)' % (desc, lb, ln))
            fails.append(desc)

def main():
    if len(sys.argv) != 3:
        print('usage: theme_contrast.py <src/theme.c> <systemd/synui-apply-theme.sh>')
        return 2
    check_presets(sys.argv[1])
    check_kitty(sys.argv[2])
    print()
    if fails:
        print('theme_contrast: %d of %d check(s) failed' % (len(fails), passes + len(fails)))
        return 1
    print('theme_contrast: all %d checks passed' % passes)
    return 0

if __name__ == '__main__':
    sys.exit(main())
