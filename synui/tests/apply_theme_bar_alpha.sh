#!/bin/sh
# apply_theme_bar_alpha.sh — the theme's own bar alpha, and the one bar it is
# allowed to reach
#
# macOS 26's menu bar has no background at all. Every other theme's bar alpha is
# picked from the SCHEME (0.85 dark, 0.95 light), which is what shipped before
# this argument existed, so the helper now takes a 15th positional argument and
# an out-of-band "-" for "this caller has no opinion" — 0.00 having become a
# value a theme can mean.
#
# What is asserted here is the FILE CONTRACT, because writing those two files is
# the helper's whole job on this path:
#
#   * omitted, and "-", leave the scheme's alpha alone — the Antiquity bar calls
#     this helper with nothing but colours
#   * a value reaches theme.json, which is what the quickshell bar reads
#   * it does NOT reach waybar-style.css. This is the deliberate half: Theme.qml
#     flips its ink from backdrop.state to survive a clear background and waybar
#     has no such machinery, so handing waybar 0.00 would leave the Antiquity
#     bar's near-black text on whatever the wallpaper happens to be
#   * a malformed value is refused rather than interpolated. It is pasted into
#     JSON, and Theme.qml falls back WHOLESALE on a parse error — so one bad
#     character here is not one wrong colour, it is the bar's entire palette
#
# HOME points at a scratch directory throughout, so a test about the desktop's
# config cannot write the desktop's config on the machine running it.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

helper=${1:?usage: apply_theme_bar_alpha.sh <systemd/synui-apply-theme.sh>}

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
CSS="$tmp/.config/synui/waybar-style.css"

# Stubs for everything the helper would otherwise push at a live session.
mkdir -p "$tmp/bin"
for t in gsettings kwriteconfig6 kwriteconfig5 synui-firefox-glass dbus-send pkill; do
    printf '#!/bin/sh\nexit 0\n' > "$tmp/bin/$t"
    chmod +x "$tmp/bin/$t"
done

# scheme is the first argument because it is what the alpha defaults FROM.
run() {  # run <scheme> [bar_alpha]
    scheme=$1; shift
    HOME="$tmp" PATH="$tmp/bin:/usr/bin:/bin" \
        sh "$helper" "$scheme" 0 122 255 0 86 214 245 245 247 29 29 31 off \
        ${1:+"$1"} >"$tmp/out" 2>"$tmp/err"
}

# ⚠ `grep -c` PRINTS 0 and EXITS 1 on no match, so `grep -c … || echo 0` emits
# "0\n0". Capture first, default second. (Same trap as the square_chrome test.)
count() {  # count <pattern-args...> — 0 when the file is missing too
    n=$(grep -c "$@" 2>/dev/null) || n=0
    printf '%s' "$n"
}
json_alpha() { sed -n 's/.*"barAlpha": *\([0-9.]*\),.*/\1/p' "$JSON"; }

# ── omitted: the scheme decides, exactly as before ───────────
run light
check "light scheme, no argument → 0.95 in theme.json" 0.95 "$(json_alpha)"
check "…and 0.95 in the waybar CSS"                    1 \
      "$(count -F 'rgba(245,245,247,0.95)' "$CSS")"

run dark
check "dark scheme, no argument → 0.85 in theme.json"  0.85 "$(json_alpha)"

# ── "-" is the same as omitted, not a value ──────────────────
run light -
check "'-' leaves the scheme's alpha alone"            0.95 "$(json_alpha)"

# ── a theme with an opinion ──────────────────────────────────
run light 0.00
check "0.00 reaches theme.json"                        0.00 "$(json_alpha)"
# THE ONE THAT MATTERS. Both bars read a palette from this helper; only the
# quickshell one can pick an ink that survives having no background.
check "0.00 does NOT reach the waybar CSS"             1 \
      "$(count -F 'rgba(245,245,247,0.95)' "$CSS")"
check "…so no clear waybar background is written"      0 \
      "$(count -F 'rgba(245,245,247,0.00)' "$CSS")"

run dark 0.40
check "a mid value reaches theme.json too"             0.40 "$(json_alpha)"
run dark 1
check "a bare 1 is accepted"                           1 "$(json_alpha)"

# ── malformed: refused, and the palette survives ─────────────
run light '0.5); background: red; ('
check "an injected value is refused"                   1 \
      "$(count -F 'ignoring bar_alpha' "$tmp/err")"
check "…and the scheme's alpha is what lands"          0.95 "$(json_alpha)"
check "…and nothing of it reaches theme.json"          0 \
      "$(count -F 'background: red' "$JSON")"

run light 2.0
check "an out-of-range value is refused"               1 \
      "$(count -F 'ignoring bar_alpha' "$tmp/err")"
check "…and the scheme's alpha is what lands"          0.95 "$(json_alpha)"

# The palette has to still PARSE after all of that — the whole reason the value
# is validated is that Theme.qml drops every colour on a malformed file.
if command -v python3 >/dev/null 2>&1; then
    ok=$(python3 -c 'import json,sys; json.load(open(sys.argv[1])); print(1)' \
         "$JSON" 2>/dev/null) || ok=0
    check "theme.json is still valid JSON"             1 "$ok"
fi

if [ "$fails" -eq 0 ]; then
    printf 'apply_theme_bar_alpha: all checks passed\n'
    exit 0
fi
printf 'apply_theme_bar_alpha: %d check(s) failed\n' "$fails"
exit 1
