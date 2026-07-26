#!/bin/sh
# synui-sound — the desktop's event sounds, and the single writer of their state.
#
# SynapseOS boots SILENT. Every event below is off and this file does not exist
# until someone turns one on, exactly like the desktop widgets: a system that
# starts making noises after an upgrade is a system that gets its speakers muted,
# and then the notification you actually wanted is gone too.
#
# Modelled on synui-widgets down to the layout, because a second state file that
# worked a second way would be its own small bug. Two jobs in one script on
# purpose:
#
#   synui-sound play login        the compositor's side — play it IF enabled
#   synui-sound login on          the panel's side — change the setting
#
# Keeping both here is what makes "is this event enabled?" a question with one
# answer. The compositor caches the same file to skip the fork when everything is
# off (sound.c), but it is a cache: this script re-checks, so a sound is never
# played because the compositor's copy was stale.
#
# Sounds come from the XDG sound theme (freedesktop.org sound-naming spec), so
# they are the same samples every other desktop uses and a user's own theme in
# ~/.local/share/sounds works with no changes here.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
# https://github.com/velle999/SYNAPSE
set -u

CONF_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
STATE="$CONF_HOME/synui/sounds.state"
DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"

# The event named for the volume OSD is volume_change, not volume: `volume` is
# already a setting key in the same flat file, and an event of that name made
# get()/load() read the event's line into the master volume and silence
# everything. One namespace, so the names have to be distinct.
EVENTS="login logout device_added device_removed lock unlock notify screenshot volume_change error"

# XDG sound id per event, primary first. The primary is the name the sound-naming
# spec gives the event; the fallback is one the freedesktop theme actually ships.
# Several spec names (desktop-login, desktop-screen-lock) are in the spec but in
# no theme on disk, and canberra answering "no such sample" is indistinguishable
# from a broken toggle — so every event resolves to something audible out of the
# box, and a theme that DOES ship the proper name wins because it is tried first.
#
# This is the AUTOMATIC chain, used when the event has no sample picked for it.
# `synui-sound sound <event> <id>` (and [ ] in the Super+S panel) overrides it
# with one specific id, and then only that id is tried — a deliberate choice is
# not silently replaced by a fallback that happens to exist.
ids() {
    case "$1" in
        login)          echo "desktop-login service-login" ;;
        logout)         echo "desktop-logout service-logout" ;;
        device_added)   echo "device-added" ;;
        device_removed) echo "device-removed" ;;
        lock)           echo "desktop-screen-lock bell" ;;
        unlock)         echo "desktop-unlock complete" ;;
        notify)         echo "message-new-instant message" ;;
        screenshot)     echo "screen-capture camera-shutter" ;;
        volume_change)  echo "audio-volume-change" ;;
        error)          echo "dialog-error" ;;
        *)              echo "" ;;
    esac
}

# The ids to actually try for an event: the picked one if there is one, else the
# automatic chain above. The override key is "<event>_sound" — distinct from the
# event's own on/off key, which is bare "<event>", because both live in the same
# flat namespace and `volume` already taught us what a collision there costs.
resolve_ids() {
    eval "pick=\${snd_$1:-default}"
    case "$pick" in
        ''|default|auto) ids "$1" ;;
        *)               echo "$pick" ;;
    esac
}

label() {
    case "$1" in
        login)          echo "Login" ;;
        logout)         echo "Logout" ;;
        device_added)   echo "Device plugged in" ;;
        device_removed) echo "Device unplugged" ;;
        lock)           echo "Screen locked" ;;
        unlock)         echo "Screen unlocked" ;;
        notify)         echo "Notification" ;;
        screenshot)     echo "Screenshot" ;;
        volume_change)  echo "Volume change" ;;
        error)          echo "Error / alert" ;;
        *)              echo "$1" ;;
    esac
}

