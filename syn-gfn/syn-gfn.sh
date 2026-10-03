#!/usr/bin/env bash
#
# syn-gfn — GeForce NOW: NVIDIA's own app when it is installed, otherwise a
# browser that can hold your mouse.
#
# ⇒ NVIDIA'S APP COMES FIRST. NVIDIA ships a native Linux client,
# com.nvidia.geforcenow (out of beta 2026-08), from its OWN Flatpak remote —
# not Flathub, which is why a Flathub search finds only the community
# Electron wrapper described below and the browser path was built without it
# ever being compared. Measured on the desktop 2026-10-02 (RTX 3060, synui,
# Fortnite):
#
#   * it runs through Xwayland, which is what NVIDIA asks for on its own GPUs
#     ("Xorg (X11) for NVIDIA GPUs"), and fills the window it is given;
#   * it decodes on the GPU through Vulkan Video ("Selected HEVC codec",
#     "decode h.265") — the browser path on an NVIDIA card has no VA-API
#     driver to do that with;
#   * its pointer lock HOLDS: `synctl pointer` read `locked persistent
#     bound:true` on every sample mid-match, and the app lets go by itself
#     whenever the game shows a cursor;
#   * 11 ms average round trip, 0.00% loss, 0.01% of frames dropped.
#
# Its window's app_id is `GeForceNOW`, not syn-gfn — see GeForceNOW.desktop
# for what that means to the dock.
#
# The browser path stays, for every machine without the app and for --web.
#
# ⛔ THE BROWSER PATH EXISTS BECAUSE THE ELECTRON CLIENT CANNOT DO THE TWO
# THINGS A GAME STREAM IS. Measured against geforcenow-electron 3.0.2
# (2026-08-24), the Flathub build, on this desktop:
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
# ⚠ AND FIREFOX IS NOW A SUPPORTED GEFORCE NOW BROWSER — ON WINDOWS. That
# landed 2026-08-19 with Firefox 154 and does not reach this platform: see the
# measurements above STREAM_BROWSERS before deciding it does.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
# https://github.com/velle999/SYNAPSE
set -u

URL_DEFAULT="https://play.geforcenow.com"
PROFILE_DEFAULT="${XDG_DATA_HOME:-$HOME/.local/share}/syn-gfn"

# ── NVIDIA's own app ────────────────────────────────────────────────────────
#
# `flatpak info` with no --user/--system looks in both installations, so a
# per-user install (no root needed) and a system one are both found.
NATIVE_APP="com.nvidia.geforcenow"
NATIVE_REPO="https://international.download.nvidia.com/GFNLinux/flatpak/geforcenow.flatpakrepo"

have_native() {
    command -v flatpak >/dev/null 2>&1 || return 1
    flatpak info "$NATIVE_APP" >/dev/null 2>&1
}

# ⚠ THE flathub LINE IS NOT DECORATION. NVIDIA's remote carries the app and
# not its runtime (org.freedesktop.Platform//24.08, from Flathub). The desktop
# was installed this way, --user with Flathub added --user first, so the steps
# below are the ones known to work — no root, no polkit prompt.
native_install_steps() {
    cat <<EOF
    flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
    flatpak remote-add --user --if-not-exists GeForceNOW $NATIVE_REPO
    flatpak install --user GeForceNOW $NATIVE_APP
EOF
}

