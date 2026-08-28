#!/bin/bash
# syn_rgb.sh — the desktop's accent reaches the lights, and only when asked.
#
# ⛔ NO REAL HARDWARE IS TOUCHED. `openrgb` here is a stub on PATH that writes
# down what it was asked for — the same trick the launcher tests use for
# quickshell — because a test that changed the colour of the machine running
# it would be a test nobody could run twice in a room with the lights on.
#
# What is asserted is the whole contract: off does nothing, on follows the
# wallpaper, a greyscale wallpaper leaves the lights alone, brightness is a
# darker colour rather than a different one, and a device with no `direct`
# mode still gets its colour.
set -u

SCRIPT=${1:-$(dirname "$0")/../tools/syn-rgb}
[ -x "$SCRIPT" ] || { echo "no syn-rgb at $SCRIPT" >&2; exit 1; }
SCRIPT=$(readlink -f "$SCRIPT")

TMP=$(mktemp -d /tmp/syn-rgb-test-XXXXXX) || exit 1
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$TMP/bin" "$TMP/cfg/synui"
cat > "$TMP/bin/openrgb" <<'STUB'
#!/bin/sh
# ⚠ THE LISTING IS NOT A PUSH, and it is counted separately. syn-rgb enumerates
# to learn which devices can hold a colour on their own; a listing logged as a
# push would make every "how many times did it touch the hardware" assertion
# below read one too high.
case "$*" in
    *--list-devices*)
        echo "openrgb $*" >> "$RGBLIST"
        # Two devices, and the difference between them is the whole point: RAM
        # that offers Direct and nothing else, and a mainboard that also has a
        # Static its own controller runs.
        printf '0: Stub DRAM\n  Modes: [Direct] Custom '"'"'Color Shift'"'"'\n\n'
        printf '1: Stub Mainboard\n  Modes: [Direct] Off Static Breathing\n\n'
        # A third device that is absent (RGBGONE unset), present (RGBGONE=1),
        # or LATE: RGBLATE=N holds it back until the Nth listing, which is what
        # the SDK server does at login — it answers with the controllers it has
        # registered so far and says nothing about the ones still coming.
        if [ -n "${RGBGONE:-}" ]; then
            n=$(wc -l < "$RGBLIST")
            if [ -z "${RGBLATE:-}" ] || [ "$n" -ge "${RGBLATE:-0}" ]; then
                printf '2: Stub Keyboard\n  Modes: [Direct] Static\n\n'
            fi
        fi
        exit 0 ;;
esac
echo "openrgb $*" >> "$RGBLOG"
# A device that has no Direct mode is the ordinary case for half the hardware
# OpenRGB drives, so the stub can be told to be one.
case "${RGBNODIRECT:-}" in
    1) case "$*" in *"-m direct"*|*"--mode direct"*) exit 1 ;; esac ;;
esac
# A device in the map that is no longer on the bus: openrgb fails the whole
# call, which is what has to send syn-rgb back to enumerate again.
case "${RGBGONE:-}" in
    '') case "$*" in *"-d 2"*) exit 1 ;; esac ;;
esac
# The wallpaper can change WHILE openrgb is running — that nine-second window is
# the whole reason cmd_apply re-checks — so the stub can be told to move it. n is
# how many calls have been made; RGBCHASE_TIMES how many of them rewrite.
if [ -n "${RGBCHASE:-}" ]; then
    n=$(cat "$RGBCHASE.n" 2>/dev/null || echo 0)
    n=$((n + 1))
    echo "$n" > "$RGBCHASE.n"
    if [ "$n" -le "${RGBCHASE_TIMES:-1}" ]; then
        printf 'use=yes\nok=yes\naccent=#%02d0000\n' "$((n + 1))" > "$RGBCHASE"
    fi
fi
exit 0
STUB
chmod +x "$TMP/bin/openrgb"

export XDG_CONFIG_HOME=$TMP/cfg
export SYN_RGB_BIN=$TMP/bin/openrgb
export RGBLOG=$TMP/log
export RGBLIST=$TMP/listlog
# ⛔ PINNED, because the answer to "is an OpenRGB SDK server listening" decides
# which of two paths syn-rgb takes, and on a developer's own desktop that
# answer is yes — the tests would then quietly stop covering the machine
# without one. Each half is asked for by name below.
export SYN_RGB_SDK=no
# The settle wait is in whole seconds of real time on the hardware. Nothing
# here is worth six of them.
export SYN_RGB_SETTLE_MAX=5
export SYN_RGB_SETTLE_QUIET=2
PAL=$TMP/cfg/synui/palette.state

