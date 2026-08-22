#!/bin/bash
# synstudio engine tests.
#
# Everything here drives the BINARY, with no display, no compositor and no
# GPU. That is possible because the window is only a renderer over this same
# command line, so a passing suite is evidence about the app and not merely
# about a library underneath it.
#
# Scratch lives in a mktemp -d and every path is inside it. Nothing in this
# file writes to a photograph, a sidecar or a config outside that directory.
set -u

BIN=${1:-./build/synstudio}
[ -x "$BIN" ] || { echo "no binary at $BIN" >&2; exit 1; }
BIN=$(readlink -f "$BIN")

# The effects are DATA, and an uninstalled build has not got them where the
# binary looks. Point it at the source tree — the same variable a third party
# would use to load a bundle of their own.
#
# Relative to THIS SCRIPT and not to the binary: a sanitiser build lives in
# /tmp, and a path worked out from there finds nothing and takes seventeen
# assertions down with it, all of them reading as broken effects.
if [ -z "${SYNSTUDIO_EFFECTS:-}" ]; then
    for d in "$(dirname "$0")/../data/effects" "$(dirname "$BIN")/../data/effects"; do
        [ -d "$d" ] || continue
        SYNSTUDIO_EFFECTS=$(cd "$d" && pwd)
        export SYNSTUDIO_EFFECTS
        break
    done
fi

# The looks are data in the same way, and found the same way.
if [ -z "${SYNSTUDIO_LOOKS:-}" ]; then
    for d in "$(dirname "$0")/../data/looks" "$(dirname "$BIN")/../data/looks"; do
        [ -d "$d" ] || continue
        SYNSTUDIO_LOOKS=$(cd "$d" && pwd)
        export SYNSTUDIO_LOOKS
        break
    done
fi

TMP=$(mktemp -d /tmp/synstudio-test-XXXXXX) || exit 1
trap 'rm -rf "$TMP"' EXIT

# Verdicts go to a FILE, not to shell variables.
#
# `seen` is always called on the right of a pipe, and bash runs that in a
# SUBSHELL — so every pass and every fail it recorded was thrown away when the
# subshell exited. A failing substring assertion printed its FAIL line and
# then reported "0 failed", and the suite exited 0. Sixty-odd assertions in
# this file are `seen`, and none of them could fail the build. An append to a
# file crosses a subshell boundary; a variable does not.
RESULTS=$TMP/.results
: > "$RESULTS"

ok()   { echo "p" >> "$RESULTS"; }
bad()  { echo "f $*" >> "$RESULTS"; printf '  FAIL  %s\n' "$*"; }

check() {   # check <label> <expected> <actual>
    if [ "$2" = "$3" ]; then ok; else bad "$1: expected [$2] got [$3]"; fi
}

# Substring assertions go through a FILE, never `| grep -q`. With pipefail a
# matching grep -q can kill the producer with SIGPIPE and return 141, which
# reads as a failure on a passing assertion — a documented trap in this repo
# that has broken two other suites.
seen() {    # seen <label> <needle> <<< haystack on stdin
    local label=$1 needle=$2 f="$TMP/.seen"
    cat > "$f"
    if grep -F -- "$needle" "$f" >/dev/null; then ok
    else bad "$label: no [$needle] in output"; head -3 "$f" | sed 's/^/        /'; fi
}

# Same contract as seen(), but the needle is an extended regex. Used where a
# plain substring would match somewhere harmless — `format=rgba` appears in
# almost every clip chain, so asserting it says nothing about whether the
# generated INPUT was the one that asked for alpha.
rxseen() {  # rxseen <label> <regex> <<< haystack on stdin
    local label=$1 rx=$2 f="$TMP/.seen"
    cat > "$f"
    if grep -E -- "$rx" "$f" >/dev/null; then ok
    else bad "$label: nothing matches /$rx/"; head -3 "$f" | sed 's/^/        /'; fi
}

# Two pixels the same to within a code value or two. Not bit for bit: the
# monitor and the export reach the same picture through graphs that round in
# different places — a moving edge, a blur, a blend — and anything larger than
# a couple of values is a different picture rather than a different rounding.
samepx() {  # samepx <label> <a> <b>
    if awk -v a="$2" -v b="$3" 'BEGIN {
            split(a, x, ","); split(b, y, ",");
            for (i = 1; i <= 3; i++) { d = x[i] - y[i]; if (d < 0) d = -d;
                                       if (d > 3) exit 1 }
            exit 0 }'; then ok
    else bad "$1: [$2] and [$3] are different pictures"; fi
}

notseen() {
    local label=$1 needle=$2 f="$TMP/.seen"
    cat > "$f"
    if grep -F -- "$needle" "$f" >/dev/null; then bad "$label: unexpected [$needle]"
    else ok; fi
}

# Floating point, to a tolerance. Comparing renderer output exactly would make
# the suite fail on a different libm.
near() {    # near <label> <expected> <actual> <tol>
    awk -v e="$2" -v a="$3" -v t="${4:-0.002}" -v l="$1" '
        BEGIN { d = e - a; if (d < 0) d = -d;
                if (d <= t) exit 0;
                printf "  FAIL  %s: expected %s got %s (tol %s)\n", l, e, a, t;
                exit 1 }'
    if [ $? -eq 0 ]; then ok; else echo "f $1" >> "$RESULTS"; fi
}

have() { command -v "$1" >/dev/null 2>&1; }

echo "== basics"
check "version"     "0.1.0"  "$($BIN version)"
check "key count"   "66"     "$($BIN keys | wc -l)"
$BIN help | seen "help mentions the sidecar" "sidecar"

echo "== the setting table"
check "unknown key rejected"  "1" "$($BIN pixel 0.5 0.5 0.5 --set nosuchkey=1 >/dev/null 2>&1; echo $?)"
$BIN pixel 0.5 0.5 0.5 --set nosuchkey=1 2>&1 >/dev/null | seen "and names it" "nosuchkey"
check "out of range rejected" "1" "$($BIN pixel 0.5 0.5 0.5 --set exposure=99 >/dev/null 2>&1; echo $?)"
check "in range accepted"     "0" "$($BIN pixel 0.5 0.5 0.5 --set exposure=2 >/dev/null 2>&1; echo $?)"
check "non-numeric rejected"  "1" "$($BIN pixel 0.5 0.5 0.5 --set exposure=warm >/dev/null 2>&1; echo $?)"

echo "== colour maths (the pixel probe: no file, no ffmpeg, no display)"
set -- $($BIN pixel 0.5 0.5 0.5)
near "identity R" 0.5 "$1"; near "identity G" 0.5 "$2"; near "identity B" 0.5 "$3"

# +1 stop is a DOUBLING IN LINEAR, not a doubling of the encoded value. 0.5
# encoded is 0.2140 linear; doubled and re-encoded that is 0.6858. Getting
# 0.75 here would mean the exposure control is operating in the wrong domain,
# which is the single most common way to get this wrong.
set -- $($BIN pixel 0.5 0.5 0.5 --set exposure=1)
near "exposure +1 is linear" 0.685757 "$1"
set -- $($BIN pixel 0.5 0.5 0.5 --set exposure=-1)
near "exposure -1 is linear" 0.360780 "$1"

# Raising the temperature must make the picture WARMER. It is trivial to get
# this backwards (a high-Kelvin illuminant is BLUE) and the result still looks
# like a working white balance control until someone uses it.
set -- $($BIN pixel 0.5 0.5 0.5 --set temp=8000)
warm_rb=$(awk -v r="$1" -v b="$3" 'BEGIN{print (r>b) ? "warm" : "cool"}')
check "temp 8000 is warm" "warm" "$warm_rb"
set -- $($BIN pixel 0.5 0.5 0.5 --set temp=4000)
cool_rb=$(awk -v r="$1" -v b="$3" 'BEGIN{print (r>b) ? "warm" : "cool"}')
check "temp 4000 is cool" "cool" "$cool_rb"
set -- $($BIN pixel 0.5 0.5 0.5 --set temp=6500)
near "temp 6500 is neutral R" 0.5 "$1" 0.01
near "temp 6500 is neutral B" 0.5 "$3" 0.01

# Positive tint is magenta, i.e. green comes DOWN.
set -- $($BIN pixel 0.5 0.5 0.5 --set tint=100)
green_down=$(awk -v g="$2" 'BEGIN{print (g<0.5) ? "yes" : "no"}')
check "tint + is magenta" "yes" "$green_down"

# Contrast must push the two sides APART and must stay monotone at the ends.
lo=$($BIN pixel 0.25 0.25 0.25 --set contrast=100 | cut -f1)
hi=$($BIN pixel 0.75 0.75 0.75 --set contrast=100 | cut -f1)
check "contrast darkens the low side" "yes" "$(awk -v v=$lo 'BEGIN{print (v<0.25)?"yes":"no"}')"
check "contrast lifts the high side"  "yes" "$(awk -v v=$hi 'BEGIN{print (v>0.75)?"yes":"no"}')"

# Monotonicity across the whole range, for both signs. A non-monotone tone
# curve inverts detail — a highlight comes back darker than its surround — and
# it is invisible in a single spot check.
for c in 100 -100; do
    prev=-1; mono=yes
    for i in $(seq 0 20); do
        v=$(awk -v i=$i 'BEGIN{printf "%.4f", i/20}')
        o=$($BIN pixel $v $v $v --set contrast=$c | cut -f1)
        [ "$(awk -v a=$o -v b=$prev 'BEGIN{print (a>=b-1e-6)?1:0}')" = 1 ] || mono=no
        prev=$o
    done
    check "contrast $c stays monotone" "yes" "$mono"
done

# Saturation -100 must produce a true grey.
set -- $($BIN pixel 0.8 0.3 0.3 --set saturation=-100)
check "desaturate makes grey" "yes" \
      "$(awk -v r=$1 -v g=$2 -v b=$3 'BEGIN{print (r==g && g==b)?"yes":"no"}')"

# An HSL band must move its OWN hue and leave the others alone.
red_before=$($BIN pixel 0.9 0.1 0.1 | cut -f1)
red_after=$($BIN pixel 0.9 0.1 0.1 --set hsl.red.sat=-100 | cut -f1)
check "red band desaturates red" "yes" \
      "$(awk -v a=$red_before -v b=$red_after 'BEGIN{print (b<a)?"yes":"no"}')"
blue_before=$($BIN pixel 0.1 0.1 0.9 | cut -f3)
blue_after=$($BIN pixel 0.1 0.1 0.9 --set hsl.red.sat=-100 | cut -f3)
near "red band leaves blue alone" "$blue_before" "$blue_after" 0.01

echo "== curves"
# A curve must pass exactly through the points it was given.
set -- $($BIN pixel 0.25 0.25 0.25 --set 'curve.rgb=0,0 0.25,0.15 0.75,0.85 1,1')
near "curve hits its control point" 0.15 "$1"
set -- $($BIN pixel 0.75 0.75 0.75 --set 'curve.rgb=0,0 0.25,0.15 0.75,0.85 1,1')
near "curve hits the other one" 0.85 "$1"
# Monotone interpolation: this curve has a flat region that a natural spline
# would overshoot on both sides.
prev=-1; mono=yes
for i in $(seq 0 30); do
    v=$(awk -v i=$i 'BEGIN{printf "%.4f", i/30}')
    o=$($BIN pixel $v $v $v --set 'curve.rgb=0,0 0.4,0.5 0.6,0.5 1,1' | cut -f1)
    [ "$(awk -v a=$o -v b=$prev 'BEGIN{print (a>=b-1e-6)?1:0}')" = 1 ] || mono=no
    prev=$o
done
check "flat-region curve never overshoots" "yes" "$mono"
# The shape that actually needs the tangent LIMITER, as opposed to the flat
# -secant special case above: a near-flat run into a very steep one. An
# unlimited cubic dives well below the low plateau on its way up, so the
# picture gets a dark band just before a bright one. Checked at fine
# resolution because the dip is narrow.
prev=-1; mono=yes
for i in $(seq 0 60); do
    v=$(awk -v i=$i 'BEGIN{printf "%.4f", i/60}')
    o=$($BIN pixel $v $v $v --set 'curve.rgb=0,0 0.45,0.06 0.55,0.94 1,1' | cut -f1)
    [ "$(awk -v a=$o -v b=$prev 'BEGIN{print (a>=b-1e-6)?1:0}')" = 1 ] || mono=no
    prev=$o
done
check "steep-into-flat curve never dips" "yes" "$mono"

check "a linear curve is identity" "0.500000" \
      "$($BIN pixel 0.5 0.5 0.5 --set 'curve.rgb=0,0 1,1' | cut -f1)"

echo "== the sidecar"
img=$TMP/pic.png
if have ffmpeg; then
    ffmpeg -v error -y -f lavfi -i testsrc2=size=160x120 -frames:v 1 "$img"
fi
if [ -f "$img" ]; then
    check "no sidecar is not an error" "0" "$($BIN get "$img" >/dev/null 2>&1; echo $?)"
    $BIN set "$img" exposure=0.75 vibrance=30 'curve.rgb=0,0 0.5,0.6 1,1'
    check "sidecar keeps exposure" "0.75" "$($BIN get "$img" exposure)"
    check "sidecar keeps vibrance" "30"   "$($BIN get "$img" vibrance)"
    $BIN get "$img" curve.rgb | seen "sidecar keeps the curve" "0.5,0.6"
    # The promise the whole sidecar design exists to keep: editing must not
    # touch a byte of the photograph.
    before=$(md5sum < "$img")
    $BIN set "$img" contrast=40 saturation=20
    $BIN render "$img" --out "$TMP/untouched.png" >/dev/null
    check "the original is never written" "$before" "$(md5sum < "$img")"
    $BIN set "$img" exposure=0.75 vibrance=30

    # Regression: reading a sidecar back must not TURN ON cropping. The
    # default crop.w is 1, and a setter that enabled cropping whenever a
    # crop.* key arrived flipped it on for every image on every load.
    check "loading does not enable crop" "0" "$($BIN get "$img" crop)"
    $BIN set "$img" crop.x=0.1
    check "but typing crop.x does" "1" "$($BIN get "$img" crop)"

    $BIN reset "$img"
    check "reset clears it" "0" "$($BIN get "$img" exposure)"
    check "reset twice is fine" "0" "$($BIN reset "$img" >/dev/null 2>&1; echo $?)"

    echo "== masks"
    check "first mask is 0" "0" "$($BIN mask "$img" add radial)"
    check "second mask is 1" "1" "$($BIN mask "$img" add linear)"
    check "two masks listed" "2" "$($BIN mask "$img" list | wc -l)"
    $BIN mask "$img" 0 exposure=-1 geom=0.5,0.5,0.25,0.25,0.6
    $BIN mask "$img" list | seen "mask geometry survives" "0.2500"
    check "masks survive a reload" "2" \
          "$($BIN get "$img" | awk -F'\t' '/^masks/{print $2}')"
    $BIN mask "$img" remove 0
    check "removing one leaves one" "1" "$($BIN mask "$img" list | wc -l)"
    $BIN mask "$img" list | seen "the right one was kept" "linear"
    $BIN reset "$img"

    echo "== rendering"
    $BIN render "$img" --out "$TMP/o.png" | seen "render reports its size" "width	160"
    check "render made a file" "yes" "$([ -s "$TMP/o.png" ] && echo yes || echo no)"

    $BIN render "$img" --out "$TMP/small.png" --size 80 | seen "--size shrinks it" "width	80"

    # Geometry changes the frame, and the reported size has to follow or the
    # GUI draws the old aspect ratio.
    $BIN set "$img" rotate90=1
    $BIN render "$img" --out "$TMP/rot.png" | seen "a quarter turn swaps w/h" "width	120"
    $BIN reset "$img"

    $BIN set "$img" crop.x=0.25 crop.y=0.25 crop.w=0.5 crop.h=0.5
    $BIN render "$img" --out "$TMP/crop.png" | seen "a crop halves the width" "width	80"
    $BIN reset "$img"

    echo "== histogram"
    $BIN histogram "$img" | seen "histogram counts pixels" "pixels	19200"
    check "256 bins" "256" "$($BIN histogram "$img" | grep -c '^bin')"
    # A hugely overexposed frame must REPORT clipping. A histogram that
    # silently clamps before counting cannot warn anyone about anything.
    clipped=$($BIN histogram "$img" --set exposure=6 | awk -F'\t' '/^clipped_white/{print ($2>1000)?"yes":"no"}')
    check "overexposure is reported" "yes" "$clipped"
else
    echo "  (skipping file tests: ffmpeg produced no fixture)"
fi

echo "== the LUT bridge"
$BIN lut --out "$TMP/id.cube" --lut-size 17
head -2 "$TMP/id.cube" | seen "cube declares its size" "LUT_3D_SIZE 17"
check "17^3 entries" "4913" "$(grep -c '^[0-9]' "$TMP/id.cube")"
# Red varies FASTEST in a .cube. If that ordering is wrong the LUT loads
# without complaint and swaps red and blue in the picture, which is the kind
# of bug that ships.
second=$(grep '^[0-9]' "$TMP/id.cube" | sed -n 2p)
check "red moves first" "yes" \
      "$(echo $second | awk '{print ($1>0.01 && $2<0.01 && $3<0.01)?"yes":"no"}')"
# An empty grade must bake to an identity LUT.
last=$(grep '^[0-9]' "$TMP/id.cube" | tail -1)
check "identity ends at white" "yes" \
      "$(echo $last | awk '{print ($1>0.99 && $2>0.99 && $3>0.99)?"yes":"no"}')"
$BIN lut --out "$TMP/sp.cube" --set clarity=50 2>&1 >/dev/null \
    | seen "it says what it could not bake" "not colour"

# The architectural claim, tested: applying the baked LUT through ffmpeg must
# reproduce what the still renderer does. If these two ever diverge, a grade
# built on a photograph would look different on a clip, and the whole reason
# the colour maths lives in one file is gone.
if [ -f "$img" ] && have ffmpeg; then
    $BIN set "$img" contrast=30 vibrance=50 temp=7200 saturation=-10
    $BIN render "$img" --out "$TMP/engine.png" --bits 8 >/dev/null
    $BIN lut --from "$img" --out "$TMP/g.cube"
    ffmpeg -v error -y -i "$img" -vf "lut3d=file=$TMP/g.cube:interp=tetrahedral" \
           "$TMP/vialut.png" 2>/dev/null
    if [ -s "$TMP/vialut.png" ]; then
        psnr=$(ffmpeg -v error -i "$TMP/engine.png" -i "$TMP/vialut.png" \
               -lavfi psnr=stats_file=- -f null - 2>&1 | awk -F'psnr_avg:' '/psnr_avg/{print $2+0}' | tail -1)
        check "LUT matches the still renderer (>45dB)" "yes" \
              "$(awk -v p="${psnr:-0}" 'BEGIN{print (p>45)?"yes":"no"}')"
    fi
    $BIN reset "$img"
fi

echo "== the timeline"
proj=$TMP/p.syntl
$BIN timeline new "$proj" --size 1920x1080 --fps 30
$BIN timeline show "$proj" | seen "project size" "size	1920	1080"
check "first track is 0" "0" "$($BIN timeline track "$proj" video V1)"
check "second track is 1" "1" "$($BIN timeline track "$proj" audio A1)"

if have ffmpeg; then
    mp4=$TMP/clip.mp4
    ffmpeg -v error -y -f lavfi -i testsrc2=size=320x240:rate=25:duration=3 \
           -c:v libx264 -pix_fmt yuv420p "$mp4" 2>/dev/null
    $BIN timeline clip "$proj" 0 "$mp4" --at 0 --in 0 --out-at 2 >/dev/null
    $BIN timeline clip "$proj" 0 "$mp4" --at 2 --in 0 --out-at 2 --fade-in 0.4 >/dev/null
    check "duration is the far edge" "4.0000" \
          "$($BIN timeline show "$proj" | awk -F'\t' '/^# duration/{print $2}')"

    $BIN timeline grade "$proj" 0 1 contrast=40 temp=8000
    $BIN timeline show "$proj" | seen "the grade round-trips" "temp	8000"

    graph=$($BIN timeline export "$proj" --out "$TMP/o.mp4" --print)
    echo "$graph" | seen "graph grades with a LUT" "lut3d=file="
    echo "$graph" | seen "graph fades" "fade=t=in"
    # Regression: the overlay chain must connect base -> bg1 -> bg2. Naming
    # every stage's input "bg" left bg1 dangling and ffmpeg rejected the whole
    # graph with a message about an unconnected output.
    echo "$graph" | seen "overlay chain is connected" "[bg1][v1]overlay"
    echo "$graph" | notseen "no unnumbered stage" "[bg][v"

    $BIN timeline export "$proj" --out "$TMP/o.mp4" >/dev/null 2>&1
    check "the export exists" "yes" "$([ -s "$TMP/o.mp4" ] && echo yes || echo no)"
    if [ -s "$TMP/o.mp4" ]; then
        d=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$TMP/o.mp4")
        check "exported length matches the timeline" "yes" \
              "$(awk -v d="$d" 'BEGIN{print (d>3.8 && d<4.2)?"yes":"no"}')"
    fi
fi

# A path with a space, a quote and a semicolon has to survive every layer:
# argv (never a shell), the tab-separated document, and the ffmpeg filter
# escaping.
if have ffmpeg; then
    odd="$TMP/a we;ird 'name'.mp4"
    cp "$TMP/clip.mp4" "$odd" 2>/dev/null
    if [ -f "$odd" ]; then
        p2=$TMP/odd.syntl
        $BIN timeline new "$p2"
        $BIN timeline track "$p2" video >/dev/null
        $BIN timeline clip "$p2" 0 "$odd" --at 0 --in 0 --out-at 1 >/dev/null
        $BIN timeline show "$p2" | seen "an awkward path round-trips" "a we;ird 'name'.mp4"
        $BIN timeline export "$p2" --out "$TMP/odd.mp4" >/dev/null 2>&1
        check "and exports" "yes" "$([ -s "$TMP/odd.mp4" ] && echo yes || echo no)"
    fi
fi

echo "== the video editor"

# The clip property table, which the inspector panel is built from exactly as
# the develop panel is built from `keys`.
check "clip keys are listed" "yes" \
      "$([ "$($BIN timeline keys | wc -l)" -ge 20 ] && echo yes || echo no)"
$BIN timeline keys | seen "an enum carries its choices" "none|dissolve|wipeleft"

vp=$TMP/v.syntl
$BIN timeline new "$vp" --size 640x360 --fps 25
$BIN timeline track "$vp" video V1 >/dev/null
$BIN timeline track "$vp" audio A1 >/dev/null

# Track state is editable, not just addable.
$BIN timeline track "$vp" 1 --mute 1 --name Music
$BIN timeline show "$vp" | seen "a track can be muted and renamed" "track	audio	Music	1	0"

