#!/bin/bash
# synui-wallhaven — browse wallhaven.cc and make one of them the wallpaper.
#
# The wallpaper picker (Super+W, wppick.c) lists what is already on the disk.
# This is where more of it comes from: a grid of thumbnails from wallhaven.cc,
# filtered by category and sorted by what is popular, and picking one downloads
# it and hands it to the picker's own setter.
#
# ⛔ OFF BY DEFAULT, AND THAT IS NOT A DETAIL. This is the second thing in synui
# that talks to the internet — the weather is the other — and on a distribution
# careful about that, a wallpaper browser quietly resolving a third-party name
# because somebody pressed a key near the wallpaper picker would be exactly the
# surprise the weather switch exists to prevent. `synui-wallhaven on` is the
# whole opt-in, and nothing here resolves a name until it is given.
#
# ⛔ BUT THE WINDOW ASKS, AND THAT IS WHY `toggle` DOES NOT CHECK. It used to:
# the window path refused while the switch was off, on the reasoning that a key
# which opens a panel saying "turn me on" is worse than a key that says it
# itself. The key cannot say it. Super+Ctrl+W spawns this with no terminal
# attached, so the refusal went to a stderr nobody was reading and the key did
# nothing at all — reported as the keybind not responding. The window opens on
# its own switch instead, which is one place to say yes and no terminal needed;
# `search`, `get` and `set` still refuse, because those are the commands that
# actually reach the network and they run where somebody can read the answer.
#
# ── Why a script and not compositor code ────────────────────────────────────
#
# The picker is drawn by the compositor, and this is not, deliberately. A grid
# of remote thumbnails means HTTP, JSON and JPEG decoding, and the compositor's
# event loop is the one place on the machine where a slow DNS lookup is a frozen
# desktop. weather.c and news.c pay for their network with a worker thread, a
# condvar and a stop flag wired into libcurl's progress callback — roughly four
# hundred lines of machinery apiece before a single pixel. Quickshell's Image
# loads an https URL by itself, asynchronously, with a cache; measured against a
# real wallhaven thumbnail in a nested compositor before this was written.
#
# ⚠ SO THE TIE-IN IS `synctl dispatch wallpaper <path>`, which is the SAME entry
# point the Antiquity theme picker uses and which lands in wppick_set_path().
# There is one definition of "make this the wallpaper" and this is not a second
# one.
#
# ⚠ AND THE DOWNLOAD LANDS IN ~/Pictures/Wallpapers, which is the FIRST
# directory wppick_scan() walks. A wallpaper taken from wallhaven is a local
# wallpaper from then on: it is in Super+W's list, it survives this script being
# uninstalled, and nothing has to remember where it came from.
#
# ⛔ NO jq. synui's own code deliberately has none — see the depends= comment in
# the PKGBUILD; jq is there for the Omarchy plugins it hosts, not for this. The
# JSON is read by python3, which synui already depends on and which
# synui-media-inhibit already uses.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

API=https://wallhaven.cc/api/v1/search
STATE="${XDG_CONFIG_HOME:-$HOME/.config}/synui/wallhaven.state"
DEST="${SYNUI_WALLHAVEN_DIR:-$HOME/Pictures/Wallpapers}"
QML_USER="$HOME/.config/quickshell/synapse/wallhaven.qml"
QML_PKG=/usr/share/synui/quickshell/wallhaven.qml

# ⚠ AND A REFUSAL SAYS SO SOMEWHERE VISIBLE. Every caller that is not a
# terminal — a keybind, the picker's button, a .desktop entry — throws stderr
# away, so a bare `>&2` there is a command that failed in silence. Not a
# fallback for the message: the same message, in the only place that caller has.
die()  {
    printf 'synui-wallhaven: %s\n' "$*" >&2
    if [ ! -t 2 ] && command -v notify-send >/dev/null 2>&1; then
        notify-send -a synui "Wallhaven" "$*" || true
    fi
    exit 1
}