pass=0; fail=0
ok()  { pass=$((pass + 1)); }
bad() { fail=$((fail + 1)); printf '  FAIL  %s\n' "$*"; }
check() { if [ "$2" = "$3" ]; then ok; else bad "$1: expected [$2] got [$3]"; fi; }
calls()  { wc -l < "$RGBLOG" | tr -d ' '; }
probes() { wc -l < "$RGBLIST" 2>/dev/null | tr -d ' '; }

wallpaper() { printf 'use=yes\nok=%s\naccent=%s\n' "$1" "$2" > "$PAL"; }

: > "$RGBLOG"; : > "$RGBLIST"
wallpaper yes '#9B610F'

# ---- off is off ---------------------------------------------------------
#
# ⛔ It writes to HARDWARE, so nothing happens until somebody says so. An
# update that took over the lights on a machine already set up the way its
# owner wanted would be the worst kind of feature.
check "a fresh install is off" "no" \
      "$("$SCRIPT" status | awk -F'\t' '/^on/{print $2}')"
"$SCRIPT" apply >/dev/null
check "and applying does nothing at all" "0" "$(calls)"

# ---- on follows the wallpaper -------------------------------------------
"$SCRIPT" on >/dev/null
check "on pushes the accent" "openrgb -d 0 -m direct -c 9B610F -d 1 -m static -c 9B610F" "$(tail -1 "$RGBLOG")"
wallpaper yes '#3355FF'
"$SCRIPT" apply >/dev/null
check "a new wallpaper is a new colour" "openrgb -d 0 -m direct -c 3355FF -d 1 -m static -c 3355FF" \
      "$(tail -1 "$RGBLOG")"

# ⚠ `ok` is the PICTURE's own answer: a greyscale wallpaper offers no hue, so
# the lights KEEP what they have rather than snapping to something invented.
before=$(calls)
wallpaper no '#3355FF'
"$SCRIPT" apply >/dev/null
check "a wallpaper with no hue leaves them alone" "$before" "$(calls)"
wallpaper yes '#3355FF'

# ---- brightness is a darker colour, not a different one -----------------
"$SCRIPT" brightness 0.5 >/dev/null
check "half brightness halves the channels" "openrgb -d 0 -m direct -c 192A7F -d 1 -m static -c 192A7F" \
      "$(tail -1 "$RGBLOG")"
"$SCRIPT" brightness 1.0 >/dev/null

# ---- the theme is the other source --------------------------------------
printf '{\n  "accent": [254, 128, 25]\n}\n' > "$TMP/cfg/synui/theme.json"
"$SCRIPT" follow theme >/dev/null
check "the theme's accent can be followed instead" \
      "openrgb -d 0 -m direct -c FE8019 -d 1 -m static -c FE8019" "$(tail -1 "$RGBLOG")"
"$SCRIPT" follow accent >/dev/null

# ---- the mode is per DEVICE, and it is one the hardware KEEPS ------------
#
# ⛔ THE BUG THIS REPLACES TURNED THE LIGHTS OFF. `direct` means "show what the
# host is streaming", and an ASUS Aura USB board goes dark the moment that host
# exits — which, for a oneshot, is always. It read as every device lighting up
# at login and then all of them but the RAM and the keyboard dropping out
# seconds later: those two latch their last direct frame, the board does not.
#
# So a device that offers Static gets Static, which its own controller runs
# with nothing attached, and a device that has only Direct still gets Direct.
check "a device that can hold a colour is given static" \
      "openrgb -d 0 -m direct -c 3355FF -d 1 -m static -c 3355FF" \
      "$(tail -1 "$RGBLOG")"

# ⚠ And the enumeration is paid ONCE. `--list-devices` is a full bus probe —
# 8.6 seconds on the hardware this was written for — so a push that re-read the
# device list every time would be the nine-second hole all over again.
before=$(probes)
"$SCRIPT" apply >/dev/null
check "and the device list is not re-read for every push" "$before" "$(probes)"

# ⚠ A device that has GONE is the one thing a cached map cannot ride out:
# openrgb fails the WHOLE call rather than the missing device, so a stale map
# has to be noticed by the push failing, rebuilt, and the push retried. Nothing
# else in this program watches for hardware changing under it.
rm -f "$TMP/cfg/synui/rgb.devices"
RGBGONE=1 "$SCRIPT" apply >/dev/null      # enumerated with a third device on it
before=$(probes)
: > "$RGBLOG"
"$SCRIPT" apply >/dev/null                # and now that device is not there
check "a device that went away rebuilds the map" "$((before + 1))" "$(probes)"
check "and the colour still lands on what is left" \
      "openrgb -d 0 -m direct -c 3355FF -d 1 -m static -c 3355FF" \
      "$(tail -1 "$RGBLOG")"

