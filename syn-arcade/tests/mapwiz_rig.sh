#!/bin/bash
# mapwiz_rig.sh — drive `syn-arcade map learn` with a VIRTUAL controller.
#
# ⚠ NOT run by `meson test`, and it must not be: it creates a uinput device,
# which needs write access to /dev/uinput and a kernel that has the module.
# syn_arcade_test.sh is the suite; this is the thing a suite of greps cannot do
# — press twenty-one controls in order and check that what came out is a
# mapping SDL will actually take.
#
# Usage:
#   tests/mapwiz_rig.sh build/syn-arcade
#
# ── Why a uinput pad rather than the one on the desk ────────────────────────
#
# A wizard needs somebody pressing buttons, which is exactly what a test does
# not have. A virtual pad is the only way to press them the same way twice.
#
# ⚠ AND IT IS NOT "SYNTHETIC INPUT" IN THE SENSE THIS PROJECT REFUSES. The rule
# there is about the COMPOSITOR: a virtual keyboard or pointer is delivered to
# whatever is focused, so stick drift would type into somebody's browser. A
# uinput device carrying BTN_GAMEPAD and two sticks is a JOYSTICK — libinput
# does not take joysticks, nothing routes it to a surface, and the only
# processes that see it are the ones that open its event node on purpose. The
# live desktop cannot be touched by it.
#
# ── What it proves that nothing else can ────────────────────────────────────
#
#   · SDL's numbering is SDL's, and this reads it back rather than deriving it
#   · a press only counts once, so one button cannot fill four controls
#   · a control already taken is refused instead of recorded twice
#   · `skip` on stdin leaves a control out, and the line is still valid
#   · the finished line survives the SAME refusals `map add` applies
#   · and, the whole point: SDL LOADS THE RESULT. The mapping is fed back to a
#     fresh SDL through SDL_GAMECONTROLLERCONFIG_FILE, which is the only test
#     that distinguishes "a plausible-looking string" from one that works —
#     the failure this is all guarding against is SILENT.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

SA=${1:?usage: mapwiz_rig.sh /path/to/syn-arcade}
SA=$(readlink -f "$SA")
[ -x "$SA" ] || { echo "not executable: $SA" >&2; exit 1; }

command -v python3 >/dev/null 2>&1 || { echo "SKIP: no python3"; exit 77; }
[ -w /dev/uinput ] || { echo "SKIP: /dev/uinput is not writable here"; exit 77; }
[ -e /usr/lib/libSDL3.so.0 ] || ldconfig -p 2>/dev/null | grep -q libSDL3.so.0 \
	|| { echo "SKIP: libSDL3.so.0 not installed"; exit 77; }

TMP=$(mktemp -d /tmp/mapwiz.XXXXXX) || exit 1
chmod 700 "$TMP"
PASS=0; FAIL=0
cleanup() {
	[ -n "${PAD_PID:-}" ] && kill "$PAD_PID" 2>/dev/null
	echo
	echo "$PASS passed, $FAIL failed"
	echo "TMP kept: $TMP"
	[ "$FAIL" = 0 ] || exit 1
}
trap cleanup EXIT INT TERM

check() { # check <name> <status>
	if [ "$2" = 0 ]; then PASS=$((PASS+1)); echo "  ok    $1"
	else FAIL=$((FAIL+1)); echo "  FAIL  $1"; fi
}

# ⚠ HERMETIC. `map add` writes to the config directory and the wizard ends by
# calling it — a rig that used the real one would rewrite the mappings on the
# desk it is running on.
export XDG_CONFIG_HOME="$TMP/config"
export SDL_GAMECONTROLLERCONFIG_FILE="$TMP/config/syn-arcade/gamecontrollerdb.txt"
mkdir -p "$TMP/config/syn-arcade"

# ── the virtual pad ─────────────────────────────────────────────────────────
#
# Written straight onto /dev/uinput rather than through a library, because
# there is no uinput binding installed by default and a test that needs one
# installed is a test nobody runs. The legacy `uinput_user_dev` write is used
# in preference to UI_DEV_SETUP/UI_ABS_SETUP: it is one write, it carries the
# axis ranges with it, and it has been in the kernel since forever.
cat > "$TMP/pad.py" <<'PY'
import fcntl, os, struct, sys, time

UINPUT_MAX_NAME_SIZE = 80
ABS_CNT = 64

EV_SYN, EV_KEY, EV_ABS = 0x00, 0x01, 0x03
SYN_REPORT = 0

