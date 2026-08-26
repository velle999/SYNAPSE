#!/bin/sh
# panel_chrome.sh — every panel gets the corner radius and the frosted glass
#
# ⛔ THE SCREENSAVER PANEL NEVER HAD EITHER. synui_ui_apply_chrome() walks a
# hand-written roster of panels, sets the theme's corner radius on each one's
# background rect and hangs a backdrop blur behind it. `saver` was not on that
# roster, so the screensaver settings panel came up SQUARE while every other
# panel was rounded, and with nothing frosted behind it — which at the 0.94
# alpha they all share means you read the window underneath straight through
# it. Reported as "the screensaver screen isn't following the theme quite the
# same, more transparent than the rest". Both halves, and neither is a colour.
#
# ⚠ IT IS INVISIBLE FROM INSIDE THE PANEL. Nothing fails, nothing logs, and the
# panel looks perfectly reasonable on its own — it only reads as wrong beside
# another one. A roster that must be kept in step with a set defined somewhere
# else is exactly the shape that goes stale silently, so this is the check that
# it has not.
#
# The rule: every `s-><name>_ui.bg` in render.c is either ON the roster or in
# the EXCLUDED list below, which is short, closed, and has a reason per entry.
#
# Pure text — no compositor, no output, no client — so it runs anywhere.
#
# Usage: panel_chrome.sh [path/to/render.c]
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
render=${1:-$here/../src/render.c}
[ -f "$render" ] || { echo "  ABORT no render.c at $render"; exit 1; }

fails=0

# Full-screen backgrounds, and ONLY full-screen backgrounds. A dim the size of
# the output has no corners to round — rounding it would cut a transparent
# notch out of each corner of the SCREEN — and nothing to frost, since the blur
# would be sampling the very thing it is drawn over.
EXCLUDED="overview appgrid"

# The roster, read out of the PANEL_FULL/PANEL_BG macros. Scoped to the array
# rather than the whole file: the macros are #undef'd right after it, so a
# whole-file grep would be reading the same lines anyway — but the scope is
# what keeps this honest if the macros are ever reused elsewhere.
listed=$(sed -n '/const struct panel_chrome panels\[\]/,/^    };/p' "$render" |
         grep -oE 'PANEL_(FULL|BG)\([a-z_]+\)' |
         sed -e 's/PANEL_[A-Z]*(//' -e 's/)//' | sort -u)

n_listed=$(printf '%s\n' "$listed" | grep -c .)
echo "=== $n_listed panels on the chrome roster ==="
if [ "$n_listed" -lt 20 ]; then
    echo "  ABORT only $n_listed entries parsed out of $render — the extractor is"
    echo "        broken, and every check below would pass for the wrong reason."
    exit 1
fi

# Every panel that HAS a background rect to give chrome to.
have=$(grep -oE 's->[a-z_]+_ui\.bg' "$render" |
       sed -e 's/s->//' -e 's/_ui\.bg//' | sort -u)

n_have=$(printf '%s\n' "$have" | grep -c .)
if [ "$n_have" -lt "$n_listed" ]; then
    echo "  ABORT $n_have panels own a bg rect but $n_listed are on the roster —"
    echo "        the roster names something that does not exist, so the two"
    echo "        lists are not comparable and the diff below is meaningless."
    exit 1
fi

for p in $have; do
    if printf '%s\n' "$listed" | grep -qx "$p"; then
        printf '  ok    %-12s rounded and frosted\n' "$p"
        continue
    fi
    excused=0
    for e in $EXCLUDED; do [ "$p" = "$e" ] && excused=1; done
    if [ "$excused" = 1 ]; then
        printf '  ok    %-12s full-screen, deliberately bare\n' "$p"
        continue
    fi
    printf '  FAIL  %-12s has a background rect and is on NEITHER list — it\n' "$p"
    echo   "        will come up square-cornered with no glass behind it, which"
    echo   "        reads as a panel that ignores the theme and is more"
    echo   "        transparent than every other one. Add PANEL_FULL($p) to the"
    echo   "        roster, or to EXCLUDED here with the reason, if its"
    echo   "        background really does cover the whole output."
    fails=$((fails + 1))
done

# And the other direction: an entry on the roster whose panel has lost its bg
# rect is a silent no-op, and the next reader would take it as proof the panel
# is covered.
for p in $listed; do
    printf '%s\n' "$have" | grep -qx "$p" && continue
    printf '  FAIL  %-12s is on the roster but owns no `%s_ui.bg` — the loop\n' "$p" "$p"
    echo   "        skips a NULL, so this entry does nothing and only looks"
    echo   "        like coverage."
    fails=$((fails + 1))
done

# The exclusions have to stay honest too: a panel excused for being full-screen
# must actually size its rect to the output.
for e in $EXCLUDED; do
    if grep -q "s->${e}_ui\.bg" "$render"; then
        if sed -n "/synui_render_${e}(/,/^}/p" "$render" |
           grep -q "ob\.width, ob\.height"; then
            printf '  ok    %-12s exclusion checks out (sized to the output)\n' "$e"
        else
            printf '  FAIL  %-12s is excused as full-screen but its bg is not\n' "$e"
            echo   "        sized to the output box — the excuse has expired."
            fails=$((fails + 1))
        fi
    else
        printf '  FAIL  %-12s is in EXCLUDED but owns no bg rect at all\n' "$e"
        fails=$((fails + 1))
    fi
done

if [ "$fails" -gt 0 ]; then
    echo "panel_chrome: $fails problem(s)"
    exit 1
fi
echo "all checks passed"
