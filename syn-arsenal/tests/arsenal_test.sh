#!/usr/bin/env bash
#
# arsenal_test — covers the parts of SYNAPSE Arsenal that can be checked without
# the BlackArch repository being enabled on the build machine. The fixture is
# what makes that possible: it is the same TSV arsenal-query emits, so both
# front-ends can be driven end-to-end offline.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$here/.."
fail=0

check() {  # check <label> <expected> <actual>
    if [ "$2" = "$3" ]; then
        printf '  ok    %s\n' "$1"
    else
        printf '  FAIL  %s — expected %s, got %s\n' "$1" "$2" "$3"
        fail=$((fail + 1))
    fi
}

# ── Fixture ─────────────────────────────────────────────────────────────────
F=$(mktemp -d); trap 'rm -rf "$F"' EXIT
printf 'blackarch-scanner\t3\nblackarch-webapp\t2\n' > "$F/categories.tsv"
{
    printf 'nmap\t1\t7.9-1\tNetwork scanner.\n'
    printf 'masscan\t0\t1.3-2\tFast port scanner.\n'
    printf 'zmap\t0\t3.0-1\tInternet-wide scanner.\n'
} > "$F/packages.blackarch-scanner.tsv"

Q="$root/arsenal-query.sh"
export SYN_ARSENAL_FIXTURE="$F"

echo
echo "  arsenal-query"
check "status reports the fixture as enabled" "enabled" \
    "$("$Q" status | cut -f1)"
check "categories are listed" "2" \
    "$("$Q" categories | wc -l)"
check "packages are listed" "3" \
    "$("$Q" packages blackarch-scanner | wc -l)"
check "every package row has 4 fields" "0" \
    "$("$Q" packages blackarch-scanner | awk -F'\t' 'NF!=4' | wc -l)"
check "the installed flag survives the round trip" "1" \
    "$("$Q" packages blackarch-scanner | awk -F'\t' '$1=="nmap"{print $2}')"

# A non-blackarch group must be refused rather than listed: the same code path
# would otherwise happily render 'gnome' as a security category.
"$Q" packages gnome >/dev/null 2>&1
check "a non-blackarch group is rejected" "1" "$?"
check "a missing group argument is rejected" "1" \
    "$("$Q" packages >/dev/null 2>&1; echo $?)"

# ── TUI ─────────────────────────────────────────────────────────────────────
echo
echo "  arsenal-tui"
export SYN_ARSENAL_QUERY="$Q" NO_COLOR=1 SYN_ARSENAL_PAGE=10
tui_out=$(printf '1\nq\nq\n' | "$root/arsenal-tui.sh" 2>&1 | sed 's/\x1b\[[0-9;]*[A-Za-z]//g')
check "the category list renders" "yes" \
    "$(printf '%s' "$tui_out" | grep -q 'scanner' && echo yes || echo no)"
check "drilling into a category lists its packages" "yes" \
    "$(printf '%s' "$tui_out" | grep -q 'masscan' && echo yes || echo no)"
check "an installed package is marked" "yes" \
    "$(printf '%s' "$tui_out" | grep -q '●.*nmap' && echo yes || echo no)"
check "the blackarch- prefix is stripped from categories" "0" \
    "$(printf '%s' "$tui_out" | grep -c 'blackarch-scanner')"

# ── Repo bootstrap: the check that actually matters ──────────────────────────
#
# arsenal-enable-repo refuses to run a strap.sh that no longer pins BlackArch's
# master key. That predicate is the security boundary of the whole feature, so
# it is exercised directly here against a tampered script.
echo
echo "  arsenal-enable-repo"
FPR=$(grep -oE '[0-9A-F]{40}' "$root/arsenal-enable-repo.sh" | head -1)
check "a master key fingerprint is pinned" "40" "${#FPR}"

printf 'echo hi\n--recv-keys %s\n' "$FPR" > "$F/good-strap.sh"
printf 'echo hi\n--recv-keys DEADBEEF00000000000000000000000000000000\n' > "$F/evil-strap.sh"
check "a strap.sh pinning our key passes the check" "0" \
    "$(grep -qF "$FPR" "$F/good-strap.sh"; echo $?)"
check "a strap.sh pinning a DIFFERENT key is refused" "1" \
    "$(grep -qF "$FPR" "$F/evil-strap.sh"; echo $?)"
