#!/usr/bin/env bash
#
# syn-gfn — GeForce NOW, in a browser that can hold your mouse.
#
# ⛔ THIS EXISTS BECAUSE THE ELECTRON CLIENT CANNOT DO THE TWO THINGS A GAME
# STREAM IS. Measured against geforcenow-electron 3.0.2 (2026-08-24), the
# Flathub build, on this desktop:
#
#   * Its Wayland branch hardcodes `--use-gl=egl`, a value Chromium 142
#     removed — "Requested GL implementation (gl=none,angle=none) not found in
#     allowed implementations". The GPU process dies at every launch, three
#     retries, then software compositing. In that fallback its Wayland surface
#     never repaints at the size the compositor gives it: an 800x600 page in
#     the corner of a full-screen window, with the desktop showing through the
#     rest. The pointer only works where it painted.
#   * It binds zwp_pointer_constraints_v1 and NEVER CALLS lock_pointer. Nothing
#     ever asks the compositor to hold the cursor, so it walks off the edge of
#     the window mid-game and onto the next monitor.
#
# Both of those belong to the browser engine, not to a wrapper — so the fix is
# to use a browser that gets them right rather than to reimplement them. Every
# hard part of a cloud-gaming client (pointer lock, keyboard lock, fullscreen,
# H.264/HEVC decode, WebRTC) is Chromium's, already written and already tested
# against this exact service.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
# https://github.com/velle999/SYNAPSE
set -u

URL_DEFAULT="https://play.geforcenow.com"
PROFILE_DEFAULT="${XDG_DATA_HOME:-$HOME/.local/share}/syn-gfn"

# ⚠ CHROMIUM-FAMILY ONLY, AND THE ORDER IS THE PREFERENCE. GeForce NOW's web
# client is supported on Chrome/Edge and refuses Firefox outright, so a Gecko
# browser is not a fallback — it is a different, broken answer. Vivaldi first
# because it is the one SynapseOS boxes tend to have; the rest are what people
# install instead.
BROWSERS="vivaldi-stable chromium chromium-browser google-chrome-stable google-chrome brave brave-browser microsoft-edge-stable"

usage() {
    cat <<EOF
usage: syn-gfn [--browser=NAME] [--profile=DIR] [--url=URL] [-- <browser args>]
       syn-gfn --list-browsers
       syn-gfn --help

  GeForce NOW as a dedicated web app, in its own browser profile.

  --browser=NAME   use this browser instead of the first one found
  --profile=DIR    profile directory (default: $PROFILE_DEFAULT)
  --url=URL        open somewhere else (default: $URL_DEFAULT)
  --list-browsers  print which browsers are installed and which would be used
  --                everything after this is passed to the browser unchanged

  Its own profile, never your browsing one: a stream is a full-screen thing
  that wants the keyboard, and it has no business sharing a session, a cookie
  jar or a window with the tabs you had open.

  ⚠ PRESS GEFORCE NOW'S OWN FULLSCREEN BUTTON, NOT F11. Escape only reaches
  the game while the PAGE holds the keyboard (navigator.keyboard.lock()), and
  the browser only allows that in fullscreen the page itself asked for. Under
  F11 the browser owns Escape and spends it leaving fullscreen, which is why
  the in-game menu never opens. This launcher pre-grants the keyboard-lock,
  pointer-lock and fullscreen permissions for the site so nothing has to be
  clicked through first.

  What your account streams at is NOT ours to set: resolution and frame rate
  come from the membership tier and the account's own streaming-quality
  setting. A Free membership tops out at 1080p60.
EOF
}

# ── Which browser ───────────────────────────────────────────────────────────
find_browser() {
    local b
    for b in $BROWSERS; do
        command -v "$b" >/dev/null 2>&1 && { printf '%s' "$b"; return 0; }
    done
    return 1
}

list_browsers() {
    local b found=0
    printf 'looked for, in order:\n'
    for b in $BROWSERS; do
        if command -v "$b" >/dev/null 2>&1; then
            if [ "$found" -eq 0 ]; then
                printf '  %-24s installed  <- would use this one\n' "$b"
                found=1
            else
                printf '  %-24s installed\n' "$b"
            fi
        else
            printf '  %-24s -\n' "$b"
        fi
    done
    [ "$found" -eq 1 ] || printf '\n  none installed. Any Chromium-family browser will do.\n'
}