# ── the network switch ──────────────────────────────────────────────────────
#
# ⚠ ABSENT MEANS OFF. A missing state file is a machine that has never been
# asked, and the answer to "has this user agreed to talk to wallhaven.cc" is no
# until they say so.
sw_get() { [ -f "$STATE" ] && grep -qs '^enabled=1$' "$STATE"; }
sw_set() {
    mkdir -p "$(dirname "$STATE")" || die "cannot create $(dirname "$STATE")"
    printf 'enabled=%d\n' "$1" > "$STATE" || die "cannot write $STATE"
}

need_on() {
    sw_get || die "wallhaven is off. It is the only part of the wallpaper picker
  that leaves this machine, so it is opt-in:  synui-wallhaven on"
}

# ── search ──────────────────────────────────────────────────────────────────
#
# ⛔ THE RECORD IS TAB-SEPARATED AND ENGLISH, like every other --rec table in
# this project: the window matches on `category` and `id`, not on words. The
# only human-facing text here is the table `search` prints without --rec.
#
# ⚠ purity IS PINNED TO 100 — sfw — AND IS NOT A SETTING. The other two levels
# need an API key, which means asking somebody to paste a credential into a
# wallpaper picker, and a key in a config file that a screenshot of the settings
# window would leak. A wallpaper browser that ships one filter and applies it is
# a smaller promise and a keepable one.
cmd_search() {
    local cats=111 sort=toplist range=1M page=1 rec=0 q=
    while [ $# -gt 0 ]; do
        case "$1" in
            --categories=*) cats=${1#*=} ;;
            --sort=*)       sort=${1#*=} ;;
            --range=*)      range=${1#*=} ;;
            --page=*)       page=${1#*=} ;;
            --query=*)      q=${1#*=} ;;
            --rec)          rec=1 ;;
            *) die "unknown option for search: $1" ;;
        esac
        shift
    done

    case "$cats" in [01][01][01]) ;; *) die "categories is three bits — general, anime, people (111, 100, 011…), not '$cats'" ;; esac
    case "$sort" in toplist|date_added|random|views|favorites|relevance) ;;
        *) die "sort takes toplist, date_added, random, views, favorites or relevance (not '$sort')" ;; esac

    need_on
    SYNWH_API="$API" SYNWH_CATS="$cats" SYNWH_SORT="$sort" SYNWH_RANGE="$range" \
    SYNWH_PAGE="$page" SYNWH_Q="$q" SYNWH_REC="$rec" python3 - <<'PY'
import json, os, sys, urllib.parse, urllib.request

qs = {
    "categories": os.environ["SYNWH_CATS"],
    # See the header: sfw only, and not a setting.
    "purity":     "100",
    "sorting":    os.environ["SYNWH_SORT"],
    "topRange":   os.environ["SYNWH_RANGE"],
    "page":       os.environ["SYNWH_PAGE"],
}
q = os.environ.get("SYNWH_Q", "")
if q:
    qs["q"] = q
url = os.environ["SYNWH_API"] + "?" + urllib.parse.urlencode(qs)

try:
    # ⚠ A TIMEOUT, because this is called from a key press. Without one a
    # black-holed route leaves the grid empty for as long as the kernel's
    # default keeps the socket, with nothing on screen saying why.
    req = urllib.request.Request(url, headers={"User-Agent": "synui-wallhaven"})
    with urllib.request.urlopen(req, timeout=15) as r:
        doc = json.load(r)
except Exception as e:
    sys.stderr.write("synui-wallhaven: wallhaven.cc did not answer: %s\n" % e)
    sys.exit(1)

items = doc.get("data") or []
meta  = doc.get("meta") or {}

