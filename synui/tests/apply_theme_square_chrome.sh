#!/bin/sh
# apply_theme_square_chrome.sh — a retro chrome has to reach the windows synui
# does not decorate
#
# A Windows 95 desktop showed rounded Firefox corners, and turning the
# compositor's radius off changed nothing, because neither was drawing them.
# synui squares its own chrome already — chrome_corner_radius() returns 0 for
# every style but FLAT — but Firefox never BINDS xdg-decoration, so it keeps its
# GTK frame and draws its own corners from the GTK theme's `decoration` node.
# Measured on a live desktop: Firefox's left edge walked inward one pixel per
# row for eight rows while an SSD window beside it was square.
#
# So the chrome style now travels to synui-apply-theme, which answers it once in
# GTK where every self-decorating client reads it. What is asserted here is the
# FILE CONTRACT, because the helper's whole job is writing that file:
#
#   * `on` writes the managed block, into GTK 3 and GTK 4 alike
#   * running it twice does not write it twice
#   * `off` takes it away again and writes no radius of its own — restoring the
#     theme's corner means getting out of the way, not guessing Adwaita's value
#   * a user's own CSS around the block survives both directions
#   * OMITTING the argument changes nothing, which is not the same as `off`:
#     the Antiquity bar calls this helper with nothing but colours, and a caller
#     that does not know the chrome must not undo the caller that did
#
# HOME points at a scratch directory throughout, so a test about GTK config
# cannot write GTK config on the machine running it.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

helper=${1:?usage: apply_theme_square_chrome.sh <systemd/synui-apply-theme.sh>}

fails=0
check() {  # check <description> <expected> <actual>
    if [ "$2" = "$3" ]; then
        printf '  ok    %s\n' "$1"
    else
        printf '  FAIL  %s — expected %s, got %s\n' "$1" "$2" "$3"
        fails=$((fails + 1))
    fi
}

tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM

G3="$tmp/.config/gtk-3.0/gtk.css"
G4="$tmp/.config/gtk-4.0/gtk.css"
MINE='button { min-height: 20px; }'

mkdir -p "$tmp/.config/gtk-3.0" || exit 1
printf '/* velle wrote this by hand */\n%s\n' "$MINE" > "$G3"

# Every other tool this helper drives is absent from PATH or a no-op on a test
# box; only the GTK half is under test, and the helper has no `set -e` precisely
# so the rest failing cannot stop it. PATH is emptied of the ones that would
# touch a real desktop.
run() {  # run [square]
    HOME="$tmp" PATH="$tmp/bin:/usr/bin:/bin" \
        sh "$helper" dark 61 125 255 61 125 255 192 192 192 0 0 0 ${1:+"$1"} \
        >"$tmp/out" 2>"$tmp/err"
}

# A stub for anything the helper would otherwise push at the live session. It
# answers nothing and records nothing — it exists so those steps are no-ops.
mkdir -p "$tmp/bin"
for t in gsettings kwriteconfig6 kwriteconfig5 synui-firefox-glass dbus-send; do
    printf '#!/bin/sh\nexit 0\n' > "$tmp/bin/$t"
    chmod +x "$tmp/bin/$t"
done

# ⚠ `grep -c` PRINTS 0 and EXITS 1 when it matches nothing, so the obvious
# `grep -c … || echo 0` emits "0\n0" and every zero-expecting check fails while
# the helper is doing exactly the right thing. Capture first, default second.
count() {  # count <pattern-args...> — 0 when the file is missing too
    n=$(grep -c "$@" 2>/dev/null) || n=0
    printf '%s' "$n"
}
blocks()  { count 'synui-apply-theme BEGIN' "$1"; }
squares() { count 'border-radius: 0' "$1"; }

# ── on ───────────────────────────────────────────────────────
run on
check "on writes the managed block to gtk-3.0"  1 "$(blocks "$G3")"
check "on writes the managed block to gtk-4.0"  1 "$(blocks "$G4")"
check "gtk-3.0 gets the decoration rule"        1 \
      "$(count '^decoration, decoration:backdrop { border-radius: 0; }$' "$G3")"
check "gtk-4.0 gets the window.csd rule"        1 \
      "$(count '^window.csd, window.csd:backdrop { border-radius: 0; }$' "$G4")"
check "the user's own CSS survives"             1 \
      "$(count -F "$MINE" "$G3")"

# ── idempotent ───────────────────────────────────────────────
run on
check "a second run does not duplicate the block" 1 "$(blocks "$G3")"
check "...nor the rules inside it"                2 "$(squares "$G3")"
check "...nor the user's CSS"                     1 "$(count -F "$MINE" "$G3")"

# ── omitted is not off ───────────────────────────────────────
run
check "omitting the argument leaves the block alone" 1 "$(blocks "$G3")"
check "...and leaves the rules alone"                2 "$(squares "$G3")"

# ── a value that is neither ──────────────────────────────────
run maybe
check "a value that is not on|off is refused" 1 \
      "$(count 'square_chrome must be on|off' "$tmp/err")"
check "...and changes nothing"                1 "$(blocks "$G3")"

# ── off ──────────────────────────────────────────────────────
run off
check "off removes the block from gtk-3.0" 0 "$(blocks "$G3")"
check "off removes the block from gtk-4.0" 0 "$(blocks "$G4")"
# The corner belongs to the GTK theme once we are out of the way. A radius of
# our own here would be a second bug wearing the first one's clothes.
check "off leaves NO radius behind"        0 "$(squares "$G3")"
check "the user's own CSS still survives"  1 "$(count -F "$MINE" "$G3")"

# ── off, then omitted, stays off ─────────────────────────────
run
check "omitting after off does not bring it back" 0 "$(blocks "$G3")"

if [ "$fails" -eq 0 ]; then
    printf 'apply_theme_square_chrome: all checks passed\n'
    exit 0
fi
printf 'apply_theme_square_chrome: %d check(s) failed\n' "$fails"
exit 1