if have ffmpeg; then
    vclip=$TMP/v.mp4
    ffmpeg -v error -y -f lavfi -i testsrc=size=640x360:rate=25:duration=8 \
           -c:v libx264 -pix_fmt yuv420p "$vclip" 2>/dev/null

    c0=$($BIN timeline clip "$vp" 0 "$vclip" --at 0 --in 0 --out-at 4)
    check "adding a clip reports its index" "0" "$c0"

    # ---- the property table --------------------------------------------
    #
    # fade.in, fade.out, trans.dur and speed are DOUBLE on the struct and the
    # rest are float. A table that wrote a float through a double's offset set
    # four bytes of a mantissa and read back zero — no warning, no error, the
    # value simply did not stick. Every one of them is asserted here because
    # that failure is invisible from the outside.
    $BIN timeline set "$vp" 0 0 fade.in=0.75 fade.out=1.5 speed=2 opacity=0.5
    check "fade.in is a double that sticks"  "0.75" "$($BIN timeline get "$vp" 0 0 fade.in)"
    check "fade.out is a double that sticks" "1.5"  "$($BIN timeline get "$vp" 0 0 fade.out)"
    check "speed is a double that sticks"    "2"    "$($BIN timeline get "$vp" 0 0 speed)"
    check "opacity is a float that sticks"   "0.5"  "$($BIN timeline get "$vp" 0 0 opacity)"
    $BIN timeline set "$vp" 0 0 speed=1 opacity=1 fade.in=0 fade.out=0

    check "an unknown property is refused" "1" \
          "$($BIN timeline set "$vp" 0 0 nosuchthing=1 >/dev/null 2>&1; echo $?)"
    check "an out-of-range property is refused" "1" \
          "$($BIN timeline set "$vp" 0 0 opacity=9 >/dev/null 2>&1; echo $?)"
    check "an enum takes its name" "dissolve" \
          "$($BIN timeline set "$vp" 0 0 trans=dissolve; $BIN timeline get "$vp" 0 0 trans)"
    check "an enum refuses a name it does not have" "1" \
          "$($BIN timeline set "$vp" 0 0 trans=starwipe >/dev/null 2>&1; echo $?)"
    $BIN timeline set "$vp" 0 0 trans=none

    # ---- editing --------------------------------------------------------
    $BIN timeline move "$vp" 0 0 --to 1.5
    check "move sets the timeline position" "1.500000" \
          "$($BIN timeline get "$vp" 0 0 | awk -F'\t' '/^tl_in/{print $2}')"

    # A head trim moves the source in point AND the position together, so the
    # frame under the cursor stays put. Asserting only one of the two would
    # pass for a trim that slid the shot sideways under the cut.
    $BIN timeline trim "$vp" 0 0 --head 0.5
    check "a head trim moves the in point"  "0.500000" \
          "$($BIN timeline get "$vp" 0 0 | awk -F'\t' '/^src_in/{print $2}')"
    check "and the position with it"        "2.000000" \
          "$($BIN timeline get "$vp" 0 0 | awk -F'\t' '/^tl_in/{print $2}')"
    check "so the length shortens by exactly that" "3.500000" \
          "$($BIN timeline get "$vp" 0 0 | awk -F'\t' '/^length/{print $2}')"

    $BIN timeline trim "$vp" 0 0 --tail 0.5
    check "a tail trim lengthens the clip" "4.000000" \
          "$($BIN timeline get "$vp" 0 0 | awk -F'\t' '/^length/{print $2}')"
    check "a trim that would leave nothing is refused" "1" \
          "$($BIN timeline trim "$vp" 0 0 --head 99 >/dev/null 2>&1; echo $?)"

    # The razor. Clip 0 runs 2.0 .. 6.0; cutting at 3.0 leaves 1s and 3s.
    n=$($BIN timeline split "$vp" 0 --at 3.0)
    check "split returns the new second half" "1" "$n"
    check "the first half ends at the razor" "1.000000" \
          "$($BIN timeline get "$vp" 0 0 | awk -F'\t' '/^length/{print $2}')"
    check "the second half starts there" "3.000000" \
          "$($BIN timeline get "$vp" 0 1 | awk -F'\t' '/^tl_in/{print $2}')"
    check "and runs to the old out point" "3.000000" \
          "$($BIN timeline get "$vp" 0 1 | awk -F'\t' '/^length/{print $2}')"
    # The source is continuous across the cut: no frame is repeated or lost.
    check "the cut is continuous in the source" "yes" \
          "$(a=$($BIN timeline get "$vp" 0 0 | awk -F'\t' '/^src_out/{print $2}')
             b=$($BIN timeline get "$vp" 0 1 | awk -F'\t' '/^src_in/{print $2}')
             [ "$a" = "$b" ] && echo yes || echo no)"
    check "a razor on the edge is refused" "1" \
          "$($BIN timeline split "$vp" 0 0 --at 2.0 >/dev/null 2>&1; echo $?)"

    # The hit test the window clicks with.
    check "at finds the clip under a time" "1" "$($BIN timeline at "$vp" 0 --at 4.0)"
    check "at finds nothing in a gap"     "-1" "$($BIN timeline at "$vp" 0 --at 0.5)"

    # Lift leaves the gap; ripple closes it. They are different edits.
    $BIN timeline delete "$vp" 0 0
    check "a lift leaves the later clip where it was" "3.000000" \
          "$($BIN timeline get "$vp" 0 0 | awk -F'\t' '/^tl_in/{print $2}')"
    # Clip 0 sits at 3.0. Dropping a 2s clip in front of it and deleting THAT
    # with --ripple has to pull clip 0 back by exactly two seconds.
    $BIN timeline clip "$vp" 0 "$vclip" --at 0 --in 0 --out-at 2 >/dev/null
    $BIN timeline delete "$vp" 0 1 --ripple
    check "a ripple closes the gap behind it" "1.000000" \
          "$($BIN timeline get "$vp" 0 0 | awk -F'\t' '/^tl_in/{print $2}')"

    # ---- titles, solids and stills -------------------------------------
    tp=$TMP/t.syntl
    still=$TMP/still.png
    ffmpeg -v error -y -f lavfi -i "color=c=orange:s=640x360" -frames:v 1 "$still" 2>/dev/null
    $BIN timeline new "$tp" --size 640x360 --fps 25
    $BIN timeline track "$tp" video V >/dev/null
    $BIN timeline clip "$tp" 0 "$still" --at 0 --dur 2 >/dev/null
    check "a photograph is recorded as a still" "1" \
          "$($BIN timeline get "$tp" 0 0 | awk -F'\t' '/^still/{print $2}')"
    check "a movie is not" "0" \
          "$($BIN timeline get "$vp" 0 0 | awk -F'\t' '/^still/{print $2}')"

    # A caption with a colon, a comma, an apostrophe and a PERCENT SIGN. The
    # percent is the one that matters: drawtext runs its own %{...} expansion
    # over a textfile too, so without expansion=none this fails the graph with
    # "Stray %" at export time, long after the title was typed.
    $BIN timeline title "$tp" 0 "It's 100% done: yes, really" --at 2 --dur 2 >/dev/null
    $BIN timeline solid "$tp" 0 --at 4 --dur 1 --colour 0.1,0.2,0.3 >/dev/null
    $BIN timeline show "$tp" | seen "a caption round-trips whole" "It's 100% done: yes, really"

    tgraph=$($BIN timeline export "$tp" --out "$TMP/t.mp4" --print)
    echo "$tgraph" | seen "a still is looped" "-loop"
    echo "$tgraph" | seen "a caption is drawn from a file" "textfile="
    echo "$tgraph" | seen "and never through % expansion" "expansion=none"
    # Read as an INPUT, lavfi settles on an opaque format with nothing
    # downstream to negotiate with, and a title's transparent backdrop lands
    # as solid black over the shot it was labelling.
    # On the INPUT line specifically. `format=rgba` also appears in the clip
    # chains, so a plain substring passes whether or not the thing that
    # actually needs it — the lavfi input — ever asked.
    echo "$tgraph" | rxseen "a generated clip is asked for alpha" \
                            '^color=c=.*,format=rgba$'

    $BIN timeline export "$tp" --out "$TMP/t.mp4" >/dev/null 2>&1
    check "a timeline of stills and titles exports" "yes" \
          "$([ -s "$TMP/t.mp4" ] && echo yes || echo no)"
    if [ -s "$TMP/t.mp4" ]; then
        # A still with no -loop contributes ONE frame to a graph expecting
        # seconds of them, and the export finishes early with no error at all.
        d=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$TMP/t.mp4")
        check "the still holds its full length" "yes" \
              "$(awk -v d="$d" 'BEGIN{print (d>4.8 && d<5.4)?"yes":"no"}')"
    fi

    # ---- transitions ----------------------------------------------------
    #
    # Every one of them is xfade, and a transition is a LAYER: the two clips
    # are composited into project-sized frames of their own, joined, and laid
    # over the background for exactly the overlap.
    $BIN timeline set "$tp" 0 1 trans=dissolve trans.dur=0.5
    $BIN timeline export "$tp" --out "$TMP/t.mp4" --print \
        | seen "a dissolve is an xfade" "xfade=transition=fade"
    $BIN timeline set "$tp" 0 1 trans=wipeleft
    $BIN timeline export "$tp" --out "$TMP/t.mp4" --print \
        | seen "and a wipe is the soft-edged one" "xfade=transition=smoothright"
    $BIN timeline export "$tp" --out "$TMP/t.mp4" --print \
        | seen "the outgoing clip's picture is split, not decoded twice" "split[m"
    $BIN timeline set "$tp" 0 1 trans=none trans.dur=0

    # ---- the program monitor --------------------------------------------
    fgraph=$($BIN timeline frame "$vp" --at 3.0 --out "$TMP/f.png" --print)
    # A seeked input hands the graph frames carrying their SOURCE timestamps
    # while the one-frame base sits at zero, so overlay waits for a secondary
    # frame at t<=0, never gets one, and emits the black base alone. The
    # monitor then goes black at every moment a seek was needed.
    echo "$fgraph" | seen "the monitor rebases a seeked input" "setpts=PTS-STARTPTS"
    # Only what is on screen. A monitor that fed the whole timeline in would
    # cost a scrub to minute nine nine minutes of decode.
    check "the monitor opens only the clips on screen" "1" \
          "$(echo "$fgraph" | grep -c -- '^-i$')"

    $BIN timeline frame "$vp" --at 3.0 --out "$TMP/f.png" --size 320 >/dev/null 2>&1
    check "the monitor renders a frame" "yes" \
          "$([ -s "$TMP/f.png" ] && echo yes || echo no)"
    if [ -s "$TMP/f.png" ]; then
        check "at the size it was asked for" "320" \
              "$(ffprobe -v error -show_entries stream=width -of csv=p=0 "$TMP/f.png")"
    fi
    # An empty stretch is black, not an error and not a missing file.
    $BIN timeline frame "$vp" --at 900 --out "$TMP/e.png" >/dev/null 2>&1
    check "past the end is a black frame, not a failure" "yes" \
          "$([ -s "$TMP/e.png" ] && echo yes || echo no)"

    # ---- the monitor and the export have to AGREE ------------------------
    #
    # This is the one that earns its keep. The monitor evaluates an animated
    # transform in C; the export hands the same two endpoints to zoompan. They
    # can drift, and they did: zoompan was given on/(nfr-1) where the C path
    # uses t/len, so the move arrived one frame early and the two disagreed
    # about framing for the whole length of it. Nothing about that is visible
    # from a graph string, so this MEASURES the picture.
    #
    # The still is a white box over the central half of the frame. Centre-
    # cropping at zoom z puts its left edge at x = 320 - 160z, so the first
    # bright column of the middle row reports the zoom directly.
    box=$TMP/box.png
    ffmpeg -v error -y -f lavfi \
        -i "color=c=black:s=640x360,drawbox=x=160:y=90:w=320:h=180:color=white:t=fill" \
        -frames:v 1 "$box" 2>/dev/null

    first_bright() {    # first_bright <image> -> column, or -1
        ffmpeg -v error -i "$1" -vf "crop=640:1:0:180" -f rawvideo -pix_fmt gray - \
            2>/dev/null | od -An -tu1 -v | tr -s ' ' '\n' \
            | awk 'NF { n++; if ($1 > 128 && !f) f = n } END { print f ? f-1 : -1 }'
    }

    if [ -s "$box" ]; then
        zp=$TMP/z.syntl
        $BIN timeline new "$zp" --size 640x360 --fps 25
        $BIN timeline track "$zp" video V >/dev/null
        $BIN timeline clip "$zp" 0 "$box" --at 0 --dur 4 >/dev/null
        $BIN timeline set "$zp" 0 0 xform.animate=1 xform.scale=1 xform.scale2=2
        $BIN timeline export "$zp" --out "$TMP/z.mp4" >/dev/null 2>&1

        # A four-second move drifts by one frame's worth of zoom — 0.01 —
        # if the two paths disagree about the frame-index convention, which
        # is under the measurement resolution and passes either way. Over
        # TEN frames the same one-frame error is 0.056 of zoom, nine pixels,
        # and impossible to miss. A slow move cannot test this; a fast one
        # can, and it is the same code path.
        $BIN timeline new "$zp.fast" --size 640x360 --fps 25
        $BIN timeline track "$zp.fast" video V >/dev/null
        $BIN timeline clip "$zp.fast" 0 "$box" --at 0 --dur 0.4 >/dev/null
        $BIN timeline set "$zp.fast" 0 0 xform.animate=1 xform.scale=1 xform.scale2=2
        $BIN timeline export "$zp.fast" --out "$TMP/zfast.mp4" >/dev/null 2>&1
        $BIN timeline frame "$zp.fast" --at 0.2 --out "$TMP/zff.png" >/dev/null 2>&1
        ffmpeg -v error -y -ss 0.2 -i "$TMP/zfast.mp4" -frames:v 1 "$TMP/zfe.png" 2>/dev/null
        mz=$(awk -v c="$(first_bright "$TMP/zff.png")" 'BEGIN{printf "%.4f",(320-c)/160}')
        ez=$(awk -v c="$(first_bright "$TMP/zfe.png")" 'BEGIN{printf "%.4f",(320-c)/160}')
        near "a fast move is halfway zoomed at its midpoint" "1.5" "$mz" 0.02
        near "and the export lands on the same frame of it"  "$mz" "$ez" 0.02

        for at in 1.0 3.0; do
            want=$(awk -v t=$at 'BEGIN { printf "%.4f", 1 + t/4 }')
            $BIN timeline frame "$zp" --at $at --out "$TMP/zf.png" >/dev/null 2>&1
            ffmpeg -v error -y -ss $at -i "$TMP/z.mp4" -frames:v 1 "$TMP/ze.png" 2>/dev/null
            mz=$(awk -v c="$(first_bright "$TMP/zf.png")" 'BEGIN{printf "%.4f",(320-c)/160}')
            ez=$(awk -v c="$(first_bright "$TMP/ze.png")" 'BEGIN{printf "%.4f",(320-c)/160}')
            near "the monitor zooms by the stated amount at $at" "$want" "$mz" 0.02
            near "and the export agrees with it at $at"          "$mz"   "$ez" 0.02
        done
    fi

    # ---- the preview render, which is what the window PLAYS --------------
    #
    # Compositing twenty-five frames a second live is not something a process
    # per frame can do, so playback is the EXPORT, played. That only holds up
    # if it is the SAME graph — a second, cheaper renderer would disagree with
    # the real one about exactly the things a preview exists to check.
    pgraph=$($BIN timeline export "$tp" --out "$TMP/p.mp4" --preview --print)
    echo "$pgraph" | seen "a preview trades encode time away" "ultrafast"
    # A plain mp4, NOT fragmented. It was fragmented so a player could open it
    # mid-write; nothing does, because playback waits for the render and the
    # window renders one in the background before anybody asks. An empty_moov
    # file can report the wrong duration and stop early, which is the one
    # thing a preview must never do.
    echo "$pgraph" | notseen "a preview is a plain mp4" "frag_keyframe"
    # The grade still goes through the LUT: a preview that skipped it would be
    # a preview of a different cut.
    echo "$pgraph" | seen "and it is still the same graph" "overlay=eof_action=pass"

    $BIN timeline export "$tp" --out "$TMP/p.mp4" --preview >/dev/null 2>&1
    check "a preview renders" "yes" "$([ -s "$TMP/p.mp4" ] && echo yes || echo no)"

    # A deliverable export is NOT downscaled. The preview is capped at 960 on
    # the long edge; a project already smaller than that is left alone.
    hd=$TMP/hd.syntl
    $BIN timeline new "$hd" --size 1920x1080 --fps 25
    $BIN timeline track "$hd" video V >/dev/null
    $BIN timeline clip "$hd" 0 "$still" --at 0 --dur 1 >/dev/null
    $BIN timeline export "$hd" --out "$TMP/hd-p.mp4" --preview >/dev/null 2>&1
    $BIN timeline export "$hd" --out "$TMP/hd-f.mp4" >/dev/null 2>&1
    check "a preview is capped at 960 wide" "960" \
          "$(ffprobe -v error -show_entries stream=width -of csv=p=0 "$TMP/hd-p.mp4" 2>/dev/null)"
    check "and the deliverable keeps the project size" "1920" \
          "$(ffprobe -v error -show_entries stream=width -of csv=p=0 "$TMP/hd-f.mp4" 2>/dev/null)"

    # ---- a grade that MOVES ---------------------------------------------
    #
    # Two keyframes and the grade ramps between them. A 3D LUT is a static
    # table and ffmpeg cannot fade between two of them, so this renders as a
    # run of cubes each gated to its own span — which means the interesting
    # question is not whether the ends are right but whether the BOUNDARIES
    # are, and no amount of reading the graph answers that.
    #
    # A flat grey still, exposure ramped from -1.5 to +1.5 stops. The source
    # never changes, so every change in the picture is the grade, and the luma
    # of the exported frames has to climb without ever going backwards.
    grey=$TMP/grey.png
    ffmpeg -v error -y -f lavfi -i "color=c=0x808080:s=160x90" \
           -frames:v 1 "$grey" 2>/dev/null
    kf=$TMP/kf.syntl
    $BIN timeline new "$kf" --size 160x90 --fps 25
    $BIN timeline track "$kf" video V >/dev/null
    $BIN timeline clip "$kf" 0 "$grey" --at 0 --dur 4 >/dev/null
    $BIN timeline key "$kf" 0 0 add --at 0 --set exposure=-1.5 >/dev/null
    $BIN timeline key "$kf" 0 0 add --at 4 --set exposure=1.5  >/dev/null

    check "two keyframes make the grade move" "yes" \
          "$([ "$($BIN timeline key "$kf" 0 0 list | awk -F'\t' '/^steps/{print $2}')" -gt 1 ] \
             && echo yes || echo no)"
    # One cube per step, and each span must be HALF OPEN. `between(t,a,b)` is
    # inclusive at both ends, so a frame landing exactly on a boundary
    # satisfies two of them and — because they are chained — gets the grade
    # applied TWICE. It showed as single frames of roughly double the grade,
    # once a second, wherever the arithmetic came out exact.
    $BIN timeline export "$kf" --out "$TMP/kf.mp4" --print \
        | notseen "gated spans never overlap" ":interp=tetrahedral:enable='between(t,"

    $BIN timeline export "$kf" --out "$TMP/kf.mp4" >/dev/null 2>&1
    if [ -s "$TMP/kf.mp4" ]; then
        ffmpeg -v error -i "$TMP/kf.mp4" -vf "scale=1:1,format=gray" \
               -f rawvideo - 2>/dev/null | od -An -tu1 -v \
               | tr -s ' ' '\n' | grep -v '^$' > "$TMP/ramp.txt"
        check "the ramp has a value per frame" "yes" \
              "$([ "$(wc -l < "$TMP/ramp.txt")" -gt 50 ] && echo yes || echo no)"
        # Never backwards. THIS is the assertion that caught the overlap.
        check "and it never goes backwards" "yes" \
              "$(awk 'NR>1 && $1 < prev-1 {print "no"; exit} {prev=$1} END {print "yes"}' \
                 "$TMP/ramp.txt" | head -1)"
        check "and it actually climbs" "yes" \
              "$(awk 'NR==1{f=$1} END{print ($1 > f+40)?"yes":"no"}' "$TMP/ramp.txt")"
    fi

    # The MONITOR has to show the moving grade too, and agree with the export
    # about it. It bakes only the one cube it needs for the instant it is
    # drawing, so a chain that named all forty-eight referenced forty-seven
    # files nobody wrote — ffmpeg refuses a graph it cannot open a file for,
    # the frame failed, and the window went on showing the last one that
    # worked. A stale picture is the worst possible failure for a monitor,
    # because nothing about it looks like a failure.
    for at in 0.2 2.0 3.8; do
        $BIN timeline frame "$kf" --at $at --out "$TMP/mf.png" >/dev/null 2>&1
        check "the monitor renders a moving grade at $at" "yes" \
              "$([ -s "$TMP/mf.png" ] && echo yes || echo no)"
        ffmpeg -v error -i "$TMP/mf.png" -vf "scale=1:1,format=gray" \
               -f rawvideo - 2>/dev/null | od -An -tu1 -v | tr -d ' \n' \
               >> "$TMP/monramp.txt"
        echo >> "$TMP/monramp.txt"
    done
    check "and it climbs with the grade" "yes" \
          "$(awk 'NR>1 && $1 <= prev {print "no"; exit} {prev=$1} END {print "yes"}' \
             "$TMP/monramp.txt" | head -1)"
    # The same instant, through the two different builders. They quantise the
    # grade identically on purpose, so they have to land on the same value.
    mid=$(sed -n 2p "$TMP/monramp.txt")
    exp=$(awk 'NR==51 {print $1}' "$TMP/ramp.txt")
    near "the monitor and the export agree mid-ramp" "$exp" "$mid" 6

    # A razor through a moving grade splits the keyframes with it, and plants
    # one at the cut holding the value the grade had reached there — otherwise
    # the second half inherits keys timed to moments now in the first.
    $BIN timeline split "$kf" 0 --at 2.0 >/dev/null
    check "a split leaves keys on the first half"  "yes" \
          "$([ "$($BIN timeline key "$kf" 0 0 list | grep -c '^key')" -ge 2 ] && echo yes || echo no)"
    check "and on the second"                      "yes" \
          "$([ "$($BIN timeline key "$kf" 0 1 list | grep -c '^key')" -ge 2 ] && echo yes || echo no)"
    check "the second half's keys start at zero" "0.000000" \
          "$($BIN timeline key "$kf" 0 1 list | awk -F'\t' '/^key/{print $3; exit}')"

    # ---- the transform survives a round trip ----------------------------
    $BIN timeline set "$vp" 0 0 xform.scale=1.4 xform.x=-0.25 xform.rotate=12 \
                                xform.animate=1 xform.scale2=1.8
    $BIN timeline show "$vp" | seen "a transform is written" "xform	1.40000	-0.25000"
    check "and reads back" "1.8" "$($BIN timeline get "$vp" 0 0 xform.scale2)"
    # An identity transform is not written at all, so a timeline of plain cuts
    # stays as readable as it was before transforms existed.
    $BIN timeline set "$vp" 0 0 xform.scale=1 xform.x=0 xform.rotate=0 \
                                xform.animate=0 xform.scale2=1
    $BIN timeline show "$vp" | notseen "an identity transform is not written" "xform	"
fi

echo "== keyframes on everything that is not colour"

# A grade key carries a whole develop stack because colour has to be baked to
# a cube. Everything else about a clip is one number, and ffmpeg takes an
# expression for it — so these keys cost a string, not forty-eight files.
#
# The thing under test is always the same: the monitor evaluates the keys in
# C, the export turns the SAME keys into a filter expression, and the two must
# land on the same pixel. A graph string cannot show that, so this measures
# the picture.

$BIN timeline keys | rxseen "the table says which properties can be keyed" \
    '^opacity(	[^	]*){7}	1$'
# ⚠ `speed` used to be the example of a property that CANNOT be keyed. It can
# since 0.1.0-19 — a keyed speed is a ramp — so the example moved to one that
# still cannot: a fade's length is a number the graph bakes into a filter
# argument, not something it evaluates per frame.
$BIN timeline keys | rxseen "and which cannot" '^fade\.in(	[^	]*){7}	0$'