if os.environ["SYNWH_REC"] == "1":
    # ⛔ The header NAMES the columns and the window keys off those names.
    print("\t".join(("id", "thumb", "full", "resolution", "category", "ratio")))
    for w in items:
        print("\t".join((
            str(w.get("id", "")),
            str((w.get("thumbs") or {}).get("small", "")),
            str(w.get("path", "")),
            str(w.get("resolution", "")),
            str(w.get("category", "")),
            str(w.get("ratio", "")),
        )))
    # ⚠ The paging facts go on a LAST line rather than a second command: the
    # window needs "is there a next page" at the same moment it needs the rows,
    # and a second request to answer it would be a second chance to disagree.
    print("\t".join(("#page", str(meta.get("current_page", 1)),
                     str(meta.get("last_page", 1)), str(meta.get("total", 0)))))
else:
    for w in items:
        print("  %-8s %-12s %-8s %s" % (w.get("id", ""), w.get("resolution", ""),
                                        w.get("category", ""), w.get("path", "")))
    print("\n  page %s of %s (%s wallpapers)" % (meta.get("current_page", 1),
                                                 meta.get("last_page", 1),
                                                 meta.get("total", 0)))
PY
}

# ── one of them, onto the disk and onto the desktop ─────────────────────────
cmd_get() {
    local id=${1:-}
    [ -n "$id" ] || die "get needs a wallpaper id (synui-wallhaven search)"
    case "$id" in *[!a-zA-Z0-9]*) die "'$id' is not a wallhaven id" ;; esac
    need_on

    mkdir -p "$DEST" || die "cannot create $DEST"

    # ⚠ THE EXTENSION COMES FROM THE ANSWER, not from a guess. wallhaven serves
    # jpg and png and the path carries which; a .jpg holding PNG bytes decodes
    # fine here and is a surprise to everything else that ever reads the file.
    local url ext out
    url=$(SYNWH_ID="$id" python3 - <<'PY'
import json, os, sys, urllib.request
i = os.environ["SYNWH_ID"]
try:
    req = urllib.request.Request("https://wallhaven.cc/api/v1/w/" + i,
                                 headers={"User-Agent": "synui-wallhaven"})
    with urllib.request.urlopen(req, timeout=15) as r:
        print((json.load(r).get("data") or {}).get("path", ""))
except Exception as e:
    sys.stderr.write("synui-wallhaven: could not ask about %s: %s\n" % (i, e))
    sys.exit(1)
PY
) || exit 1
    [ -n "$url" ] || die "wallhaven has no wallpaper called '$id'"

    ext=${url##*.}
    case "$ext" in jpg|jpeg|png) ;; *) ext=jpg ;; esac
    out="$DEST/wallhaven-$id.$ext"

    # Already here from a previous pick: say so and use it. Re-downloading four
    # megabytes to set a wallpaper somebody has already chosen once is the kind
    # of thing that makes a picker feel slow for no reason.
    if [ ! -s "$out" ]; then
        # ⚠ To a temporary name first. A half-written file in the destination is
        # a broken entry in the Super+W list — wppick scans that directory — and
        # it would survive every future run because the size check above would
        # find it non-empty.
        local tmp="$out.part"
        curl --fail --silent --show-error --location --max-time 120 \
             --user-agent synui-wallhaven -o "$tmp" "$url" ||
            { rm -f "$tmp"; die "download failed: $url"; }
        mv -f "$tmp" "$out" || { rm -f "$tmp"; die "cannot write $out"; }
    fi
    printf '%s\n' "$out"
}

cmd_set() {
    local path
    path=$(cmd_get "$@") || exit 1
    # ⚠ THE ONE DEFINITION OF "make this the wallpaper". wppick_set_path() is
    # what Super+W and the theme picker both end up in; a second setter here
    # would be a second answer to per-monitor overrides, scaling mode and the
    # persisted state file.
    synctl dispatch wallpaper "$path" >/dev/null 2>&1 ||
        die "synui did not answer — is it running?"
    printf '%s\n' "$path"
}