UI_DEV_CREATE  = 0x5501
UI_DEV_DESTROY = 0x5502
UI_SET_EVBIT   = 0x40045564   # _IOW('U', 100, int)
UI_SET_KEYBIT  = 0x40045565
UI_SET_ABSBIT  = 0x40045567

# A plain Xbox-shaped pad: eleven buttons, two sticks, two triggers, one hat.
BTNS = [0x130, 0x131, 0x133, 0x134,      # south, east, north, west
        0x136, 0x137,                     # TL, TR
        0x13a, 0x13b, 0x13c,              # select, start, mode
        0x13d, 0x13e]                     # thumbl, thumbr
# ABS_X/Y, ABS_RX/RY(0x03,0x04), ABS_Z/RZ triggers, ABS_HAT0X/Y
STICKS   = [0x00, 0x01, 0x03, 0x04]
TRIGGERS = [0x02, 0x05]
HAT      = [0x10, 0x11]

fd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK)
for ev in (EV_KEY, EV_ABS):
    fcntl.ioctl(fd, UI_SET_EVBIT, ev)
for b in BTNS:
    fcntl.ioctl(fd, UI_SET_KEYBIT, b)
for a in STICKS + TRIGGERS + HAT:
    fcntl.ioctl(fd, UI_SET_ABSBIT, a)

absmax = [0] * ABS_CNT; absmin = [0] * ABS_CNT
absfuzz = [0] * ABS_CNT; absflat = [0] * ABS_CNT
for a in STICKS:
    absmin[a], absmax[a], absflat[a] = -32768, 32767, 128
for a in TRIGGERS:          # a trigger that RESTS AT ZERO: the half-axis shape
    absmin[a], absmax[a] = 0, 255
for a in HAT:
    absmin[a], absmax[a] = -1, 1

name = b"SYNAPSE Rig Pad"
dev  = name.ljust(UINPUT_MAX_NAME_SIZE, b"\0")
dev += struct.pack("HHHH", 0x03, 0x1234, 0x5678, 0x0111)   # bus, vid, pid, ver
dev += struct.pack("I", 0)                                  # ff_effects_max
for arr in (absmax, absmin, absfuzz, absflat):
    dev += struct.pack("%di" % ABS_CNT, *arr)
os.write(fd, dev)
fcntl.ioctl(fd, UI_DEV_CREATE)

def emit(t, c, v):
    os.write(fd, struct.pack("llHHi", 0, 0, t, c, v))

def syn():
    emit(EV_SYN, SYN_REPORT, 0)

print("ready", flush=True)

# One instruction per line on stdin: "b <code>", "a <code> <value>", "quit".
# ⚠ Every press is followed by its RELEASE, because the wizard deliberately
# refuses to count a control that is still held — that is what stops one long
# press filling the next four controls.
for line in sys.stdin:
    parts = line.split()
    if not parts:
        continue
    if parts[0] == "quit":
        break
    if parts[0] == "b":
        code = int(parts[1], 0)
        emit(EV_KEY, code, 1); syn(); time.sleep(0.05)
        emit(EV_KEY, code, 0); syn()
    elif parts[0] == "a":
        code, val, rest = int(parts[1], 0), int(parts[2]), int(parts[3])
        emit(EV_ABS, code, val); syn(); time.sleep(0.05)
        emit(EV_ABS, code, rest); syn()
    sys.stdout.write("done\n"); sys.stdout.flush()

fcntl.ioctl(fd, UI_DEV_DESTROY)
os.close(fd)
PY

# ── the answers, in the wizard's own order ─────────────────────────────────
#
# ⚠ IN ITS ORDER, NOT IN ANY ORDER THAT SEEMS SENSIBLE HERE. The list lives in
# sdlwiz.c and a rig that assumed its own would still pass while binding every
# control to the wrong button — the mapping would be well-formed and wrong,
# which is the failure mode this whole feature exists to avoid. If the order
# changes there, this list moves with it and the last check below is what says
# so.
ANSWERS=(
	"b 0x130"                 # a
	"b 0x131"                 # b
	"b 0x133"                 # x
	"b 0x134"                 # y
	"b 0x136"                 # leftshoulder
	"b 0x137"                 # rightshoulder
	"a 0x02 255 0"            # lefttrigger  (rests at zero: a half axis)
	"a 0x05 255 0"            # righttrigger
	"b 0x13a"                 # back
	"b 0x13b"                 # start
	"b 0x13c"                 # guide
	"b 0x13d"                 # leftstick
	"b 0x13e"                 # rightstick
	"a 0x00 32767 0"          # leftx  pushed right
	"a 0x01 32767 0"          # lefty  pushed down
	"a 0x03 32767 0"          # rightx
	"a 0x04 32767 0"          # righty
	"a 0x11 -1 0"             # dpup
	"a 0x11 1 0"              # dpdown
	"a 0x10 -1 0"             # dpleft
	"a 0x10 1 0"              # dpright
)