# ── The three permissions a stream needs, granted before the first frame ────
#
# Chromium 142 keeps `keyboard_lock`, `pointer_lock` and `automatic_fullscreen`
# as ordinary per-site content settings. Left unset they are a prompt, and a
# prompt during a stream is a prompt nobody sees: the page is full screen and
# the pointer is captured, which is precisely the state the prompt is asking
# about. Granted here, for THIS site in THIS profile only — the profile exists
# to hold exactly one origin, so there is nothing else in it to widen.
#
# ⚠ WRITTEN ONLY WHILE THE BROWSER IS NOT RUNNING, and only for keys that are
# missing. Chromium rewrites Preferences from memory on exit, so a write under
# a live browser is a write that gets thrown away.
seed_permissions() {  # seed_permissions <profile-dir> <url>
    command -v python3 >/dev/null 2>&1 || return 0
    python3 - "$1" "$2" <<'PY'
import json, os, sys, time
profile, url = sys.argv[1], sys.argv[2]

# The pattern Chromium writes for a site permission: origin with its port,
# then the wildcard secondary pattern.
from urllib.parse import urlsplit
u = urlsplit(url)
port = u.port or (443 if u.scheme == "https" else 80)
pattern = "%s://%s:%d,*" % (u.scheme, u.hostname, port)

path = os.path.join(profile, "Default", "Preferences")
os.makedirs(os.path.dirname(path), exist_ok=True)

try:
    with open(path) as f:
        prefs = json.load(f)
except (OSError, ValueError):
    prefs = {}

# Chromium's own timestamp format: microseconds since 1601-01-01, as a string.
now = str(int((time.time() + 11644473600) * 1000000))

ex = prefs.setdefault("profile", {}).setdefault("content_settings", {}) \
          .setdefault("exceptions", {})

changed = False
for key in ("keyboard_lock", "pointer_lock", "automatic_fullscreen"):
    bucket = ex.setdefault(key, {})
    if pattern in bucket:
        continue
    bucket[pattern] = {"last_modified": now, "setting": 1}   # 1 = allow
    changed = True

if changed:
    tmp = path + ".syn-gfn.tmp"
    with open(tmp, "w") as f:
        json.dump(prefs, f)
    os.replace(tmp, path)
PY
}

# ── Arguments ───────────────────────────────────────────────────────────────
BROWSER=""
PROFILE="$PROFILE_DEFAULT"
URL="$URL_DEFAULT"
PASSTHROUGH=0
EXTRA=""

while [ $# -gt 0 ]; do
    if [ "$PASSTHROUGH" -eq 1 ]; then
        EXTRA="$EXTRA $1"; shift; continue
    fi
    case "$1" in
        --browser=*)    BROWSER=${1#*=} ;;
        --profile=*)    PROFILE=${1#*=} ;;
        --url=*)        URL=${1#*=} ;;
        --list-browsers) list_browsers; exit 0 ;;
        -h|--help)      usage; exit 0 ;;
        --)             PASSTHROUGH=1 ;;
        *)  printf 'syn-gfn: unknown option %s\n' "$1" >&2
            printf '  try: syn-gfn --help\n' >&2
            exit 2 ;;
    esac
    shift
done

if [ -n "$BROWSER" ]; then
    command -v "$BROWSER" >/dev/null 2>&1 || {
        printf 'syn-gfn: %s is not installed\n' "$BROWSER" >&2
        printf '  syn-gfn --list-browsers shows what is\n' >&2
        exit 1; }
else
    BROWSER=$(find_browser) || {
        printf 'syn-gfn: no Chromium-family browser installed\n' >&2
        printf '  GeForce NOW refuses Firefox, so this needs one of:\n' >&2
        printf '    %s\n' $BROWSERS >&2
        printf '  e.g. synpkg install chromium\n' >&2
        exit 1; }
fi

mkdir -p "$PROFILE" || exit 1
seed_permissions "$PROFILE" "$URL"

# ⚠ THE SESSION EXPORTS MANGOHUD=1, which loads MangoHud's Vulkan layer into
# every Vulkan client — and a browser is one. On AMD that layer segfaults the
# client inside its own vkCreateDevice hook and on NVIDIA it never does, which
# is how it took synui-wpengine and synstudio down on the ThinkPad while the
# desktop stayed happy. DISABLE_MANGOHUD is the manifest's own
# disable_environment and beats the enable; MANGOHUD=0 goes with it for the
# OpenGL side. The same pair those two launchers set, for the same reason.
#
# ⚠ NOT MEASURED AS A CRASH HERE. Vivaldi starts fine under MANGOHUD=1 on this
# NVIDIA box — the startup crash that cost an evening was --start-fullscreen,
# below. This is the AMD class, kept off because a cloud stream has no frame
# times of its own worth measuring: the numbers that matter are the service's.
export DISABLE_MANGOHUD=1 MANGOHUD=0

# ⚠ --class AND NOT --app. `--app=<url>` gives the cleaner window — no tab
# strip — but Chromium derives the app_id from the URL and the profile
# ("vivaldi-play.geforcenow.com__-Default") and IGNORES --class, which breaks
# the one rule synui's dock has: it resolves a pinned window through a direct
# <app_id>.desktop lookup and, on a miss, runs the app_id as a command. A
# window nobody can pin, whose name changes with the browser, is worse than a
# tab strip you stop seeing the moment you go full screen.
#
# ⛔ AND NOT --start-fullscreen: Vivaldi 8.1 core-dumps on it under Wayland
# (SIGTRAP at startup, no protocol error — a bare window with the same flags
# survives). GeForce NOW's own fullscreen control is the one to use anyway;
# see the note about Escape in --help.
exec "$BROWSER" \
    --class=syn-gfn \
    --user-data-dir="$PROFILE" \
    --use-angle=gl \
    --enable-features=VaapiVideoDecoder,AcceleratedVideoDecodeLinuxGL \
    --disable-features=UseChromeOSDirectVideoDecoder,Vulkan,DefaultANGLEVulkan,VulkanFromANGLE \
    $EXTRA \
    "$URL"