ap=$TMP/anim.syntl
$BIN timeline new "$ap" --size 640x360 --fps 25
$BIN timeline track "$ap" video V >/dev/null
if [ -s "$box" ]; then
    $BIN timeline clip "$ap" 0 "$box" --at 0 --dur 2 >/dev/null

    # ---- the evaluator ---------------------------------------------------
    $BIN timeline set "$ap" 0 0 xform.x=0.5
    check "a key with no value pins what is already there" "0.500000" \
          "$($BIN timeline anim "$ap" 0 0 add xform.x --at 0 >/dev/null;
             $BIN timeline anim "$ap" 0 0 at xform.x --at 0)"
    $BIN timeline anim "$ap" 0 0 add xform.x --at 0 --value -0.4 >/dev/null
    check "a second key at the same instant replaces it, never stacks" "1" \
          "$($BIN timeline anim "$ap" 0 0 list xform.x | wc -l)"
    $BIN timeline anim "$ap" 0 0 add xform.x --at 2 --value 0.4 >/dev/null

    check "before the first key it HOLDS"  "-0.400000" \
          "$($BIN timeline anim "$ap" 0 0 at xform.x --at -1)"
    check "between two keys it moves"      "0.000000" \
          "$($BIN timeline anim "$ap" 0 0 at xform.x --at 1)"
    check "after the last key it HOLDS"    "0.400000" \
          "$($BIN timeline anim "$ap" 0 0 at xform.x --at 9)"

    # Every ease has to be a shape ffmpeg's expression language can also
    # evaluate, which is why they are all polynomials. Measured at the
    # midpoint, where they differ most from each other.
    for pair in "in 0.5:-0.200000" "out 0.5:0.200000" "inout 0.5:0.000000" \
                "hold 0.5:-0.400000"; do
        e=${pair%% *}; want=${pair#*:}
        $BIN timeline anim "$ap" 0 0 add xform.x --at 0 --value -0.4 --ease $e >/dev/null
        check "ease $e is itself at the midpoint" "$want" \
              "$($BIN timeline anim "$ap" 0 0 at xform.x --at 1)"
    done
    $BIN timeline anim "$ap" 0 0 add xform.x --at 0 --value -0.4 --ease linear >/dev/null

    # The inspector's whole data source: `get --at` is how a panel parked on
    # a moving clip knows what to show. Without it the sliders would report
    # the static field, which once a property is keyed is not what the
    # renderer reads and not what is on screen.
    check "get --at reports the value at that instant" "-0.4" \
          "$($BIN timeline get "$ap" 0 0 xform.x --at 0)"
    check "and interpolates between the keys" "-0.2" \
          "$($BIN timeline get "$ap" 0 0 xform.x --at 0.5)"
    check "a property with no keys ignores --at" "1" \
          "$($BIN timeline get "$ap" 0 0 speed --at 0.5)"

    # ---- the document ----------------------------------------------------
    $BIN timeline show "$ap" | seen "a key is one line of the document" \
        "anim	xform.x	0.000000	-0.400000	linear"
    check "and survives the round trip" "2" \
          "$($BIN timeline anim "$ap" 0 0 list xform.x | wc -l)"
    $BIN timeline anim "$ap" 0 0 add opacity --at 0 --value 0.25 >/dev/null
    check "properties do not see each other's keys" "1" \
          "$($BIN timeline anim "$ap" 0 0 list opacity | wc -l)"
    $BIN timeline anim "$ap" 0 0 clear opacity >/dev/null
    check "clear takes one property" "2" \
          "$($BIN timeline anim "$ap" 0 0 list | wc -l)"

    $BIN timeline anim "$ap" 0 0 add fade.in --at 0 --value 2 >/dev/null 2>&1
    check "a property the renderer cannot animate is refused" "1" "$?"
    # And one it CAN, which is what makes the refusal above mean something.
    $BIN timeline anim "$ap" 0 0 add speed --at 0 --value 2 >/dev/null 2>&1
    check "and a speed ramp is accepted" "0" "$?"
    $BIN timeline anim "$ap" 0 0 clear speed >/dev/null 2>&1

    # ---- the picture: a pan ----------------------------------------------
    #
    # This one found a bug that had been shipping since transforms existed.
    # The export used to position an animated clip by sliding zoompan's CROP
    # WINDOW, and a window sliding right shows what is to the right of it — so
    # the picture went LEFT while the monitor, which has no zoompan, moved it
    # RIGHT. An animated pan came out MIRRORED, and the test that measured the
    # picture only ever measured the zoom.
    #
    # Frame-exact, not a seek: the value belongs to the frame's own timestamp,
    # and asking for a second lands on whichever frame is nearest, which at a
    # fast pan is half the movement.
    exp_frame() {   # exp_frame <mp4> <n> <out.png>
        ffmpeg -v error -y -i "$1" -vf "select=eq(n\,$2)" -fps_mode passthrough \
               -frames:v 1 -update 1 "$3" 2>/dev/null
    }
    $BIN timeline export "$ap" --out "$TMP/anim.mp4" >/dev/null 2>&1
    for n in 0 25 49; do
        at=$(awk -v n=$n 'BEGIN { printf "%.6f", n/25 }')
        $BIN timeline frame "$ap" --at $at --out "$TMP/am.png" >/dev/null 2>&1
        exp_frame "$TMP/anim.mp4" $n "$TMP/ae.png"
        mc=$(first_bright "$TMP/am.png"); ec=$(first_bright "$TMP/ae.png")
        near "the export pans where the monitor pans, frame $n" "$mc" "$ec" 2
    done
    # ...and in the right DIRECTION, which is the half a mirrored pan gets
    # right by accident when only the distance is checked.
    $BIN timeline frame "$ap" --at 0 --out "$TMP/a0.png" >/dev/null 2>&1
    exp_frame "$TMP/anim.mp4" 49 "$TMP/a1.png"
    check "a pan to the right ends right of where it started" "yes" \
          "$([ "$(first_bright "$TMP/a1.png")" -gt "$(first_bright "$TMP/a0.png")" ] \
             && echo yes || echo no)"

    # ---- the picture: a zoom that dips BELOW 1 ---------------------------
    #
    # zoompan only ever zooms in, so a scale under 1 — the picture smaller
    # than the frame — is impossible to ask it for directly. It is padding
    # that makes it possible, and without the padding the export silently
    # clamps to 1 while the monitor obeys, which is a disagreement no graph
    # string shows.
    zd=$TMP/zdip.syntl
    $BIN timeline new "$zd" --size 640x360 --fps 25
    $BIN timeline track "$zd" video V >/dev/null
    $BIN timeline clip "$zd" 0 "$box" --at 0 --dur 2 >/dev/null
    $BIN timeline anim "$zd" 0 0 add xform.scale --at 0 --value 1 >/dev/null
    $BIN timeline anim "$zd" 0 0 add xform.scale --at 2 --value 0.5 >/dev/null
    $BIN timeline export "$zd" --out "$TMP/zdip.mp4" >/dev/null 2>&1
    $BIN timeline frame "$zd" --at 1.96 --out "$TMP/zdm.png" >/dev/null 2>&1
    exp_frame "$TMP/zdip.mp4" 49 "$TMP/zde.png"
    zdm=$(first_bright "$TMP/zdm.png"); zde=$(first_bright "$TMP/zde.png")
    near "the monitor shrinks past the frame" "241" "$zdm" 4
    near "and the export shrinks with it"     "$zdm" "$zde" 4

    # ---- the picture: an angle that changes while the framing moves ------
    #
    # The animated branch of the export had NO rotate filter in it at all, so
    # a clip that was both moving and turning exported without the turn while
    # the monitor showed it. A rotated box is wider than an upright one, which
    # is what this measures.
    rt=$TMP/rot.syntl
    $BIN timeline new "$rt" --size 640x360 --fps 25
    $BIN timeline track "$rt" video V >/dev/null
    $BIN timeline clip "$rt" 0 "$box" --at 0 --dur 2 >/dev/null
    $BIN timeline set "$rt" 0 0 xform.animate=1 xform.scale=1 xform.scale2=1.4 \
                               xform.rotate=0 xform.rotate2=45
    $BIN timeline export "$rt" --out "$TMP/rot.mp4" >/dev/null 2>&1
    $BIN timeline frame "$rt" --at 1.96 --out "$TMP/rm.png" >/dev/null 2>&1
    exp_frame "$TMP/rot.mp4" 49 "$TMP/re.png"
    rm_=$(first_bright "$TMP/rm.png"); re_=$(first_bright "$TMP/re.png")
    check "the monitor turns the shot" "yes" \
          "$([ "$rm_" -lt 200 ] && echo yes || echo no)"
    near "and so does the export"       "$rm_" "$re_" 3

    # ---- the picture: opacity, which is the one that is NOT an expression -
    #
    # No ffmpeg filter multiplies alpha by an expression, so a keyed opacity
    # is sendcmd stepping a colorchannelmixer. The steps are placed where the
    # value crosses a code value and ss_clip_prop_at rounds down the same way,
    # which is what makes the monitor EQUAL to the export here rather than
    # close to it.
    op=$TMP/op.syntl
    $BIN timeline new "$op" --size 640x360 --fps 25
    $BIN timeline track "$op" video V >/dev/null
    $BIN timeline clip "$op" 0 "$box" --at 0 --dur 2 >/dev/null
    $BIN timeline anim "$op" 0 0 add opacity --at 0 --value 0 >/dev/null
    $BIN timeline anim "$op" 0 0 add opacity --at 2 --value 1 >/dev/null
    $BIN timeline export "$op" --out "$TMP/op.mp4" >/dev/null 2>&1
    centre() {  # centre <image> -> the value of the pixel at the middle
        ffmpeg -v error -i "$1" -vf "crop=1:1:320:180" -f rawvideo -pix_fmt gray - \
            2>/dev/null | od -An -tu1 -v | tr -d ' \n'
    }
    prev=-1
    for n in 0 12 25 37 49; do
        at=$(awk -v n=$n 'BEGIN { printf "%.6f", n/25 }')
        $BIN timeline frame "$op" --at $at --out "$TMP/om.png" >/dev/null 2>&1
        exp_frame "$TMP/op.mp4" $n "$TMP/oe.png"
        om=$(centre "$TMP/om.png"); oe=$(centre "$TMP/oe.png")
        near "a keyed opacity matches the monitor at frame $n" "$om" "$oe" 2
        # A gate that is inclusive at both ends applies twice on the frame
        # that satisfies two of them, and the ramp goes BACKWARDS for one
        # frame a second. Never seen in a graph string; obvious here.
        check "and never goes backwards at frame $n" "yes" \
              "$([ "$oe" -ge "$prev" ] && echo yes || echo no)"
        prev=$oe
    done

    # ---- a keyed fader, measured ------------------------------------------
    #
    # volume takes an expression once it is told to evaluate one per frame, so
    # this needs no steps at all. Measured as ProRes/pcm: a low-level tone
    # comes back three decibels hot through AAC, which swamps a fader
    # assertion.
    tone=$TMP/atone.wav
    ffmpeg -v error -y -f lavfi -i "sine=frequency=440:duration=4:sample_rate=48000" \
           -c:a pcm_s16le "$tone" 2>/dev/null
    if [ -s "$tone" ]; then
        gp=$TMP/gain.syntl
        $BIN timeline new "$gp" --size 320x180 --fps 25
        $BIN timeline track "$gp" audio A >/dev/null
        $BIN timeline clip "$gp" 0 "$tone" --at 0 >/dev/null
        $BIN timeline anim "$gp" 0 0 add gain --at 0 --value -20 >/dev/null
        $BIN timeline anim "$gp" 0 0 add gain --at 4 --value 0 >/dev/null
        $BIN timeline export "$gp" --out "$TMP/gain.mov" --format prores \
            >/dev/null 2>&1
        mean_at() {  # mean_at <file> <start> <length> -> mean dBFS
            ffmpeg -v info -ss "$2" -t "$3" -i "$1" -af volumedetect -f null - \
                2>&1 | awk -F': ' '/mean_volume/ { print $2 + 0; exit }'
        }
        base=$(mean_at "$tone" 0 1)
        a=$(mean_at "$TMP/gain.mov" 0 1)
        b=$(mean_at "$TMP/gain.mov" 3 1)
        # The first second averages -17.5 dB of gain, the last -2.5.
        near "a keyed fader starts where it was told to"  \
             "$(awk -v b="$base" 'BEGIN { printf "%.2f", b - 17.5 }')" "$a" 1.0
        near "and arrives where it was told to"           \
             "$(awk -v b="$base" 'BEGIN { printf "%.2f", b - 2.5 }')"  "$b" 1.0
    fi

    # ---- the razor cuts a move in two ------------------------------------
    #
    # Keys are timed into the CLIP, so a split has to plant one at the cut in
    # both halves or the second half restarts the move from the beginning.
    sp=$TMP/asplit.syntl
    $BIN timeline new "$sp" --size 640x360 --fps 25
    $BIN timeline track "$sp" video V >/dev/null
    $BIN timeline clip "$sp" 0 "$box" --at 0 --dur 4 >/dev/null
    $BIN timeline anim "$sp" 0 0 add xform.x --at 0 --value -0.4 >/dev/null
    $BIN timeline anim "$sp" 0 0 add xform.x --at 4 --value 0.4 >/dev/null
    $BIN timeline split "$sp" 0 --at 2 >/dev/null
    check "the second half starts where the first left off" "0.000000" \
          "$($BIN timeline anim "$sp" 0 1 at xform.x --at 0)"
    check "and still finishes the move" "0.400000" \
          "$($BIN timeline anim "$sp" 0 1 at xform.x --at 2)"
    check "the first half ends at the cut, not at the end" "0.000000" \
          "$($BIN timeline anim "$sp" 0 0 at xform.x --at 2)"
fi

echo "== transitions (one filter, sixty looks)"

# Every transition is ffmpeg's xfade now, on BOTH sides of the program: the
# export hands it two streams, and the monitor hands it two frames — the same
# picture twice, the second stamped where the playhead is — so a scrub and a
# render cannot disagree about what a wipe looks like halfway through. They
# used to: the wipes were a geq on the export side and a plain uniform fade on
# the monitor's, which is not the same picture at all.

check "the catalogue is a table, like the formats" "yes" \
      "$([ "$($BIN timeline transitions | wc -l)" -ge 50 ] && echo yes || echo no)"
$BIN timeline transitions | seen "with a name and something to call it" \
    "dissolve	Dissolve"
$BIN timeline transitions | seen "the ones that came before xfade are still here" \
    "wipeleft"

# A length saved while the kind is still `none` used to vanish with the line
# it would have been written on, so setting the length first and the kind
# second gave a transition of zero — which reads as the kind not working.
lp=$TMP/tlen.syntl
$BIN timeline new "$lp" --size 160x90 --fps 25
$BIN timeline track "$lp" video V >/dev/null
$BIN timeline solid "$lp" 0 --at 0 --dur 2 >/dev/null
$BIN timeline set "$lp" 0 0 trans.dur=1.5
check "a length outlives having no kind yet" "1.5" \
      "$($BIN timeline get "$lp" 0 0 trans.dur)"

# ⚠ Every name has to be one THIS ffmpeg knows. A typo, or a mirror that names
# a transition which does not exist, fails the graph at export time — long
# after it was picked from a list that looked fine.
xhave=$TMP/xfade.txt
ffmpeg -hide_banner -h filter=xfade 2>&1 \
    | sed -n '/transition /,/duration /p' \
    | awk 'NF > 1 && $1 != "transition" && $1 != "duration" { print $1 }' \
    | sort > "$xhave"
check "ffmpeg has an xfade catalogue to map onto" "yes" \
      "$([ "$(wc -l < "$xhave")" -ge 40 ] && echo yes || echo no)"

xp=$TMP/xf.syntl
$BIN timeline new "$xp" --size 160x90 --fps 25
$BIN timeline track "$xp" video V >/dev/null
$BIN timeline solid "$xp" 0 --at 0 --dur 2 --colour 1,0,0 >/dev/null
$BIN timeline solid "$xp" 0 --at 1 --dur 2 --colour 0,0,1 >/dev/null
$BIN timeline set "$xp" 0 1 trans.dur=1

missing=0
for k in $($BIN timeline transitions | cut -f2); do
    [ "$k" = none ] && continue
    [ "$k" = dip ] && continue
    $BIN timeline set "$xp" 0 1 trans=$k
    x=$($BIN timeline export "$xp" --out /dev/null --print 2>/dev/null \
        | grep -o 'xfade=transition=[a-z]*' | head -1 | cut -d= -f3)
    grep -qx "$x" "$xhave" || { missing=$((missing + 1)); echo "        $k -> [$x]"; }
done
check "every kind names an xfade this ffmpeg has" "0" "$missing"

# ---- the direction rule ------------------------------------------------
#
# Ours says where the incoming picture comes FROM; xfade's says which way the
# boundary TRAVELS. They are opposites, so every directional row in the table
# is MIRRORED — and a row that is not is a bug you can only see by rendering
# it. Half way through a transition from red to blue, the blue has to be on
# the side the name says it came from.
pixel_at() {    # pixel_at <image> <w> <x> <y> -> "r,g,b"
    ffmpeg -v error -i "$1" -vf "crop=1:1:$3:$4" -f rawvideo -pix_fmt rgb24 - \
        2>/dev/null | od -An -tu1 -v | tr -s ' ' ',' | sed 's/^,//;s/,$//'
}
bluer() {       # bluer <image> <x> <y> -> yes if that pixel is the incoming clip
    p=$(pixel_at "$1" 160 "$2" "$3")
    b=${p##*,}; r=${p%%,*}
    [ "$b" -gt "$r" ] && echo yes || echo no
}

for pair in "wipeleft 20 80:right" "wiperight 140 80:left" \
            "slideleft 20 80:right" "slidedown 80 70:up"; do
    k=${pair%% *}; rest=${pair#* }
    x=${rest%% *}; rest=${rest#* }
    y=${rest%%:*}
    $BIN timeline set "$xp" 0 1 trans=$k
    $BIN timeline frame "$xp" --at 1.5 --out "$TMP/dir.png" >/dev/null 2>&1
    check "$k brings the shot in from the $k's own side" "yes" \
          "$(bluer "$TMP/dir.png" $x $y)"
done

# ---- the monitor and the export are the same picture -------------------
exp_frame() {   # exp_frame <mp4> <n> <out.png>
    ffmpeg -v error -y -i "$1" -vf "select=eq(n\,$2)" -fps_mode passthrough \
           -frames:v 1 -update 1 "$3" 2>/dev/null
}
for k in dissolve slideleft circleopen; do
    $BIN timeline set "$xp" 0 1 trans=$k
    $BIN timeline export "$xp" --out "$TMP/xf.mp4" >/dev/null 2>&1
    same=0; n=0
    for f in 27 31 37 44; do
        at=$(awk -v f=$f 'BEGIN { printf "%.6f", f/25 }')
        $BIN timeline frame "$xp" --at $at --out "$TMP/xm.png" >/dev/null 2>&1
        exp_frame "$TMP/xf.mp4" $f "$TMP/xe.png"
        m=$(pixel_at "$TMP/xm.png" 160 40 45)
        e=$(pixel_at "$TMP/xe.png" 160 40 45)
        n=$((n + 1))
        # Within a code value or two, not bit for bit: a geometric transition
        # puts a moving EDGE somewhere, and a pixel that lands on it is
        # resampled by two paths that round differently. Anything larger than
        # that is a different picture, not a different rounding.
        awk -v a="$m" -v b="$e" 'BEGIN {
            split(a, x, ","); split(b, y, ",");
            for (i = 1; i <= 3; i++) { d = x[i] - y[i]; if (d < 0) d = -d;
                                       if (d > 3) exit 1 }
            exit 0 }' && same=$((same + 1))
    done
    check "the monitor is the export, frame for frame ($k)" "$n" "$same"
done

# ---- dip to a colour ---------------------------------------------------
#
# The one kind that is not an xfade of the two clips: two dissolves THROUGH a
# colour. Both halves are xfades all the same, on both sides, because `fade`
# steps by frame index and an alpha worked out from the time is a few code
# values away from it — near enough to look right and not near enough to be
# the same picture.
$BIN timeline set "$xp" 0 1 trans=dip trans.r=1 trans.g=1 trans.b=1
$BIN timeline export "$xp" --out "$TMP/dip.mp4" >/dev/null 2>&1
# 1.52 and not 1.5: the frames either side of the middle are what the export
# actually writes, and the monitor stamps its own frame on the same grid — so
# asking for an instant BETWEEN two frames gets the earlier one, which is the
# picture that will be on screen there. That is the right answer and not a
# rounding error.
$BIN timeline frame "$xp" --at 1.52 --out "$TMP/dipm.png" >/dev/null 2>&1
exp_frame "$TMP/dip.mp4" 38 "$TMP/dipe.png"
check "the middle of a dip IS the colour" "255,255,255" \
      "$(pixel_at "$TMP/dipm.png" 160 80 45)"
check "and the export dips through the same one" "255,255,255" \
      "$(pixel_at "$TMP/dipe.png" 160 80 45)"
$BIN timeline frame "$xp" --at 1.25 --out "$TMP/dipq.png" >/dev/null 2>&1
exp_frame "$TMP/dip.mp4" 31 "$TMP/dipqe.png"
check "and they are the same picture on the way in" \
      "$(pixel_at "$TMP/dipq.png" 160 80 45)" \
      "$(pixel_at "$TMP/dipqe.png" 160 80 45)"

# ---- a transition with nothing to come from ----------------------------
np=$TMP/nopart.syntl
$BIN timeline new "$np" --size 160x90 --fps 25
$BIN timeline track "$np" video V >/dev/null
$BIN timeline solid "$np" 0 --at 0 --dur 2 --colour 0,0,1 >/dev/null
$BIN timeline set "$np" 0 0 trans=slideleft trans.dur=1
$BIN timeline export "$np" --out "$TMP/nopart.mp4" >/dev/null 2>&1
check "a transition at the head of a track still renders" "yes" \
      "$([ -s "$TMP/nopart.mp4" ] && echo yes || echo no)"

# ---- the sound crosses with the picture --------------------------------
#
# Two clips overlapping used to ADD, because nothing faded either of them:
# the picture dissolved and the sound got LOUDER for the length of it. Two
# qsin fades hold the power constant across the overlap, which is what a
# crossfade is. Measured as ProRes/pcm — a tone comes back three decibels hot
# through AAC, which is the same size as the thing being measured.
t1=$TMP/tone1.wav; t2=$TMP/tone2.wav
ffmpeg -v error -y -f lavfi -i "sine=frequency=440:duration=3:sample_rate=48000" \
       -c:a pcm_s16le "$t1" 2>/dev/null
ffmpeg -v error -y -f lavfi -i "sine=frequency=700:duration=3:sample_rate=48000" \
       -c:a pcm_s16le "$t2" 2>/dev/null
if [ -s "$t1" ] && [ -s "$t2" ]; then
    ap=$TMP/axf.syntl
    $BIN timeline new "$ap" --size 160x90 --fps 25
    $BIN timeline track "$ap" audio A >/dev/null
    $BIN timeline clip "$ap" 0 "$t1" --at 0 >/dev/null
    $BIN timeline clip "$ap" 0 "$t2" --at 2 >/dev/null
    mean_of() {
        ffmpeg -v info -ss "$2" -t "$3" -i "$1" -af volumedetect -f null - \
            2>&1 | awk -F': ' '/mean_volume/ { print $2 + 0; exit }'
    }
    $BIN timeline export "$ap" --out "$TMP/axf1.mov" --format prores >/dev/null 2>&1
    plain=$(mean_of "$TMP/axf1.mov" 2.2 0.5)
    alone=$(mean_of "$TMP/axf1.mov" 0.5 0.5)
    $BIN timeline set "$ap" 0 1 trans=dissolve trans.dur=1
    $BIN timeline export "$ap" --out "$TMP/axf2.mov" --format prores >/dev/null 2>&1
    crossed=$(mean_of "$TMP/axf2.mov" 2.2 0.5)
    check "two clips overlapping with no transition ADD" "yes" \
          "$(awk -v a="$alone" -v p="$plain" \
                 'BEGIN { print (p - a > 1.5) ? "yes" : "no" }')"
    near "and a transition crosses them instead" "$alone" "$crossed" 1.5
fi

# ---- the cut under the playhead, in one command ------------------------
#
# A transition is not only a property: the two clips have to OVERLAP, which is
# an edit. Out of the outgoing clip's handles when it has them — nothing else
# on the timeline moves — and by rippling what follows when it does not.
if [ -s "$vclip" ]; then
    hp=$TMP/hand.syntl
    $BIN timeline new "$hp" --size 160x90 --fps 25
    $BIN timeline track "$hp" video V >/dev/null
    $BIN timeline clip "$hp" 0 "$vclip" --at 0 --dur 2 >/dev/null
    $BIN timeline clip "$hp" 0 "$vclip" --at 2 --dur 2 >/dev/null
    before=$($BIN timeline show "$hp" | awk -F'\t' '/^# duration/ { print $2 }')
    out=$($BIN timeline transition "$hp" 0 --at 2 --kind slideleft --dur 1)
    check "a trimmed clip pays for the transition out of its handles" "handles" \
          "$(echo "$out" | cut -f4)"
    check "and nothing else moves" "$before" \
          "$($BIN timeline show "$hp" | awk -F'\t' '/^# duration/ { print $2 }')"
    check "the transition landed on the incoming clip" "slideleft" \
          "$($BIN timeline get "$hp" 0 1 trans)"
    $BIN timeline transition "$hp" 0 --at 2 --kind nosuchwipe >/dev/null 2>&1
    check "and a kind the renderer does not have is refused" "1" "$?"

    rp=$TMP/ripple.syntl
    $BIN timeline new "$rp" --size 160x90 --fps 25
    $BIN timeline track "$rp" video V >/dev/null
    $BIN timeline clip "$rp" 0 "$vclip" --at 0 >/dev/null
    $BIN timeline clip "$rp" 0 "$vclip" --at 8 >/dev/null
    rbefore=$($BIN timeline show "$rp" | awk -F'\t' '/^# duration/ { print $2 }')
    rout=$($BIN timeline transition "$rp" 0 --at 8 --dur 1)
    check "a clip with no handles ripples instead" "ripple" \
          "$(echo "$rout" | cut -f4)"
    check "and the programme is a transition shorter" "yes" \
          "$(awk -v b="$rbefore" \
                 -v a="$($BIN timeline show "$rp" | awk -F'\t' '/^# duration/ { print $2 }')" \
                 'BEGIN { print (b - a > 0.9 && b - a < 1.1) ? "yes" : "no" }')"
fi

echo "== effects (a recipe is a FILE somebody else can write)"

# The whole point of the format: an effect is a text manifest naming an ffmpeg
# filter chain, so a third party ships one with no compiler and nothing to
# rebuild when ffmpeg bumps a SONAME. Which means the engine is running a
# string somebody else wrote, and most of this section is about that.

check "the shipped effects load" "yes" \
      "$([ "$($BIN fx list | wc -l)" -ge 24 ] && echo yes || echo no)"
$BIN fx list | seen "each with a group to file it under" "glow	Glow	Light"
$BIN fx show glow | seen "and parameters with ranges and defaults" \
    "param	radius	16	1	80"
check "an effect nobody has is not an effect" "1" \
      "$($BIN fx show nosuchthing >/dev/null 2>&1; echo $?)"

# ---- every shipped recipe RENDERS ------------------------------------
#
# The parser can accept a recipe that only names allowed filters, interpolates
# only declared parameters — and is still nonsense, because a misspelled
# option or a label going nowhere is not found until ffmpeg builds the graph.
# One frame of 64x64 grey through each is the answer.
broken=0
for e in $($BIN fx list | cut -f1); do
    $BIN fx check "$e" >/dev/null 2>&1 || { broken=$((broken + 1)); echo "        $e"; }
done
check "every one of them builds a graph ffmpeg accepts" "0" "$broken"

# ---- what a recipe may NOT do ----------------------------------------
#
# A filter string can do anything ffmpeg can, INCLUDING READ FILES. That is
# the whole risk of shipping effects as text, and these are the four walls
# around it. Each of these files parses; none of them is allowed to load.
mkdir -p "$TMP/badfx"
cat > "$TMP/badfx/reader.synfx" <<'FX'
name    reader
filter  [$in]movie=/etc/passwd[m];[$in][m]blend=all_mode=screen[$out]
FX
cat > "$TMP/badfx/filearg.synfx" <<'FX'
name    filearg
filter  [$in]lut3d=file=/tmp/whatever.cube[$out]
FX
cat > "$TMP/badfx/undeclared.synfx" <<'FX'
name    undeclared
filter  [$in]gblur=sigma=$radius[$out]
FX
cat > "$TMP/badfx/noports.synfx" <<'FX'
name    noports
filter  gblur=sigma=4
FX
cat > "$TMP/badfx/multiframe.synfx" <<'FX'
name    multiframe
filter  [$in]tmix=frames=5[$out]
FX
# Nothing to do with files: `crop` is refused because it changes the GEOMETRY
# out from under the transform that owns it, and the monitor and the export
# would then disagree about where the picture is.
cat > "$TMP/badfx/geometry.synfx" <<'FX'
name    geometry
filter  [$in]crop=10:10:0:0[$out]
FX

$BIN fx check "$TMP/badfx/reader.synfx" 2>&1 \
    | seen "a filter that reads a file is not allowed" "may not name a file"
$BIN fx check "$TMP/badfx/filearg.synfx" 2>&1 \
    | seen "nor is an argument that names one" "may not name a file"
$BIN fx check "$TMP/badfx/undeclared.synfx" 2>&1 \
    | seen "a parameter nobody declared is refused" "is not a parameter"
$BIN fx check "$TMP/badfx/noports.synfx" 2>&1 \
    | seen "a chain with no way in or out is refused" "must take"
$BIN fx check "$TMP/badfx/multiframe.synfx" 2>&1 \
    | seen "and a filter needing a WINDOW of frames is refused" \
      "not an allowed filter"
$BIN fx check "$TMP/badfx/geometry.synfx" 2>&1 \
    | seen "so is one that changes the size of the frame" \
      "not an allowed filter"

# None of them reached the catalogue either — the check is at LOAD, not at use.
oldfx=${SYNSTUDIO_EFFECTS:-}
SYNSTUDIO_EFFECTS="$TMP/badfx" $BIN fx list > "$TMP/badlist.txt" 2>/dev/null
check "and none of them is in the catalogue" "0" \
      "$(grep -cE '^(reader|filearg|undeclared|noports|multiframe|geometry)	' \
              "$TMP/badlist.txt")"

# ---- a good one, from outside ---------------------------------------
cat > "$TMP/badfx/mine.synfx" <<'FX'
name    mine
label   Mine
group   Local
param   amount  0.5  0  1  Amount
filter  [$in]hue=s=$amount[$out]
FX
SYNSTUDIO_EFFECTS="$TMP/badfx" $BIN fx check mine \
    | seen "an effect dropped in a folder is an effect" "ok	mine"

if have ffmpeg && [ -s "$box" ]; then
    fp=$TMP/fx.syntl
    $BIN timeline new "$fp" --size 160x90 --fps 25
    $BIN timeline track "$fp" video V >/dev/null
    $BIN timeline clip "$fp" 0 "$box" --at 0 --dur 1 >/dev/null

    # ---- the stack ---------------------------------------------------
    check "adding an effect reports where it landed" "0" \
          "$($BIN timeline fx "$fp" 0 0 add blur radius=4)"
    check "and the next one goes after it" "1" \
          "$($BIN timeline fx "$fp" 0 0 add glow)"
    $BIN timeline fx "$fp" 0 0 list | seen "a knob set on the way in sticks" \
        "blur	Blur	1	radius=4"
    $BIN timeline fx "$fp" 0 0 list | seen "and the rest come from the recipe" \
        "glow	Glow	1	amount=0.5	radius=16"

    # Order is the reason this is a list and not a set.
    $BIN timeline fx "$fp" 0 0 move 1 0
    check "move puts one before the other" "glow" \
          "$($BIN timeline fx "$fp" 0 0 list | head -1 | cut -f2)"
    $BIN timeline export "$fp" --out /dev/null --print \
        | rxseen "and the graph applies them in that order" \
          'blend=all_mode=screen.*gblur=sigma=4'
    $BIN timeline fx "$fp" 0 0 move 0 1

    # ---- a value out of a document cannot become a filter -------------
    #
    # Every parameter is a NUMBER, clamped to the recipe's own range and
    # printed by the engine — never carried through from the document as
    # text. This is what stops a project file smuggling an argument into
    # somebody else's filter chain.
    $BIN timeline fx "$fp" 0 0 set 0 "radius=4:crop=10:10:0:0"
    $BIN timeline export "$fp" --out /dev/null --print \
        | notseen "a parameter cannot smuggle in a filter" "crop=10"
    $BIN timeline fx "$fp" 0 0 set 0 radius=9999
    check "and it is clamped to the range the recipe declared" "radius=60" \
          "$($BIN timeline fx "$fp" 0 0 list | head -1 | cut -f5)"
    $BIN timeline fx "$fp" 0 0 set 0 radius=4

    # And a value that never went through the setter at all — somebody's hand
    # in the project file — is clamped when it is READ, so the number that
    # reaches the filter is always one the recipe allows.
    sed 's/radius=4/radius=99999/' "$fp" > "$fp.hand"
    $BIN timeline export "$fp.hand" --out /dev/null --print \
        | seen "a hand-edited value is clamped on the way in" "gblur=sigma=60"

    # ---- the document ------------------------------------------------
    $BIN timeline show "$fp" | seen "the stack is written by NAME" \
        "fx	blur	1	radius=4"
    check "and reads back" "2" "$($BIN timeline fx "$fp" 0 0 list | wc -l)"

    # An effect this machine has not got must survive being loaded and
    # SAVED — dropping it would delete somebody else's work from their
    # project the first time it was opened on the wrong machine.
    kp=$TMP/keep.syntl
    sed 's/^fx\tglow/fx\tnotinstalled/' "$fp" > "$kp"
    $BIN timeline fx "$kp" 0 0 list | seen "an uninstalled effect says so" \
        "notinstalled	(missing)"
    $BIN timeline set "$kp" 0 0 opacity=0.9
    $BIN timeline show "$kp" | seen "and is written back exactly as it came" \
        "fx	notinstalled	1	amount=0.5	radius=16"
    $BIN timeline export "$kp" --out /dev/null --print 2>&1 \
        | seen "the export says what it is missing" "no effect called notinstalled"

    # ---- the monitor and the export are the same picture --------------
    $BIN timeline export "$fp" --out "$TMP/fx.mp4" >/dev/null 2>&1
    check "a clip with effects still exports" "yes" \
          "$([ -s "$TMP/fx.mp4" ] && echo yes || echo no)"
    $BIN timeline frame "$fp" --at 0.5 --out "$TMP/fxm.png" >/dev/null 2>&1
    ffmpeg -v error -y -i "$TMP/fx.mp4" -vf "select=eq(n\,12)" \
           -fps_mode passthrough -frames:v 1 -update 1 "$TMP/fxe.png" 2>/dev/null
    samepx "and the monitor is the same picture" \
           "$(pixel_at "$TMP/fxm.png" 160 40 45)" \
           "$(pixel_at "$TMP/fxe.png" 160 40 45)"

    # ---- an effect that would die at export time never lands ----------
    cat > "$TMP/badfx/lies.synfx" <<'FX'
name    lies
filter  [$in]blend=all_mode=thereisnosuchmode[$out]
FX
    SYNSTUDIO_EFFECTS="$TMP/badfx" \
        $BIN timeline fx "$fp" 0 0 add lies >/dev/null 2>&1
    check "an effect ffmpeg will not build is refused when it lands" "1" "$?"
    check "and it is not on the clip" "2" \
          "$($BIN timeline fx "$fp" 0 0 list | wc -l)"

    # ---- a key needs the chain to carry alpha -------------------------
    $BIN timeline fx "$fp" 0 0 add greenscreen >/dev/null
    $BIN timeline export "$fp" --out /dev/null --print \
        | rxseen "an effect that makes transparency gets an rgba chain" \
          'format=rgba,scale|format=rgba,'
fi

echo "== the audio envelope (waveforms)"

if have ffmpeg; then
    # Silent for the first half, a full-scale tone for the second. That shape
    # is the whole test: it catches a bucket mapping that is reversed,
    # off-by-one, or scrambled, none of which a single loud file would show.
    wav=$TMP/half.wav
    ffmpeg -v error -y -f lavfi -i "sine=frequency=1000:duration=2:sample_rate=44100" \
           -af "volume=enable='lt(t,1)':volume=0" "$wav" 2>/dev/null

    if [ -s "$wav" ]; then
        $BIN peaks "$wav" --count 10 > "$TMP/pk.tsv" 2>/dev/null
        check "peaks emits one row per bucket" "10" "$(wc -l < "$TMP/pk.tsv")"
        check "each row is peak and RMS" "2" \
              "$(head -1 "$TMP/pk.tsv" | awk -F'\t' '{print NF}')"

        # First half silent, second half loud. Asserting the SHAPE rather than
        # the numbers keeps this true across encoders.
        # Compared against EACH OTHER, not against an absolute level. ffmpeg's
        # sine source is about -18 dBFS rather than full scale, so a threshold
        # written down here is a threshold that fails on the next ffmpeg.
        early=$(awk -F'\t' 'NR<=4 {if ($1>m) m=$1} END {printf "%.6f", m}' "$TMP/pk.tsv")
        late=$(awk -F'\t' 'NR>=7 {if ($1>m) m=$1} END {printf "%.6f", m}' "$TMP/pk.tsv")
        check "the silent half reads silent" "yes" \
              "$(awk -v e="$early" 'BEGIN{print (e<0.01)?"yes":"no"}')"
        check "and the loud half is far above it" "yes" \
              "$(awk -v e="$early" -v l="$late" \
                     'BEGIN{print (l>0.02 && l>e*10)?"yes":"no"}')"

        # A sine's crest factor is sqrt(2), so peak/RMS is 3.01 dB. Getting
        # this right means the RMS is an RMS and not a second peak, and that
        # the samples were read as SIGNED little-endian shorts — a byte-order
        # or sign mistake shows up here and almost nowhere else.
        ratio=$(awk -F'\t' '$2>0.01 {print $1/$2; exit}' "$TMP/pk.tsv")
        near "a sine's peak is sqrt(2) times its RMS" "1.414" "${ratio:-0}" 0.1

        # And the level itself, against ffmpeg's OWN measurement rather than
        # against a number written down here.
        want=$(ffmpeg -hide_banner -i "$wav" -af volumedetect -f null - 2>&1 \
               | awk '/max_volume/ {print $(NF-1)}')
        got=$(awk -F'\t' 'BEGIN{m=0} {if ($1>m) m=$1} END {print 20*log(m)/log(10)}' "$TMP/pk.tsv")
        near "and the peak level matches ffmpeg's own" "${want:-0}" "$got" 0.6
    fi

    # No audio is an ANSWER, not a failure: 100, and nothing on stdout. A
    # photograph on a timeline goes through this for its waveform.
    silent=$TMP/silent.mp4
    ffmpeg -v error -y -f lavfi -i "testsrc=size=64x64:rate=5:duration=1" \
           -c:v libx264 -pix_fmt yuv420p "$silent" 2>/dev/null
    check "a video with no audio answers 100" "100" \
          "$($BIN peaks "$silent" --count 4 >/dev/null 2>&1; echo $?)"
    check "and says nothing on stdout" "0" \
          "$($BIN peaks "$silent" --count 4 2>/dev/null | wc -c)"
    check "a photograph answers 100 too" "100" \
          "$($BIN peaks "$TMP/still.png" --count 4 >/dev/null 2>&1; echo $?)"

    # An audio-only file. ss_probe_file is about the PICTURE and fails
    # outright without a video stream, so everything that asked IT how long a
    # file is got no answer for a music bed: peaks reported "no audio" for a
    # file that is nothing but audio, and a whole album added to a track
    # arrived as a five second clip.
    bed=$TMP/bed.flac
    ffmpeg -v error -y -f lavfi -i "sine=frequency=200:duration=7" "$bed" 2>/dev/null
    if [ -s "$bed" ]; then
        check "peaks reads an audio-only file" "0" \
              "$($BIN peaks "$bed" --count 4 >/dev/null 2>&1; echo $?)"
        near "and knows how long it is" "7" "$(ss_dur=$($BIN peaks "$bed" --count 1 >/dev/null 2>&1; echo)
                                              $BIN timeline new "$TMP/bed.syntl" >/dev/null
                                              $BIN timeline track "$TMP/bed.syntl" audio A >/dev/null
                                              $BIN timeline clip "$TMP/bed.syntl" 0 "$bed" >/dev/null
                                              $BIN timeline get "$TMP/bed.syntl" 0 0 \
                                                | awk -F'\t' '/^length/{print $2}')" 0.2
        $BIN browse "$TMP" | seen "and the picker can find it" "audio	bed.flac"
    fi
fi

# A project is a document this program opens, so the picker has to be able to
# find one. It could not: browse listed what the ENGINE can decode, and a
# timeline is not decoded by anything.
projdir=$TMP/projbrowse
mkdir -p "$projdir"
$BIN timeline new "$projdir/cut.syntl" >/dev/null 2>&1
: > "$projdir/notes.txt"
$BIN browse "$projdir" | seen "browse lists a project" "project	cut.syntl"
$BIN browse "$projdir" | notseen "and still not what it cannot open" "notes.txt"

# ---------------------------------------------------------------- browse --
#
# The picker's data source. This exists because the Open button once ran
# `synfiles --pick image` — a flag synfiles has never had — so the button was
# dead and nothing tested it. The rule the tests below encode is that a row
# which is DRAWN is a row that will OPEN: nothing unreadable, nothing
# undecodable, and no dotfiles.

echo "== browse (the picker's data source)"

bdir=$TMP/browse
mkdir -p "$bdir/sub" "$bdir/.hidden"
touch "$bdir/A.JPG" "$bdir/z.png" "$bdir/m.cr2" "$bdir/reel.MP4" \
      "$bdir/notes.txt" "$bdir/thing.so"
ln -s sub "$bdir/linkdir"
ln -s /no/such/target "$bdir/dangling"

$BIN browse "$bdir" > "$TMP/b.txt"
check "browse succeeds" "0" "$?"

# The first line says where it landed, which is what the breadcrumb draws.
check "the first row reports the directory" "$bdir" \
      "$(head -1 "$TMP/b.txt" | cut -f3)"

check "a dotfile directory is not a row" "0" \
      "$(grep -c '	.hidden	' "$TMP/b.txt")"
check "an undecodable file is not a row" "0" \
      "$(grep -cE '	(notes.txt|thing.so)	' "$TMP/b.txt")"
check "a dangling symlink is not a row" "0" \
      "$(grep -c '	dangling	' "$TMP/b.txt")"

# Case matters: a camera writes .JPG and .MP4 as often as the lower-case forms,
# and matching only lower case hides half of somebody's holiday.
check "an upper-case still is an image" "image" \
      "$(awk -F'\t' '$2=="A.JPG"{print $1}' "$TMP/b.txt")"
check "an upper-case movie is a video" "video" \
      "$(awk -F'\t' '$2=="reel.MP4"{print $1}' "$TMP/b.txt")"
check "camera raw is an image" "image" \
      "$(awk -F'\t' '$2=="m.cr2"{print $1}' "$TMP/b.txt")"
# stat, not d_type: a symlink to a directory is a place you can walk into.
check "a symlink to a directory is a directory" "dir" \
      "$(awk -F'\t' '$2=="linkdir"{print $1}' "$TMP/b.txt")"

# Parent first, then directories, then files — and within the file band by
# NAME, not by kind: A.JPG m.cr2 reel.MP4 z.png. A picker that grouped stills
# and movies apart would scatter one shoot across two places in the list.
check "the parent is the first row after the header" "up" \
      "$(sed -n 2p "$TMP/b.txt" | cut -f1)"
check "directories sort above files, then name orders them" \
      "dir dir image image video image" \
      "$(tail -n +2 "$TMP/b.txt" | tail -n +2 | cut -f1 | tr '\n' ' ' | sed 's/ $//')"

# .. from a symlinked directory goes where the user can SEE it went, because
# the path is resolved before it is listed.
$BIN browse "$bdir/linkdir" | seen "a symlink resolves to its target" "$bdir/sub"
$BIN browse "$bdir/sub/../linkdir" | seen "and .. resolves too" "$bdir/sub"

# The root has no parent. A `..` row there would loop on itself.
$BIN browse / | notseen "the root offers no parent" "	..	"

$BIN browse "$TMP/does-not-exist" >/dev/null 2>&1
check "a missing directory fails loudly" "1" "$?"

# ------------------------------------------------------------------- mix --
#
# The mixer is measured, never read. Every assertion here exports a real file
# and puts a meter on it, because a fader that is written to the document and
# dropped on the way to the graph looks exactly like a fader that works.
#
# ProRes, so the audio is pcm and the measurement is of the MIX rather than of
# what AAC did to it — a low-level sine comes back up to 3dB hot through a
# lossy encoder, which is not a bug and would make every number here noise.
echo "== the mixer (levels, measured)"

mdir=$TMP/mix
mkdir -p "$mdir"
ffmpeg -v error -y -f lavfi -i "sine=frequency=4000:duration=4" -af volume=-12dB \
       "$mdir/tone.wav" 2>/dev/null
ffmpeg -v error -y -f lavfi -i "sine=frequency=120:duration=4" -af volume=-12dB \
       "$mdir/low.wav" 2>/dev/null
ffmpeg -v error -y -f lavfi -i color=c=white:s=64x48:d=4:r=25 \
       -f lavfi -i "sine=frequency=440:duration=4" -shortest -pix_fmt yuv420p \
       -c:a aac "$mdir/whitetone.mp4" 2>/dev/null

# Integrated loudness of an exported file, to one decimal.
lufs() { $BIN loudness "$1" | awk -F'\t' '$1=="lufs"{print $2}'; }
# Peak of one frequency band, which is how a tone is found in a mix.
band() {    # band <file> <lo> <hi>
    ffmpeg -v info -nostats -i "$1" \
        -af "highpass=f=$2:poles=2,highpass=f=$2:poles=2,lowpass=f=$3:poles=2,lowpass=f=$3:poles=2,volumedetect" \
        -f null - 2>&1 | grep -o 'max_volume: [^ ]*' | head -1 | awk '{print $2}'
}

mp=$mdir/m.syntl
$BIN timeline new "$mp" --size 64x48 --fps 25 >/dev/null
$BIN timeline track "$mp" audio A1 >/dev/null
$BIN timeline clip "$mp" 0 "$mdir/tone.wav" --at 0 >/dev/null

$BIN timeline export "$mp" --out "$mdir/g0.mov" --format prores >/dev/null 2>&1
base=$(lufs "$mdir/g0.mov")
$BIN timeline track "$mp" 0 --gain -6
$BIN timeline export "$mp" --out "$mdir/g6.mov" --format prores >/dev/null 2>&1
down=$(lufs "$mdir/g6.mov")
# dB, so the difference is exact rather than approximate. Six is six.
check "the track fader moves the export by exactly its dB" "6.0" \
      "$(awk -v a="$base" -v b="$down" 'BEGIN{printf "%.1f", a-b}')"

$BIN timeline track "$mp" 0 --gain 0
$BIN timeline master "$mp" --gain -6 >/dev/null
$BIN timeline export "$mp" --out "$mdir/mm.mov" --format prores >/dev/null 2>&1
check "and so does the master" "6.0" \
      "$(awk -v a="$base" -v b="$(lufs "$mdir/mm.mov")" 'BEGIN{printf "%.1f", a-b}')"
$BIN timeline master "$mp" --gain 0 >/dev/null

# A pan must not cost level. Routing a MONO clip through a stereo upmix takes
# 3dB off it, so hard left measured quieter than centre — the one thing a pan
# can never do.
$BIN timeline track "$mp" 0 --pan -1
$BIN timeline export "$mp" --out "$mdir/pl.mov" --format prores >/dev/null 2>&1
lpk=$(ffmpeg -v info -nostats -i "$mdir/pl.mov" -af "pan=mono|c0=c0,volumedetect" \
      -f null - 2>&1 | grep -o 'max_volume: [^ ]*' | head -1 | awk '{print $2}')
rpk=$(ffmpeg -v info -nostats -i "$mdir/pl.mov" -af "pan=mono|c0=c1,volumedetect" \
      -f null - 2>&1 | grep -o 'max_volume: [^ ]*' | head -1 | awk '{print $2}')
check "hard left keeps its level" "1" \
      "$(awk -v v="$lpk" 'BEGIN{print (v > -31.0) ? 1 : 0}')"
check "and the right channel is empty" "1" \
      "$(awk -v v="$rpk" 'BEGIN{print (v < -60.0) ? 1 : 0}')"
$BIN timeline track "$mp" 0 --pan 0

# Solo is a property of the TIMELINE: one soloed track silences the others.
sp=$mdir/s.syntl
$BIN timeline new "$sp" --size 64x48 --fps 25 >/dev/null
$BIN timeline track "$sp" audio A1 >/dev/null
$BIN timeline track "$sp" audio A2 >/dev/null
$BIN timeline clip "$sp" 0 "$mdir/low.wav"  --at 0 >/dev/null
$BIN timeline clip "$sp" 1 "$mdir/tone.wav" --at 0 >/dev/null
$BIN timeline export "$sp" --out "$mdir/both.mov" --format prores >/dev/null 2>&1
check "both tracks are in the mix" "1" \
      "$(awk -v a="$(band "$mdir/both.mov" 90 160)" -v b="$(band "$mdir/both.mov" 3000 6000)" \
             'BEGIN{print (a > -50 && b > -50) ? 1 : 0}')"
$BIN timeline track "$sp" 0 --solo 1
$BIN timeline export "$sp" --out "$mdir/solo.mov" --format prores >/dev/null 2>&1
check "solo keeps the soloed track" "1" \
      "$(awk -v a="$(band "$mdir/solo.mov" 90 160)" 'BEGIN{print (a > -50) ? 1 : 0}')"
check "and silences the other one" "1" \
      "$(awk -v b="$(band "$mdir/solo.mov" 3000 6000)" 'BEGIN{print (b < -50) ? 1 : 0}')"

# hide is about the PICTURE and mute is about the SOUND. They used to be one
# condition, so muting a video track took its picture away with it.
hp=$mdir/h.syntl
$BIN timeline new "$hp" --size 64x48 --fps 25 >/dev/null
$BIN timeline track "$hp" video V1 >/dev/null
$BIN timeline clip "$hp" 0 "$mdir/whitetone.mp4" --at 0 >/dev/null
white() {   # mean luma of a frame one second in: 255 white, 0 black
    ffmpeg -v error -y -ss 1 -i "$1" -frames:v 1 -f rawvideo -pix_fmt gray - 2>/dev/null \
        | od -An -tu1 -v | awk '{for(i=1;i<=NF;i++){s+=$i;n++}} END{printf "%d", (n?s/n:0)+0.5}'
}
loud() { ffmpeg -v info -nostats -i "$1" -af volumedetect -f null - 2>&1 \
         | grep -o 'max_volume: [^ ]*' | head -1 | awk '{print $2}'; }

$BIN timeline export "$hp" --out "$mdir/plain.mov" --format prores >/dev/null 2>&1
check "a plain video track shows its picture" "1" \
      "$(awk -v v="$(white "$mdir/plain.mov")" 'BEGIN{print (v > 200) ? 1 : 0}')"

$BIN timeline track "$hp" 0 --mute 1
$BIN timeline export "$hp" --out "$mdir/muted.mov" --format prores >/dev/null 2>&1
check "muting a VIDEO track leaves the picture" "1" \
      "$(awk -v v="$(white "$mdir/muted.mov")" 'BEGIN{print (v > 200) ? 1 : 0}')"
check "and takes only the sound" "" "$(loud "$mdir/muted.mov")"

$BIN timeline track "$hp" 0 --mute 0 --hide 1
$BIN timeline export "$hp" --out "$mdir/hidden.mov" --format prores >/dev/null 2>&1
check "hiding it leaves the sound" "1" \
      "$(awk -v v="$(loud "$mdir/hidden.mov")" 'BEGIN{print (v > -40) ? 1 : 0}')"
check "and takes only the picture" "1" \
      "$(awk -v v="$(white "$mdir/hidden.mov")" 'BEGIN{print (v < 20) ? 1 : 0}')"

# ----------------------------------------------------------------- undo --
#
# Whole documents in `<project>.undo/`, not inverse operations: a .syntl is a
# few kilobytes of text, every verb is a separate process that loads-changes-
# saves, and there is no session to keep a stack in. On disk, so it survives
# the window being closed and an edit made from the command line in between.
echo "== undo (whole documents, on disk)"

udir=$TMP/undo
mkdir -p "$udir"
ffmpeg -v error -y -f lavfi -i testsrc=size=64x48:rate=25:duration=1 \
       -pix_fmt yuv420p "$udir/clip.mp4" 2>/dev/null
up=$udir/u.syntl
$BIN timeline new "$up" --size 64x48 --fps 25 >/dev/null
$BIN timeline track "$up" video V1 >/dev/null
$BIN timeline clip "$up" 0 "$udir/clip.mp4" --at 0 >/dev/null
$BIN timeline clip "$up" 0 "$udir/clip.mp4" --at 5 >/dev/null

clips() { grep -c '^clip' "$1"; }
check "two clips to start" "2" "$(clips "$up")"
$BIN timeline history "$up" | seen "and three steps to walk back" "undo	3"

$BIN timeline undo "$up" >/dev/null
check "undo takes the last edit off" "1" "$(clips "$up")"
$BIN timeline undo "$up" >/dev/null
check "and the one before it" "0" "$(clips "$up")"
$BIN timeline redo "$up" >/dev/null
check "redo puts one back" "1" "$(clips "$up")"

# An edit after an undo is a new future; the old one is not reachable and must
# not pretend to be.
$BIN timeline clip "$up" 0 "$udir/clip.mp4" --at 9 >/dev/null
$BIN timeline history "$up" | seen "an edit after an undo drops the redo" "redo	0"

# The floor. Undo past the beginning is not an error, it is nothing to do —
# and it must leave the document alone rather than emptying it.
i=0
while [ $i -lt 20 ]; do $BIN timeline undo "$up" >/dev/null 2>&1; i=$((i+1)); done
$BIN timeline undo "$up" >/dev/null 2>&1
check "undo past the start does nothing" "1" "$?"
check "and the document is still a document" "1" \
      "$(grep -c '^# synstudio timeline' "$up")"

# History belongs to the PROJECT, not to a session: a second invocation of the
# program picks up where the first left off, which is the whole reason it is a
# directory and not a variable.
check "the history is on disk beside the project" "1" \
      "$([ -f "$up.undo/head" ] && echo 1 || echo 0)"

# --------------------------------------------------------------- markers --
echo "== markers"

mkp=$udir/m.syntl
$BIN timeline new "$mkp" --size 64x48 >/dev/null
$BIN timeline mark "$mkp" --at 4.5 --text "fix the audio here" --colour 1 >/dev/null
$BIN timeline mark "$mkp" --at 1.0 --text "start" --colour 3 >/dev/null
$BIN timeline show "$mkp" | seen "a marker keeps its note" "fix the audio here"
# Sorted by time, because "the next marker" should be the next one in the
# array rather than a search.
check "and they are kept in time order" "1.000000" \
      "$($BIN timeline show "$mkp" | awk -F'\t' '$1=="marker"{print $2; exit}')"
$BIN timeline unmark "$mkp" 0 >/dev/null
$BIN timeline show "$mkp" | notseen "unmark takes the right one" "start"
$BIN timeline show "$mkp" | seen "and leaves the other" "fix the audio here"
$BIN timeline undo "$mkp" >/dev/null
$BIN timeline show "$mkp" | seen "and undo brings it back" "start"
$BIN timeline unmark "$mkp" 9 >/dev/null 2>&1
check "unmark refuses a marker that is not there" "1" "$?"

# --------------------------------------------------------------- record --
#
# A microphone is not required and must never be: `--format lavfi` records a
# GENERATED signal, which is the same code path with a source that exists on
# every machine. `-re` is what makes it stand in for a device — without it a
# generated source produces an hour of audio in a few seconds, which is
# rendering rather than recording.
echo "== record (a take, and a meter while it is taken)"

rdir=$TMP/rec
mkdir -p "$rdir"

$BIN record --out "$rdir/take.wav" --format lavfi \
     --device "sine=frequency=440" --limit 1 > "$rdir/out.txt" 2>&1
check "a take succeeds" "0" "$?"
seen "and says where it went" "out	$rdir/take.wav" < "$rdir/out.txt"
check "the file is really there" "1" \
      "$([ -s "$rdir/take.wav" ] && echo 1 || echo 0)"
check "and is the length that was asked for" "1" \
      "$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$rdir/take.wav" \
         | awk '{d=$1-1; if(d<0)d=-d; print (d<0.15)?1:0}')"

# The meter, and the thing that makes it one.
#
# `ametadata=print` writes through avio's 4KB buffer, so without direct=1 a
# take under about eight seconds delivers NOTHING until ffmpeg exits and then
# everything at once — which on screen is indistinguishable from a microphone
# that was never live. So it is not enough that the lines exist: the FIRST one
# has to arrive while the take is still running.
$BIN record --out "$rdir/m.wav" --format lavfi --device "sine=frequency=440" \
     --limit 3 2>/dev/null \
    | while IFS= read -r l; do
          case "$l" in level*) echo "$(date +%s.%N) $l" ;; esac
      done > "$rdir/levels.txt"
