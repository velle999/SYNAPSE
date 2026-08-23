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
echo "openrgb $*" >> "$RGBLOG"
# A device that has no Direct mode is the ordinary case for half the hardware
# OpenRGB drives, so the stub can be told to be one.
case "${RGBNODIRECT:-}" in
    1) case "$*" in *"--mode direct"*) exit 1 ;; esac ;;
esac
exit 0
STUB
chmod +x "$TMP/bin/openrgb"

export XDG_CONFIG_HOME=$TMP/cfg
export SYN_RGB_BIN=$TMP/bin/openrgb
export RGBLOG=$TMP/log
PAL=$TMP/cfg/synui/palette.state

pass=0; fail=0
ok()  { pass=$((pass + 1)); }
bad() { fail=$((fail + 1)); printf '  FAIL  %s\n' "$*"; }
check() { if [ "$2" = "$3" ]; then ok; else bad "$1: expected [$2] got [$3]"; fi; }
calls() { wc -l < "$RGBLOG" | tr -d ' '; }

wallpaper() { printf 'use=yes\nok=%s\naccent=%s\n' "$1" "$2" > "$PAL"; }

: > "$RGBLOG"
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
check "on pushes the accent" "openrgb --mode direct --color 9B610F" "$(tail -1 "$RGBLOG")"
wallpaper yes '#3355FF'
"$SCRIPT" apply >/dev/null
check "a new wallpaper is a new colour" "openrgb --mode direct --color 3355FF" \
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
check "half brightness halves the channels" "openrgb --mode direct --color 192A7F" \
      "$(tail -1 "$RGBLOG")"
"$SCRIPT" brightness 1.0 >/dev/null

# ---- the theme is the other source --------------------------------------
printf '{\n  "accent": [254, 128, 25]\n}\n' > "$TMP/cfg/synui/theme.json"
"$SCRIPT" follow theme >/dev/null
check "the theme's accent can be followed instead" \
      "openrgb --mode direct --color FE8019" "$(tail -1 "$RGBLOG")"
"$SCRIPT" follow accent >/dev/null

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
check "a fixed colour stays put" "openrgb --mode direct --color FF0000" \
      "$(tail -1 "$RGBLOG")"
wallpaper yes '#11EE22'
"$SCRIPT" apply >/dev/null
check "and a new wallpaper does not move it" "openrgb --mode direct --color FF0000" \
      "$(tail -1 "$RGBLOG")"
"$SCRIPT" dark >/dev/null
check "dark is black" "openrgb --mode direct --color 000000" "$(tail -1 "$RGBLOG")"

# ⚠ Turning the bridge off does NOT blank the lights: that is `dark`, and it is
# its own word. Off means stop following.
"$SCRIPT" off >/dev/null
before=$(calls)
wallpaper yes '#00FF00'
"$SCRIPT" apply >/dev/null
check "off stops following" "$before" "$(calls)"

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