# ── the window ──────────────────────────────────────────────────────────────
#
# The same toggle-across-a-process-boundary the welcome guide uses: closing it
# quits it, so "closed" and "not running" are one state. Ask a running instance
# to toggle; start one when nothing answers.
qml_path() { [ -f "$QML_USER" ] && printf '%s\n' "$QML_USER" || printf '%s\n' "$QML_PKG"; }

cmd_window() {
    local verb=$1 out=${2:-} qml; qml=$(qml_path)
    [ -f "$qml" ] || die "$qml is missing — is synui installed?"
    # ⛔ NO need_on HERE. The window asks — see the switch note at the top of
    # this file. A key press has no stderr, so a refusal here was a key that did
    # nothing; the window opening on "this is off, turn it on?" is the same
    # question in the one place the person who pressed the key can read it.
    # ⚠ THE OUTPUT RIDES ON BOTH PATHS AND NEITHER OF THEM IS THE OTHER. A
    # running instance is told over IPC; the FIRST one cannot be — `quickshell
    # -p file.qml` has no way to hand a config a positional argument, and the
    # IPC path only exists once a process is up. So the very first window, the
    # one this is for, is told by the environment, exactly as synui-welcome
    # tells the guide.
    #
    # ⛔ hide TAKES NO ARGUMENT. It is the one verb with nothing to place.
    if [ "$verb" = hide ]; then
        quickshell -p "$qml" ipc call wallhaven hide >/dev/null 2>&1
        return 0
    fi
    if quickshell -p "$qml" ipc call wallhaven "$verb" "$out" >/dev/null 2>&1; then
        return 0
    fi
    SYNUI_WALLHAVEN_OUTPUT="$out" exec quickshell -p "$qml"
}

usage() {
    cat <<'USAGE'
synui-wallhaven — browse wallhaven.cc and set one as the wallpaper

  synui-wallhaven                    open the browser (Super+Ctrl+W, or w in
                                     the Super+W wallpaper picker)
  synui-wallhaven toggle|show|hide [output]
                                     the window, across a process boundary.
                                     synui names the focused monitor when it is
                                     the caller; without a name the window opens
                                     on the first screen, never on all of them.
  synui-wallhaven search [--rec]     what is on wallhaven right now
       --categories=BITS             general/anime/people as three bits (111)
       --sort=WHICH                  toplist (popular), date_added, random,
                                     views, favorites, relevance
       --range=WHEN                  for toplist: 1d 3d 1w 1M 3M 6M 1y
       --page=N                      1-based; --rec's last line carries the last
       --query=TEXT                  a search term
  synui-wallhaven get <id>           download it, print where it landed
  synui-wallhaven set <id>           …and make it the wallpaper
  synui-wallhaven on | off | status  the network switch

Wallpapers land in ~/Pictures/Wallpapers, which is the first directory the
Super+W picker scans — so one taken from wallhaven is a local wallpaper from
then on.

⛔ OFF BY DEFAULT. This is the only part of the wallpaper picker that leaves
this machine. Nothing here resolves a name until it is switched on — either
with `synui-wallhaven on`, or from the window itself, which opens on that
question while the switch is off and browses nothing until it is answered.
Results are filtered to wallhaven's `sfw` purity, which is not a setting.

Inside the window, `w` opens the Super+W wallpaper picker, and `w` there comes
back here — one key flips between what is on the disk and what is not.
USAGE
}

case "${1:-toggle}" in
    toggle|show|hide) cmd_window "${1:-toggle}" "${2:-}" ;;
    search)  shift; cmd_search "$@" ;;
    get)     shift; cmd_get "$@" ;;
    set)     shift; cmd_set "$@" ;;
    on)      sw_set 1; echo "wallhaven: on" ;;
    off)     sw_set 0; echo "wallhaven: off" ;;
    status)  sw_get && echo on || echo off ;;
    help|--help|-h) usage ;;
    *) die "unknown command '$1' (try: synui-wallhaven help)" ;;
esac