check "the meter reports while it records" "1" \
      "$([ "$(grep -c . "$rdir/levels.txt")" -gt 5 ] && echo 1 || echo 0)"
# The first line's wall clock against the last one's: buffered, they are the
# same instant; live, they are seconds apart.
check "and it arrives DURING the take, not at the end of it" "1" \
      "$(awk 'NR==1{a=$1} END{print ($1-a > 1.0) ? 1 : 0}' "$rdir/levels.txt")"

# Stopping is the ordinary end of a take. The engine catches the signal,
# forwards it, and waits — so the WAV gets its real length written into its
# header rather than being whatever was on disk when the process died.
$BIN record --out "$rdir/stop.wav" --format lavfi --device "sine=frequency=440" \
     --limit 600 > "$rdir/stop.txt" 2>&1 &
recpid=$!
sleep 2
kill -TERM $recpid
wait $recpid
check "a stopped take exits cleanly" "0" "$?"
seen "and still says where it went" "out	$rdir/stop.wav" < "$rdir/stop.txt"
check "with a real length in the header" "1" \
      "$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$rdir/stop.wav" \
         | awk '{print ($1 > 0.5 && $1 < 30) ? 1 : 0}')"
# Scoped to THIS take's file, because the machine running the tests may well
# have somebody's real recording open at the same time.
check "and nothing left holding the microphone" "0" \
      "$({ pgrep -c -f "$rdir/stop.wav" 2>/dev/null || echo 0; } | head -1)"

