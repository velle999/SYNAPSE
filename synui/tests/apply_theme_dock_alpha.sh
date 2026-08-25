#!/bin/sh
# apply_theme_dock_alpha.sh — the theme's own dock alpha, for the desktop
# widgets that cannot ask a running compositor.
#
# theme_dock_alpha() (synui.h) mirrors theme_bar_alpha() exactly: negative
# means "this theme has no opinion", and the two Prisms are currently the only
# ones with one (0.05, the frosted floor). The dock BODY resolves this
# in-process and needs nothing from this file; the desktop WIDGETS are
# quickshell's own process and had no theme-aware fallback at all before this
# argument existed — BarConfig.dockOpacity's declared default was a flat 0.72
# regardless of theme, so a Prism desktop that had never touched Dock opacity
# came up with a frosted dock beside solid 0.72 widgets. This is the helper's
# half of the fix: a 16th positional argument, written into theme.json's
# dockAlpha exactly the way bar_alpha reaches barAlpha.
#
# Unlike bar_alpha, the fallback is ONE constant rather than a per-scheme pair
# — SYN_DOCK_ALPHA_DEFAULT (0.72) was always a flat number regardless of light
# or dark, so that is what "no opinion" keeps here too.
#
# HOME points at a scratch directory throughout, so a test about the desktop's
# config cannot write the desktop's config on the machine running it.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

helper=${1:?usage: apply_theme_dock_alpha.sh <systemd/synui-apply-theme.sh>}

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

JSON="$tmp/.config/synui/theme.json"

# Stubs for everything the helper would otherwise push at a live session.
mkdir -p "$tmp/bin"
for t in gsettings kwriteconfig6 kwriteconfig5 synui-firefox-glass dbus-send pkill; do
    printf '#!/bin/sh\nexit 0\n' > "$tmp/bin/$t"
    chmod +x "$tmp/bin/$t"
done

# scheme, then bar_alpha (arg 15, always "-" here — this test is about the
# dock argument one slot later), then dock_alpha.
run() {  # run <scheme> [dock_alpha]
    scheme=$1; shift
    HOME="$tmp" PATH="$tmp/bin:/usr/bin:/bin" \
        sh "$helper" "$scheme" 0 122 255 0 86 214 245 245 247 29 29 31 off - \
        ${1:+"$1"} >"$tmp/out" 2>"$tmp/err"
}

count() {  # count <pattern-args...> — 0 when the file is missing too
    n=$(grep -c "$@" 2>/dev/null) || n=0
    printf '%s' "$n"
}
json_dock_alpha() { sed -n 's/.*"dockAlpha": *\([0-9.]*\),.*/\1/p' "$JSON"; }

# ── omitted: the compiled default, on EITHER scheme ──────────
run light
check "light scheme, no argument → 0.72 in theme.json" 0.72 "$(json_dock_alpha)"

run dark
check "dark scheme, no argument → 0.72 too — not per-scheme like bar" \
      0.72 "$(json_dock_alpha)"

# ── "-" is the same as omitted, not a value ──────────────────
run light -
check "'-' leaves the compiled default alone"          0.72 "$(json_dock_alpha)"

# ── a theme with an opinion (Prism's frosted floor) ──────────
run dark 0.05
check "0.05 (Prism) reaches theme.json"                0.05 "$(json_dock_alpha)"

run light 0.00
check "0.00 reaches theme.json"                        0.00 "$(json_dock_alpha)"

run dark 1
check "a bare 1 is accepted"                           1 "$(json_dock_alpha)"

# ── malformed: refused, and the palette survives ─────────────
run light '0.5); background: red; ('
check "an injected value is refused"                   1 \
      "$(count -F 'ignoring dock_alpha' "$tmp/err")"
check "…and the compiled default is what lands"        0.72 "$(json_dock_alpha)"
check "…and nothing of it reaches theme.json"          0 \
      "$(count -F 'background: red' "$JSON")"

run light 2.0
check "an out-of-range value is refused"               1 \
      "$(count -F 'ignoring dock_alpha' "$tmp/err")"
check "…and the compiled default is what lands"        0.72 "$(json_dock_alpha)"

# The palette has to still PARSE after all of that — the whole reason the
# value is validated is that Theme.qml drops every colour on a malformed file.
if command -v python3 >/dev/null 2>&1; then
    ok=$(python3 -c 'import json,sys; json.load(open(sys.argv[1])); print(1)' \
         "$JSON" 2>/dev/null) || ok=0
    check "theme.json is still valid JSON"             1 "$ok"
fi

if [ "$fails" -eq 0 ]; then
    printf 'apply_theme_dock_alpha: all checks passed\n'
    exit 0
fi
printf 'apply_theme_dock_alpha: %d check(s) failed\n' "$fails"
exit 1
