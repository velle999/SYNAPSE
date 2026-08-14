#!/usr/bin/env python3
"""theme_contrast.py - colours drawn on a surface they were not chosen against

The bug this exists for is one mistake in two places: a colour picked against
one background and drawn on another. synui has a runtime corrector for exactly
that (src/contrast.c, covered by tests/panel_contrast_test.c), so anything
reaching the screen through set_accent() or set_ink() is already handled and is
NOT re-checked here. What is checked is the two ways round it.

1. A literal the ink ladder never sees.

   The calendar drew its day numbers as a hardcoded near-white, from when every
   panel was the same near-black navy. 13.6:1 there, 1.31:1 on 95's silver - and
   invisible to the corrector, because a literal is not a rung. That was the
   calendar nobody could read; the accent behind it had been corrected all
   along, which is why looking at the accent first was looking in the wrong
   place.

2. kitty's light ANSI sixteen, against the surface it is composited onto.

   These were measured against solid #C0C0C0 and are drawn on #C0C0C0 at the
   0.90 alpha floor synui-glass imposes on a light scheme - over whatever the
   wallpaper happens to be. Black is the worst case and the one the floor was
   designed around, so black is what they are measured over.

   The bright half is held to the same bar as the normal half but reached from
   the other side: on a pale surface "brighter" has to mean DEEPER, or the two
   halves converge and sixteen colours become eight.

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

def ok(desc):
    global passes
    print('  ok    %s' % desc)
    passes += 1

def bad(desc):
    print('  FAIL  %s' % desc)
    fails.append(desc)

def check(desc, got, floor):
    if got >= floor:
        ok('%-52s %5.2f:1' % (desc, got))
    else:
        bad('%-52s %5.2f:1 (needs %.1f)' % (desc, got, floor))

# -- 1. a panel literal the ink ladder never sees ----------------------------
# set_ink is a POSITION between the panel and its ink, so it flips with the
# theme and is floored by syn_ink_floor(); a cairo_set_source_rgba literal can
# do neither. This asserts the calendar uses the ladder rather than re-deriving
# the arithmetic, which contrast.c owns and panel_contrast_test.c already tests.
def check_calendar(path):
    src = open(path, encoding='utf-8').read()
    print('the calendar draws its day numbers through the ink ladder')
    m = re.search(r'void synui_render_calendar\(.*?\n\}', src, re.S)
    if not m:
        bad('could not find synui_render_calendar in %s' % path)
        return
    body = m.group(0)
    # ANY cairo_set_source_rgba here is the bug, whatever its arguments look
    # like. The first version of this check looked for a literal starting with a
    # DIGIT and found neither of the two that were actually there — both begin
    # with a ternary (`is_sel ? 0.95 : 0.82`, `i == 0 || i == 6 ? 0.72 : 0.55`),
    # so it passed against the unfixed source and would have shipped as
    # decoration. set_ink/set_accent/panel_bg_color are the sanctioned ways to
    # colour this panel and none of them is this call.
    lits = re.findall(r'cairo_set_source_rgba\s*\(', body)
    if lits:
        bad('%d cairo_set_source_rgba call(s) in the calendar - use the ladder'
            % len(lits))
    else:
        ok('no colour set outside set_ink/set_accent')
    # Both text runs: the day numbers and the weekday header row.
    n_ink = len(re.findall(r'set_ink\s*\(\s*cr\s*,', body))
    if n_ink >= 3:
        ok('day numbers, weekday headers and the hint all use set_ink (%d)' % n_ink)
    else:
        bad('only %d set_ink call(s) in the calendar, expected 3' % n_ink)

# -- 2. kitty's light sixteen, where they are actually drawn ------------------
# #C0C0C0 is the darkest light base any shipped preset uses (95's silver; XP's
# beige and bubblegum's pink are paler and only gain contrast). 0.90 is
# synui-glass's light-scheme floor, and black is the worst wallpaper under it.
SILVER = hexrgb('c0c0c0')
FLOOR_ALPHA = 0.90
COMPOSITED = [FLOOR_ALPHA * c for c in SILVER]     # over black

FLOOR = 3.5

def check_kitty(path):
    src = open(path, encoding='utf-8').read()
    # The light set is the `else` arm of the scheme test, so it is the LAST
    # ansi="..." assignment. Anchored on the assignment so a stray colorN
    # elsewhere in the file cannot be picked up.
    arms = re.findall(r'ansi="(.*?)"', src, re.S)
    if len(arms) < 2:
        bad('parsed %d ansi palettes out of %s (expected 2)' % (len(arms), path))
        return
    cols = dict(re.findall(r'color(\d+)\s+(#[0-9a-fA-F]{6})', arms[-1]))
    if len(cols) != 16:
        bad('light palette has %d colours, expected 16' % len(cols))
        return

    print('kitty light ANSI, on #C0C0C0 at the %.2f glass floor over black'
          % FLOOR_ALPHA)
    for n in range(16):
        c = cols.get(str(n))
        check('color%-2d %s' % (n, c), contrast(hexrgb(c), COMPOSITED), FLOOR)

    # The rule the comment states, asserted rather than described. Every bright
    # colour used to ship LIGHTER than its normal counterpart - all seven - which
    # is the wrong direction on a pale surface and is exactly what the comment
    # above them said not to do.
    #
    # color0/color8 is exempt and is the only exemption: black has nowhere
    # deeper to go, and 8 has to stay distinguishable from 0.
    print()
    print('the bright half is DEEPER than the normal half (pale surface)')
    for n in range(1, 7):
        ln, lb = lum(hexrgb(cols[str(n)])), lum(hexrgb(cols[str(n + 8)]))
        desc = 'color%d is deeper than color%d' % (n + 8, n)
        if lb < ln:
            ok(desc)
        else:
            bad('%s - bright is LIGHTER (%.4f vs %.4f)' % (desc, lb, ln))

def main():
    if len(sys.argv) != 3:
        print('usage: theme_contrast.py <src/render.c> <systemd/synui-apply-theme.sh>')
        return 2
    check_calendar(sys.argv[1])
    print()
    check_kitty(sys.argv[2])
    print()
    if fails:
        print('theme_contrast: %d of %d check(s) failed'
              % (len(fails), passes + len(fails)))
        return 1
    print('theme_contrast: all %d checks passed' % passes)
    return 0

if __name__ == '__main__':
    sys.exit(main())