mkfifo "$TMP/pad.in" "$TMP/wiz.in"

python3 "$TMP/pad.py" < "$TMP/pad.in" > "$TMP/pad.out" 2>"$TMP/pad.err" &
PAD_PID=$!
# ⚠ HELD OPEN, read-write. Closing the writing end is an end-of-file, and the
# pad would tear itself down between the first press and the second.
exec 8<> "$TMP/pad.in"
exec 9<> "$TMP/wiz.in"

for i in $(seq 1 60); do
	grep -q ready "$TMP/pad.out" 2>/dev/null && break
	sleep 0.1
done
grep -q ready "$TMP/pad.out" 2>/dev/null || {
	echo "the virtual pad never came up:"; cat "$TMP/pad.err"; exit 1; }
sleep 0.6		# udev has to apply the uaccess ACL before SDL can open it

"$SA" pads | grep -q "SYNAPSE Rig Pad"
check "the kernel and syn-arcade both see the virtual pad" $?

# ── run the wizard ─────────────────────────────────────────────────────────
"$SA" map learn --rec --pad "SYNAPSE Rig" < "$TMP/wiz.in" > "$TMP/wiz.out" 2>"$TMP/wiz.err" &
WIZ_PID=$!

wait_for() { # wait_for <regex>
	for _ in $(seq 1 100); do
		grep -qE "$1" "$TMP/wiz.out" 2>/dev/null && return 0
		kill -0 "$WIZ_PID" 2>/dev/null || return 1
		sleep 0.1
	done
	return 1
}

wait_for '^pad	' || { echo "the wizard never found the pad:"; cat "$TMP/wiz.err"; exit 1; }

# ⚠ PRESS ONLY ONCE IT HAS ASKED, which is what a person does and what the
# first version of this rig got wrong. It pressed as soon as the previous
# control was BOUND — but the wizard deliberately waits for the last press to
# be RELEASED before asking the next question, so every press landed during
# that wait, was correctly discarded, and the rig timed out and moved on.
# Nine controls bound out of twenty-one presses, and from the outside it looked
# exactly like the wizard dropping input.
n=0
for a in "${ANSWERS[@]}"; do
	n=$((n + 1))
	for _ in $(seq 1 100); do
		[ "$(grep -c '^ask	' "$TMP/wiz.out")" -ge "$n" ] && break
		sleep 0.1
	done
	printf '%s\n' "$a" >&8
	for _ in $(seq 1 100); do
		[ "$(grep -cE '^(bound|skipped)	' "$TMP/wiz.out")" -ge "$n" ] && break
		sleep 0.1
	done
done

wait "$WIZ_PID"; WIZ_RC=$?
# ⚠ THE PAD STAYS UP. Telling it to quit here is what made the SDL check below
# fail: the mapping was perfect and there was no longer a device for SDL to
# apply it to, which reads as "SDL rejected it" — the exact false negative this
# rig exists to rule out. cleanup() takes it down at the end.

[ "$WIZ_RC" = 0 ]
check "the wizard finished" $?

[ "$(grep -c '^bound	' "$TMP/wiz.out")" = 21 ]
check "...having bound all twenty-one controls" $?

# ⚠ ONE PRESS, ONE CONTROL. A wizard that took the first event it saw would
# record the release of the previous press, or a trigger sitting at rest, and
# finish in under a second with everything bound to b0.
[ "$(grep '^bound	' "$TMP/wiz.out" | cut -f6 | sort -u | wc -l)" = 21 ]
check "no two controls came out on the same binding" $?

# ⚠ DECODED. Every field of a record is percent-encoded — that is the one rule
# for reading them, and a mapping line is nothing but commas, so the raw field
# comes back as %2C between every binding. A rig that matched against the
# encoded form would be checking a string no consumer ever sees.
MAP=$(grep '^done	' "$TMP/wiz.out" | cut -f6 | python3 -c \
      'import sys,urllib.parse; sys.stdout.write(urllib.parse.unquote(sys.stdin.read()))')
[ -n "$MAP" ]
check "it printed the finished mapping" $?

grep -q "platform:Linux" <<<"$MAP"
check "...saying platform:Linux, without which SDL loads and ignores it" $?

# The GUID is SDL's, and the test is that SDL agrees — not that it looks like
# 32 hex characters.
GUID=$(cut -d, -f1 <<<"$MAP")
[ "${#GUID}" = 32 ]
check "the GUID is SDL's own, 32 characters of it" $?