check "enable-repo requires root" "1" \
    "$("$root/arsenal-enable-repo.sh" >/dev/null 2>&1; echo $?)"

# strap.sh untars the keyring straight into /usr/share/pacman/keyrings, leaving
# three files owned by no package — so a plain `pacman -S blackarch-keyring`
# afterwards dies with "exists in filesystem". Every site that installs the
# keyring must scope an --overwrite over those paths, or enabling the repo and
# then owning its keyring can never both succeed on the same machine.
ka=$(grep -c -- "--overwrite '/usr/share/pacman/keyrings/blackarch\*'" "$root/arsenal-enable-repo.sh")
check "every keyring install in enable-repo scopes --overwrite" "2" "$ka"
# Counted against the INVOCATIONS, not against mentions of the package name:
# comments and the `pacman -Q` presence check name it too, and counting those
# made this assert 6 == 2 and fail for no real reason.
ki=$(grep -c 'pacman -S --noconfirm --needed' "$root/arsenal-enable-repo.sh")
check "no keyring install site was missed" "$ki" "$ka"

# ── Palette ─────────────────────────────────────────────────────────────────
#
# The GUI once rendered black text on a near-black background and an accent that
# painted as nothing, which together made the window look empty and made a
# category click look like it had not registered. Neither is visible to any test
# that does not open a window, so both are pinned structurally here.
echo
echo "  arsenal.qml palette"
QML="$root/arsenal.qml"

# theme.json writes accent/glyph/bar/popup as [r,g,b] ARRAYS and only fg/clockFg
# as hex strings. Handing a QML `color` an array fails to convert and the colour
# paints as nothing, so every array key must go through themed().
check "no array palette key is read as a colour directly" "0" \
    "$(grep -cE '(^|[^a-zA-Z])p\.(accent|glyph|bar|popup)([^a-zA-Z]|$)' "$QML")"
check "the array keys go through themed()" "2" \
    "$(grep -cE 'themed\("(bar|accent)"' "$QML")"

# Ink and surface must come from the same place. Taking fg from the theme while
# hardcoding the background is exactly what produced black-on-black.
check "surfaces are themed, not hardcoded" "1" \
    "$(grep -c 'cPanel: themed("bar"' "$QML")"
check "the ink is contrast-guarded against the background" "1" \
    "$(grep -c 'contrast(cInk, cBg) >= 4.5' "$QML")"
check "the accent is contrast-guarded against the chrome" "1" \
    "$(grep -c 'readable(cAccentRaw, cPanel, 4.5)' "$QML")"

# ── arsenal.qml font ────────────────────────────────────────────────────────
# The desktop font is ~/.config/synui/font.state, not theme.json, and it carries
# a SCALE as well as a family. This window read neither until 2026-08-11: the
# control panel's font picker moved Settings and Files while Arsenal and
# Software stayed on the startup face, which reads as "the theming missed those
# apps". Qt resolves an application font once at startup, so both have to be
# bindings on every Text — a bare `font.pixelSize: 13` is the regression.
echo
echo "  arsenal.qml font"
check "the desktop font file is watched" "1" \
    "$(grep -c 'config/synui/font.state' "$QML")"
check "the scale is read from the same file" "1" \
    "$(grep -c 'root.textScale = s' "$QML")"
# Every size goes through ui(). The monospace hint is exempt from the FAMILY
# rule — a command to type is not prose — but not from the size rule.
#
# awk rather than `grep -c ... | grep -vc ...`: grep exits 1 on no matches, and
# under pipefail a correct zero would be read as a failed check.
check "no pixel size bypasses ui()" "0" \
    "$(awk '/pixelSize: *[0-9]/ { n++ } END { print n + 0 }' "$QML")"
check "every literal family is the deliberate monospace" "0" \
    "$(awk '/family: *"/ && !/family: *"monospace"/ { n++ } END { print n + 0 }' "$QML")"

# ── Launcher ────────────────────────────────────────────────────────────────
echo
echo "  syn-arsenal"
check "--help works" "0" \
    "$("$root/syn-arsenal.sh" --help >/dev/null 2>&1; echo $?)"
