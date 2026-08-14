#!/bin/sh
# apply_theme_color_scheme.sh — a light theme has to SAY it is light
#
# Reported as "firefox keeps losing its theme color in 95, it's switching to a
# darker grey", intermittently, for a long time.
#
# Firefox ships the System theme (default-theme@mozilla.org), which follows the
# xdg-desktop-portal's org.freedesktop.appearance color-scheme — which is just
# GNOME's org.gnome.desktop.interface color-scheme re-exported. synui-apply-theme
# set that to `prefer-dark` for a dark theme and to **`default`** for a light one.
#
# `default` is not light. It means NO PREFERENCE, and the portal hands out 0 for
# it. So on a Windows 95 desktop — as light as a desktop gets — nothing in the
# session ever claimed to be light, and Firefox fell back to its own heuristic
# for whether to use dark chrome. That answer is not stable across launches,
# which is exactly why this was intermittent rather than simply broken.
#
# Measured on the live box before the fix:
#
#   gsettings color-scheme            'default'
#   portal org.freedesktop.appearance  u 0     <- no preference
#
# and after setting prefer-light, the portal reported `u 2`.
#
# Nothing else caught it because every OTHER consumer of the scheme is handed an
# explicit COLOUR (kdeglobals, the GTK theme name, the kitty and rofi palettes).
# Firefox's chrome is the only thing that reads the SIGNAL, so it was the only
# thing that could get this wrong — and a signal nobody else reads is a signal
# nothing else can fail loudly on.
#
# What is asserted is the call the helper makes, not Firefox's behaviour: this
# runs on a build box with no portal and no browser. HOME and PATH both point at
# a scratch directory, so a test about desktop settings cannot change the desktop
# settings of the machine running it.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

helper=${1:?usage: apply_theme_color_scheme.sh <systemd/synui-apply-theme.sh>}

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

mkdir -p "$tmp/bin" || exit 1

# The gsettings stub RECORDS instead of doing nothing — the whole point here is
# which value the helper asks for. Everything else the helper drives is stubbed
# inert, as in apply_theme_square_chrome.sh.
cat > "$tmp/bin/gsettings" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$SYNUI_TEST_LOG"
exit 0
EOF
chmod +x "$tmp/bin/gsettings"

for t in kwriteconfig6 kwriteconfig5 synui-firefox-glass dbus-send; do
    printf '#!/bin/sh\nexit 0\n' > "$tmp/bin/$t"
    chmod +x "$tmp/bin/$t"
done

# The palette is 95's silver, so a reader can see which desktop this is about.
run() {  # run <dark|light>
    : > "$tmp/log"
    HOME="$tmp" PATH="$tmp/bin:/usr/bin:/bin" SYNUI_TEST_LOG="$tmp/log" \
        sh "$helper" "$1" 61 125 255 61 125 255 192 192 192 0 0 0 \
        >"$tmp/out" 2>"$tmp/err"
}

# `grep -c` prints 0 and EXITS 1 on no match, so capture first and default
# second — same trap as the sibling test.
count() {
    n=$(grep -c "$@" 2>/dev/null) || n=0
    printf '%s' "$n"
}

scheme_arg() {  # the value the helper passed to `gsettings set … color-scheme`
    sed -n 's/^set org\.gnome\.desktop\.interface color-scheme //p' "$tmp/log" \
        | tail -1
}

# ── light ────────────────────────────────────────────────────
run light
check "a light theme asks for prefer-light" prefer-light "$(scheme_arg)"

# The regression itself, named so a failure says what broke rather than only
# which string differed.
check "a light theme never asks for 'default' (= no preference)" \
      0 "$(count -F 'color-scheme default' "$tmp/log")"

check "a light theme sets gtk-application-prefer-dark-theme=0" 1 \
      "$(count -F 'gtk-application-prefer-dark-theme=0' "$tmp/.config/gtk-3.0/settings.ini")"

# ── dark ─────────────────────────────────────────────────────
# The half that was always right. Asserted so a fix to the light path cannot
# quietly make every theme claim to be light.
run dark
check "a dark theme still asks for prefer-dark" prefer-dark "$(scheme_arg)"
check "a dark theme sets gtk-application-prefer-dark-theme=1" 1 \
      "$(count -F 'gtk-application-prefer-dark-theme=1' "$tmp/.config/gtk-3.0/settings.ini")"

# ── the scheme is stated exactly once ────────────────────────
# Two `gsettings set … color-scheme` calls with different values would leave the
# session on whichever ran last, which is the same class of bug wearing a
# different hat.
check "color-scheme is set exactly once per run" 1 \
      "$(count -F 'color-scheme' "$tmp/log")"

if [ "$fails" -eq 0 ]; then
    printf 'apply_theme_color_scheme: all checks passed\n'
    exit 0
fi
printf 'apply_theme_color_scheme: %d check(s) failed\n' "$fails"
exit 1