# ── Which browsers can actually stream ──────────────────────────────────────
#
# ⚠ THIS USED TO SAY "GeForce NOW REFUSES FIREFOX OUTRIGHT". That stopped being
# true on 2026-08-19, when NVIDIA and Mozilla shipped official GeForce NOW
# support in Firefox 154 — and it is still not a reason to launch Firefox here,
# because that support is **Windows only**. Mozilla's own announcement is
# titled "GeForce NOW on Firefox for Windows"; on Linux, macOS, Android and iOS
# Firefox can browse the catalogue and cannot start a stream.
#
# ⚠ AND THE ENGINE HERE AGREES, which is worth knowing independently of what
# the service decides to allow. Measured on Firefox 154.0.1, Linux x86_64:
#
#     navigator.keyboard         ABSENT      <- Chromium's Keyboard Lock API
#     Element.requestPointerLock PRESENT
#     RTCPeerConnection          PRESENT
#     H.264 decode               yes
#
# Keyboard Lock is the one that matters. It is what lets Escape reach the game
# instead of the browser (see the note in --help), and Firefox does not
# implement Chromium's API for it at all. It has its own, unshipped by default:
# `requestFullscreen({keyboardLock: true})` behind
# `dom.fullscreen.keyboard_lock.enabled`. Different shape, different spelling —
# so "Firefox is supported now" does not make a GFN session on Linux usable,
# and this launcher must not pretend otherwise.
#
# The order is the preference. Vivaldi first because it is the one SynapseOS
# boxes tend to have; the rest are what people install instead.
STREAM_BROWSERS="vivaldi-stable chromium chromium-browser google-chrome-stable google-chrome brave brave-browser microsoft-edge-stable"

# Gecko. Known, named, and NOT chosen automatically — see GECKO_CAN_STREAM.
GECKO_BROWSERS="firefox firefox-esr firefox-developer-edition librewolf"

# ⇒ THE ONE LINE TO CHANGE WHEN GEFORCE NOW ENABLES FIREFOX ON LINUX.
# Set it to 1 and Gecko joins the automatic search as a last resort. Everything
# else — the profile, the prefs, the window class, --browser=firefox — already
# works, and is exercised by the tests. Re-check both halves before flipping
# it: that the service allows Linux Firefox, and that
# `dom.fullscreen.keyboard_lock.enabled` is on by default (or that setting it
# in the profile is enough), because a stream nobody can press Escape in is not
# a working stream.
GECKO_CAN_STREAM=0

# What the rest of the script asks instead of hardcoding a family.
engine_of() {  # engine_of <browser-command>
    case " $GECKO_BROWSERS " in *" $1 "*) printf 'gecko';  return 0 ;; esac
    printf 'chromium'
}