$BIN record --limit 1 >/dev/null 2>&1
check "record without --out is refused" "1" "$?"

# `devices` asks ffmpeg what can capture. A machine with no sound server has
# nothing to say and says so; what must never happen is a row that is not a
# row — the window builds a list from these.
$BIN devices > "$rdir/dev.txt" 2>/dev/null
devrc=$?
check "devices either answers or fails cleanly" "1" \
      "$([ $devrc -eq 0 ] || [ $devrc -eq 1 ] && echo 1 || echo 0)"
if [ $devrc -eq 0 ] && [ -s "$rdir/dev.txt" ]; then
    check "every device row is kind, name, id, default" "0" \
          "$(awk -F'\t' 'NF != 4 || ($1 != "input" && $1 != "monitor")' "$rdir/dev.txt" | grep -c .)"
fi

# ------------------------------------------------------------- loudness --
echo "== loudness and normalise"

$BIN loudness "$mdir/tone.wav" | seen "loudness reports LUFS" "lufs"
$BIN loudness "$mdir/tone.wav" | seen "and a true peak" "peak"
printf 'not audio\n' > "$mdir/notes.txt"
$BIN loudness "$mdir/notes.txt" >/dev/null 2>&1
check "and refuses something with no sound in it" "1" "$?"

# The engine measures AND decides: a window that read a number and did the
# subtraction itself would be a second place where "how loud should this be"
# is answered.
np=$mdir/n.syntl
$BIN timeline new "$np" --size 64x48 --fps 25 >/dev/null
$BIN timeline track "$np" audio A1 >/dev/null
$BIN timeline clip "$np" 0 "$mdir/tone.wav" --at 0 >/dev/null
$BIN timeline normalise "$np" 0 0 --target -20 | seen "normalise says what it measured" "measured"
$BIN timeline export "$np" --out "$mdir/norm.mov" --format prores >/dev/null 2>&1
# ebur128 over four seconds of tone lands within a few tenths of the target.
check "and the export really is on target" "1" \
      "$(awk -v v="$(lufs "$mdir/norm.mov")" 'BEGIN{d=v+20; if(d<0)d=-d; print (d < 1.0) ? 1 : 0}')"

# The mixer survives a round trip through the document, and a timeline nobody
# has touched still writes no mix line at all.
$BIN timeline track "$mp" 0 --gain -4.5 --pan 0.25 --solo 1
$BIN timeline show "$mp" | seen "the mix line records the fader" "mix	-4.500	0.2500	1"
$BIN timeline track "$mp" 0 --gain 0 --pan 0 --solo 0
$BIN timeline show "$mp" | notseen "and vanishes when it is all default" "mix	"

# ------------------------------------------------------------------ kind --
#
# `browse` classifies a directory by extension and has to: one process per row
# would make opening a folder cost hundreds. `kind` is the other half — one
# file, asked of ffmpeg — and the whole point of it is that the ANSWER DOES
# NOT COME FROM THE NAME, so every case here is tested with the extension
# taken away.
echo "== kind (what a file is, asked of ffmpeg)"

kdir=$TMP/kind
mkdir -p "$kdir"
ffmpeg -v error -y -f lavfi -i testsrc=size=64x48:rate=25:duration=1 \
       -pix_fmt yuv420p "$kdir/clip.mp4" 2>/dev/null
ffmpeg -v error -y -f lavfi -i "sine=frequency=440:duration=1" "$kdir/tone.flac" 2>/dev/null
ffmpeg -v error -y -f lavfi -i testsrc=size=64x48:duration=1 -frames:v 1 "$kdir/still.png" 2>/dev/null
printf 'not media\n' > "$kdir/notes.txt"
cp "$kdir/clip.mp4"  "$kdir/anon_video"
cp "$kdir/tone.flac" "$kdir/anon_audio"
cp "$kdir/still.png" "$kdir/anon_image"

check "kind: a movie"          "video"   "$($BIN kind "$kdir/clip.mp4")"
check "kind: a sound"          "audio"   "$($BIN kind "$kdir/tone.flac")"
check "kind: a photograph"     "image"   "$($BIN kind "$kdir/still.png")"
check "kind: a project"        "project" "$($BIN kind "$kdir/none.syntl")"

# The extension is not the answer. These three have none at all.
check "kind: a movie with no extension" "video" "$($BIN kind "$kdir/anon_video")"
check "kind: a sound with no extension" "audio" "$($BIN kind "$kdir/anon_audio")"
check "kind: a still with no extension" "image" "$($BIN kind "$kdir/anon_image")"

# Cover art is a VIDEO STREAM. Reading that as a movie is how an album ends up
# on the video track — a still frame as far as anything downstream can tell.
ffmpeg -v error -y -i "$kdir/tone.flac" -i "$kdir/still.png" \
       -map 0:a -map 1:v -c:a copy -c:v copy -disposition:v:0 attached_pic \
       "$kdir/withart.mp3" 2>/dev/null
if [ -s "$kdir/withart.mp3" ]; then
    check "kind: cover art does not make it a movie" "audio" \
          "$($BIN kind "$kdir/withart.mp3")"
fi

check "kind: something it cannot open" "none" "$($BIN kind "$kdir/notes.txt")"
$BIN kind "$kdir/notes.txt" >/dev/null 2>&1
check "and says so in its exit status" "1" "$?"

# --------------------------------------------------------------- formats --
#
# Both tables are printed for the window to build a picker from, the same way
# `keys` builds the develop panel. A format the window can offer and the
# engine cannot write is the failure these are here to prevent.
echo "== formats (what an export can come out as)"

$BIN formats | seen "stills list PNG" "png"
$BIN formats | seen "stills list TIFF" "tif"
$BIN timeline formats | seen "the cut can be mp4" "mp4"
$BIN timeline formats | seen "and WebM" "webm"
# ProRes is a .mov, and a table that said otherwise would produce a file no
# editor reads as ProRes.
$BIN timeline formats | seen "ProRes writes a .mov" "prores	mov"

fdir=$TMP/fmt
mkdir -p "$fdir"
proj=$fdir/cut.syntl
$BIN timeline new "$proj" --size 64x48 --fps 25 >/dev/null
$BIN timeline track "$proj" video V1 >/dev/null
$BIN timeline clip "$proj" 0 "$kdir/clip.mp4" --at 0 >/dev/null

$BIN timeline export "$proj" --out "$fdir/a.webm" --format webm >/dev/null 2>&1
check "an export in another format succeeds" "0" "$?"
ffprobe -v error -show_entries stream=codec_name -of csv=p=0 "$fdir/a.webm" 2>/dev/null \
    | seen "and is really VP9" "vp9"

# No --format at all: the extension decides, which is what makes `--out
# cut.webm` do the obvious thing without saying it twice.
$BIN timeline export "$proj" --out "$fdir/b.webm" >/dev/null 2>&1
ffprobe -v error -show_entries stream=codec_name -of csv=p=0 "$fdir/b.webm" 2>/dev/null \
    | seen "the extension alone picks the format" "vp9"

$BIN timeline export "$proj" --out "$fdir/c.mov" --format prores >/dev/null 2>&1
ffprobe -v error -show_entries stream=codec_name -of csv=p=0 "$fdir/c.mov" 2>/dev/null \
    | seen "ProRes really is ProRes" "prores"

$BIN timeline export "$proj" --out "$fdir/d.mp4" --format nonesuch >/dev/null 2>&1
check "an unknown format is refused" "1" "$?"

echo "== reading somebody else's .cube"

ldir=$TMP/luts
mkdir -p "$ldir"

# A 2-node cube that halves everything. Two nodes is the smallest lattice
# there is, so every value in the picture is INTERPOLATED — which is what
# makes it a test of the interpolator rather than of a table lookup.
{
    echo 'TITLE "half"'
    echo 'LUT_3D_SIZE 2'
    echo 'DOMAIN_MIN 0.0 0.0 0.0'
    echo 'DOMAIN_MAX 1.0 1.0 1.0'
    for b in 0.0 0.5; do for g in 0.0 0.5; do for r in 0.0 0.5; do
        echo "$r $g $b"
    done; done; done
} > "$ldir/half.cube"

set -- $($BIN pixel 0.5 0.5 0.5 --set lut="$ldir/half.cube")
near "a .cube applies" 0.25 "$1"

# ⚠ THE ORDERING TEST, and the only one that catches it. .cube rows vary RED
# FASTEST; a reader that walked them the other way loads every file without
# complaint and swaps red and blue in the picture. A grey ramp cannot see it
# and neither can a gamma — it takes a cube that MOVES a channel somewhere
# else. This one maps (r,g,b) to (b,g,r).
{
    echo 'LUT_3D_SIZE 2'
    for b in 0 1; do for g in 0 1; do for r in 0 1; do
        echo "$b $g $r"
    done; done; done
} > "$ldir/swap.cube"
set -- $($BIN pixel 0.8 0.2 0.4 --set lut="$ldir/swap.cube")
near "red really does vary fastest (R)" 0.4 "$1"
near "and the swap lands on blue"       0.8 "$3"

# Strength is a mix toward the LUT, so 0 is the picture and 100 is the table.
set -- $($BIN pixel 0.5 0.5 0.5 --set lut="$ldir/half.cube" --set lut.amount=50)
near "strength mixes" 0.375 "$1"
set -- $($BIN pixel 0.5 0.5 0.5 --set lut="$ldir/half.cube" --set lut.amount=0)
near "strength 0 is the picture untouched" 0.5 "$1"

# Naming a LUT and nothing else has to APPLY it. Zero is the null value for
# every field in this struct — which would make a LUT set on its own render as
# nothing and read as a failed import — so the command layer fills the
# strength in, the way it switches cropping on for a crop rectangle.
$BIN keys | rxseen "the strength defaults to nothing in the table" '^lut.amount	0	'
set -- $($BIN pixel 0.5 0.5 0.5 --set lut="$ldir/half.cube")
near "but naming a LUT alone applies all of it" 0.25 "$1"

# A DOMAIN other than [0,1] is part of the format, and ignoring it applies the
# table to the wrong inputs. This one is defined over the top half only, so
# everything below 0.5 clamps to its first entry: black.
{
    echo 'LUT_3D_SIZE 2'
    echo 'DOMAIN_MIN 0.5 0.5 0.5'
    echo 'DOMAIN_MAX 1.0 1.0 1.0'
    for b in 0.0 1.0; do for g in 0.0 1.0; do for r in 0.0 1.0; do
        echo "$r $g $b"
    done; done; done
} > "$ldir/dom.cube"
set -- $($BIN pixel 0.25 0.25 0.25 --set lut="$ldir/dom.cube")
near "below the domain clamps to its floor" 0.0 "$1"
set -- $($BIN pixel 0.75 0.75 0.75 --set lut="$ldir/dom.cube")
near "and the middle of the domain is the middle of the table" 0.5 "$1"

# A 1D cube is three curves, and plenty of transfer functions ship as one.
{
    echo 'LUT_1D_SIZE 3'
    echo '0.0 0.0 0.0'
    echo '0.25 0.25 0.25'
    echo '1.0 1.0 1.0'
} > "$ldir/one.cube"
set -- $($BIN pixel 0.5 0.5 0.5 --set lut="$ldir/one.cube")
near "a 1D cube reads as three curves" 0.25 "$1"
$BIN lut show "$ldir/one.cube" | seen "and says it is one-dimensional" "dims	1"

# ⚠ A TRUNCATED LUT IS THE FAILURE THAT MATTERS. A download that stopped half
# way parses perfectly row by row; only the COUNT catches it, and without that
# check the missing rows read as black and the top of every picture dies.
head -12 "$ldir/half.cube" > "$ldir/cut.cube"
sed -i '$d' "$ldir/cut.cube"
check "a truncated cube is refused" "1" \
      "$($BIN lut show "$ldir/cut.cube" >/dev/null 2>&1; echo $?)"
$BIN lut show "$ldir/cut.cube" 2>&1 >/dev/null | seen "and says how many rows it wanted" "expected"

printf 'LUT_3D_SIZE 2\nNONSENSE 4\n' > "$ldir/bad.cube"
check "an unknown keyword is refused" "1" \
      "$($BIN lut show "$ldir/bad.cube" >/dev/null 2>&1; echo $?)"
printf 'LUT_3D_SIZE 900\n' > "$ldir/huge.cube"
check "an absurd size is refused" "1" \
      "$($BIN lut show "$ldir/huge.cube" >/dev/null 2>&1; echo $?)"

# The catalogue: a bare NAME is what travels between machines, a path is what
# a file dropped in from anywhere is.
SYNSTUDIO_LUTS="$ldir" $BIN luts | seen "the catalogue finds a name" "half"
set -- $(SYNSTUDIO_LUTS="$ldir" $BIN pixel 0.5 0.5 0.5 --set lut=half)
near "and a name applies the same as a path" 0.25 "$1"

# ⚠ A LUT THIS MACHINE HAS NOT GOT IS KEPT, not dropped. Somebody opening a
# colleague's photograph must not save their look away.
lp=$TMP/keep.png
cp "${img:-/dev/null}" "$lp" 2>/dev/null || ffmpeg -v error -y -f lavfi \
    -i "testsrc2=size=32x24" -frames:v 1 "$lp" 2>/dev/null
if [ -s "$lp" ]; then
    $BIN set "$lp" lut=nosuchlut >/dev/null 2>&1
    $BIN get "$lp" lut | seen "a missing LUT survives a load and a save" "nosuchlut"
    set -- $($BIN pixel 0.5 0.5 0.5 --set lut=nosuchlut 2>/dev/null)
    near "and renders as nothing rather than as black" 0.5 "$1"
    $BIN pixel 0.5 0.5 0.5 --set lut=nosuchlut 2>&1 >/dev/null \
        | seen "and says so by name" "nosuchlut"
    $BIN reset "$lp"
fi

# THE ARCHITECTURAL CLAIM, for an IMPORTED look this time: it goes on at the
# end of the pointwise chain, and the .cube baker walks that same chain — so
# an imported LUT comes out INSIDE the baked cube, and video needs no second
# lut3d to get it. If this ever diverges, a look would land on a photograph
# and not on the clip beside it.
if [ -f "$img" ] && have ffmpeg; then
    $BIN set "$img" contrast=25 vibrance=40 lut="$ldir/half.cube" lut.amount=80
    $BIN render "$img" --out "$TMP/lengine.png" --bits 8 >/dev/null
    $BIN lut --from "$img" --out "$TMP/lg.cube"
    ffmpeg -v error -y -i "$img" -vf "lut3d=file=$TMP/lg.cube:interp=tetrahedral" \
           "$TMP/lvia.png" 2>/dev/null
    if [ -s "$TMP/lvia.png" ]; then
        psnr=$(ffmpeg -v error -i "$TMP/lengine.png" -i "$TMP/lvia.png" \
               -lavfi psnr=stats_file=- -f null - 2>&1 \
               | awk -F'psnr_avg:' '/psnr_avg/{print $2+0}' | tail -1)
        check "an imported LUT survives the bake (>45dB)" "yes" \
              "$(awk -v p="${psnr:-0}" 'BEGIN{print (p>45)?"yes":"no"}')"
    fi
    # And a clip carrying one bakes at all — the cube is what the export hands
    # to lut3d, so a clip with ONLY a LUT must still produce one.
    $BIN reset "$img"