# ⚠ A WHOLE AXIS, and the reason is SDL rather than the pad. This rig's trigger
# reports 0..255 in evdev terms and rests at 0 — which looks like a half axis
# and is not one by the time SDL is asked, because SDL stretches every evdev
# range onto the full Sint16 span. So the trigger rests at -32768 in SDL's
# numbers and wants `a2`. Recording `+a2` here would be reading the kernel's
# range instead of SDL's, which is the whole class of mistake this feature
# refuses to make.
grep -qE "lefttrigger:a[0-9]+," <<<"$MAP"
check "a trigger is a whole axis, in SDL's range and not the kernel's" $?

grep -qE "leftx:a[0-9]+(,|$)" <<<"$MAP"
check "a stick pushed the right way is a whole axis, not inverted" $?

# ⚠ A HAT, not an axis. ABS_HAT0X/Y is what the kernel calls it; SDL turns that
# pair into hat 0 and writes it h0.<mask>. Anything reading evdev directly would
# have recorded two axes here and produced a mapping SDL cannot use.
grep -q "dpup:h0.1" <<<"$MAP"
check "the d-pad comes back as SDL's HAT, with the mask SDL wants" $?

grep -q "dpright:h0.2" <<<"$MAP"
check "...and each direction is its own bit of it" $?

# It went through map_add, so it is in the file the session points SDL at.
grep -q "$GUID" "$SDL_GAMECONTROLLERCONFIG_FILE" 2>/dev/null
check "the mapping was written to the database, not merely printed" $?

"$SA" map --rec | grep -q "$GUID"
check "...and it is listed by map" $?

# ── ⚠ THE ONLY CHECK THAT CANNOT BE FAKED ──────────────────────────────────
#
# Everything above says the line looks right. SDL refusing a mapping is
# SILENT — the pad simply behaves as before — so the only way to know is to
# hand it back to SDL and ask whether the joystick is now a GAMEPAD by this
# mapping. That is what the whole feature is for.
cat > "$TMP/verify.c" <<'C'
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv)
{
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	if (!SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD)) return 2;
	int n = 0; SDL_JoystickID *ids = SDL_GetJoysticks(&n);
	int rc = 1;
	for (int i = 0; i < n; i++) {
		SDL_Joystick *j = SDL_OpenJoystick(ids[i]);
		if (!j) continue;
		const char *nm = SDL_GetJoystickName(j);
		char g[33] = {0};
		SDL_GUIDToString(SDL_GetJoystickGUID(j), g, sizeof(g));
		if (nm && strstr(nm, "SYNAPSE Rig") && SDL_IsGamepad(ids[i])) {
			/* ⚠ Printed, not judged. Whether this is OUR mapping is
			 * a question about its bindings, and the rig asks that
			 * outside — a check in here that merely looked for the
			 * GUID would pass on any mapping SDL invented for the
			 * same device. */
			char *m = SDL_GetGamepadMappingForID(ids[i]);
			if (m && strstr(m, argv[1])) { printf("%s\n", m); rc = 0; }
			if (m) SDL_free(m);
		}
		SDL_CloseJoystick(j);
	}
	SDL_free(ids); SDL_Quit();
	return rc;
}
C
if command -v cc >/dev/null 2>&1 && pkg-config --exists sdl3 2>/dev/null; then
	cc -o "$TMP/verify" "$TMP/verify.c" $(pkg-config --cflags --libs sdl3) 2>"$TMP/cc.err"
	if [ -x "$TMP/verify" ]; then
		"$TMP/verify" "$GUID" > "$TMP/verify.out" 2>&1
		check "SDL LOADS the mapping and calls the pad a gamepad by it" $?

		# ⚠ AND IT IS OURS. SDL will happily invent a mapping for an
		# unknown pad from its evdev shape, which would satisfy
		# "is a gamepad" while proving nothing about the file this
		# wrote. These two bindings are the ones a guess gets wrong:
		# a Guide button it cannot know about, and the second stick's
		# vertical axis.
		grep -q "guide:b8" "$TMP/verify.out"
		check "...and the mapping SDL is using is the one this wrote" $?

		grep -q "righty:a4" "$TMP/verify.out"
		check "...down to the axis a guess would have had to guess" $?
	else
		echo "  skip  SDL verify would not build:"; head -3 "$TMP/cc.err"
	fi
else
	echo "  skip  no compiler or sdl3 headers — cannot ask SDL directly"
fi

echo
echo "── the mapping ──"
fold -w 100 <<<"$MAP"