# ---- a device that ARRIVED --------------------------------------------------
#
# ⛔ THIS IS THE ONE THE CACHE COULD NOT SEE, AND IT IS THE ONE THAT HAPPENS AT
# EVERY LOGIN. A device that has GONE fails the whole openrgb call, and the test
# above is that failure rebuilding the map. A device that has ARRIVED fails
# nothing: the push still succeeds on every device the map does know, so nothing
# ever asks again.
#
# On velle's box the SDK server has the two i2c DIMMs 0.6s after it starts and
# the USB mainboard and keyboard only at 7.8s — and syn-rgb.service runs about a
# second in. So the map cached at login named the RAM, every push after it
# pushed the RAM, and the board and the keyboard sat on whatever colour they
# happened to be holding until the next reboot. It read as the lights being
# stuck, with the RAM alone still following the wallpaper.
: > "$RGBLOG"
RGBGONE=1 "$SCRIPT" apply >/dev/null
check "with no server the cached map stands" \
      "openrgb -d 0 -m direct -c 3355FF -d 1 -m static -c 3355FF" \
      "$(tail -1 "$RGBLOG")"

# ⚠ And with one up, the listing is a socket round-trip — 0.03s measured
# against 8.6 for the bus probe — so it is taken every push and the cache stops
# being able to hide anything.
: > "$RGBLOG"
SYN_RGB_SDK=yes RGBGONE=1 "$SCRIPT" apply >/dev/null
check "a device that arrived is picked up" \
      "openrgb -d 0 -m direct -c 3355FF -d 1 -m static -c 3355FF -d 2 -m static -c 3355FF" \
      "$(tail -1 "$RGBLOG")"

# ⚠ The mode is the one thing in that file a HUMAN may have set, and a refresh
# on every push would otherwise mean a hand edit lasted exactly one wallpaper.
sed -i 's/^2\tstatic\t/2\tdirect\t/' "$TMP/cfg/synui/rgb.devices"
: > "$RGBLOG"
SYN_RGB_SDK=yes RGBGONE=1 "$SCRIPT" apply >/dev/null
check "and a mode set by hand survives the refresh" \
      "openrgb -d 0 -m direct -c 3355FF -d 1 -m static -c 3355FF -d 2 -m direct -c 3355FF" \
      "$(tail -1 "$RGBLOG")"
sed -i 's/^2\tdirect\t/2\tstatic\t/' "$TMP/cfg/synui/rgb.devices"

# ⛔ AND A SHORT LISTING IS NOT BELIEVED. The count sits perfectly still at two
# for the seven seconds between the DIMMs and the USB devices, so "wait until it
# stops changing" answers the wrong question — the only thing that knows how
# many devices this machine has is the map from the last time they were all
# counted. A listing with fewer than that is asked again.
: > "$RGBLOG"; : > "$RGBLIST"
SYN_RGB_SDK=yes RGBGONE=1 RGBLATE=2 "$SCRIPT" apply 2>/dev/null >/dev/null
check "a listing taken mid-enumeration does not shrink the map" "3" \
      "$(grep -c . "$TMP/cfg/synui/rgb.devices")"
check "and the device it was missing still gets its colour" \
      "openrgb -d 0 -m direct -c 3355FF -d 1 -m static -c 3355FF -d 2 -m static -c 3355FF" \
      "$(tail -1 "$RGBLOG")"

# ⚠ ...but a device that is REALLY gone has to be let go of, or the map would
# only ever grow and every push after an unplug would fail. The wait is bounded,
# and what it finds at the end of it is the answer.
: > "$RGBLOG"; : > "$RGBLIST"
SYN_RGB_SDK=yes "$SCRIPT" apply 2>/dev/null >/dev/null
check "a device that stayed away is dropped" "2" \
      "$(grep -c . "$TMP/cfg/synui/rgb.devices")"
check "and the colour lands on what is left" \
      "openrgb -d 0 -m direct -c 3355FF -d 1 -m static -c 3355FF" \
      "$(tail -1 "$RGBLOG")"