fi

echo "== looks (.synlook: the sliders, not the table)"

check "the shipped looks are there" "12" "$($BIN look list | wc -l)"
$BIN look list | seen "including one everybody recognises" "Teal and Orange"
$BIN look show noir | seen "a look says what it sets" "set	saturation	-100"

lkdir=$TMP/looks
mkdir -p "$lkdir"
lkimg=$TMP/look.png
ffmpeg -v error -y -f lavfi -i "testsrc2=size=32x24" -frames:v 1 "$lkimg" 2>/dev/null

if [ -s "$lkimg" ]; then
    # ⚠ A LOOK LANDS ON TOP, it does not replace. The exposure and white
    # balance a photograph needed are corrections; the look is a decision. A
    # look that reset them would undo the work it was put on top of.
    $BIN set "$lkimg" exposure=0.5 >/dev/null
    $BIN look apply teal-orange --to "$lkimg" >/dev/null
    $BIN get "$lkimg" exposure | seen "a look leaves the correction under it" "0.5"
    $BIN get "$lkimg" grade.shadow.hue | seen "and sets what it names" "195"

    # ⚠ AND IT NEVER CARRIES GEOMETRY. A crop belongs to one photograph; a
    # look is meant to travel. Written as a group check in the engine, so a
    # geometry control added later is excluded by being in the right group.
    $BIN set "$lkimg" crop.x=0.1 >/dev/null
    # ⚠ the look is NOT called "cropped": the file carries its own name on a
    # label line, so a needle of "crop" matches the label and reports the bug
    # it was written to catch. The same shape as grepping for MANGOHUD=1
    # inside DISABLE_MANGOHUD=1.
    SYNSTUDIO_LOOKS="$lkdir" $BIN look save geo --from "$lkimg" >/dev/null
    notseen "a saved look carries no crop" "crop" < "$lkdir/geo.synlook"
    $BIN reset "$lkimg"

    # Only what MOVED. A look that wrote all sixty-six fields would carry a
    # default contrast onto every photograph it touched.
    $BIN set "$lkimg" contrast=40 saturation=-20 >/dev/null
    SYNSTUDIO_LOOKS="$lkdir" $BIN look save two --from "$lkimg" \
        | seen "a saved look holds only what moved" "fields	2"
    $BIN reset "$lkimg"

    # A look of your own WINS over a shipped one of the same name, which is
    # how one gets fixed without waiting for anybody.
    printf 'label\tMine\ncontrast\t7\n' > "$lkdir/noir.synlook"
    SYNSTUDIO_LOOKS="$lkdir" $BIN look show noir | seen "your own copy wins on a name" "Mine"
    $BIN look show noir | seen "and the shipped one is still there without it" "Noir"
fi

# A look on a CLIP is the same look, through the same table.
if [ -n "${kdir:-}" ] && [ -f "$kdir/clip.mp4" ]; then
    lproj=$TMP/look.syntl
    $BIN timeline new "$lproj" --size 64x48 --fps 25 >/dev/null
    $BIN timeline track "$lproj" video V1 >/dev/null
    $BIN timeline clip "$lproj" 0 "$kdir/clip.mp4" --at 0 >/dev/null
    $BIN timeline grade "$lproj" 0 0 --look noir >/dev/null
    grep -F 'saturation	-100' "$lproj" > /dev/null
    check "a look lands on a clip too" "0" "$?"
    check "an unknown look on a clip is refused" "1" \
          "$($BIN timeline grade "$lproj" 0 0 --look nosuchlook >/dev/null 2>&1; echo $?)"
fi

# ⚠ and the picker has to LIST a .cube, or a LUT is reachable by typing a path
# and no other way — the same hole a project had before `browse` listed one.
$BIN browse "$ldir" | seen "the picker lists a .cube" "look	half.cube"

echo "== what the window is launched with"

# THE BUG THIS EXISTS FOR (synstudio 0.1.0-15). The session exports MANGOHUD=1
# so games get the overlay, and MangoHud's Vulkan manifest declares
#
#     "enable_environment": { "MANGOHUD": "1" }
#
# which loads its layer into EVERY Vulkan client. Building a QML MediaPlayer
# builds a QMediaPlayer, whose ffmpeg backend asks libavutil for a Vulkan
# hardware device on construction — so opening the editor on an AMD laptop
# SEGFAULTED quickshell in MangoHud's vkCreateDevice hook before a frame was
# drawn. NVIDIA picks a different hwdevice, never calls it, and never sees it.
#
# `gui` execs quickshell, so what is asserted here is the ENVIRONMENT it is
# handed: a stub named quickshell, first on PATH, that prints what it got.
# ⚠ the stub needs an explicit interpreter — PATH inside a spawned stub is
# whatever the caller had, and a stub that assumed one has cost this repo a
# passing test over a broken fix before.
gdir=$TMP/gui
mkdir -p "$gdir/bin"
cat > "$gdir/bin/quickshell" <<'STUB'
#!/bin/bash
env
STUB
chmod +x "$gdir/bin/quickshell"

genv=$(PATH="$gdir/bin:$PATH" $BIN gui 2>/dev/null)
printf '%s\n' "$genv" | seen "the launcher disables MangoHud" "DISABLE_MANGOHUD=1"
printf '%s\n' "$genv" | seen "and turns the enable off with it" "MANGOHUD=0"
# The one that made it crash. DISABLE_MANGOHUD beats the enable in the
# manifest, but leaving MANGOHUD=1 set as well is the state that was shipped,
# so assert the launcher really overwrote it rather than adding beside it.
#
# ⚠ ANCHORED, and that is the whole point of writing it as a regex: the
# substring "MANGOHUD=1" is inside "DISABLE_MANGOHUD=1", so a plain notseen
# here fails on the very environment that proves the fix works.
check "MANGOHUD=1 does not survive the launcher" "0" \
      "$(printf '%s\n' "$genv" | grep -c '^MANGOHUD=1$')"
# And it still says which program the window belongs to.
printf '%s\n' "$genv" | seen "the window still claims its own app_id" "QS_APP_ID=synstudio"

# And that the window FILE still loads at all.
#
# ⚠ A failed QML import, a duplicate id or a function whose name collides with
# an existing property fails the WHOLE FILE, not the line — this repo has lost
# an entire window to `id: gc` (the engine's own garbage collector) and to a
# `function selKey()` beside a `property int selKey`, and the second of those
# opened normally and broke one gesture at call time. Loading it offscreen
# costs a couple of seconds and catches the whole class.
#
# Skipped rather than failed where quickshell or a runtime dir is missing: a
# build chroot has neither, and a test that only passes on a desktop is a test
# that gets turned off.
# ------------------------------------------------------------ versions ---
#
# Undo is already the auto-save half: every save records the state it left, so
# nothing is lost between saves. What undo does NOT do is keep anything for
# long — it is a ring of a hundred states and the oldest falls off the end. A
# version is a document somebody decided to KEEP, with a name they chose, that
# nothing expires and no edit disturbs.

vp=$TMP/versions.sstl
$BIN timeline new "$vp" --size 320x180 --fps 25 >/dev/null
$BIN timeline track "$vp" video V >/dev/null
$BIN timeline solid "$vp" 0 --at 0 --dur 1 --colour 1,0,0 >/dev/null
$BIN timeline solid "$vp" 0 --at 1 --dur 1 --colour 0,1,0 >/dev/null

check "a project has no versions to begin with" "0" \
      "$($BIN timeline version "$vp" list | awk -F'\t' '/^versions/{print $2}')"
$BIN timeline version "$vp" save two-clips | seen "one can be kept" "saved	two-clips"
check "and it is listed"  "1" \
      "$($BIN timeline version "$vp" list | awk -F'\t' '/^versions/{print $2}')"
$BIN timeline version "$vp" list | seen "with the moment it was kept" "version	two-clips	20"

# ⚠ A name becomes a FILE. A slash or a leading dot would write outside the
# project's own directory, and sanitising it quietly would mean a later
# `restore` cannot find what it just saved — so it is refused instead.
$BIN timeline version "$vp" save "bad/name" 2>&1 | seen "a name with a slash is refused" "becomes a file"
$BIN timeline version "$vp" save ".hidden"  2>&1 | seen "and so is a leading dot"       "becomes a file"

$BIN timeline delete "$vp" 0 1 >/dev/null
check "the project can then be changed" "1" "$($BIN timeline show "$vp" | grep -c '^clip')"
$BIN timeline version "$vp" restore two-clips | seen "and the version restored" "restored"
check "which brings the clips back" "2" "$($BIN timeline show "$vp" | grep -c '^clip')"

# ⚠ THE ASSERTION THAT MATTERS. A restore goes through the ordinary save path,
# so it is itself undoable — a restore that could not be undone would be the
# one operation in this program able to lose work.
$BIN timeline undo "$vp" >/dev/null
check "and a restore is itself undoable" "1" "$($BIN timeline show "$vp" | grep -c '^clip')"

# ---------------------------------------------------------- watermark ----
#
# A PICTURE, so unlike the burn-in it cannot be a filter on the end of the
# chain — it is another input, and it goes in LAST for the same reason the
# subtitles do: every label in the graph names an input by number.
if have ffmpeg; then
    wmk=$TMP/logo.png
    ffmpeg -v error -y -f lavfi -i "color=c=yellow@0.85:s=200x60,format=rgba" \
           -frames:v 1 "$wmk" 2>/dev/null
    # ⚠ Its own project. `$dp` belongs to the delivery section further down
    # this file, and reaching forward for a variable that does not exist yet
    # is how a test ends up asserting against an empty string.
    wmp=$TMP/watermark.sstl
    $BIN timeline new "$wmp" --size 640x360 --fps 25 >/dev/null
    $BIN timeline track "$wmp" video V >/dev/null
    $BIN timeline solid "$wmp" 0 --at 0 --dur 2 --colour 0.8,0.1,0.1 >/dev/null
    wg=$($BIN timeline export "$wmp" --out "$TMP/wm.mp4" --watermark "$wmk" --print)
    echo "$wg" | seen "a watermark is overlaid"        "overlay=W-w-H*0.04"
    # Sized as a FRACTION of the frame, so one file marks a 1080 delivery and
    # a 4K one identically.
    echo "$wg" | seen "and scaled to the frame, not to pixels" "scale=iw*0.1200"
    $BIN timeline export "$wmp" --out "$TMP/wm.mp4" --watermark "$TMP/nope.png" 2>&1 \
        | seen "a missing watermark is caught before the render" "cannot read"
    $BIN timeline export "$wmp" --out "$TMP/wm.mp4" --watermark "$wmk" >/dev/null 2>&1
    check "and the delivery renders with it" "yes" \
          "$([ -s "$TMP/wm.mp4" ] && echo yes || echo no)"
fi

# ------------------------------------------------ copy, paste, duplicate --
#
# The clipboard is a one-clip DOCUMENT, written and read by the same two
# functions the project file uses. Not a struct dumped to disk: an ss_clip
# carries four curve tables and a develop stack and its layout changes
# whenever a control is added, so a binary clipboard would be a file that
# silently means something different after an update.
#
# What that buys is the assertion below — everything the project format knows
# about a clip travels, rather than the handful of fields a bespoke copy would
# have remembered to carry.

cbp=$TMP/copy.sstl
export SYNSTUDIO_CLIPBOARD=$TMP/clipboard
rm -f "$SYNSTUDIO_CLIPBOARD"
$BIN timeline new "$cbp" --size 640x360 --fps 25 >/dev/null
$BIN timeline track "$cbp" video V >/dev/null
$BIN timeline solid "$cbp" 0 --at 0 --dur 2 --colour 0.9,0.2,0.1 >/dev/null
$BIN timeline solid "$cbp" 0 --at 2 --dur 2 --colour 0.1,0.2,0.9 >/dev/null
$BIN timeline solid "$cbp" 0 --at 4 --dur 2 --colour 0.1,0.9,0.2 >/dev/null
$BIN timeline grade "$cbp" 0 0 exposure=1.5 contrast=30 >/dev/null
$BIN timeline set   "$cbp" 0 0 nr=25 comp=40 opacity=0.5 >/dev/null

$BIN timeline paste "$cbp" 0 2>&1 | seen "pasting with nothing copied is refused" "nothing has been copied"
$BIN timeline copy "$cbp" 0 0 | seen "a clip copies" "copied	0	0"
$BIN timeline paste "$cbp" 0 --at 6 | seen "and pastes as a new one" "pasted	0	3"

# ⚠ EVERYTHING travels, not just the obvious fields. The sound chain and the
# develop stack are the two that a hand-written copy would most likely drop,
# because neither is a member of the clip a casual reader would think to look
# for.
check "the sound chain travels with it" "25" \
      "$($BIN timeline get "$cbp" 0 3 | awk -F'\t' '/^nr\t/{print $2}')"
check "and the levels"                  "0.5" \
      "$($BIN timeline get "$cbp" 0 3 | awk -F'\t' '/^opacity/{print $2}')"
$BIN timeline show "$cbp" | seen "and the grade" "exposure	1.5"

# ---- a grade onto clips that already exist -------------------------------
#
# The half that saves an afternoon: take ONLY the develop stack off the
# clipboard and leave the target's timing, framing and sound alone.
$BIN timeline paste "$cbp" 0 1 --grade | seen "a grade pastes onto one clip" "graded	1"
check "and the target keeps its own timing" "2.000000" \
      "$($BIN timeline get "$cbp" 0 1 | awk -F'\t' '/^tl_in/{print $2}')"
# ⚠ The discriminating half: a GRADE paste must not drag the rest of the
# clipboard clip with it. The copied clip has nr=25 and opacity 0.5; this one
# must still have neither.
check "and its own sound"   "0" \
      "$($BIN timeline get "$cbp" 0 1 | awk -F'\t' '/^nr\t/{print $2}')"
check "and its own levels"  "1" \
      "$($BIN timeline get "$cbp" 0 1 | awk -F'\t' '/^opacity/{print $2}')"

$BIN timeline paste "$cbp" 0 --grade --all | seen "or onto every clip on a track" "graded	4"
check "and all four are graded" "4" "$(grep -c '^grade	' "$cbp")"

# ---- duplicate -----------------------------------------------------------
$BIN timeline duplicate "$cbp" 0 1 | seen "a clip duplicates" "duplicated"
# Straight after itself, which is what duplicating is for: clip 1 runs 2..4,
# so its copy starts at 4.
check "straight after itself by default" "4.000000" \
      "$($BIN timeline duplicate "$cbp" 0 1 | awk -F'\t' '/^at/{print $2}')"
unset SYNSTUDIO_CLIPBOARD

# ----------------------------------------------------- the sound chain ---
#
# Clean it, shape it, control it — noise reduction, gate, EQ, compressor,
# de-esser, in that order. It is not a matter of taste: each stage decides
# what the next one gets to work on. A gate AFTER a compressor gates a signal
# whose quiet parts have already been lifted, and a de-esser BEFORE an EQ
# chases sibilance the EQ is about to move.

sp2=$TMP/sound.sstl
$BIN timeline new "$sp2" --size 320x180 --fps 25 >/dev/null
$BIN timeline track "$sp2" video V >/dev/null
if have ffmpeg; then
    sclip=$TMP/sound.mp4
    ffmpeg -v error -y -f lavfi -i "testsrc2=s=320x180:d=4:r=25" \
           -f lavfi -i "sine=f=440:d=4" -shortest \
           -c:v libx264 -pix_fmt yuv420p -c:a aac "$sclip" 2>/dev/null
    $BIN timeline clip "$sp2" 0 "$sclip" --at 0 --dur 4 >/dev/null

    # Nothing on: the chain has to be ABSENT, not present and neutral. A
    # filter set to do nothing still costs a pass over every sample, and
    # afftdn in particular is not cheap.
    q=$($BIN timeline export "$sp2" --out "$TMP/s.mp4" --print)
    echo "$q" | notseen "an untouched clip has no denoiser" "afftdn"
    echo "$q" | notseen "no compressor"                     "acompressor"
    echo "$q" | notseen "and no EQ"                         "equalizer="

    $BIN timeline set "$sp2" 0 0 nr=30 gate=20 eq.200=-4 eq.2k=3 comp=60 \
         comp.thresh=-20 deess=40 >/dev/null
    q=$($BIN timeline export "$sp2" --out "$TMP/s.mp4" --print)
    echo "$q" | seen "noise reduction reaches the graph" "afftdn=nr="
    echo "$q" | seen "the gate"                          "agate=threshold="
    echo "$q" | seen "the compressor"                    "acompressor=threshold="
    echo "$q" | seen "the de-esser"                      "deesser=i="
    # ⚠ Two bands were set and four were not, so exactly two biquads belong in
    # the chain. A six-band EQ that always emits six filters is six passes over
    # the samples to do nothing four times.
    check "and one biquad per band that was set" "2" \
          "$(echo "$q" | grep -o 'equalizer=f=' | grep -c .)"
    echo "$q" | seen "at the frequency it was set at" "equalizer=f=200:"

    # The ORDER, which is the whole design. Measured as positions in the one
    # string, so a reshuffle that still contains every filter fails here.
    ac=$(echo "$q" | tr ';' '\n' | grep "0:a" | head -1)
    check "the chain is built in the right order" "yes" \
          "$(awk -v s="$ac" 'BEGIN{
                n=index(s,"afftdn"); g=index(s,"agate"); e=index(s,"equalizer");
                c=index(s,"acompressor"); d=index(s,"deesser");
                print (n<g && g<e && e<c && c<d) ? "yes" : "no" }')"

    $BIN timeline export "$sp2" --out "$TMP/s.mp4" >/dev/null 2>&1
    check "and it renders" "yes" \
          "$([ -s "$TMP/s.mp4" ] && echo yes || echo no)"

    # ---- fade shapes -----------------------------------------------------
    $BIN timeline set "$sp2" 0 0 fade.in=0.5 fade.shape=qsin >/dev/null
    $BIN timeline export "$sp2" --out "$TMP/s.mp4" --print \
        | seen "a fade takes the shape it was given" "afade=t=in:st=0:d=0.5000:curve=qsin"
    $BIN timeline set "$sp2" 0 0 fade.shape=linear >/dev/null
    # ⚠ afade calls the straight one `tri`. The name a person types and the
    # name the filter takes are different, and the table is what maps them.
    $BIN timeline export "$sp2" --out "$TMP/s.mp4" --print \
        | seen "and linear is afade's own tri" "curve=tri"

    # ---- it survives a save and a reload ---------------------------------
    # ⚠ A REAL tab. grep's BRE has no \t escape — `'^sound\t'` is the pattern
    # `^soundt`, which matches nothing, and the check then fails on a file
    # that is perfectly correct.
    check "the sound chain round-trips" "1" "$(grep -c '^sound	' "$sp2")"
    check "with the band that was set"  "-4" \
          "$($BIN timeline get "$sp2" 0 0 | awk -F'\t' '/^eq.200/{print $2}')"
fi

# ---- delivery loudness ---------------------------------------------------
#
# After the mix, after the master fader and after the limiter, because a
# broadcast target is a statement about the FILE and nothing downstream of it
# may change the level again.
$BIN timeline loudness "$sp2" | seen "a project has no loudness target by default" "none"
$BIN timeline loudness "$sp2" --value -14 | seen "one can be set" "target	-14.00"
$BIN timeline export "$sp2" --out "$TMP/s.mp4" --print \
    | seen "and it reaches the graph after the limiter" "alimiter=limit=0.99:level=disabled,loudnorm=I=-14.00"
$BIN timeline loudness "$sp2" --value -3 2>&1 | seen "an impossible target is refused" "between -40 and -5"

if have ffmpeg; then
    # ⚠ Measured, not asserted: the program has its own meter, so what came
    # out can be held against what was asked for.
    $BIN timeline export "$sp2" --out "$TMP/s14.mp4" >/dev/null 2>&1
    got=$($BIN loudness "$TMP/s14.mp4" 2>/dev/null | awk -F'\t' '/^lufs/{print $2}')
    check "and the delivered file measures at the target" "yes" \
          "$(awk -v g="${got:-0}" 'BEGIN{d=g+14; if(d<0)d=-d; print (d<1.0)?"yes":"no"}')"
fi
$BIN timeline loudness "$sp2" --off | seen "and it can be taken off" "none"

# ---- ducking -------------------------------------------------------------
#
# A music bed gets out of the way of the dialogue. The dialogue is another
# TRACK, so its sound has to exist as one stream before it can key anything —
# and every clip on it has already been spent on the main mix, which is why
# each one is SPLIT rather than read twice: naming a stream twice fails the
# whole graph.
if have ffmpeg; then
    dk=$TMP/duck.sstl
    ffmpeg -v error -y -f lavfi -i "sine=f=200:d=6" -c:a aac "$TMP/music.m4a" 2>/dev/null
    ffmpeg -v error -y -f lavfi -i "anoisesrc=d=2:c=pink:a=0.5" \
           -af "adelay=2000|2000,apad=whole_dur=6" -c:a aac "$TMP/voice.m4a" 2>/dev/null
    $BIN timeline new "$dk" --size 320x180 --fps 25 >/dev/null
    $BIN timeline track "$dk" audio MUSIC >/dev/null
    $BIN timeline track "$dk" audio VOICE >/dev/null
    $BIN timeline clip "$dk" 0 "$TMP/music.m4a" --at 0 --dur 6 >/dev/null
    $BIN timeline clip "$dk" 1 "$TMP/voice.m4a" --at 0 --dur 6 >/dev/null

    $BIN timeline track "$dk" 0 --duck 1 --duck-amount 80 >/dev/null
    dq=$($BIN timeline export "$dk" --out "$TMP/dk.m4a" --print)
    echo "$dq" | seen "the key track is keyed into a compressor" "sidechaincompress"
    echo "$dq" | seen "and split, because it is wanted twice"    "asplit="
    $BIN timeline track "$dk" 1 --duck 1 2>&1 | seen "a track cannot duck itself" "cannot duck itself"

    $BIN timeline export "$dk" --out "$TMP/dk.m4a"  >/dev/null 2>&1
    $BIN timeline track "$dk" 0 --duck off >/dev/null
    $BIN timeline export "$dk" --out "$TMP/dk0.m4a" >/dev/null 2>&1
    at() { ffmpeg -v info -ss "$2" -t 1 -i "$1" -af volumedetect -f null - 2>&1 \
           | awk -F': ' '/mean_volume/{print $2+0}'; }
    # The discriminating pair: identical where the key is SILENT, and quieter
    # where it is not. Either half alone would pass on a mix that was simply
    # turned down.
    check "nothing is ducked while the key track is silent" "yes" \
          "$(awk -v a="$(at "$TMP/dk.m4a" 0)" -v b="$(at "$TMP/dk0.m4a" 0)" \
                 'BEGIN{d=a-b; if(d<0)d=-d; print (d<0.5)?"yes":"no"}')"
    check "and the bed drops while it is not" "yes" \
          "$(awk -v a="$(at "$TMP/dk.m4a" 3)" -v b="$(at "$TMP/dk0.m4a" 3)" \
                 'BEGIN{print (a < b-1.0)?"yes":"no"}')"
fi

# ------------------------------------------------- over-range highlights --
#
# ⚠ A WHITE PIXEL RENDERED BLACK, and five points of contrast were enough.
#
# apply_contrast is the cubic -4v^3+6v^2-2v, which is a contrast curve on
# [0,1] and a cliff outside it. Anything that lifts a highlight past white
# leaves an encoded value above 1 in the float pipeline; at 5.55 the cubic
# returns MINUS 510, `gain = nv/v` comes out negative, and every channel of
# that pixel is multiplied by a negative number. The tone-region block
# directly above it had always clamped its input. This one did not.

