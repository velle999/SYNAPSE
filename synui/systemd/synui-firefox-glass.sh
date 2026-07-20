#!/usr/bin/env bash
# synui-firefox-glass — install (or remove) the glass Firefox chrome, and paint
# its tint with the current theme's colours.
#
# Firefox is the one major app synui cannot make translucent from the outside:
# it declares an opaque region and fills its own surface, so real transparency
# needs THREE prefs plus a userChrome.css inside the profile. That is too much
# to ask of a user, so SynapseOS ships it on by default — and, because a browser
# that silently rewrites itself is obnoxious, ships an off switch with it.
#
#   synui-firefox-glass on            enable + install into every profile
#   synui-firefox-glass off           revert the prefs and remove our files
#   synui-firefox-glass status        report per profile
#   synui-firefox-glass [palette...]  apply the CURRENT setting (login/theme hook)
#
# The palette form is what synui-apply-theme calls; args match its own tail:
#   <dark|light> <base_r> <base_g> <base_b>
#
# Best effort and idempotent like the rest of the theme helpers: no `set -e`, a
# box with no Firefox profile is simply a no-op.
set -u

# ── The off switch ───────────────────────────────────────────────────────────
# Absent = enabled: this is a default-on feature, so the state file only has to
# exist to turn it OFF. That way a fresh install needs no state at all.
state_dir="$HOME/.config/synui"
state="$state_dir/firefox-glass.off"

# ── Where the packaged templates live ────────────────────────────────────────
tpl=${SYNUI_FIREFOX_TPL:-/usr/share/synui/firefox}

# Only ever touch a file that carries this marker (or does not exist). A user who
# strips the line — or who had their own userChrome.css first — keeps their file.
SENTINEL='SYNUI-MANAGED'

owned() {   # owned <file> → true if absent, or ours to overwrite
    [ -e "$1" ] || return 0
    head -5 "$1" 2>/dev/null | grep -q "$SENTINEL"
}

# ── user.js: a managed block, not an append ──────────────────────────────────
# Appending would duplicate the prefs every run. Rewrite the region between the
# markers instead, leaving any prefs the user set around it alone.
BEGIN='// >>> synui-firefox-glass BEGIN — managed, do not edit'
END='// <<< synui-firefox-glass END'

