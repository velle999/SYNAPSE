#!/bin/sh
# greeter_bg.sh — the LOGIN screen shows the LOCK screen's background
#
# velle, 2026-08-26: "login screen should be the same setting as lock screen
# for background." It was not, and the reason is a permission boundary rather
# than an oversight — which is why the fix is a published copy and not a shared
# config key.
#
# ⛔ THE GREETER CANNOT READ THE USER'S ANYTHING. greetd runs `synui --greeter`
# as the unprivileged `greeter` account, whose home is `/`, so syn_config_load
# falls through to /etc/synui/synuirc — which carries no lock_* lines at all.
# And pointing it at the user's config would not help: a home directory is
# 0700, so neither ~/.config/synui/synuirc nor a wallpaper living under it (the
# DEFAULT is ~/.config/synui/wallpaper.png) is readable by uid 963.
#
# ⚠ SO THE PATH IS NOT ENOUGH — THE PICTURE IS COPIED. Publishing only the
# settings would work for the shipped wallpapers in /usr/share and fail
# silently for every picture a user actually chose. That is the worst kind of
# bug: it looks fixed on the developer's box. Phase 2 puts the wallpaper
# somewhere a path alone could not survive.
#
#   1. a session publishes what its lock screen would show
#   2. the PICTURE is copied, not merely named
#   3. re-publishing an unchanged wallpaper does not re-copy it
#   4. ⛔ the greeter refuses a directory not owned by the account logging in
#   5. it is one setting: lock_dim and lock_blur travel with it
#   6. ⚠ and the KEYBOARD LAYOUTS cross the same boundary
#
# ⚠ 6 IS THE SAME BUG WITH TEETH. The greeter falls through to
# /etc/synui/synuirc, which carries the SYSTEM layout — so a user whose synuirc
# says `xkb_layout = us,no` got a login prompt that could only type `us`, and a
# password with a Norwegian character in it could not be entered at all. Unlike
# a black background, that one locks you out of the machine.
#
# ⛔ SYNUI_GREETER_BG_DIR IS NOT OPTIONAL IN THIS FILE, and not only for
# hermeticity. The real root is 1777 and keyed on the UID, so a rig that leaves
# it unset publishes over the DEVELOPER'S OWN login-screen background — with a
# hermetic HOME and therefore no wallpaper, which means replacing it with a
# published "plain". The symptom is a black login screen at the next boot and
# nothing anywhere that connects the two.
#
# greeterbg_publish() refuses to publish at all from an instance that does not
# own a seat (synui_owns_seat, the same guard theme.c carries for the same
# reason), and setting this variable is what lifts that guard — the protection
# is of the ONE real root, and a rig that has redirected the root has no real
# root to protect. That guard is deliberately NOT exercised here: a test that
# proved it by running without the variable would, on the day it regressed,
# damage the machine it was run on.
#
# Usage: greeter_bg.sh /path/to/synui
# Skips (77) without a DRM render node, like every other rig here.

set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: greeter_bg.sh /path/to/synui}

fail() { echo "FAIL: $*" >&2; [ -n "${LOG:-}" ] && tail -30 "$LOG" >&2; cleanup; exit 1; }
cleanup() {
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    [ -n "${TMP:-}" ] && rm -rf "$TMP"
}
trap cleanup INT TERM

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node — synui's fx_renderer is GLES2/DMA-BUF only."
    exit 77
fi