if have ffmpeg; then
    wp=$TMP/white.png
    ffmpeg -v error -y -f lavfi -i "color=c=0x808080:s=32x32" -frames:v 1 "$wp" 2>/dev/null
    rgbof() {   # rgbof <png> -> "R G B" of the first pixel
        ffmpeg -v error -y -i "$1" -vf format=rgb24 -f rawvideo - 2>/dev/null \
            | od -An -tu1 -N3 | tr -s ' ' ' ' | sed 's/^ //;s/ $//'
    }
    $BIN render "$wp" --out "$TMP/w0.png" --set exposure=8 >/dev/null 2>&1
    check "a blown highlight is white" "255 255 255" "$(rgbof "$TMP/w0.png")"
    # The regression itself.
    $BIN render "$wp" --out "$TMP/w5.png" --set exposure=8 --set contrast=5 >/dev/null 2>&1
    check "and STAYS white when contrast is added" "255 255 255" "$(rgbof "$TMP/w5.png")"
    $BIN render "$wp" --out "$TMP/w40.png" --set exposure=8 --set contrast=40 >/dev/null 2>&1
    check "at any amount of it" "255 255 255" "$(rgbof "$TMP/w40.png")"
    # ⚠ And the fix is a NO-OP in range: sc(0) and sc(1) are both zero, so
    # nothing at or beyond either end moves, and mid grey is still the pivot.
    $BIN render "$wp" --out "$TMP/wm.png" --set contrast=40 >/dev/null 2>&1
    check "mid grey is still the pivot" "127 127 127" "$(rgbof "$TMP/wm.png")"
    rm -f "$wp.synstudio"
fi

# --------------------------------------------------------- shot match ----
#
# Fitted, not solved. Every control has a transfer function of its own, and
# solving one in closed form means writing a second model of what colour.c
# does — which drifts the first time colour.c is improved. So each control is
# set, rendered THROUGH THE REAL ENGINE, measured and bisected.

if have ffmpeg; then
    mA=$TMP/shotA.png
    mB=$TMP/shotB.png
    ffmpeg -v error -y -f lavfi \
        -i "gradients=s=320x180:c0=0x2a3550:c1=0xd8c9a8:x0=0:y0=0:x1=320:y1=180" \
        -frames:v 1 "$mA" 2>/dev/null
    # The same shot, a little darker, cooler and flatter — which is what two
    # cameras on one scene actually look like.
    ffmpeg -v error -y -i "$mA" \
        -vf "eq=brightness=-0.06:contrast=0.85,colorchannelmixer=rr=0.93:bb=1.10" \
        "$mB" 2>/dev/null
    rm -f "$mA.synstudio" "$mB.synstudio"

    $BIN match "$mB" --ref "$mA" > "$TMP/match.out" 2>&1
    seen "a match reports what it fitted" "exposure" < "$TMP/match.out"
    # It has to LAND, not just run. The report prints what was wanted beside
    # what was got, so the assertion is on the distance and not on a promise.
    check "and lands the brightness" "yes" \
          "$(awk -F'\t' '/^luma/{d=$2-$3; if(d<0)d=-d; print (d<0.01)?"yes":"no"}' "$TMP/match.out")"
    check "and the contrast" "yes" \
          "$(awk -F'\t' '/^spread/{d=$2-$3; if(d<0)d=-d; print (d<0.01)?"yes":"no"}' "$TMP/match.out")"
    # ⚠ The temperature is KELVIN, 2000..12000 — not a -100..100 slider. The
    # first version of the fit hardcoded the wrong range, every `set` was
    # refused as out of range, and the fit silently left the control alone and
    # made tint do all the colour work until it pinned at its limit.
    check "the temperature it set is a real one" "yes" \
          "$(awk -F'\t' '/^temp/{print ($2>=2000 && $2<=12000)?"yes":"no"}' "$TMP/match.out")"

    # And the PICTURE is closer, which is the only claim that matters.
    $BIN render "$mB" --out "$TMP/mB_after.png" >/dev/null 2>&1
    $BIN render "$mA" --out "$TMP/mA_dev.png" >/dev/null 2>&1
    mpsnr() { ffmpeg -v error -i "$1" -i "$2" -lavfi psnr=stats_file=- -f null - 2>&1 \
              | awk -F'psnr_avg:' '/psnr_avg/{split($2,a," "); v=(a[1]=="inf")?999:a[1]+0; print v}' \
              | tail -1; }
    before=$(mpsnr "$mB" "$TMP/mA_dev.png")
    after=$(mpsnr "$TMP/mB_after.png" "$TMP/mA_dev.png")
    check "and the shot is measurably closer to the reference" "yes" \
          "$(awk -v a="${before:-0}" -v b="${after:-0}" 'BEGIN{print (b>a+4)?"yes":"no"}')"

    $BIN match "$mB" 2>&1 | seen "a match needs a reference" "needs --ref"
    rm -f "$mA.synstudio" "$mB.synstudio"

    # ---- clip to clip ----------------------------------------------------
    ffmpeg -v error -y -loop 1 -i "$mA" -t 1 -r 25 -c:v libx264 -pix_fmt yuv420p \
           "$TMP/vA.mp4" 2>/dev/null
    ffmpeg -v error -y -loop 1 -i "$mB" -t 1 -r 25 -c:v libx264 -pix_fmt yuv420p \
           "$TMP/vB.mp4" 2>/dev/null
    mp=$TMP/match.sstl
    $BIN timeline new "$mp" --size 320x180 --fps 25 >/dev/null
    $BIN timeline track "$mp" video V >/dev/null
    $BIN timeline clip "$mp" 0 "$TMP/vA.mp4" --at 0 --dur 1 >/dev/null
    $BIN timeline clip "$mp" 0 "$TMP/vB.mp4" --at 1 --dur 1 >/dev/null
    $BIN timeline match "$mp" 0 1 0 0 > "$TMP/tmatch.out" 2>&1
    check "a clip matches another clip" "yes" \
          "$(awk -F'\t' '/^luma/{d=$2-$3; if(d<0)d=-d; print (d<0.01)?"yes":"no"}' "$TMP/tmatch.out")"
    # ⚠ `^grade<TAB>` and not `^grade`: the develop stack writes grade.shadow.hue
    # and five more keys that all start with the same six letters, so the loose
    # pattern counts six and would have passed on a document with no clip
    # grade in it at all.
    check "and the grade is written into the document" "1" \
          "$(grep -c '^grade	' "$mp")"
    $BIN timeline match "$mp" 0 1 0 1 2>&1 \
        | seen "a clip cannot match itself" "already matches itself"
    $BIN timeline solid "$mp" 0 --at 2 --dur 1 >/dev/null
    $BIN timeline match "$mp" 0 2 0 0 2>&1 \
        | seen "and a title has no shot in it" "have to be footage"
fi

# -------------------------------------------------------------- scopes ---
#
# Computed HERE and not by an ffmpeg filter, for the reason the histogram is:
# a scope is read to decide whether a shot is legal and whether two shots
# match, and an answer from a different renderer than the picture is an answer
# about something else.

if have ffmpeg; then
    scsrc=$TMP/scope.png
    ffmpeg -v error -y -f lavfi -i "testsrc2=s=640x360" -frames:v 1 "$scsrc" 2>/dev/null
    for k in waveform parade vector; do
        $BIN scope "$scsrc" --out "$TMP/sc_$k.png" --kind "$k" --size 480 \
            | seen "a $k renders" "kind	$k"
    done
    # A vectorscope is a POLAR plot, so it is square — a wide one stretches
    # the hue wheel into an ellipse and every angle read off it is wrong.
    check "a vectorscope is square" "yes" \
          "$($BIN scope "$scsrc" --out "$TMP/sq.png" --kind vector --size 400 \
             | awk -F'\t' '/^width/{w=$2} /^height/{h=$2} END{print (w==h)?"yes":"no"}')"
    check "a waveform is not" "yes" \
          "$($BIN scope "$scsrc" --out "$TMP/wf.png" --kind waveform --size 400 \
             | awk -F'\t' '/^width/{w=$2} /^height/{h=$2} END{print (w>h)?"yes":"no"}')"
    $BIN scope "$scsrc" --out "$TMP/x.png" --kind nonesuch 2>&1 \
        | seen "an unknown kind is refused" "waveform, parade or vector"

    # ⚠ The assertion that matters: a scope is not normalised by its BUSIEST
    # cell. It was, and a vectorscope of colour bars came out almost black —
    # the greys pile into a few cells at the centre and every hue around them
    # divides down to nothing. Measured as: enough of the plot is actually
    # lit to read.
    lit=$(ffmpeg -v error -y -i "$TMP/sc_vector.png" -vf format=gray -f rawvideo - 2>/dev/null \
          | od -An -tu1 -v | tr -s ' ' '\n' | awk 'NF && $1>16 {n++} END{print n+0}')
    check "and the vectorscope is actually visible" "yes" \
          "$(awk -v n="${lit:-0}" 'BEGIN{print (n>1500)?"yes":"no"}')"

    # A scope of the CUT, composited first — grade, titles, transitions and
    # all — so it describes the picture that will be delivered.
    scp=$TMP/scopetl.sstl
    $BIN timeline new "$scp" --size 640x360 --fps 25 >/dev/null
    $BIN timeline track "$scp" video V >/dev/null
    $BIN timeline solid "$scp" 0 --at 0 --dur 2 --colour 0.5,0.2,0.1 >/dev/null
    $BIN timeline scope "$scp" --at 1 --out "$TMP/tlsc.png" --kind parade \
        | seen "a scope of the timeline composites first" "kind	parade"
    check "and writes a picture" "yes" \
          "$([ -s "$TMP/tlsc.png" ] && echo yes || echo no)"
fi

# ------------------------------------------------------------ delivery ---
#
# A range, a preset, a burn-in, an image sequence and a queue. Everything here
# is about what leaves the program, and none of it is allowed to change the
# CUT: a render range is a window onto the finished picture, not a different
# edit, and a burn-in must never survive into a master.

dp=$TMP/deliver.sstl
$BIN timeline new "$dp" --size 640x360 --fps 25 >/dev/null
$BIN timeline track "$dp" video V >/dev/null
$BIN timeline solid "$dp" 0 --at 0 --dur 3 --colour 0.8,0.1,0.1 >/dev/null
$BIN timeline solid "$dp" 0 --at 3 --dur 3 --colour 0.1,0.1,0.8 >/dev/null

check "a project renders the whole timeline by default" "yes" \
      "$($BIN timeline range "$dp" | awk -F'\t' '/^whole/{print $2}')"
$BIN timeline range "$dp" --at 2 --to 5 | seen "a range can be set" "length	3.000"
check "and it is written into the document" "1" \
      "$(grep -c '^range	' "$dp")"
check "and read back" "2.000" "$($BIN timeline range "$dp" | awk -F'\t' '/^in/{print $2}')"
$BIN timeline export "$dp" --out "$TMP/rng.mp4" --print \
    | seen "the export trims to it" "trim=start=2.000000:end=5.000000"
# ⚠ setpts=PTS-STARTPTS after the trim, or the frames keep the timestamps they
# had on the timeline and a render of minutes 9 to 10 arrives with nine
# minutes of nothing at the front of it.
$BIN timeline export "$dp" --out "$TMP/rng.mp4" --print \
    | seen "and restarts the clock" "setpts=PTS-STARTPTS"

if have ffmpeg && have ffprobe; then
    $BIN timeline export "$dp" --out "$TMP/rng.mp4" >/dev/null 2>&1
    d=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$TMP/rng.mp4")
    check "a ranged render is the length of the range" "yes" \
          "$(awk -v d="${d:-0}" 'BEGIN{print (d>2.9 && d<3.15)?"yes":"no"}')"
    # And it is a WINDOW, not a different edit: one second into the render is
    # three seconds into the timeline, which is the second solid and not the
    # first. The discriminating half is that it does NOT match one second in.
    ffmpeg -v error -y -i "$TMP/rng.mp4" -vf "select=eq(n\,25)" \
           -fps_mode passthrough -frames:v 1 "$TMP/rng1.png"
    $BIN timeline frame "$dp" --at 3.0 --out "$TMP/tl3.png" >/dev/null 2>&1
    $BIN timeline frame "$dp" --at 1.0 --out "$TMP/tl1.png" >/dev/null 2>&1
    # `inf` for an exact match — see psnr2 above.
    psnr3() { ffmpeg -v error -i "$1" -i "$2" -lavfi psnr=stats_file=- -f null - 2>&1 \
              | awk -F'psnr_avg:' '/psnr_avg/{split($2,a," "); v=(a[1]=="inf")?999:a[1]+0; print v}' \
              | tail -1; }
    check "one second in is three seconds along the timeline" "yes" \
          "$(awk -v p="$(psnr3 "$TMP/rng1.png" "$TMP/tl3.png")" 'BEGIN{print (p>30)?"yes":"no"}')"
    yv() { ffmpeg -v error -i "$1" -vf "signalstats,metadata=print:key=lavfi.signalstats.YAVG:file=-" -f null - 2>&1 | awk -F= '/YAVG/{print $2}' | tail -1; }
    check "and not one second along it" "yes" \
          "$(awk -v p="$(psnr3 "$TMP/rng1.png" "$TMP/tl1.png")" 'BEGIN{print (p<25)?"yes":"no"}')"
fi
$BIN timeline range "$dp" --off | seen "a range can be taken off again" "whole	yes"

# ---- presets -------------------------------------------------------------
check "seven presets ship" "7" "$($BIN timeline presets | grep -c .)"
$BIN timeline presets | seen "named for where they are going" "youtube-1080p"
$BIN timeline export "$dp" --out "$TMP/p.mp4" --preset nonesuch 2>&1 \
    | seen "an unknown preset is refused" "no preset called"
# A preset is applied by rendering the whole COMPOSITE at that size — every
# clip, the base and the titles are built from the project's dimensions — so
# the base itself changes size and nothing is scaled afterwards.
$BIN timeline export "$dp" --out "$TMP/p.mp4" --preset youtube-720p --print \
    | seen "a preset resizes the composite" "s=1280x720"
if have ffmpeg && have ffprobe; then
    $BIN timeline export "$dp" --out "$TMP/p.mp4" --preset youtube-720p >/dev/null 2>&1
    check "and the delivered file is that size" "1280,720" \
          "$(ffprobe -v error -select_streams v -show_entries stream=width,height \
                     -of csv=p=0 "$TMP/p.mp4")"
fi

# ---- burn-in -------------------------------------------------------------
bg=$($BIN timeline export "$dp" --out "$TMP/b.mp4" --burn both --print)
echo "$bg" | seen "a burn-in draws the timecode" "timecode="
echo "$bg" | seen "and the file's own name"      "text='b.mp4'"
$BIN timeline export "$dp" --out "$TMP/b.mp4" --burn nonesuch 2>&1 \
    | seen "an unknown burn-in is refused" "timecode, name, both or off"
# ⚠ The timecode starts at the RANGE. The trim resets timestamps, so a burn-in
# reading 00:00:00 on a render that starts nine minutes in is a wrong answer to
# the exact question it was added to answer.
$BIN timeline range "$dp" --at 2 --to 5 >/dev/null
$BIN timeline export "$dp" --out "$TMP/b.mp4" --burn timecode --print \
    | seen "and it starts at the range, not at zero" "timecode='00\:00\:02\:00'"
$BIN timeline range "$dp" --off >/dev/null

# ---- image sequences -----------------------------------------------------
$BIN timeline formats | seen "a PNG sequence is a delivery format" "png"
if have ffmpeg; then
    rm -rf "$TMP/seq"; mkdir -p "$TMP/seq"
    $BIN timeline export "$dp" --out "$TMP/seq/f_%04d.png" --format png >/dev/null 2>&1
    check "and writes one frame per file" "150" "$(ls "$TMP/seq" | wc -l)"
    check "starting at the first one" "yes" \
          "$([ -s "$TMP/seq/f_0001.png" ] && echo yes || echo no)"
fi
# ⚠ A format with no audio codec has nowhere to put the sound. Mapping the mix
# into a PNG sequence fails the whole render with "Could not find tag for
# codec", after the graph is built and the encode has started.
$BIN timeline export "$dp" --out "$TMP/seq/f_%04d.png" --format png --print \
    | notseen "and never maps a sound track into it" "-c:a"

# ---- the queue -----------------------------------------------------------
#
# A file of commands, not a daemon: running the queue is running those
# commands, so a job somebody typed and a job the window queued are the same
# object and there is no second code path that renders things.
export SYNSTUDIO_QUEUE=$TMP/queue.txt
rm -f "$SYNSTUDIO_QUEUE"
$BIN timeline queue "$dp" add --out "$TMP/q1.mp4" | seen "a job queues" "queued"
$BIN timeline queue "$dp" add --out "$TMP/q2.mp4" --preset youtube-720p >/dev/null
check "and the queue holds both" "2" \
      "$($BIN timeline queue "$dp" list | awk -F'\t' '/^jobs/{print $2}')"
$BIN timeline queue "$dp" list | seen "with the arguments it was given" "--preset youtube-720p"
if have ffmpeg; then
    $BIN timeline queue "$dp" run >/dev/null 2>&1
    check "running it renders every job" "yes" \
          "$([ -s "$TMP/q1.mp4" ] && [ -s "$TMP/q2.mp4" ] && echo yes || echo no)"
    # ⚠ The queue is KEPT after a run. A job that failed is a job to look at,
    # and a queue that empties itself takes the evidence with it.
    check "and the queue is still there afterwards" "2" \
          "$($BIN timeline queue "$dp" list | awk -F'\t' '/^jobs/{print $2}')"
fi
$BIN timeline queue "$dp" clear >/dev/null
check "clear empties it" "0" \
      "$($BIN timeline queue "$dp" list | awk -F'\t' '/^jobs/{print $2}')"
unset SYNSTUDIO_QUEUE

# ------------------------------------------------------------- retime ----
#
# Speed stopped being one number in 0.1.0-19. A clip can run backwards, hold
# one frame, or ramp — and a ramp moves the TIMEBASE rather than a number
# inside it, so the clip's length, the frame the monitor seeks to, the setpts
# expression and the tempo the sound runs at all have to come from the same
# arithmetic. They come from ss_clip_retime, which samples the curve once.

if have ffmpeg && have ffprobe; then
    rtc=$TMP/rt.mp4
    ffmpeg -v error -y -f lavfi -i "testsrc2=s=320x180:d=4:r=25" \
           -f lavfi -i "sine=f=440:d=4" -shortest \
           -c:v libx264 -pix_fmt yuv420p -c:a aac "$rtc" 2>/dev/null

    mkrt() {   # mkrt <project>
        rm -f "$1"
        $BIN timeline new "$1" --size 320x180 --fps 25 >/dev/null
        $BIN timeline track "$1" video V >/dev/null
        $BIN timeline clip "$1" 0 "$rtc" --at 0 --dur 4 >/dev/null
    }
    # ⚠ IDENTICAL pictures give psnr `inf`, and `$2+0` on that is ZERO — a
    # perfect match reads as the worst possible one, and the assertion that
    # two frames are the same fails precisely when they are.
    #
    # ⚠ And the test for it is on the FIRST TOKEN, not the field. The line is
    # `psnr_avg:4.86 psnr_r:3.12 psnr_g:inf psnr_b:3.08` — one matching
    # channel puts `inf` in the middle of a field whose number is 4.86, so
    # `$2 ~ /inf/` calls two completely different pictures identical.
    psnr2() {  # psnr2 <a> <b>
        ffmpeg -v error -i "$1" -i "$2" -lavfi psnr=stats_file=- -f null - 2>&1 \
            | awk -F'psnr_avg:' '/psnr_avg/{split($2,a," "); v=(a[1]=="inf")?999:a[1]+0; print v}' \
            | tail -1
    }
    nthframe() {  # nthframe <file> <n> <out.png>
        ffmpeg -v error -y -i "$1" -vf "select=eq(n\,$2)" \
               -fps_mode passthrough -frames:v 1 "$3"
    }

    # ---- the one that failed the whole export ---------------------------
    #
    # ⚠ atempo's range is 0.5..100 and the speed property's is 0.1..10, so
    # EVERY clip slower than half speed with a sound track on it died at the
    # end of the render: "Value 0.200000 for parameter 'tempo' out of range".
    # Halvings multiply, so a chain of them reaches any slowdown.
    slow=$TMP/slow.sstl
    mkrt "$slow"
    $BIN timeline set "$slow" 0 0 speed=0.2 >/dev/null
    $BIN timeline export "$slow" --out "$TMP/slow.mp4" --print \
        | seen "a fifth-speed clip chains atempo" "atempo=0.5"
    $BIN timeline export "$slow" --out "$TMP/slow.mp4" >/dev/null 2>&1
    check "and a fifth-speed clip EXPORTS" "yes" \
          "$([ -s "$TMP/slow.mp4" ] && echo yes || echo no)"
    d=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$TMP/slow.mp4")
    check "five times as long as the source" "yes" \
          "$(awk -v d="${d:-0}" 'BEGIN{print (d>19.5 && d<20.5)?"yes":"no"}')"

    # ---- backwards -------------------------------------------------------
    rev=$TMP/rev.sstl
    mkrt "$rev"
    $BIN timeline set "$rev" 0 0 reverse=1 >/dev/null
    rg=$($BIN timeline export "$rev" --out "$TMP/rev.mp4" --print)
    echo "$rg" | seen "the picture runs backwards" "[0:v]reverse,"
    echo "$rg" | seen "and so does the sound"      "areverse"
    $BIN timeline export "$rev" --out "$TMP/rev.mp4" >/dev/null 2>&1
    nthframe "$rtc" 99 "$TMP/src_last.png"
    nthframe "$rtc" 0  "$TMP/src_first.png"
    nthframe "$TMP/rev.mp4" 0 "$TMP/rev_first.png"
    # The DISCRIMINATING pair: it has to match the source's last frame AND not
    # its first. A reverse that silently did nothing would pass the second
    # assertion on its own.
    check "it starts on the source's LAST frame" "yes" \
          "$(awk -v p="$(psnr2 "$TMP/src_last.png" "$TMP/rev_first.png")" \
                 'BEGIN{print (p>30)?"yes":"no"}')"
    check "and not on its first"                "yes" \
          "$(awk -v p="$(psnr2 "$TMP/src_first.png" "$TMP/rev_first.png")" \
                 'BEGIN{print (p<25)?"yes":"no"}')"

    # ---- a held frame ----------------------------------------------------
    frz=$TMP/frz.sstl
    mkrt "$frz"
    $BIN timeline set "$frz" 0 0 freeze=1.5 >/dev/null
    $BIN timeline export "$frz" --out "$TMP/frz.mp4" --print \
        | seen "a freeze holds one frame" "loop=loop="
    $BIN timeline export "$frz" --out "$TMP/frz.mp4" >/dev/null 2>&1
    nthframe "$TMP/frz.mp4" 10 "$TMP/frz_a.png"
    nthframe "$TMP/frz.mp4" 80 "$TMP/frz_b.png"
    check "and every frame of it is the same one" "yes" \
          "$(awk -v p="$(psnr2 "$TMP/frz_a.png" "$TMP/frz_b.png")" \
                 'BEGIN{print (p>40)?"yes":"no"}')"
    # ⚠ It has to be the frame ASKED FOR. The export opens the source at the
    # clip's in point for everything else, and a freeze that inherited that
    # held the wrong picture — convincingly, which is why this is measured
    # against the source rather than looked at.
    nthframe "$rtc" 38 "$TMP/src38.png"
    nthframe "$rtc" 0  "$TMP/src00.png"
    check "the frame it holds is the one asked for" "yes" \
          "$(awk -v p="$(psnr2 "$TMP/src38.png" "$TMP/frz_a.png")" \
                 'BEGIN{print (p>30)?"yes":"no"}')"
    check "and not the clip's in point"             "yes" \
          "$(awk -v p="$(psnr2 "$TMP/src00.png" "$TMP/frz_a.png")" \
                 'BEGIN{print (p<25)?"yes":"no"}')"

    # ---- a ramp ----------------------------------------------------------
    #
    # Keys on `speed`, and the only keyed property whose axis is SOURCE
    # seconds: a ramp says "at this point in the shot", and the output length
    # is then the integral of 1/speed over the source rather than an equation
    # to be solved. A linear 1x -> 2x over four source seconds is 4*ln(2).
    ramp=$TMP/ramp.sstl
    mkrt "$ramp"
    $BIN timeline anim "$ramp" 0 0 add speed --at 0 --value 1 >/dev/null
    $BIN timeline anim "$ramp" 0 0 add speed --at 4 --value 2 >/dev/null
    rl=$($BIN timeline get "$ramp" 0 0 | awk -F'\t' '/^length/{print $2}')
    check "a ramp's length is the integral of 1/speed" "yes" \
          "$(awk -v l="${rl:-0}" 'BEGIN{w=4*log(2); print (l>w-0.01 && l<w+0.01)?"yes":"no"}')"
    rgz=$($BIN timeline export "$ramp" --out "$TMP/ramp.mp4" --print)
    echo "$rgz" | seen "and the export is a piecewise setpts" "setpts='if(lt(T,"
    # The sound follows it through ONE atempo stepped by sendcmd — and the
    # commands are timed on the SOURCE axis, because asendcmd sits before the
    # atempo and the frames it is timing have not been stretched yet.
    echo "$rgz" | seen "the sound follows the ramp"          "asendcmd"
    $BIN timeline export "$ramp" --out "$TMP/ramp.mp4" >/dev/null 2>&1
    va=$(ffprobe -v error -select_streams v -show_entries stream=duration -of csv=p=0 "$TMP/ramp.mp4")
    aa=$(ffprobe -v error -select_streams a -show_entries stream=duration -of csv=p=0 "$TMP/ramp.mp4")
    check "and comes out the same length as the picture" "yes" \
          "$(awk -v v="${va:-0}" -v a="${aa:-9}" \
                 'BEGIN{d=v-a; if(d<0)d=-d; print (d<0.1)?"yes":"no"}')"

    # A ramp that leaves atempo's range cannot be pitched by one filter and a
    # chain of them cannot be commanded as a unit, so it says so and drops the
    # sound rather than shipping a graph that fails or a sync that drifts.
    ramp2=$TMP/ramp2.sstl
    mkrt "$ramp2"
    $BIN timeline anim "$ramp2" 0 0 add speed --at 0 --value 1 >/dev/null
    $BIN timeline anim "$ramp2" 0 0 add speed --at 4 --value 0.25 >/dev/null
    $BIN timeline export "$ramp2" --out "$TMP/ramp2.mp4" --print 2>&1 >/dev/null \
        | seen "a ramp out of atempo's range says so" "sound is dropped"

    # ---- the frames that were never shot ---------------------------------
    for m in blend flow; do
        $BIN timeline set "$slow" 0 0 "retime=$m" >/dev/null
        $BIN timeline export "$slow" --out "$TMP/x.mp4" --print \
            | seen "retime=$m interpolates" "minterpolate=fps=25:mi_mode="
    done
    # ⚠ Only where the timebase actually moved. At 1x every output frame IS an
    # input frame, and minterpolate would spend minutes rebuilding what it was
    # handed.
    $BIN timeline set "$slow" 0 0 speed=1 retime=flow >/dev/null
    $BIN timeline export "$slow" --out "$TMP/x.mp4" --print \
        | notseen "and does nothing at 1x" "minterpolate"

    # ---- the monitor and the export agree about a retimed clip -----------
    #
    # This is the assertion that caught both of the bugs above. A one-frame
    # disagreement measures around 19 dB here and the same frame through two
    # encoders measures around 39, so the two are not close to each other.
    for pj in rev frz ramp; do
        $BIN timeline frame "$TMP/$pj.sstl" --at 1.0 --out "$TMP/mon_$pj.png" \
             >/dev/null 2>&1
        nthframe "$TMP/$pj.mp4" 25 "$TMP/exp_$pj.png"
        check "the monitor draws what the export writes ($pj)" "yes" \
              "$(awk -v p="$(psnr2 "$TMP/mon_$pj.png" "$TMP/exp_$pj.png")" \
                     'BEGIN{print (p>30)?"yes":"no"}')"
    done