usage() {
    cat <<EOF
usage: syn-gfn
       syn-gfn --web [--browser=NAME] [--profile=DIR] [--url=URL] [-- <browser args>]
       syn-gfn --list-browsers
       syn-gfn --help

  GeForce NOW. Opens NVIDIA's own GeForce NOW app when it is installed;
  otherwise a browser, as a dedicated web app in its own profile.

  NVIDIA's app decodes the stream on the graphics card and holds the mouse
  while you play. It is not on Flathub; install it for your user with:

$(native_install_steps)

  --web            use a browser even when NVIDIA's app is installed
                   (--browser, --profile, --url and -- all imply it)
  --browser=NAME   use this browser instead of the first one found
  --profile=DIR    profile directory (default: $PROFILE_DEFAULT)
  --url=URL        open somewhere else (default: $URL_DEFAULT)
  --list-browsers  print whether NVIDIA's app is installed, which browsers
                   are, and which one would be used
  --                everything after this is passed to the browser unchanged

  ⚠ FIREFOX IS SUPPORTED BY GEFORCE NOW, ON WINDOWS. Since 2026-08-19 the
  service officially supports Firefox 154 — for Windows browsers only. On
  Linux, Firefox loads the site and lists your library and will not start a
  game. It also has no Keyboard Lock API at all (that is Chromium's
  navigator.keyboard.lock(); Firefox has its own unshipped spelling), so even
  once a stream did start, Escape would leave full screen instead of opening
  the in-game menu. So this launcher still picks a Chromium-family browser,
  and --browser=firefox opens one anyway for browsing the catalogue.

  Its own profile, never your browsing one: a stream is a full-screen thing
  that wants the keyboard, and it has no business sharing a session, a cookie
  jar or a window with the tabs you had open.

  In a browser:

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
#
# The automatic search is streaming browsers only, unless GECKO_CAN_STREAM says
# Gecko has joined them. Picking Firefox on its own would swap "nothing
# happens" for "the catalogue loads and no game starts", which is worse: the
# first is obviously broken, the second looks like the account's fault.
search_order() {
    printf '%s' "$STREAM_BROWSERS"
    [ "$GECKO_CAN_STREAM" = 1 ] && printf ' %s' "$GECKO_BROWSERS"
    printf '\n'
}

find_browser() {
    local b
    for b in $(search_order); do
        command -v "$b" >/dev/null 2>&1 && { printf '%s' "$b"; return 0; }
    done
    return 1
}

have_gecko() {
    local b
    for b in $GECKO_BROWSERS; do
        command -v "$b" >/dev/null 2>&1 && { printf '%s' "$b"; return 0; }
    done
    return 1
}

list_browsers() {
    local b found=0 native=0
    printf "NVIDIA's app:\n"
    if have_native; then
        printf '  %-26s installed  <- would use this one\n' "$NATIVE_APP"
        native=1
    else
        printf '  %-26s -          (not on Flathub; see syn-gfn --help)\n' "$NATIVE_APP"
    fi

    printf '\nbrowsers that can stream, in preference order:\n'
    for b in $STREAM_BROWSERS; do
        if command -v "$b" >/dev/null 2>&1; then
            if [ "$found" -eq 0 ] && [ "$native" -eq 1 ]; then
                printf '  %-26s installed  <- with --web\n' "$b"
                found=1
            elif [ "$found" -eq 0 ]; then
                printf '  %-26s installed  <- would use this one\n' "$b"
                found=1
            else
                printf '  %-26s installed\n' "$b"
            fi
        else
            printf '  %-26s -\n' "$b"
        fi
    done

    printf '\ncatalogue only on Linux:\n'
    for b in $GECKO_BROWSERS; do
        if command -v "$b" >/dev/null 2>&1; then
            printf '  %-26s installed\n' "$b"
        else
            printf '  %-26s -\n' "$b"
        fi
    done
    printf '  GeForce NOW added Firefox support on 2026-08-19, for WINDOWS.\n'
    printf '  On Linux these can browse your library but cannot start a game,\n'
    printf '  and Firefox has no Keyboard Lock API, so Escape would not reach it.\n'
    printf '  syn-gfn --browser=firefox opens one anyway.\n'

    if [ "$found" -eq 0 ] && [ "$native" -eq 0 ]; then
        printf "\n  Nothing here can stream. Install NVIDIA's app (syn-gfn --help),\n"
        printf '  or a browser: synpkg install chromium\n'
    fi
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
seed_chromium_permissions() {  # seed_chromium_permissions <profile-dir> <url>
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

# ── The same three things, spelled Gecko ────────────────────────────────────
#
# Firefox has no content-settings JSON to write. What it has is `user.js` in
# the profile root, re-applied at every startup — which is the wrong thing for
# a browsing profile (it overrides what the user changes in the UI, every time)
# and exactly the right thing for a profile that exists to hold one origin and
# is rebuilt by this script anyway.
#
# ⚠ THE KEYBOARD LOCK PREF IS NOT CHROMIUM'S API UNDER ANOTHER NAME. Firefox
# does not implement navigator.keyboard.lock(); it implements
# requestFullscreen({keyboardLock: true}), gated on
# dom.fullscreen.keyboard_lock.enabled. Setting it here costs nothing and means
# that if GeForce NOW ever asks for it on Linux, the answer is already yes —
# but the page has to ask in Firefox's spelling, and today it does not.
seed_gecko_prefs() {  # seed_gecko_prefs <profile-dir>
    local js="$1/user.js"
    mkdir -p "$1" || return 0
    cat > "$js" <<'JS'
// Written by syn-gfn. This profile holds one site; edit the launcher, not this.
user_pref("dom.fullscreen.keyboard_lock.enabled", true);
// The full-screen and pointer-lock nags are the same problem the Chromium
// content settings solve: they are drawn over a stream, where they are both
// unreadable and in the way.
user_pref("full-screen-api.warning.timeout", 0);
user_pref("full-screen-api.warning.delay", 0);
user_pref("pointer-lock-api.warning.timeout", 0);
// Hardware video decode, which is the whole point on a stream.
user_pref("media.ffmpeg.vaapi.enabled", true);
user_pref("media.hardware-video-decoding.force-enabled", true);
// A dedicated profile should not ask to be your default browser, show a
// first-run tour, or open a what's-new tab in front of the game.
user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("browser.aboutwelcome.enabled", false);
user_pref("browser.startup.homepage_override.mstone", "ignore");
user_pref("datareporting.policy.firstRunURL", "");
user_pref("browser.messaging-system.whatsNewPanel.enabled", false);
JS
}

# ── Arguments ───────────────────────────────────────────────────────────────
BROWSER=""
PROFILE="$PROFILE_DEFAULT"
URL="$URL_DEFAULT"
PASSTHROUGH=0
EXTRA=""
WEB=0

# Every browser option implies --web: each one only means something to a
# browser, so somebody who typed one is asking for the browser path, and
# quietly opening NVIDIA's app instead would throw the option away.
while [ $# -gt 0 ]; do
    if [ "$PASSTHROUGH" -eq 1 ]; then
        EXTRA="$EXTRA $1"; shift; continue
    fi
    case "$1" in
        --web)          WEB=1 ;;
        --browser=*)    BROWSER=${1#*=}; WEB=1 ;;
        --profile=*)    PROFILE=${1#*=}; WEB=1 ;;
        --url=*)        URL=${1#*=}; WEB=1 ;;
        --list-browsers) list_browsers; exit 0 ;;
        -h|--help)      usage; exit 0 ;;
        --)             PASSTHROUGH=1; WEB=1 ;;
        *)  printf 'syn-gfn: unknown option %s\n' "$1" >&2
            printf '  try: syn-gfn --help\n' >&2
            exit 2 ;;
    esac
    shift