write_prefs() {   # write_prefs <profile> <on|off>
    local prof=$1 mode=$2 js="$1/user.js" tmp
    local legacy opaque transp wp
    if [ "$mode" = on ]; then
        legacy=true;  opaque=false; transp=true;  wp=false
    else
        # Firefox's own defaults. Deleting the lines would NOT be enough: once a
        # user_pref has been written it lives in prefs.js and persists, so the
        # off path has to actively set the values back.
        legacy=false; opaque=true;  transp=false; wp=true
    fi

    tmp=$(mktemp) || return 0
    if [ -f "$js" ]; then
        # Drop our old block AND any loose copies of the four prefs it owns.
        # A profile that was set up by hand before this script existed has those
        # lines outside any block; leaving them means two settings for one key,
        # and the file's last occurrence silently wins. The block is the single
        # authority for these four — everything else in user.js is left alone.
        awk -v b="$BEGIN" -v e="$END" '
            $0 == b { skip=1; next }
            $0 == e { skip=0; next }
            skip    { next }
            /user_pref\("toolkit\.legacyUserProfileCustomizations\.stylesheets"/  { next }
            /user_pref\("widget\.wayland\.opaque-region\.enabled"/                { next }
            /user_pref\("browser\.tabs\.allow_transparent_browser"/               { next }
            /user_pref\("browser\.newtabpage\.activity-stream\.newtabWallpapers\.enabled"/ { next }
            { print }
        ' "$js" > "$tmp"
    fi
    {
        printf '%s\n' "$BEGIN"
        printf '// Turn this block off with: synui-firefox-glass off\n'
        printf 'user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", %s);\n' "$legacy"
        printf '// Firefox declares an opaque region on its Wayland surface (a "nothing\n'
        printf '// behind me shows" perf hint). While set, transparent chrome renders solid.\n'
        printf 'user_pref("widget.wayland.opaque-region.enabled", %s);\n' "$opaque"
        printf '// FF128+. Without this Firefox fills its whole rendering surface opaque and\n'
        printf '// NO userChrome/userContent transparency can show through. Proven by test.\n'
        printf 'user_pref("browser.tabs.allow_transparent_browser", %s);\n' "$transp"
        printf '// activity-stream paints its own opaque wallpaper over our glass newtab.\n'
        printf 'user_pref("browser.newtabpage.activity-stream.newtabWallpapers.enabled", %s);\n' "$wp"
        printf '%s\n' "$END"
    } >> "$tmp"
    mv "$tmp" "$js" 2>/dev/null || rm -f "$tmp"
}

# ── Arguments ────────────────────────────────────────────────────────────────
# The verb and the palette are separate: `on` may be given a palette too (so the
# theme's colours land immediately rather than on the next theme switch), which
# means the scheme cannot simply be $1 — reading it that way parsed `on light
# 255 233 242` as red=light.
cmd=
case "${1:-}" in
    on|off|status) cmd=$1; shift ;;
esac
scheme=${1:-dark}
case "$scheme" in dark|light) ;; *) scheme=dark ;; esac
br=${2:-} bg_=${3:-} bb=${4:-}

write_tint() {   # write_tint <chrome dir>
    local rgb ga gh gt
    # No palette on the command line (a bare `on`): keep a tint that is already
    # there rather than stamping navy over the user's actual theme colour.
    [ -z "$br" ] && [ -f "$1/synapse-theme.css" ] && return 0
    rgb="${br:+$br,$bg_,$bb}"; rgb="${rgb:-11,11,20}"
    # A light theme's tint is near-white; at the dark alphas its dark chrome text
    # washes out over a busy wallpaper, so the light schemes get more body.
    if [ "$scheme" = light ]; then ga=0.42; gh=0.62; gt=0.58
    else                           ga=0.28; gh=0.48; gt=0.45
    fi
    cat > "$1/synapse-theme.css" <<CSS
/* SYNUI-MANAGED — generated by synui-firefox-glass on every theme switch.
 * Only the tint lives here; the layout rules are in userChrome.css. Deleting
 * this file is safe: every use there carries the original navy as a fallback. */
:root {
    --sy-glass: rgba($rgb, $ga);
    --sy-glass-hi: rgba($rgb, $gh);
    --sy-tile: rgba($rgb, $gt);
}
CSS
}

# ── Walk the profiles ────────────────────────────────────────────────────────
# Both roots: this box keeps profiles under ~/.config/mozilla (XDG), not the
# historical ~/.mozilla, and other installs go the other way.
profiles() {
    local root prof
    for root in "$HOME/.config/mozilla/firefox" "$HOME/.mozilla/firefox"; do
        [ -d "$root" ] || continue
        for prof in "$root"/*/; do
            # A real profile has prefs.js or times.json; the glob also matches
            # things like Crash Reports/ and Pending Pings/, which are not.
            [ -f "$prof/prefs.js" ] || [ -f "$prof/times.json" ] || continue
            printf '%s\n' "${prof%/}"
        done
    done
}

install_glass() {
    local prof chrome n=0
    for prof in $(profiles); do
        chrome="$prof/chrome"
        mkdir -p "$chrome" 2>/dev/null || continue
        for f in userChrome.css userContent.css; do
            [ -f "$tpl/$f" ] || continue
            if owned "$chrome/$f"; then
                cp -f "$tpl/$f" "$chrome/$f" 2>/dev/null
            else
                echo "synui-firefox-glass: keeping your own $chrome/$f" >&2
            fi
        done
        write_tint "$chrome"
        write_prefs "$prof" on
        n=$((n + 1))
    done
    [ "$n" -gt 0 ]
}

remove_glass() {
    local prof chrome f
    for prof in $(profiles); do
        chrome="$prof/chrome"
        # Revert the prefs FIRST — that is the half that actually persists.
        write_prefs "$prof" off
        [ -d "$chrome" ] || continue
        for f in userChrome.css userContent.css synapse-theme.css; do
            owned "$chrome/$f" && rm -f "$chrome/$f" 2>/dev/null
        done
        rmdir "$chrome" 2>/dev/null   # only if we left it empty
    done
}

case "$cmd" in
on)
    mkdir -p "$state_dir" 2>/dev/null
    rm -f "$state"
    install_glass \
        && echo "synui-firefox-glass: on — restart Firefox fully to see it" \
        || echo "synui-firefox-glass: on (no Firefox profile found yet; it will apply once one exists)"
    ;;
off)
    mkdir -p "$state_dir" 2>/dev/null
    : > "$state"
    remove_glass
    echo "synui-firefox-glass: off — prefs reverted; restart Firefox fully"
    ;;
status)
    if [ -e "$state" ]; then echo "setting: OFF ($state)"
    else                     echo "setting: on (default; disable with: synui-firefox-glass off)"
    fi
    for prof in $(profiles); do
        if [ -f "$prof/chrome/userChrome.css" ]; then
            printf '  %s: glass installed\n' "$(basename "$prof")"
        else
            printf '  %s: no glass\n' "$(basename "$prof")"
        fi
    done
    ;;
*)
    # The login / theme-switch hook. Honour the setting silently: re-apply the
    # tint when on, do nothing at all when the user has turned it off.
    [ -e "$state" ] && exit 0
    install_glass >/dev/null 2>&1
    ;;
esac

exit 0