usage() {
    cat <<EOF
usage: synui-sound [play|test] <event>
       synui-sound <event>|all [on|off|toggle]
       synui-sound theme [<name>]
       synui-sound sound <event> [<id>|default]
       synui-sound volume [0-100]
       synui-sound install <archive|directory>
       synui-sound remove <name>

  events: $EVENTS

  synui-sound                       show the current state
  synui-sound login on              turn one on
  synui-sound all off               silence everything (the default)
  synui-sound test login            play it once, whatever the setting says
  synui-sound themes                list the installed sound themes
  synui-sound samples               list the samples the current theme ships
  synui-sound sound login bell      play "bell" for login instead of the default
  synui-sound sound login default   go back to the automatic choice

Everything is off until switched on. Sounds come from the XDG sound theme, so
"theme" is a directory under /usr/share/sounds or ~/.local/share/sounds.

GETTING MORE SOUNDS

  From the repos, each of which adds one theme to the picker:

      sudo pacman -S sound-theme-freedesktop ocean-sound-theme \\
                     sound-theme-elementary pop-sound-theme \\
                     deepin-sound-theme cosmic-sound-theme

  From a downloaded pack, or a folder of your own .oga/.ogg/.wav files:

      synui-sound install ~/Downloads/some-sound-theme.tar.gz
      synui-sound install ~/my-sounds

  Either way it lands in $DATA_HOME/sounds/<name>/ and shows up in the
  theme row of the Super+S panel. A hand-made theme is just a directory:

      ~/.local/share/sounds/mine/stereo/desktop-login.oga
      ~/.local/share/sounds/mine/index.theme      ([Sound Theme] / Directories=stereo)

  The file names are XDG sound ids ("samples" lists the ones in use). A theme
  need not be complete — pick any sample it does have for any event with
  \`synui-sound sound <event> <id>\`, or [ ] in the panel.
EOF
}

# --- state ----------------------------------------------------------------

get() {
    [ -f "$STATE" ] || { echo "${2:-off}"; return; }
    val=$(sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*\([A-Za-z0-9_-]*\).*/\1/p" "$STATE" | tail -1)
    [ -n "$val" ] && echo "$val" || echo "${2:-off}"
}

load() {
    new_enabled=$(get enabled off)
    new_theme=$(get theme freedesktop)
    new_volume=$(get volume 70)
    for e in $EVENTS; do
        eval "new_$e=\$(get $e off)"
        eval "snd_$e=\$(get ${e}_sound default)"
    done
}

# Whole file rewritten from the current values, temp+rename so a reader can
# never see it half-written (the panel re-reads it after every change).
put() {
    mkdir -p "$(dirname "$STATE")" 2>/dev/null
    tmp="$STATE.tmp.$$"
    {
        echo "# Generated by synui-sound — desktop event sounds."
        echo "# Edit with synui-sound (or the Super+S panel), not by hand."
        printf 'enabled = %s\n' "$new_enabled"
        printf 'theme = %s\n'   "$new_theme"
        printf 'volume = %s\n'  "$new_volume"
        for e in $EVENTS; do
            eval "printf '%s = %s\n' \"\$e\" \"\$new_$e\""
        done
        # Which sample each event plays, kept in its own block: the switches
        # above are what people change, and burying them one line apart in a
        # ten-event file makes "is login on?" harder to answer at a glance.
        # "default" is the automatic chain in ids(), not a sample called that.
        echo ""
        for e in $EVENTS; do
            eval "printf '%s_sound = %s\n' \"\$e\" \"\${snd_$e:-default}\""
        done
    } > "$tmp" 2>/dev/null || { rm -f "$tmp"; echo "synui-sound: cannot write $STATE" >&2; exit 1; }
    mv -f "$tmp" "$STATE" || { rm -f "$tmp"; exit 1; }
}

valid() {
    for e in $EVENTS; do [ "$1" = "$e" ] && return 0; done
    return 1
}

flip() { [ "$1" = on ] && echo off || echo on; }

report() {
    printf '  %-16s %s\n' "master" "$new_enabled"
    if themes | grep -qx "$new_theme"; then
        printf '  %-16s %s\n' "theme" "$new_theme"
    else
        # Not a sound theme — every event below will be silent no matter what
        # its switch says, which is impossible to work out from the switches.
        printf '  %-16s %s\n' "theme" "$new_theme  <-- NOT a sound theme, nothing will play"
        printf '  %-16s %s\n' "" "pick one of: $(themes | tr '\n' ' ')"
    fi
    printf '  %-16s %s\n' "volume" "$new_volume"
    for e in $EVENTS; do
        eval "cur=\$new_$e"
        eval "pick=\${snd_$e:-default}"
        # The resolved id, not the stored "default", so the report answers the
        # question people actually have: what does this event sound like?
        heard=$(for id in $(resolve_ids "$e"); do
                    find_file "$id" >/dev/null 2>&1 && { echo "$id"; break; }
                done)
        [ -n "$heard" ] || heard="(not in $new_theme)"
        [ "$pick" = default ] || heard="$heard [picked]"
        printf '  %-16s %-4s  %-24s (%s)\n' "$e" "$cur" "$heard" "$(label "$e")"
    done
}

# A sound theme, as opposed to any other directory that happens to live under
# share/sounds. /usr/share/sounds/alsa is the case this exists for: alsa-utils
# drops nine channel-test .wav files there with no index.theme and no output
# profile subdirectory, and it was being offered in the theme picker — where
# selecting it silences every event, because it holds no XDG sample id at all.
is_theme() {
    [ -f "$1/index.theme" ] || [ -d "$1/stereo" ] || [ -d "$1/mono" ]
}

themes() {
    for base in "$DATA_HOME/sounds" /usr/local/share/sounds /usr/share/sounds; do
        [ -d "$base" ] || continue
        for d in "$base"/*/; do
            [ -d "$d" ] || continue
            is_theme "${d%/}" || continue
            name=${d%/}; name=${name##*/}
            echo "$name"
        done
    done | sort -u
}

# Every sample id a theme ships, deduped across the search path. This is what
# the panel's [ ] cycles through, so it lists ids (basenames) and not paths:
# an id is what canberra and `synui-sound sound` both take.
samples() {
    theme=${1:-$new_theme}
    for base in "$DATA_HOME/sounds" /usr/local/share/sounds /usr/share/sounds; do
        for sub in stereo mono .; do
            d="$base/$theme/$sub"
            [ -d "$d" ] || continue
            for f in "$d"/*.oga "$d"/*.ogg "$d"/*.wav; do
                [ -f "$f" ] || continue
                n=${f##*/}; echo "${n%.*}"
            done
        done
    done | sort -u
}

# --- playing --------------------------------------------------------------

# Resolve one sound id to a file inside the theme. Only used when libcanberra is
# absent; canberra does the full spec lookup (including theme inheritance) and
# this does not pretend to — it covers the layout every real theme uses.
find_file() {
    for base in "$DATA_HOME/sounds" /usr/local/share/sounds /usr/share/sounds; do
        for sub in stereo .; do
            for ext in oga ogg wav; do
                f="$base/$new_theme/$sub/$1.$ext"
                [ -f "$f" ] && { echo "$f"; return 0; }
            done
        done
    done
    return 1
}

# 0..100 as a percentage of full scale, in the dB libcanberra wants. 100 is 0 dB
# (the sample as recorded); 0 never gets here, it is treated as mute by play().
db() {
    awk -v v="$1" 'BEGIN { if (v <= 0) v = 1; printf "%.2f", 20 * log(v / 100) / log(10) }'
}

play() {
    ev=$1
    force=${2:-no}

    if [ "$force" != force ]; then
        [ "$new_enabled" = on ] || return 0
        eval "cur=\$new_$ev"
        [ "$cur" = on ] || return 0
    fi
    [ "$new_volume" -gt 0 ] 2>/dev/null || return 0

    for id in $(resolve_ids "$ev"); do
        if command -v canberra-gtk-play >/dev/null 2>&1; then
            # --property sets the theme for this one sample, so the setting takes
            # effect without touching the user's global GTK sound-theme-name.
            canberra-gtk-play -i "$id" \
                --property=canberra.xdg-theme.name="$new_theme" \
                -V "$(db "$new_volume")" >/dev/null 2>&1 && return 0
        else
            f=$(find_file "$id") || continue
            if command -v pw-play >/dev/null 2>&1; then
                pw-play --volume="$(awk -v v="$new_volume" 'BEGIN{printf "%.3f", v/100}')" \
                    "$f" >/dev/null 2>&1 && return 0
            elif command -v paplay >/dev/null 2>&1; then
                paplay --volume="$(awk -v v="$new_volume" 'BEGIN{printf "%d", v*655.35}')" \
                    "$f" >/dev/null 2>&1 && return 0
            fi
        fi
    done
    return 0   # a theme with no such sample is silence, not an error
}

# --- installing a theme ---------------------------------------------------
#
# Modelled on synui-cursor's installer, and for the same reason: these are
# untrusted archives off the internet. Nothing is ever used straight out of the
# extraction directory — adopt() re-resolves every path and refuses anything
# that does not stay inside it, which defeats ../../ traversal ("zip slip")
# however the extractor behaves. Sound themes are a flat bag of samples with no
# internal aliases, so unlike cursors there is no symlink to preserve: every
# symlink is dropped rather than followed.

warn() { echo "synui-sound: $*" >&2; }
die()  { echo "synui-sound: $*" >&2; exit 1; }

extract() {
    src=$1 dest=$2
    case "$src" in
        *.zip|*.ZIP)
            command -v unzip >/dev/null 2>&1 || die "unzip is not installed (needed for .zip)"
            unzip -qq -o "$src" -d "$dest" || return 1 ;;
        *.tar.gz|*.tgz|*.tar.xz|*.txz|*.tar.bz2|*.tbz2|*.tar.zst|*.tzst|*.tar)
            tar -xf "$src" -C "$dest" --no-same-owner --no-same-permissions || return 1 ;;
        *)
            die "unsupported archive '$src' (want .zip, .tar.gz, .tar.xz, .tar.bz2, .tar.zst)" ;;
    esac
}

adopt() {
    root=$1 theme=$2 name=$3

    # Attacker-chosen: it is the archive's own directory name, and it goes on to
    # be a shell argument (`synui-sound theme <name>`), a listing row and an ini
    # value. Refused outright rather than escaped three different ways.
    case "$name" in
        ''|.|..|-*)                 warn "refusing theme with unusable name '$name'"; return 1 ;;
        */*|*[\'\"\`\$\;\&\|\<\>]*) warn "refusing theme with unsafe name '$name'";   return 1 ;;
    esac
    if printf '%s' "$name" | LC_ALL=C grep -q '[[:cntrl:]]'; then
        warn "refusing theme whose name contains control characters"
        return 1
    fi

    rroot=$(realpath "$root" 2>/dev/null)   || return 1
    rtheme=$(realpath "$theme" 2>/dev/null) || return 1
    case "$rtheme/" in
        "$rroot"/*|"$rroot"/) ;;
        *) warn "refusing '$name': archive member escapes the extraction directory"
           return 1 ;;
    esac

    # -type l finds them without following them. A sound theme has no legitimate
    # internal symlink, so there is nothing to weigh up: all of them go.
    find "$rtheme" -type l -exec rm -f {} + 2>/dev/null

    dest="$DATA_HOME/sounds/$name"
    if [ -e "$dest" ]; then
        warn "'$name' is already installed at $dest — replacing it"
        rm -rf "$dest" || return 1
    fi

    mkdir -p "$DATA_HOME/sounds" || return 1
    cp -a "$rtheme" "$dest" || return 1
    return 0
}

archive_basename() {
    n=$(basename "$1")
    case "$n" in
        *.tar.gz|*.tar.xz|*.tar.bz2|*.tar.zst|*.tar.lz|*.tar.lzma)
            n=${n%.*}; n=${n%.tar} ;;
        *.tgz|*.txz|*.tbz2|*.tzst|*.zip|*.tar)
            n=${n%.*} ;;
    esac
    printf '%s\n' "$n"
}

install_theme() {
    [ $# -ge 1 ] || die "usage: synui-sound install <archive|directory>"
    src=$1
    [ -e "$src" ] || die "no such file: $src"

    tmp=$(mktemp -d -t synui-sound-XXXXXX) || die "cannot create a temp dir"
    trap 'rm -rf "$tmp"' EXIT INT TERM

    extract_root=""; archive_name=""
    if [ -d "$src" ]; then
        root=$src
    else
        extract "$src" "$tmp" || die "could not extract '$src'"
        root=$tmp; extract_root=$tmp
        archive_name=$(archive_basename "$src")

        # Archives wrap everything in one top-level directory. Descend through
        # it so `root` is the theme itself — but stop the moment this IS a
        # theme, or the descent walks into stereo/ and the theme ends up above
        # the root, tripping adopt's escape guard on a perfectly good pack.
        while :; do
            is_theme "$root" && break
            n=0; only=""
            for e in "$root"/* "$root"/.[!.]*; do
                [ -e "$e" ] || continue
                n=$((n + 1)); only=$e
            done
            [ "$n" -eq 1 ] && [ -d "$only" ] || break
            case "$(basename "$only")" in stereo|mono) break ;; esac
            root=$only
        done
    fi

    # A folder of loose .oga/.wav files is a theme in everything but layout, and
    # it is the most likely thing a user points this at. Rather than refusing it
    # over a missing index.theme, stage a real one: samples into stereo/, an
    # index.theme naming it, and from there it is an ordinary install.
    staged=""
    if ! is_theme "$root"; then
        if [ -n "$(find "$root" -maxdepth 1 -type f \
                   \( -name '*.oga' -o -name '*.ogg' -o -name '*.wav' \) 2>/dev/null)" ]; then
            staged="$tmp/.staged/$(basename "$(realpath "$root")")"
            mkdir -p "$staged/stereo" || die "cannot stage '$root'"
            find "$root" -maxdepth 1 -type f \
                \( -name '*.oga' -o -name '*.ogg' -o -name '*.wav' \) \
                -exec cp -f {} "$staged/stereo/" \; 2>/dev/null
            printf '[Sound Theme]\nName=%s\nDirectories=stereo\n\n[stereo]\nOutputProfile=stereo\n' \
                "$(basename "$staged")" > "$staged/index.theme"
            root="$tmp/.staged"
            extract_root=""   # the name comes from the folder, not the archive
        fi
    fi

    # Every theme root in the tree, so a pack shipping a light and a dark
    # variant side by side installs both. maxdepth 4 matches the installer's
    # descent above; deeper than that is not a layout any theme uses.
    found=0
    roots=$(find "$root" -maxdepth 4 -type d 2>/dev/null | while IFS= read -r d; do
                is_theme "$d" && echo "$d"
            done)
    [ -n "$roots" ] || die "no sound theme in '$src' (want an index.theme, a stereo/ directory, or a folder of .oga/.ogg/.wav files)"

    # Newline-split, not the default IFS: an archive is free to ship a directory
    # with a space in its name, and word-splitting one would hand adopt() two
    # halves of a path that both fail the escape check.
    oldifs=$IFS; IFS='
'
    for t in $roots; do
        IFS=$oldifs
        if [ -n "$extract_root" ] && [ "$t" = "$extract_root" ]; then
            name=$archive_name
        else
            name=$(basename "$t")
        fi
        if adopt "$root" "$t" "$name"; then
            found=$((found + 1))
            n_samples=$(samples "$name" | wc -l)
            printf "installed '%s' -> %s/sounds/%s (%s samples)\n" \
                "$name" "$DATA_HOME" "$name" "$n_samples"
            [ "$n_samples" -eq 0 ] && warn "'$name' ships no .oga/.ogg/.wav samples — it will be silent"
        fi
        IFS='
'
    done
    IFS=$oldifs

    [ "$found" -gt 0 ] || die "nothing installed from '$src'"
    echo "select it with: synui-sound theme <name>   (or the Sound theme row of Super+S)"
}

# Only ever from the user's own directory. A theme under /usr/share/sounds came
# from a package and belongs to pacman; removing it here would leave the package
# database claiming files that are gone.
remove_theme() {
    [ $# -ge 1 ] || die "usage: synui-sound remove <name>"
    name=$1
    case "$name" in
        */*|.|..|'') die "not a theme name: '$name'" ;;
    esac
    dest="$DATA_HOME/sounds/$name"
    [ -d "$dest" ] || die "'$name' is not installed in $DATA_HOME/sounds (themes under /usr/share/sounds belong to pacman — remove the package instead)"
    rm -rf "$dest" || die "could not remove $dest"
    echo "removed '$name'"

    # The selected theme just stopped existing; leaving it selected is a desktop
    # that has gone quiet for no visible reason.
    if [ "$new_theme" = "$name" ]; then
        new_theme=$(themes | head -1)
        [ -n "$new_theme" ] || new_theme=freedesktop
        put
        echo "theme was '$name' — now '$new_theme'"
    fi
}

# --- dispatch -------------------------------------------------------------

case "${1:-}" in
    -h|--help|help) usage; exit 0 ;;
    themes)         themes; exit 0 ;;
esac

load

if [ $# -eq 0 ]; then report; exit 0; fi

case "$1" in
play|test)
    ev="${2:-}"
    if ! valid "$ev"; then
        echo "synui-sound: unknown event '${ev}'" >&2
        usage >&2
        exit 2
    fi
    # `play` is the compositor's call and honours every switch; `test` is the
    # panel's preview and deliberately ignores them, so you can hear what you are
    # about to enable (and prove the audio stack works) before enabling it.
    [ "$1" = test ] && play "$ev" force || play "$ev"
    exit 0
    ;;
theme)
    if [ $# -lt 2 ]; then echo "$new_theme"; exit 0; fi
    if ! themes | grep -qx "$2"; then
        echo "synui-sound: no sound theme '$2' installed" >&2
        echo "installed:" >&2
        themes | sed 's/^/  /' >&2
        echo "install one with 'synui-sound install <archive|directory>', or see 'synui-sound --help'" >&2
        exit 2
    fi
    new_theme=$2
    put
    echo "  theme            $new_theme"
    exit 0
    ;;
samples)
    samples "${2:-$new_theme}"
    exit 0
    ;;
sound)
    ev="${2:-}"
    if ! valid "$ev"; then
        echo "synui-sound: unknown event '${ev}'" >&2
        usage >&2
        exit 2
    fi
    eval "cur=\${snd_$ev:-default}"
    if [ $# -lt 3 ]; then echo "$cur"; exit 0; fi

    case "$3" in
        default|auto)
            eval "snd_$ev=default"
            put
            printf '  %-16s %s\n' "$ev" "default ($(ids "$ev" | cut -d' ' -f1), with fallbacks)"
            ;;
        *)
            # Validated against the theme on disk. A typo'd id is not an error
            # anywhere downstream — canberra just plays nothing — so this is the
            # only place it can be caught, and a silent event is exactly the
            # symptom that is impossible to tell from a broken toggle.
            if ! samples | grep -qx "$3"; then
                echo "synui-sound: theme '$new_theme' has no sample '$3'" >&2
                echo "try: synui-sound samples" >&2
                exit 2
            fi
            eval "snd_$ev=\$3"
            put
            printf '  %-16s %s\n' "$ev" "$3"
            ;;
    esac
    exit 0
    ;;
install)
    shift
    install_theme "$@"
    exit 0
    ;;
remove)
    shift
    remove_theme "$@"
    exit 0
    ;;
volume)
    if [ $# -lt 2 ]; then echo "$new_volume"; exit 0; fi
    case "$2" in
        ''|*[!0-9]*) echo "synui-sound: volume must be 0-100" >&2; exit 2 ;;
    esac
    [ "$2" -gt 100 ] && { echo "synui-sound: volume must be 0-100" >&2; exit 2; }
    new_volume=$2
    put
    echo "  volume           $new_volume"
    exit 0
    ;;
esac

target="$1"
action="${2:-toggle}"

case "$action" in
    on|off|toggle) ;;
    *) echo "synui-sound: unknown action '$action'" >&2; usage >&2; exit 2 ;;
esac

if [ "$target" = all ]; then
    # Group-toggle turns everything OFF if anything is on, so "make it stop" is
    # always one predictable command.
    if [ "$action" = toggle ]; then
        anyon=off
        for e in $EVENTS; do
            eval "cur=\$new_$e"
            [ "$cur" = on ] && anyon=on
        done
        [ "$anyon" = on ] && action=off || action=on
    fi
    for e in $EVENTS; do eval "new_$e=\$action"; done
    # Turning any sound on implies wanting sound: the master switch follows,
    # or "all on" would be ten switches flipped and still silence.
    [ "$action" = on ] && new_enabled=on
    put
    report
    exit 0
fi

if [ "$target" = master ] || [ "$target" = enabled ]; then
    [ "$action" = toggle ] && action=$(flip "$new_enabled")
    new_enabled=$action
    put
    printf '  %-16s %s\n' master "$new_enabled"
    exit 0
fi

if ! valid "$target"; then
    echo "synui-sound: unknown event '$target'" >&2
    usage >&2
    exit 2
fi

eval "cur=\$new_$target"
[ "$action" = toggle ] && action=$(flip "$cur")
eval "new_$target=\$action"
[ "$action" = on ] && new_enabled=on
put
printf '  %-16s %-4s  (%s)\n' "$target" "$action" "$(label "$target")"