fi

# -------------------------------------------------------- stabiliser -----
#
# Two passes, and the first is not part of any graph: vidstabdetect watches
# the clip and writes a .trf beside the project, and only then can
# vidstabtransform be put in a graph that reads it.

if have ffmpeg && have ffprobe && \
   ffmpeg -hide_banner -filters 2>/dev/null > "$TMP/.filters" && \
   grep -q vidstabdetect "$TMP/.filters"; then
    shaky=$TMP/shaky.mp4
    ffmpeg -v error -y -f lavfi -i "testsrc2=s=320x180:d=3:r=25" \
           -vf "crop=280:160:20+10*sin(t*9):20+8*cos(t*11)" \
           -c:v libx264 -pix_fmt yuv420p "$shaky" 2>/dev/null

    stp=$TMP/stab.sstl
    rm -rf "$stp" "$stp.stab"
    $BIN timeline new "$stp" --size 280x160 --fps 25 >/dev/null
    $BIN timeline track "$stp" video V >/dev/null
    $BIN timeline clip "$stp" 0 "$shaky" --at 0 --dur 3 >/dev/null

    $BIN timeline stabilise "$stp" 0 0 --dur 15 --size 3 2>/dev/null \
        | seen "the analysis runs" "stab	on"
    check "and leaves a .trf beside the project" "yes" \
          "$([ -s "$stp.stab/stab_0_0.trf" ] && echo yes || echo no)"
    $BIN timeline export "$stp" --out "$TMP/stab.mp4" --print \
        | seen "the graph reads it" "vidstabtransform=input="

    # It has to actually hold the picture still. Frame-to-frame difference is
    # what shake IS, so it is what gets measured — 7.08 before, 4.13 after.
    $BIN timeline export "$stp" --out "$TMP/stab.mp4" >/dev/null 2>&1
    shakeof() {
        ffmpeg -v error -i "$1" \
            -vf "tblend=all_mode=difference,signalstats,metadata=print:key=lavfi.signalstats.YAVG:file=-" \
            -f null - 2>&1 | awk -F'=' '/YAVG/{s+=$2; n++} END{if(n)printf "%.4f", s/n; else print 0}'
    }
    check "and the shot is measurably steadier" "yes" \
          "$(awk -v a="$(shakeof "$shaky")" -v b="$(shakeof "$TMP/stab.mp4")" \
                 'BEGIN{print (a>0 && b < a*0.8)?"yes":"no"}')"

    # ⚠ --off keeps the measurement. It took as long as a render to make, and
    # turning the effect off is not the same as throwing it away.
    $BIN timeline stabilise "$stp" 0 0 --off | seen "it can be turned off" "stab	off"
    check "without losing the analysis" "yes" \
          "$([ -s "$stp.stab/stab_0_0.trf" ] && echo yes || echo no)"
    $BIN timeline export "$stp" --out "$TMP/stab.mp4" --print \
        | notseen "and the graph stops reading it" "vidstabtransform"
fi

# ----------------------------------------------------------- titles ------
#
# A title stopped being "some words in the middle" in 0.1.0-18: it has a face,
# a weight, an outline, a shadow, a plate, line spacing and — for the end of
# the film — a climb. Everything here is a fraction of the FONT SIZE rather
# than a pixel count, so a title styled on a 1080 timeline is the same title
# when the project is delivered at 4K.

ttp=$TMP/titles.sstl
$BIN timeline new "$ttp" --size 1920x1080 --fps 25 >/dev/null
$BIN timeline track "$ttp" video BG >/dev/null
$BIN timeline track "$ttp" video TITLES >/dev/null
$BIN timeline solid "$ttp" 0 --at 0 --dur 12 --colour 0.1,0.1,0.12 >/dev/null

check "five styles ship" "5" "$($BIN timeline styles | grep -c .)"

$BIN timeline title "$ttp" 1 'Sarah Okonkwo\nDirector of Photography' \
     --at 1 --dur 4 --style lower-third >/dev/null
check "a style sets the weight"    "bold"        "$($BIN timeline get "$ttp" 1 0 | awk -F'\t' '/^text.weight/{print $2}')"
check "a style sets the placement" "bottomleft"  "$($BIN timeline get "$ttp" 1 0 | awk -F'\t' '/^text.pos/{print $2}')"
check "a style sets the plate"     "0.55"        "$($BIN timeline get "$ttp" 1 0 | awk -F'\t' '/^text.box/{print $2}')"
# A lower third leans on the plate instead of an outline, so this is a style
# turning something OFF — which is the half a preset usually gets wrong.
check "and turns the outline off"  "0"           "$($BIN timeline get "$ttp" 1 0 | awk -F'\t' '/^text.border/{print $2}')"
$BIN timeline title "$ttp" 1 "x" --at 6 --dur 1 --style nonesuch 2>&1 \
    | seen "an unknown style is refused" "no style called"

# ---- a caption with a LINE BREAK in it ---------------------------------
#
# The project file is tab-separated with one record per line, so a real
# newline inside a caption would end the record halfway through and the rest
# of the clip would read as a fresh one. It travels as \n and comes back as a
# newline, and the same two bytes are what a shell can type.
$BIN timeline get "$ttp" 1 0 | seen "a caption comes back escaped" 'Okonkwo\nDirector'
check "and never as a bare newline" "1" \
      "$($BIN timeline get "$ttp" 1 0 | awk -F'\t' '/^text\t/{n++} END{print n+0}')"
$BIN timeline show "$ttp" | rxseen "the record stays one line" '^text	.*Okonkwo\\nDirector'
# The escape survives a save and a reload, which is the assertion that would
# have caught it being written raw and read back as two clips.
check "it survives a round trip" "1" \
      "$($BIN timeline show "$ttp" | grep -c 'Okonkwo\\nDirector')"

# ---- what reaches drawtext ---------------------------------------------
tstyle=$($BIN timeline export "$ttp" --out "$TMP/titles.mp4" --print)
echo "$tstyle" | seen "the plate reaches the graph"  "box=1:boxcolor="
echo "$tstyle" | seen "the shadow reaches the graph" "shadowx="
# A style that says no outline has to emit NO outline, not a thin one: the
# old code had a one-pixel floor baked into the arithmetic.
# ⚠ `:borderw=` and not `borderw=`: the plate's own `boxborderw=` contains
# the shorter string, so the loose needle passes on the graph that proves the
# bug — it was there, on the plate, the whole time.
echo "$tstyle" | notseen "and an outline that was turned off does not" ":borderw="
# The line break has to reach the PICTURE, not just the file. Two identical
# titles, one of them broken across two lines: the two-line frame carries more
# lit pixels than the one-line frame, and nothing else about them differs.
#
# Measured rather than eyeballed, and measured on the frame the monitor draws
# — which is the same graph the export uses.
if have ffmpeg; then
    lp=$TMP/lines.sstl
    $BIN timeline new "$lp" --size 640x360 --fps 25 >/dev/null
    $BIN timeline track "$lp" video V >/dev/null
    $BIN timeline solid "$lp" 0 --at 0 --dur 2 --colour 0,0,0 >/dev/null
    $BIN timeline track "$lp" video T >/dev/null
    $BIN timeline title "$lp" 1 'ALPHA BRAVO' --at 0 --dur 2 \
         --style heading >/dev/null
    $BIN timeline frame "$lp" --at 1 --out "$TMP/one.png" >/dev/null 2>&1
    $BIN timeline set "$lp" 1 0 'text=ALPHA\nBRAVO' >/dev/null
    $BIN timeline frame "$lp" --at 1 --out "$TMP/two.png" >/dev/null 2>&1
    # ⚠ NOT the whole frame's average: the same words drawn on two lines carry
    # very nearly the same amount of ink as on one (17.0603 against 17.061 —
    # the first version of this test could not tell them apart). What changes
    # is WHERE the ink is, so the measurement is a band the one-line caption
    # cannot reach: a centred block grows UPWARD, and the top of the frame is
    # background exactly until the caption has a second line.
    #
    # It also fails on the failure that matters most — an escape that never
    # became a newline draws the two characters on ONE line, which leaves this
    # band as empty as the single-line version.
    topavg() { ffmpeg -v error -i "$1" \
               -vf "crop=iw:ih*0.4:0:0,signalstats,metadata=print:key=lavfi.signalstats.YAVG:file=-" \
               -f null - 2>&1 | awk -F'=' '/YAVG/{print $2}' | tail -1; }
    y1=$(topavg "$TMP/one.png"); y2=$(topavg "$TMP/two.png")
    check "a line break reaches the picture" "yes" \
          "$(awk -v a="${y1:-0}" -v b="${y2:-0}" 'BEGIN{print (b>a+1)?"yes":"no"}')"
fi

# ---- a project written BEFORE any of this existed -----------------------
#
# No `style` line at all. It has to read back as the title it was rendered as
# — the old outline and the old line spacing — which is why those numbers are
# the DEFAULTS and not a sentinel.
old=$TMP/old.sstl
grep -v '^style' "$ttp" > "$old"
check "an old project keeps its outline"      "0.045" \
      "$($BIN timeline get "$old" 1 0 | awk -F'\t' '/^text.border/{print $2}')"
check "an old project keeps its line spacing" "0.25" \
      "$($BIN timeline get "$old" 1 0 | awk -F'\t' '/^text.line/{print $2}')"

# ---- a font is a FILE, resolved from a family --------------------------
#
# `font=Sans` only works in an ffmpeg built against fontconfig and fails the
# graph when it is not, so a family is resolved to a path here and drawtext is
# only ever handed one that opens.
echo "$tstyle" | seen "drawtext is given a font FILE" "fontfile='/"
if have fc-match; then
    # Bold with no family named still has to mean something: the generic sans
    # in bold, and not the regular face with the tick quietly ignored.
    #
    # ⚠ Asserted as "a DIFFERENT file from the regular face", not as "a path
    # with Bold in it". A machine with no bold face installed is a legitimate
    # machine, and a test that fails there fails the BUILD there — which this
    # suite has already done once, to a ThinkPad, for a reason just as
    # environmental. Where the two resolve the same, there is nothing to
    # assert and it says so.
    fontof() { $BIN timeline export "$1" --out "$TMP/t2.mp4" --print \
               | tr ' ' '\n' | grep -o "fontfile='[^']*'" | head -1; }
    bold=$(fontof "$ttp")
    $BIN timeline set "$ttp" 1 0 text.weight=regular >/dev/null
    plain=$(fontof "$ttp")
    $BIN timeline set "$ttp" 1 0 text.weight=bold >/dev/null
    if [ -n "$bold" ] && [ -n "$plain" ] && [ "$bold" != "$plain" ]; then
        ok
    else
        printf '  skip  no separate bold face here (%s), nothing asserted\n' \
               "${bold:-none}"
    fi
    $BIN fonts have "Definitely Not A Font 91" \
        | seen "a font this machine has not got says so" "no"
fi

# ---- the credit roll ----------------------------------------------------
#
# The one title property that MOVES, and therefore the one that has to be
# generated twice: an expression in the export, where a clip's own `t` is
# running, and a NUMBER in the monitor, which holds a single frame at t=0 and
# would otherwise draw every roll at its starting position while the export
# scrolled it.
rp=$TMP/roll.sstl
$BIN timeline new "$rp" --size 1280x720 --fps 25 >/dev/null
$BIN timeline track "$rp" video BG >/dev/null
$BIN timeline track "$rp" video ROLL >/dev/null
$BIN timeline solid "$rp" 0 --at 0 --dur 12 --colour 0,0,0 >/dev/null
$BIN timeline title "$rp" 1 'Directed by\nAmara Osei' --at 0 --dur 12 \
     --style credit-roll >/dev/null
$BIN timeline export "$rp" --out "$TMP/roll.mp4" --print \
    | rxseen "the export scrolls with t" 'y=\(h-\(t\*h\*0\.100000\)\)'
$BIN timeline frame "$rp" --at 4 --out "$TMP/roll.png" --size 320 >/dev/null 2>&1
check "the monitor draws a moving title where it IS" "yes" \
      "$([ -s "$TMP/roll.png" ] && echo yes || echo no)"

# ------------------------------------------------------------ subtitles --
#
# A cue is a title clip. Not a fourth clip kind and not a track type of its
# own — which is what makes an imported caption editable with the commands
# that already exist, and burning it in free.

srt=$TMP/d.srt
printf '1\r\n00:00:01,000 --> 00:00:03,500\r\n<i>Are you</i> coming?\r\n\r\n' > "$srt"
# A full stop instead of a comma, which half the tools in the world write.
printf '2\r\n00:00:04.000 --> 00:00:06,250\r\nNot until it stops raining.\r\nIt has been raining for a week.\r\n\r\n' >> "$srt"
# A cue NUMBER out of sequence, because the numbers are not what separates one
# cue from the next and files in the wild get them wrong.
printf '17\r\n00:00:07,000 --> 00:00:09,000\r\nThen we wait.\r\n\r\n' >> "$srt"

sp=$TMP/subs.sstl
$BIN timeline new "$sp" --size 1920x1080 --fps 25 >/dev/null
$BIN timeline track "$sp" video BG >/dev/null
$BIN timeline track "$sp" video SUBS >/dev/null
$BIN timeline solid "$sp" 0 --at 0 --dur 10 --colour 0.1,0.1,0.12 >/dev/null
$BIN timeline subs "$sp" 1 import "$srt" | seen "three cues import" "cues	3"

check "a cue lands at its own time"   "4.000000" \
      "$($BIN timeline get "$sp" 1 1 | awk -F'\t' '/^tl_in/{print $2}')"
check "and lasts exactly as long"     "2.250000" \
      "$($BIN timeline get "$sp" 1 1 | awk -F'\t' '/^length/{print $2}')"
$BIN timeline get "$sp" 1 0 | seen "markup is dropped, words are kept" "Are you coming?"
$BIN timeline get "$sp" 1 1 | seen "a two-line cue stays two lines" 'raining.\nIt has'
check "an imported cue is styled to be read" "0.5" \
      "$($BIN timeline get "$sp" 1 1 | awk -F'\t' '/^text.box/{print $2}')"

# Back out again, and the file that comes out has to be the file that went in.
$BIN timeline subs "$sp" 1 export "$TMP/back.srt" | seen "and they write back out" "cues	3"
check "the times survive the round trip" "00:00:04,000 --> 00:00:06,250" \
      "$(sed -n '6p' "$TMP/back.srt")"
check "and so do the line breaks" "It has been raining for a week." \
      "$(sed -n '8p' "$TMP/back.srt")"

# ---- shipping them as a STREAM instead ---------------------------------
#
# A soft stream never touches the picture, so it never touches the filter
# graph. Its input goes in LAST: every label in the graph names an input by
# number, and a file inserted anywhere else would renumber the clips and hand
# the timeline the wrong pictures.
sg=$($BIN timeline export "$sp" --out "$TMP/soft.mp4" --subs "$srt" --print)
echo "$sg" | seen "a soft stream is mapped"      ":s:0"
echo "$sg" | seen "in the codec the mp4 takes"   "mov_text"
$BIN timeline export "$sp" --out "$TMP/soft.mkv" --subs "$srt" --print \
    | seen "and the one Matroska takes"          "srt"
$BIN timeline export "$sp" --out "$TMP/soft.webm" --subs "$srt" --print \
    | seen "and the one WebM takes"              "webvtt"
# The clip inputs must keep the numbers they had. The subtitle is the LAST
# input, so its index is the number of clip inputs — two here, one solid and
# one per cue is three more, so the check is that the map names an index at
# least as high as any clip label in the graph.
echo "$sg" | rxseen "the subtitle input is the last one" '^[0-9]+:s:0$'

$BIN timeline export "$sp" --out "$TMP/soft.mp4" --subs "$TMP/nope.srt" 2>&1 \
    | seen "a missing subtitle file is caught BEFORE the render" "cannot read"

if have ffprobe; then
    $BIN timeline export "$sp" --out "$TMP/soft.mp4" --subs "$srt" >/dev/null 2>&1
    check "the delivered file really carries one" "subtitle" \
          "$(ffprobe -v error -select_streams s -show_entries stream=codec_type \
                     -of csv=p=0 "$TMP/soft.mp4" | head -1)"
fi

#
# ⚠ AND IT REPORTS A QML ERROR, NOT A SLOW MACHINE.
#
# The first version asserted that "Configuration Loaded" appears, which made
# the BUILD fail on a ThinkPad: quickshell had not finished starting inside
# the timeout — on a machine that was compiling this package at the same time
# — and printed no error of any kind. A shell that never got far enough to
# say anything is not evidence of a broken window, and a package that will
# not build on a slower laptop is a real cost paid for no information.
#
# So the FAILURE condition is a QML error in the log. Loading is what makes
# one appear, and where the load never happened this says so and asserts
# nothing — the same bargain the `have quickshell` guard above it strikes,
# one step further in.
qml=$(dirname "$0")/../data/synstudio.qml
if have quickshell && [ -n "${XDG_RUNTIME_DIR:-}" ] && [ -f "$qml" ]; then
    rc=0
    # ⚠ POLLED, not waited out. A shell that loads SUCCESSFULLY runs forever,
    # so `timeout N quickshell` always costs the full N — raising that from 25
    # to 60 seconds to help a slow machine added 35 seconds of pure waiting to
    # every build on every machine, which is most of what the suite spends its
    # time on. Watching the log costs nothing and stops the moment the answer
    # is known: about eight seconds on a desktop.
    : > "$TMP/qml.log"
    # ⚠ DISABLE_MANGOHUD=1 AND MANGOHUD=0, exactly as the launcher sets them,
    # and NOT because this test cares about an FPS counter.
    #
    # The session exports MANGOHUD=1, which loads MangoHud's Vulkan layer into
    # every Vulkan client — and a QML MediaPlayer constructs a QMediaPlayer,
    # whose ffmpeg backend calls av_hwdevice_ctx_create on construction. On
    # AMD that segfaults inside MangoHud's own vkCreateDevice hook and takes
    # quickshell with it. NVIDIA never reproduces it, which is why this test
    # passed on the development desktop and CRASHED on velle's ThinkPad,
    # failing the build there and nowhere else.
    #
    # That is the exact bug `synstudio gui` exists to prevent, and there is a
    # separate test above asserting the launcher sets these. This one loads
    # quickshell DIRECTLY to test the QML FILE, so it has to supply the same
    # environment the launcher would — otherwise it is not testing the window,
    # it is re-running a fixed bug on whichever machines still reproduce it.
    # See [[reference_mangohud_layer_crashes_vulkan_clients]].
    DISABLE_MANGOHUD=1 MANGOHUD=0 QT_QPA_PLATFORM=offscreen \
        quickshell -p "$qml" > "$TMP/qml.log" 2>&1 &
    qpid=$!
    for _ in $(seq 1 120); do
        grep -qE "Configuration Loaded|ERROR" "$TMP/qml.log" 2>/dev/null && break
        kill -0 "$qpid" 2>/dev/null || break      # it exited on its own
        sleep 0.5
    done
    kill "$qpid" 2>/dev/null
    wait "$qpid" 2>/dev/null || rc=$?
    # ⚠ `ERROR`, not `Error:`. quickshell prints "ERROR: Failed to load
    # configuration" and then the reason; the needle this test shipped with
    # matched neither, so the assertion that was supposed to catch a broken
    # window could never have fired. Verified against a deliberately broken
    # file, which is the only way to find out what a tool says when it fails.
    if grep -q "ERROR" "$TMP/qml.log"; then
        bad "the window file loads: $(grep -m1 'ERROR' "$TMP/qml.log" |
                                      sed 's/\x1b\[[0-9;]*m//g' | cut -c1-100)"
    elif grep -q "Configuration Loaded" "$TMP/qml.log"; then
        ok
    else
        # It never finished starting. Not an assertion either way: a shell
        # that got no further than opening its log file has told us nothing
        # about the QML, and failing here fails the BUILD on any machine
        # slower than the one this was written on — which it did, on a
        # ThinkPad compiling this same package at the time.
        printf '  skip  the window did not start inside 60s (rc %s), nothing asserted\n' "$rc"
    fi
fi

pass=$(grep -c '^p' "$RESULTS")
fail=$(grep -c '^f' "$RESULTS")

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
