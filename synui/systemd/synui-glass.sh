#!/usr/bin/env bash
# synui-glass — push the compositor's transparency level into the terminal's own
# background alpha, so a translucent terminal keeps its TEXT crisp ("text on
# glass") instead of the whole window fading together.
#
# synui cannot make a client's text opaque while its background goes see-through:
# it holds one composited buffer per window and one uniform opacity lever. kitty,
# though, draws its own background alpha (glyphs stay opaque), so the real glass
# happens app-side. anim.c leaves the terminal at compositor opacity 1.0 and
# theme.c calls this to set its actual alpha to the slider value.
#
# Best effort and idempotent, like synui-apply-theme: a missing config or a
# terminal that isn't running is simply a no-op. No `set -e`.
#
# Usage: synui-glass <alpha 0.00..1.00>
set -u

a=${1:-1.0}
# Clamp to the valid range and normalise to two decimals.
a=$(awk -v x="$a" 'BEGIN{ if(x<0)x=0; if(x>1)x=1; printf "%.2f", x }')

# ── kitty ────────────────────────────────────────────────────────────────────
#
# kitty.conf is flat `key value`, NOT foot's INI sections, so there is no
# [colors-dark]/[colors-light] pair to keep in step — one key covers both.
#
# THE THING THAT WILL BITE YOU: `dynamic_background_opacity yes` has to have been
# in the config when kitty STARTED. kitty's own documentation is explicit that
# changing that option by reloading the config is not supported, and that a
# background_opacity change on reload only takes effect if dynamic opacity was
# enabled in the original config. So writing it here and signalling would not
# rescue a kitty that launched without it — that instance needs a restart. Every
# shipped kitty.conf template carries the line for exactly this reason; this
# function adds it too, so a hand-written config picks it up from the next launch
# rather than staying mysteriously inert forever.
kitty_conf="$HOME/.config/kitty/kitty.conf"
if [ -f "$kitty_conf" ]; then
    tmp=$(mktemp) || exit 0

    awk -v alpha="$a" '
        /^[[:space:]]*background_opacity[[:space:]]/        { print "background_opacity " alpha; seen=1; next }
        /^[[:space:]]*dynamic_background_opacity[[:space:]]/ { print "dynamic_background_opacity yes"; dyn=1; next }
        { print }
        END {
            if (!seen) print "background_opacity " alpha
            if (!dyn)  print "dynamic_background_opacity yes"
        }
    ' "$kitty_conf" > "$tmp"

    if ! cmp -s "$kitty_conf" "$tmp"; then
        mv "$tmp" "$kitty_conf" || rm -f "$tmp"
    else
        rm -f "$tmp"
    fi

    # kitty reloads its config on SIGUSR1 — that is what kitty's own
    # reload_conf_in_all_kitties() sends. -x so we only hit kitty itself, never
    # something that merely has "kitty" in its arguments.
    pkill -USR1 -x kitty 2>/dev/null
fi

# ── foot ─────────────────────────────────────────────────────────────────────
#
# Still handled, because kitty became the DEFAULT rather than the only option:
# `terminal = foot` in synuirc remains valid, upgraded systems keep the foot.ini
# they were installed with, and dropping this would silently take glass away from
# them with nothing to explain why.
ini="$HOME/.config/foot/foot.ini"
if [ -f "$ini" ]; then
    tmp=$(mktemp) || exit 0

    # Rewrite every `alpha=` line (foot keeps it under [colors-dark]/[colors-light],
    # and both should track the same glass level) without touching anything else.
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

    # foot reloads its config on SIGUSR1 too.
    pkill -USR1 -x foot 2>/dev/null
fi

exit 0
