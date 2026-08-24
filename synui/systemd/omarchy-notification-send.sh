#!/usr/bin/env bash
#
# omarchy-notification-send — post a desktop notification, under the name
# Omarchy's plugins call.
#
# ⚠ THE THIRD HARDCODED NAME, and the quietest of them. weather-radar's
# Service.qml builds its storm alert as
#
#     ["omarchy-notification-send", "--app-name", "Weather Radar",
#      "-g", GLYPH, "-u", "critical", headline, description]
#
# and hands it to a Process. A missing binary there is not an error the plugin
# can see: the alert simply never appears, on a widget whose alerts are OFF by
# default, so the first time anyone notices is a storm that arrived unannounced
# — and by then the evidence is a "failed to start" line in a shell log from
# hours earlier. Turning the setting on and getting nothing is indistinguishable
# from the weather being fine.
#
# ⛔ IT IS A TRANSLATOR, NOT A NOTIFIER. synui has its own notification daemon;
# what is missing is Omarchy's spelling of the arguments, so this maps their
# flags onto notify-send and nothing else. Anything it does not recognise is
# dropped rather than forwarded, because notify-send exits non-zero on an
# unknown option and a rejected alert is the failure this exists to stop.
#
# Usage (Omarchy's, verbatim):
#   omarchy-notification-send [--app-name <name>] [-g <glyph>] [-u <urgency>]
#       [-i <icon>] [-t <ms>] [-r <id>] [-p] [--image <path>]
#       <headline> [description] [--exec <program> [args…]]
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

app_name="omarchy-action"
glyph=""
urgency="low"
icon=""
expire=""
replaces=""
print_id=0
headline=""
description=""
exec_argv=()
have_exec=0
positional=0

die() { echo "$1" >&2; exit 1; }

need() { [ "$2" -ge 2 ] || die "Missing value for $1"; }

while [ $# -gt 0 ]; do
    # Both spellings are live in their corpus: `--flag value` and `--flag=value`
    # come out of different plugins, and a widget that used the second one would
    # otherwise have its glyph land in the headline.
    opt=$1
    val=""
    inline=0
    case "$opt" in
    --?*=*) val=${opt#*=}; opt=${opt%%=*}; inline=1 ;;
    esac

    case "$opt" in
    --app-name) [ "$inline" = 1 ] || { need "$opt" $#; val=$2; shift; }; app_name=$val ;;
    -g | --glyph) [ "$inline" = 1 ] || { need "$opt" $#; val=$2; shift; }; glyph=$val ;;
    -u | --urgency) [ "$inline" = 1 ] || { need "$opt" $#; val=$2; shift; }; urgency=$val ;;
    -i | --icon) [ "$inline" = 1 ] || { need "$opt" $#; val=$2; shift; }; icon=$val ;;
    --image) [ "$inline" = 1 ] || { need "$opt" $#; val=$2; shift; }; icon=$val ;;
    -t | --expire-time) [ "$inline" = 1 ] || { need "$opt" $#; val=$2; shift; }; expire=$val ;;
    -r | --replace-id) [ "$inline" = 1 ] || { need "$opt" $#; val=$2; shift; }; replaces=$val ;;
    -p | --print-id) print_id=1 ;;
    --exec)
        # Everything after it belongs to the program, including things that
        # look like options to this script.
        shift
        have_exec=1
        exec_argv=("$@")
        break
        ;;
    --) shift; break ;;
    -*)
        # ⛔ AN ERROR, NOT A GUESS — and this is the one place this translator
        # refuses to be tolerant. An option it has not been taught takes an
        # unknown number of arguments: dropping just the flag leaves its value
        # standing in the headline, so a newer plugin's `--sound chime` would
        # post a notification that says "chime". Upstream errors here too, so a
        # plugin tested against the real thing sees the same answer.
        die "Unknown option: $opt"
        ;;
    *)
        if [ "$positional" = 0 ]; then headline=$opt; positional=1
        elif [ "$positional" = 1 ]; then description=$opt; positional=2
        fi
        ;;
    esac
    shift
done

# A trailing `-- headline description`.
while [ $# -gt 0 ] && [ "$have_exec" = 0 ]; do
    if [ "$positional" = 0 ]; then headline=$1; positional=1
    elif [ "$positional" = 1 ]; then description=$1; positional=2
    fi
    shift
done

[ -n "$headline" ] || die "Usage: omarchy-notification-send [options] <headline> [description]"

# The glyph rides in the summary, which is the closest thing notify-send has to
# Omarchy's separate field. Two spaces because a nerd-font icon is wider than
# the space that follows it looks.
[ -n "$glyph" ] && headline="$glyph  $headline"

argv=(notify-send -a "$app_name" -u "$urgency")
[ -n "$icon" ] && argv+=(-i "$icon")
[ -n "$expire" ] && argv+=(-t "$expire")
[ -n "$replaces" ] && argv+=(-r "$replaces")
[ "$print_id" = 1 ] && argv+=(-p)

if [ "$have_exec" = 1 ] && [ ${#exec_argv[@]} -gt 0 ]; then
    # notify-send prints the chosen action key and waits, so this blocks — which
    # is right: every caller spawns this detached.
    chosen=$("${argv[@]}" -A "default=Open" -- "$headline" "$description")
    [ "$chosen" = "default" ] && exec "${exec_argv[@]}"
    exit 0
fi

exec "${argv[@]}" -- "$headline" "$description"
