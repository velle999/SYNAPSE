#!/usr/bin/env bash
# synui-glass — push the compositor's transparency level into the terminal's own
# background alpha, so a translucent terminal keeps its TEXT crisp ("text on
# glass") instead of the whole window fading together.
#
# synui cannot make a client's text opaque while its background goes see-through:
# it holds one composited buffer per window and one uniform opacity lever. foot,
# though, draws its own background alpha (glyphs stay opaque), so the real glass
# happens app-side. anim.c leaves foot at compositor opacity 1.0 and theme.c
# calls this to set foot's actual alpha to the slider value.
#
# Best effort and idempotent, like synui-apply-theme: a missing foot.ini or a
# foot that isn't running is simply a no-op. No `set -e`.
#
# Usage: synui-glass <alpha 0.00..1.00>
set -u

a=${1:-1.0}
# Clamp to foot's valid range and normalise to two decimals.
a=$(awk -v x="$a" 'BEGIN{ if(x<0)x=0; if(x>1)x=1; printf "%.2f", x }')

ini="$HOME/.config/foot/foot.ini"
[ -f "$ini" ] || exit 0

# Rewrite every `alpha=` line (foot keeps it under [colors-dark]/[colors-light],
# and both should track the same glass level) without touching anything else.
# If the file somehow has no alpha= key, add one to the first [colors*] section.
tmp=$(mktemp) || exit 0
awk -v alpha="$a" '
    /^[[:space:]]*alpha[[:space:]]*=/ { print "alpha=" alpha; next }
    { print }
' "$ini" > "$tmp"

# awk above only rewrites existing keys; if none matched, append one under the
# first colors section so the setting has somewhere to live.
if ! grep -q '^[[:space:]]*alpha[[:space:]]*=' "$tmp"; then
    awk -v alpha="$a" '
        BEGIN { put=0 }
        { print }
        /^[[:space:]]*\[colors/ && !put { print "alpha=" alpha; put=1 }
    ' "$ini" > "$tmp"
fi

if ! cmp -s "$ini" "$tmp"; then
    mv "$tmp" "$ini" || rm -f "$tmp"
else
    rm -f "$tmp"
fi

# foot reloads its config on SIGUSR1, re-applying alpha live. -x so we only hit
# foot itself, never something that merely has "foot" in its args.
pkill -USR1 -x foot 2>/dev/null

exit 0
