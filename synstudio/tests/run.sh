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
check "key count"   "64"     "$($BIN keys | wc -l)"
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
    $BIN timeline set "$tp" 0 1 trans=dissolve trans.dur=0.5
    $BIN timeline export "$tp" --out "$TMP/t.mp4" --print \
        | seen "a dissolve is an alpha ramp" "alpha=1"
    $BIN timeline set "$tp" 0 1 trans=wipeleft
    $BIN timeline export "$tp" --out "$TMP/t.mp4" --print \
        | seen "a wipe is an alpha expression" "geq="
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

pass=$(grep -c '^p' "$RESULTS")
fail=$(grep -c '^f' "$RESULTS")

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