done

# ⚠ THE SESSION EXPORTS MANGOHUD=1, which loads MangoHud's Vulkan layer into
# every Vulkan client — and a browser is one, and NVIDIA's app draws the
# stream with Vulkan. On AMD that layer segfaults the client inside its own
# vkCreateDevice hook and on NVIDIA it never does, which is how it took
# synui-wpengine and synstudio down on the ThinkPad while the desktop stayed
# happy. DISABLE_MANGOHUD is the manifest's own disable_environment and beats
# the enable; MANGOHUD=0 goes with it for the OpenGL side. The same pair those
# two launchers set, for the same reason.
#
# ⚠ NOT MEASURED AS A CRASH HERE. Vivaldi starts fine under MANGOHUD=1 on this
# NVIDIA box — the startup crash that cost an evening was --start-fullscreen,
# below. This is the AMD class, kept off because a cloud stream has no frame
# times of its own worth measuring: the numbers that matter are the service's.
export DISABLE_MANGOHUD=1 MANGOHUD=0

# NVIDIA's app, when it is here and nobody asked for a browser. No arguments:
# it keeps its own sign-in, settings and window, and takes no URL.
if [ "$WEB" -eq 0 ] && have_native; then
    exec flatpak run "$NATIVE_APP"
fi

if [ -n "$BROWSER" ]; then
    command -v "$BROWSER" >/dev/null 2>&1 || {
        printf 'syn-gfn: %s is not installed\n' "$BROWSER" >&2
        printf '  syn-gfn --list-browsers shows what is\n' >&2
        exit 1; }