# ---- a device with no Direct mode ---------------------------------------
#
# ⚠ Not every device has every mode, and `openrgb` fails the whole call rather
# than skipping the ones that cannot. One retry on `static` covers the half of
# the hardware that only does that, without a table of devices this program
# has no way to keep current.
: > "$RGBLOG"
RGBNODIRECT=1 "$SCRIPT" apply >/dev/null
check "a device with no direct mode still gets its colour" \
      "openrgb --mode static --color 3355FF" "$(tail -1 "$RGBLOG")"

# ---- a fixed colour, and the dark ----------------------------------------
"$SCRIPT" colour ff0000 >/dev/null
check "a fixed colour stays put" "openrgb -d 0 -m direct -c FF0000 -d 1 -m static -c FF0000" \
      "$(tail -1 "$RGBLOG")"
wallpaper yes '#11EE22'
"$SCRIPT" apply >/dev/null
check "and a new wallpaper does not move it" "openrgb -d 0 -m direct -c FF0000 -d 1 -m static -c FF0000" \
      "$(tail -1 "$RGBLOG")"
"$SCRIPT" dark >/dev/null
check "dark is black" "openrgb -d 0 -m direct -c 000000 -d 1 -m static -c 000000" "$(tail -1 "$RGBLOG")"

# ⚠ Turning the bridge off does NOT blank the lights: that is `dark`, and it is
# its own word. Off means stop following.
"$SCRIPT" off >/dev/null
before=$(calls)
wallpaper yes '#00FF00'
"$SCRIPT" apply >/dev/null
check "off stops following" "$before" "$(calls)"

# ---- a colour that moves DURING the push is not lost ---------------------
#
# ⛔ THE FAILURE THIS REPLACES WAS SILENT AND PERMANENT. syn-rgb.path cannot
# re-trigger while syn-rgb.service is still running, and systemd does not queue
# what it misses — measured with a scratch path unit: three writes two seconds
# apart during one nine-second run fired the service ONCE, on the FIRST value,
# and dropped the other two entirely. So a wallpaper changed while openrgb was
# probing the bus left the lights on the colour from the START of the burst,
# until something happened to touch the file again. `syn-rgb off; syn-rgb on`
# looked like a fix because `on` calls cmd_apply directly.
#
# Nothing outside this process can close that window: by the time the run exits,
# the event that would have re-triggered the unit is already gone. So the run
# re-reads before it exits, and the stub stands in for the wallpaper moving.
"$SCRIPT" on >/dev/null 2>&1
"$SCRIPT" follow accent >/dev/null
: > "$RGBLOG"; rm -f "$PAL.n"
wallpaper yes '#010000'
RGBCHASE=$PAL RGBCHASE_TIMES=1 "$SCRIPT" apply >/dev/null
check "a colour that moved mid-push is chased" \
      "openrgb -d 0 -m direct -c 020000 -d 1 -m static -c 020000" "$(tail -1 "$RGBLOG")"
check "and the one it moved from was pushed first" "2" "$(calls)"

# ⚠ The ordinary case must still be ONE push. A re-check that always went round
# twice would double every colour change's traffic to the hardware for nothing,
# and on a bus this slow that is the difference the whole change is about.
: > "$RGBLOG"; rm -f "$PAL.n"
wallpaper yes '#3355FF'
"$SCRIPT" apply >/dev/null
check "a colour that did not move is pushed once" "1" "$(calls)"

# ⚠ And the chase is BOUNDED. A wallpaper slideshow rewriting the file faster
# than openrgb can answer would otherwise pin this process open for ever.
: > "$RGBLOG"; rm -f "$PAL.n"
wallpaper yes '#010000'
RGBCHASE=$PAL RGBCHASE_TIMES=99 "$SCRIPT" apply >/dev/null
check "a colour that never settles stops at the bound" "4" "$(calls)"
rm -f "$PAL.n"
wallpaper yes '#3355FF'

# ---- openrgb missing is an ANSWER ---------------------------------------
#
# A desktop with nothing in it that glows should not pull in a lighting
# daemon, so this is an optdepend — and the answer when it is absent has to be
# a sentence rather than a red unit in `systemctl --user`.
"$SCRIPT" on >/dev/null 2>&1
out=$(SYN_RGB_BIN=$TMP/bin/nothing-here "$SCRIPT" apply 2>&1); rc=$?
check "a missing openrgb says so"  "3" "$rc"
case $out in
    *"not installed"*) ok ;;
    *) bad "and says which one: [$out]" ;;
esac
check "and says so once" "1" "$(printf '%s\n' "$out" | grep -c .)"

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