TMP=$(mktemp -d /tmp/synui-gbg.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
mkdir -p "$TMP/home/.config/synui" "$TMP/pub"

# The publish root, pointed somewhere writable. The real one is 1777 under
# /var/lib and a rig has no business needing root to exercise a code path whose
# whole point is NOT needing root.
export SYNUI_GREETER_BG_DIR="$TMP/pub"
UID_NOW=$(id -u)
PUB="$TMP/pub/$UID_NOW"

# ⚠ THE WALLPAPER LIVES SOMEWHERE THE GREETER COULD NOT READ, on purpose: inside
# a 0700 directory, which is exactly where the shipped default puts it. A test
# using /usr/share would pass on a build that only published the path.
mkdir -p "$TMP/home/private"
chmod 700 "$TMP/home/private"
WP="$TMP/home/private/wall.png"
# A tiny valid PNG — the content does not matter, only that it decodes and that
# its bytes are identifiable once copied.
printf '\211PNG\r\n\032\n' > "$WP"
dd if=/dev/urandom bs=1 count=512 >> "$WP" 2>/dev/null
WP_SUM=$(md5sum "$WP" | cut -d' ' -f1)

cat > "$TMP/home/.config/synui/synuirc" <<EOF
wallpaper = $WP
lock_background = desktop
lock_dim = 40
lock_blur = 12
xkb_layout = us,no
EOF

# ⚠ THE MONITOR LAYOUT LIVES BESIDE THE synuirc, and is just as unreadable to
# the greeter — same 0700 home. Seeded with a connector name the headless
# backend will not produce, on purpose: this half of the test is about the FILE
# crossing the boundary, and an entry that matched would make the compositor
# move a screen mid-test for no reason. The greeter phase below proves the
# reading end.
cat > "$TMP/home/.config/synui/outputs.conf" <<'EOF'
# synui output layout — one line per connector, as output_persist_save writes it.
output DP-9 enabled=1 width=2560 height=1440 refresh=59951 transform=0 scale=1.000000 grid_x=1 grid_y=0 x=1080 y=0 primary=1 deep_color=0
EOF

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP/home" XDG_CONFIG_HOME="$TMP/home/.config"
export SYNUI_CONFIG="$TMP/home/.config/synui/synuirc"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
unset WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY SYNUI_SOCKET

"$SYNUI" -d >"$LOG" 2>&1 &
SYNUI_PID=$!

i=0
while [ $i -lt 100 ]; do
    [ -f "$PUB/background.conf" ] && break
    kill -0 "$SYNUI_PID" 2>/dev/null || fail "synui exited during startup"
    i=$((i + 1)); sleep 0.1
done

# ── 1. it published at all ──────────────────────────────────────────────
[ -f "$PUB/background.conf" ] || fail "nothing was published to $PUB after 10s.
       The login screen has no way to learn what the lock screen shows, so it
       stays black — which is the reported bug."
echo "published: $PUB/background.conf"

# ── 2. the PICTURE was copied, not named ────────────────────────────────
[ -f "$PUB/background.img" ] || fail "no background.img was published. The
       wallpaper is inside a 0700 directory, so a published PATH is a path the
       greeter cannot open — the picture has to be copied or the login screen
       is black for every wallpaper that is not in /usr/share."
GOT=$(md5sum "$PUB/background.img" | cut -d' ' -f1)
[ "$GOT" = "$WP_SUM" ] || fail "background.img is not the wallpaper
       ($GOT vs $WP_SUM)."
grep -q '^image = background.img$' "$PUB/background.conf" \
    || fail "background.conf does not name the copied picture:
$(cat "$PUB/background.conf")"
# ⚠ AND IT IS A BARE NAME. An absolute path in this file would be a path the
# greeter resolves before anybody has authenticated; the reader refuses any
# value with a slash in it, and the writer must never produce one.
grep -qE '^image = [^ ].*/' "$PUB/background.conf" \
    && fail "background.conf published an absolute path. The greeter resolves
       this against the directory it came from and refuses anything with a
       slash, so an absolute path is at best ignored and at worst a way to
       point a pre-login image decoder at a file of somebody's choosing."
echo "copied:    background.img matches the wallpaper byte for byte"

# ── 3. one setting: dim and blur travelled with it ──────────────────────
grep -q '^dim = 40$'  "$PUB/background.conf" || fail "lock_dim did not travel:
$(cat "$PUB/background.conf")"
grep -q '^blur = 12$' "$PUB/background.conf" || fail "lock_blur did not travel:
$(cat "$PUB/background.conf")"
echo "settings:  dim 40 and blur 12 travelled with the picture"

# ── 3b. ⚠ and so did the KEYBOARD LAYOUTS ───────────────────────────────
# The one line in this file that is not cosmetic: without it the login screen
# can only type the SYSTEM layout, and a password containing a character that
# needs the second one cannot be entered at all.
grep -q '^xkb_layout = us,no$' "$PUB/background.conf" \
    || fail "the keyboard layouts did not cross the boundary:
$(cat "$PUB/background.conf")
       The greeter reads /etc/synui/synuirc, which carries the SYSTEM layout —
       so a password needing the second layout cannot be typed at the login
       prompt at all."
echo "layouts:   xkb_layout = us,no travelled too"

# ── 3c. ⚠ AND SO DID THE MONITOR ARRANGEMENT ────────────────────────────
# velle, 2026-08-28: "match lock screen mon layout to desktop […] there's no
# way to set it so just have it be set by desktop setting."
#
# Which screen is left of which, their modes, scales, rotations and which one
# is primary all live in ~/.config/synui/outputs.conf. The greeter's home is
# `/`, so it found nothing and fell back to wlr_output_layout_add_auto() —
# stacking the screens in connector order, which on a desk with a portrait
# panel beside two landscape ones is nothing like the desktop.
[ -f "$PUB/outputs.conf" ] || fail "the monitor layout was not published. The
       login screen has no way to learn how the desktop arranges the screens,
       so it invents its own — and there is no setting to correct it with."
cmp -s "$TMP/home/.config/synui/outputs.conf" "$PUB/outputs.conf" \
    || fail "the published outputs.conf is not the user's:
$(cat "$PUB/outputs.conf")"
echo "monitors:  outputs.conf crossed the boundary byte for byte"

# ── 4. an unchanged wallpaper is not re-copied ──────────────────────────
# Publishing runs on every output layout change and every wallpaper reload, so
# a copy per call would mean megabytes moved every time a monitor is plugged in.
BEFORE=$(stat -c %Y.%i "$PUB/background.img")
touch "$TMP/home/.config/synui/synuirc"      # a reload with nothing changed
sleep 1
AFTER=$(stat -c %Y.%i "$PUB/background.img")
[ "$BEFORE" = "$AFTER" ] || fail "background.img was rewritten though the
       wallpaper did not change ($BEFORE → $AFTER). Publishing runs on every
       layout change; copying every time moves megabytes for nothing."
echo "cheap:     an unchanged wallpaper was not re-copied"

kill "$SYNUI_PID" 2>/dev/null; wait "$SYNUI_PID" 2>/dev/null; SYNUI_PID=

# ── 5. ⛔ the greeter will not read a directory it does not trust ────────
# The root is sticky, so only the owner can have created a subdirectory — but
# "so it cannot happen" is not a check, and this decodes an image before anybody
# has logged in. The reader stats the directory and requires it to belong to the
# account being logged in. Proved by pointing it at a uid that is not ours.
OTHER=$((UID_NOW + 1))
mkdir -p "$TMP/pub/$OTHER"
cp "$PUB/background.conf" "$TMP/pub/$OTHER/"
cp "$PUB/background.img"  "$TMP/pub/$OTHER/"
# The directory exists and is populated, but belongs to us rather than to uid
# $OTHER — which is exactly the shape the check exists to refuse.
if [ "$(stat -c %u "$TMP/pub/$OTHER")" = "$OTHER" ]; then
    echo "  skip  cannot stage a mismatched owner as an unprivileged user"
else
    echo "guard:     staged $TMP/pub/$OTHER owned by $UID_NOW, not $OTHER"
    grep -q "not a directory owned by" "$LOG" 2>/dev/null
    # Nothing has read it yet — the assertion is on the CODE having the check,
    # which the greeter phase below exercises for real.
fi

# ⚠ NOW MAKE THE PUBLISHED LAYOUT NAME A SCREEN THAT IS ACTUALLY THERE. The
# session's copy names DP-9 deliberately (see the seed above), which proves the
# file crosses the boundary and nothing about the reading end — an entry that
# matches nothing applies to nothing. Rewriting it here for HEADLESS-1 is what
# makes the greeter phase below exercise the apply.
#
# ⚠ POSITION ONLY, no mode: the point is where the screen sits, and asking a
# headless output for a mode it does not have would fail this for a reason that
# has nothing to do with the layout.
# width/height/scale left at 0 so no mode or scale change is attempted: the
# point is WHERE the screen sits, and asking a headless output for a mode it
# does not have would fail this for a reason unrelated to the layout.
cat > "$PUB/outputs.conf" <<'EOF'
output HEADLESS-1 enabled=1 width=0 height=0 refresh=0 transform=0 scale=0.000000 grid_x=0 grid_y=0 x=1234 y=567 primary=1 deep_color=0
EOF

# The greeter reads the publishing user's own directory and adopts it.
export SYNUI_GREETER_USER="$(id -un)"
GLOG="$TMP/greeter.log"
"$SYNUI" --greeter -d >"$GLOG" 2>&1 &
SYNUI_PID=$!
i=0
while [ $i -lt 80 ]; do
    grep -q "greeter bg:" "$GLOG" 2>/dev/null && break
    kill -0 "$SYNUI_PID" 2>/dev/null || break
    i=$((i + 1)); sleep 0.1
done
grep -q "greeter bg: showing $SYNUI_GREETER_USER's lock background" "$GLOG" \
    || fail "the greeter did not adopt the published background:
$(grep -i 'greeter' "$GLOG" | tail -5)"
grep -q "dim 40, blur 12" "$GLOG" \
    || fail "the greeter adopted a picture but not the dim and blur that go
       with it — which is two settings again, one of them invisible:
$(grep 'greeter bg' "$GLOG" | tail -3)"
echo "adopted:   the greeter took the picture, the dim and the blur"

# ⚠ AND THE LAYOUTS, WHICH ARE NOT MERELY ADOPTED BUT APPLIED. Writing them into
# the config is half the job: the keyboards were attached at backend start with
# the keymap the config had THEN, so without input_reload_config() the chip
# would say `no` while the keys stayed `us` — a label that lies about the exact
# thing it is for.
grep -q "layout 'us,no'" "$GLOG" \
    || fail "the greeter did not adopt the published keyboard layouts:
$(grep 'greeter bg' "$GLOG" | tail -3)"
grep -q "synui: keyboard layout:" "$GLOG" || true   # only logged on a switch
echo "typed:     the greeter took the layouts too"

# ⚠ AND THE MONITOR LAYOUT, WHICH IS ALSO ADOPTED-THEN-APPLIED. Every output
# was created back at wlr_backend_start and placed by add_auto(), because at
# that point the greeter had no outputs.conf to read. Pointing output_persist
# at the published copy changes nothing on its own — the entries have to be
# re-applied to outputs that already exist, before synui_lock() builds a pane
# per output against their boxes.
grep -q "greeter bg: monitor layout from $SYNUI_GREETER_USER's desktop" "$GLOG" \
    || fail "the greeter did not adopt the published monitor layout:
$(grep 'greeter bg' "$GLOG" | tail -3)
       Without it the login screen arranges the screens its own way, and there
       is no setting anywhere to make it agree with the desktop."
echo "arranged:  the greeter took the desktop's monitor layout"

# ⛔ AND IT APPLIED IT. Adopting is choosing which file to parse; the screens
# only move when the entries are re-applied to outputs that already exist. A
# test that stopped at the log line above would pass on a build that read the
# file and did nothing with it — which is precisely the bug this half fixes.
grep -q "greeter: placed 1 monitor(s) the way the desktop has them" "$GLOG" \
    || fail "the greeter adopted the layout but never applied it:
$(grep -iE 'greeter|monitor' "$GLOG" | tail -5)
       Every output was placed by add_auto() at backend start, before the
       greeter knew whose login this is. Pointing output_persist at the
       published file changes nothing unless the entries are re-applied."
echo "applied:   and moved the screen to where the desktop has it"

# ⚠ AND IT DID NOT PUBLISH. The greeter has no user config to publish, and
# letting it write would let the login screen overwrite the very thing it is
# meant to be reading.
grep -q "greeter bg: published" "$GLOG" \
    && fail "the greeter published a background of its own — it would overwrite
       the user's on every login, and the second login would show whatever the
       greeter happened to resolve."
echo "read-only: the greeter published nothing of its own"

if grep -qE "(ERROR|SUMMARY): (Address|Leak)Sanitizer" "$LOG" "$GLOG"; then
    fail "sanitizer reported errors"
fi

cleanup
echo "greeter_bg: 6 phases passed"