else
    BROWSER=$(find_browser) || {
        # ⚠ THE COMMON CASE ON A STOCK BOX, and it deserves better than "no
        # Chromium-family browser installed". SynapseOS ships Firefox and ticks
        # neither Chromium nor Vivaldi, so this is what somebody gets by
        # clicking the menu entry on a fresh install — and being told the
        # browser they DO have is not merely absent from a list, but cannot
        # stream on this platform and why, is the difference between a fix and
        # a shrug.
        #
        # Reached with NVIDIA's app installed only through --web or a browser
        # option, so the useful answer is that the app is right there.
        if have_native; then
            printf 'syn-gfn: no browser installed that can stream GeForce NOW\n' >&2
            printf "  NVIDIA's app is installed: run syn-gfn with no options\n" >&2
        elif gecko=$(have_gecko); then
            printf 'syn-gfn: %s cannot stream GeForce NOW on Linux.\n' "$gecko" >&2
            printf '\n' >&2
            printf '  NVIDIA and Mozilla added GeForce NOW to Firefox on 2026-08-19 —\n' >&2
            printf '  for Windows. On Linux Firefox can browse your library but cannot\n' >&2
            printf '  start a game, and it has no Keyboard Lock API, so Escape would\n' >&2
            printf '  open the browser menu instead of the game menu.\n' >&2
            printf '\n' >&2
            printf "  To play:     install NVIDIA's GeForce NOW app (syn-gfn --help)\n" >&2
            printf '               or a browser: synpkg install chromium\n' >&2
            printf '  To look:     syn-gfn --browser=%s\n' "$gecko" >&2
        else
            printf 'syn-gfn: nothing installed that can stream GeForce NOW\n' >&2
            printf "  install NVIDIA's GeForce NOW app (syn-gfn --help shows how),\n" >&2
            printf '  or one of these browsers:\n' >&2
            printf '    %s\n' $STREAM_BROWSERS >&2
            printf '  e.g. synpkg install chromium\n' >&2
        fi
        exit 1; }
fi

ENGINE=$(engine_of "$BROWSER")

# Asked for by name, on a platform where it cannot finish the job. Said once,
# plainly, and then it launches anyway — somebody who typed --browser=firefox
# has been told what it does and is entitled to their catalogue.
if [ "$ENGINE" = gecko ] && [ "$GECKO_CAN_STREAM" != 1 ]; then
    printf 'syn-gfn: %s can browse the catalogue but not start a game on Linux.\n' "$BROWSER" >&2
    printf '  GeForce NOW ships Firefox support on Windows only (2026-08-19).\n' >&2
fi

mkdir -p "$PROFILE" || exit 1

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
# ⛔ AND NOT ONE OF THESE FLAGS MEANS ANYTHING TO FIREFOX. --user-data-dir,
# --use-angle and --enable-features are Chromium's; Gecko takes a profile with
# --profile and would treat the rest as URLs to open, which is a browser
# starting up with four tabs of nonsense rather than an error anybody can read.
# Two launch paths because there are two engines, not because it reads nicer.
if [ "$ENGINE" = gecko ]; then
    seed_gecko_prefs "$PROFILE"

    # --new-instance because the ordinary Firefox is very likely already
    # running, and without it this hands the URL to that process: the stream
    # would open as a tab in the browsing session, in the browsing profile,
    # which is the one thing this launcher exists to avoid.
    #
    # ⚠ --class is accepted and is a GTK argument, not a Firefox one. Under
    # Wayland Firefox sets its own app_id, so the dock pin cannot be assumed to
    # follow the way it does for Chromium's --class=syn-gfn; see the note in
    # the Chromium branch and syn-gfn.desktop's StartupWMClass.
    exec "$BROWSER" \
        --class=syn-gfn \
        --name=syn-gfn \
        --new-instance \
        --profile "$PROFILE" \
        $EXTRA \
        "$URL"
fi

seed_chromium_permissions "$PROFILE" "$URL"

exec "$BROWSER" \
    --class=syn-gfn \
    --user-data-dir="$PROFILE" \
    --use-angle=gl \
    --enable-features=VaapiVideoDecoder,AcceleratedVideoDecodeLinuxGL \
    --disable-features=UseChromeOSDirectVideoDecoder,Vulkan,DefaultANGLEVulkan,VulkanFromANGLE \
    $EXTRA \
    "$URL"