# No display must degrade to the terminal browser, not die with a Wayland error.
out=$(env -u WAYLAND_DISPLAY -u DISPLAY sh -c "printf 'q\n' | '$root/syn-arsenal.sh'" 2>&1 \
      | sed 's/\x1b\[[0-9;]*[A-Za-z]//g')
check "no display falls back to the terminal browser" "yes" \
    "$(printf '%s' "$out" | grep -q 'falling back to the terminal' && echo yes || echo no)"


# ── the wallpaper's accent reaches this window ──────────────────────────────
#
# 387 gave the BAR the colour synui measures off the wallpaper, and only the
# bar: every app window beside it kept the preset's accent, so a desktop with
# the switch on wore two colours at once — the picture's on the bar, the
# theme's on Files, Software, Disks and the rest. These windows read
# ~/.config/synui/palette.state now, and this is the check that they do.
#
# ⚠ IT LOADS THE FILE IN A REAL ENGINE rather than grepping for the property.
# A duplicate property name is the trap this feature has sprung before: the
# file PARSES, qmllint is happy, and the type then refuses to LOAD, naming a
# line that is not the one at fault. Only running it can tell.
#
# Three cases, because two of them are the ones already got wrong once:
#   use=yes  the MEASURED colour;
#   use=no   the theme's own, because `use` is the SETTING and synui writes
#            the file whichever way it is set — reading the colour without
#            checking it is how the bar came to wear a wallpaper on themes
#            that never asked for one (386);
#   ok=no    the theme's own, the picture having no usable hue to give.
if [ -f "$QML" ] && command -v quickshell >/dev/null 2>&1; then
    WPT=$(mktemp -d)
    mkdir -p "$WPT/home/.config/synui" "$WPT/run"
    # A preset accent that is nothing like the measured one, so "it took the
    # wallpaper's" and "it kept the theme's" cannot be confused for each other.
    cat > "$WPT/home/.config/synui/theme.json" <<'WPJSON'
{ "scheme": "dark", "accent": [0,214,229], "glyph": [0,214,229],
  "bar": [25,28,35], "popup": [17,21,28], "fg": "#c8e3ee" }
WPJSON
    # A COPY with a probe timer appended INSIDE the root object — outside its
    # final brace the file is a syntax error and this would "fail" on a QML
    # that is perfectly good.
    awk 'BEGIN{RS="\0"} {
            n = match($0, /}[ \t\r\n]*$/)
            printf "%s\n    Timer { running: true; interval: 1200; repeat: false;\n             onTriggered: { console.log(\"WPACCENT=\" + root.cAccent); Qt.quit() } }\n%s", substr($0,1,n-1), substr($0,n)
         }' "$QML" > "$WPT/probe.qml"
    # ⚠ QT_ASSUME_STDERR_HAS_CONSOLE=1, or console.log() prints NOTHING at all
    # and every case below reads as an empty accent — a green suite that tested
    # the engine's silence. GSETTINGS_BACKEND=memory because the fake HOME has
    # no dconf for Qt's platform theme to find.
    wp_accent() {  # wp_accent <use> <ok> -> the colour the window resolves
        printf 'use=%s\nok=%s\naccent=#6479FF\naccent_dim=#37438C\nsecondary=#C68F14\n' \
               "$1" "$2" > "$WPT/home/.config/synui/palette.state"
        HOME="$WPT/home" XDG_RUNTIME_DIR="$WPT/run" QT_QPA_PLATFORM=offscreen \
        GSETTINGS_BACKEND=memory QT_ASSUME_STDERR_HAS_CONSOLE=1 \
        timeout 30 quickshell -p "$WPT/probe.qml" 2>&1 |
            sed -n 's/.*WPACCENT=\(#[0-9a-fA-F]*\).*/\1/p' | head -1
    }
    check "the measured wallpaper accent reaches the window" "#6479ff" \
          "$(wp_accent yes yes)"
    check "wallpaper_accent off leaves the theme's accent alone" "#00d6e5" \
          "$(wp_accent no yes)"
    check "a wallpaper with no usable hue falls back to the theme" "#00d6e5" \
          "$(wp_accent yes no)"
    rm -rf "$WPT"
else
    echo "  skip  quickshell not installed, cannot check the wallpaper accent"
fi

echo
if [ "$fail" -eq 0 ]; then echo "  all checks passed"; else echo "  $fail check(s) FAILED"; fi
echo
exit $(( fail > 0 ))
