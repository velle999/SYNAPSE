#!/usr/bin/env bash
#
# syn_arcade_test.sh — against fixtures, never against the live desktop.
#
# ⚠ THE ONE RULE FOR THIS FILE: nothing in here may touch the running session.
#
# This suite is dangerous in a way syn-disks' is not, because three of the four
# things syn-arcade writes are files the LIVE desktop reads:
#
#   ~/.config/synui/synuirc         a compositor config. Writing a two-line
#                                   file over it replaces the whole desktop
#                                   configuration — synui reads exactly one.
#   ~/.config/MangoHud/MangoHud.conf   the overlay every running game watches.
#   ~/.config/syn-arcade/*          deadzones and mappings.
#
# So EVERY invocation runs with XDG_CONFIG_HOME redirected into a mktemp -d
# that the EXIT trap removes, and the guard below refuses to run at all if that
# redirection is not in place. `binds install --reload` and `binds reload` are
# never called: they run `synctl dispatch`, which reaches the LIVE compositor
# and would reload the real desktop's config mid-test.
#
# Controllers are described in a fake /sys/class/input (SYN_ARCADE_SYSFS). The
# ioctl paths — test, rumble, calibrate — are NOT exercised: they need a real
# device node, and a suite that opened one would be a suite that rumbles a pad
# and rewrites its deadzones on the machine running the build.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

# ⛔ EVERY ASSERTION BELOW IS IN ENGLISH, AND THE BINARY ANSWERS THE DESKTOP'S
# LANGUAGE. syn-arcade's human path goes through gettext as of pkgrel 51, so an
# installed syn-arcade on a German desktop fails every assertion that names one
# of its sentences — and passes on every English one, which is how this ships
# broken.
#
# ⚠ LANGUAGE is UNSET, not set. gettext reads LANGUAGE **before** LC_ALL, so a
# desktop with LANGUAGE=de still answers German to an LC_ALL=C.UTF-8 process
# and the pin does nothing at all.
#
# ⚠ THE RECORDS WOULD NOT HAVE NOTICED — they are never translated, and
# tests/i18n_test.sh proves it by running each one under a real German locale.
# It is the `says`-and-match assertions over the human path that need this.
export LC_ALL=C.UTF-8
unset LANGUAGE

SA=${1:-./build/syn-arcade}
[ -x "$SA" ] || { echo "not executable: $SA" >&2; exit 1; }
SA=$(readlink -f "$SA")

pass=0 fail=0
ok()    { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad()   { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail + 1)); }
check() { if [ "$2" = 0 ]; then ok "$1"; else bad "$1"; fi; }

# `((n++))` returns the OLD value, so a bare post-increment exits 1 the first
# time and would kill this script under set -e. Hence $((n + 1)) above.

# ⚠ Output of a command that is EXPECTED to exit non-zero.
#
# `set -o pipefail` makes the status of `cmd | grep` the LAST non-zero status in
# the pipeline — which, for everything here that tests a refusal, is the refusal
# itself rather than grep's verdict. Capture first, match second; `says` always
# exits 0, so the only status the caller sees is grep's.
#
# It also stops `grep -q` closing the pipe early and killing the BINARY with
# SIGPIPE partway through writing its output.
#
# ⚠ Taking the binary out of the pipe did not take the WRITER out of it, and
# that cost a failed `syn-update` on 2026-08-15. `grep -q` exits the instant it
# matches, which closes this pipe while the printf below may still be in
# flight; the printf then dies of SIGPIPE, and 141 at the end of a pipeline is
# exactly what `set -o pipefail` reports. So a PASSING assertion is reported as
# a failure — a different one each run, roughly half of all runs, and more the
# busier the machine, which is why it surfaced under makepkg rather than here.
# `meson test` failing is a BUILD failure, so this was three random red lines
# in the middle of a package build with nothing wrong with the package.
#
# Ignoring PIPE turns that death into a failed write, which is discarded. The
# status the caller sees is grep's verdict, which is the whole point of says().
# Diagnosis worth repeating: print the numeric status in `bad()` — "141" says
# SIGPIPE and ends the guessing immediately, where the assertion names look
# random and suggest a dozen wrong theories.
says() {
    local out; out=$("$@" 2>&1)
    trap '' PIPE
    printf '%s\n' "$out" 2>/dev/null || true
    trap - PIPE
}

# ⛔ AND `producer | grep -q` IS THE SAME BUG WITHOUT says() IN FRONT OF IT.
#
# says() above solves this for the commands whose output it captures, and 139
# assertions use it. Seventy-four did not: `"$SA" big games --rec | cut -f2 |
# grep -qx "…"` runs the BINARY straight into the pipe, and grep -q exits the
# instant it matches — so syn-arcade takes SIGPIPE partway through writing its
# records and `set -o pipefail` (line 28) reports 141 for an assertion that
# MATCHED. Measured elsewhere in this project at about 1.2% per pipeline, which
# over seventy-four of them is roughly one failing run in two on a busy machine,
# naming a different true assertion each time.
#
# `has` counts instead of exiting early: `grep -c` has to read to EOF to produce
# a count, so it cannot kill anything upstream of it.
#
# ⚠ THE COUNT GOES INTO A VARIABLE rather than to /dev/null. GNU grep 3.12 does
# read to EOF for `grep -c x >/dev/null`, but ugrep — which is `grep` in an
# interactive shell on this machine — takes an early exit when its stdout is
# /dev/null and hands back the same 141. The variable removes the question.
has() { local n; n=$(grep -c "$@") || true; [ "${n:-0}" -gt 0 ]; }

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

# The real one, kept so the assertions at the end can prove nothing reached it —
# the `fit` section redirects HOME, and $HOME by then is a temporary directory.
REAL_HOME=$HOME

# ⛔ AND WHAT WAS ALREADY THERE, because "nothing reached the real menu" is not
# the same claim as "the real menu is empty of wrappers". A person who USES this
# feature has syn-fit-*.desktop files in it — the machine this is written on has
# one for SimCity 3000 — and asserting the absolute made the suite fail on
# exactly the boxes where syn-arcade works. Snapshotted here, before anything
# runs, and compared at the end.
REAL_FIT_MENU=$(find "$REAL_HOME/.local/share/applications" -name 'syn-fit-*' \
                2>/dev/null | sort)
REAL_FIT_DESK=$(find "$REAL_HOME/Desktop" -name 'syn-fit-*' 2>/dev/null | sort)

# ── the sandbox ─────────────────────────────────────────────────────────────
#
# Everything the binary resolves through config_path() lands under here.
export XDG_CONFIG_HOME="$T/config"
mkdir -p "$XDG_CONFIG_HOME"

# The overlay config, pinned away from both /etc and the real user file.
export MANGOHUD_CONFIGFILE="$T/config/MangoHud/MangoHud.conf"

# Everything DERIVED — the headlines, the media servers found on the network —
# lands here rather than in the real cache.
export XDG_CACHE_HOME="$T/cache"
mkdir -p "$XDG_CACHE_HOME"

# ⚠ XDG_RUNTIME_DIR, and this one is not tidiness — it is the same class of
# hazard as XDG_CONFIG_HOME above, and worse in one specific way.
#
# Big screen mode's lock file and its control FIFO live in $XDG_RUNTIME_DIR. A
# suite that used the real one would drop a syn-arcade-big.pid beside a running
# session's, and `big show` would talk to the shell on the developer's own
# television.
#
# The worse thing: libwayland falls back to $XDG_RUNTIME_DIR/wayland-0 when
# WAYLAND_DISPLAY is unset. Unsetting the variable alone is NOT enough to keep
# a client off the live compositor — it has bitten this project before — so any
# test that reaches a Wayland client (wtype, `big mouse`) has to have the
# runtime directory moved as well. With both, there is no socket to find.
export XDG_RUNTIME_DIR="$T/run"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

# The network is off for the whole run. Two commands here talk to the internet
# and to the local network; a suite that did either would pass or fail
# depending on the building's DNS.
export SYN_ARCADE_NO_NET=1

# ⚠ WAYLAND_DISPLAY unset so that any code path which would reach the live
# compositor refuses on its own rather than relying on this file never calling
# one. Belt and braces: `binds reload` checks for it.
unset WAYLAND_DISPLAY SYNUI_SOCKET SYNUI_CONFIG SDL_GAMECONTROLLERCONFIG_FILE

# The guard. If XDG_CONFIG_HOME is not inside $T, the run would write to the
# real desktop's config — stop before the first command rather than after.
case "$XDG_CONFIG_HOME" in
    "$T"/*) ;;
    *) echo "REFUSING: XDG_CONFIG_HOME is not sandboxed" >&2; exit 1 ;;
esac

case "$XDG_RUNTIME_DIR" in
    "$T"/*) ;;
    *) echo "REFUSING: XDG_RUNTIME_DIR is not sandboxed" >&2; exit 1 ;;
esac

echo "syn-arcade tests — $SA"

# ── the binary answers ──────────────────────────────────────────────────────

echo
echo "basics"

says "$SA" --version | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$'
check "--version prints a version" $?

says "$SA" --help | grep -q "the SynapseOS game assistant"
check "--help says what it is" $?

says "$SA" nonsense | grep -q "unknown command"
check "an unknown command is refused" $?

"$SA" nonsense >/dev/null 2>&1
[ $? = 2 ]
check "...with exit status 2" $?

# ── `pads hold` ─────────────────────────────────────────────────────────────
#
# The keep-awake verb. Its device path is not exercised here, for the same
# reason none of the ioctl paths are — but its LIFETIME is, and that is the
# half that can leak. A `pads hold` that misses its exit condition holds every
# event node open for the rest of the session, and on a pad that sleeps the
# symptom of that is the pad staying awake: it looks like the fix working.
says "$SA" --help | grep -q "pads hold"
check "pads hold is documented" $?

# ⚠ NOT `pads hold | grep -q ...`. grep holds the read end open waiting for
# output that never comes, and `pads hold` is by design still running — the
# assertion hangs until meson's timeout turns it into a build failure. The exit
# condition has to be the thing that CLOSES the pipe.
#
# `| true` closes the read end immediately. 10s is ~100x the observed 69ms and
# far inside meson's 120s; a timeout here means the guard is gone and the verb
# would leak a process holding every event node.
timeout 10 sh -c "'$SA' pads hold 2>'$T/hold.err' | true"
check "pads hold exits when its stdout reader goes away" $?

grep -q "unknown pads command" "$T/hold.err"
[ $? != 0 ]
check "...and is a recognised verb, not one the dispatcher rejected" $?

# ── the overlay ─────────────────────────────────────────────────────────────

echo
echo "hud"

"$SA" hud ensure >/dev/null 2>&1
[ -f "$MANGOHUD_CONFIGFILE" ]
check "hud ensure creates the config file" $?

# ⚠ The reason `ensure` exists at all: MangoHud's inotify watch is added once,
# at layer init, and fails permanently if the path is missing then.
grep -q "no_display" "$MANGOHUD_CONFIGFILE"
check "...with no_display in it" $?

says "$SA" hud state | grep -q "^state     hidden"
check "the shipped default is hidden" $?

"$SA" hud show >/dev/null 2>&1
says "$SA" hud state | grep -q "^state     visible"
check "hud show makes it visible" $?

grep -q "^no_display=0" "$MANGOHUD_CONFIGFILE"
check "...by setting no_display=0, not by deleting the line" $?

"$SA" hud toggle >/dev/null 2>&1
says "$SA" hud state | grep -q "^state     hidden"
check "toggle flips it back" $?

# ── "visible" has never meant "on screen now" ───────────────────────────────
#
# ⚠ REPORTED AS A DEAD BUTTON, and the switch was working the whole time.
# MangoHud is a Vulkan and OpenGL layer inside the game's own process: it draws
# over a game and CANNOT draw on the desktop. So `hud show` on a machine with
# no game running writes the setting, reports success, and puts nothing
# anywhere — which from the outside is indistinguishable from a command that
# did nothing at all.
#
# The fix is words, and words are exactly what a later edit tidies away. Every
# change says where it takes effect, position included: "Move it" with nothing
# running moves nothing visible for the same reason, and a message that
# explained the silence only half the time would teach people to stop reading
# it.
says "$SA" hud toggle | grep -q "inside a game"
check "a toggle says where the overlay will actually be seen" $?

says "$SA" hud show | grep -q "inside a game"
check "...and so does turning it on" $?

says "$SA" hud position top-left | grep -q "inside a game"
check "...and moving it, which is silent for exactly the same reason" $?

says "$SA" hud cycle | grep -q "inside a game"
check "...and cycling it" $?

"$SA" hud hide >/dev/null 2>&1

# The record keeps the bare word. `visible` is a fact about the config and
# every other consumer of these records wants it that way — it is only reading
# it back to somebody looking at a desktop with no game on it that misleads,
# and that belongs in the window, not in the data.
says "$SA" hud --rec | grep -q "^state	hidden	toggle:hud$"
check "the record still carries the plain state, not the explanation" $?

# The window said "Show overlay", which is a promise it cannot keep on a
# desktop. The button now says where it takes effect, so it cannot be pressed
# in the expectation of something else.
GUIQML=data/syn-arcade.qml

# ── the mapping wizard ──────────────────────────────────────────────────────
#
# ⚠ THE ONE TAB THAT EXISTS FOR A BROKEN CONTROLLER COULD NOT FIX ONE. Mappings
# listed and removed; to ADD one it named another program. The window's answer
# to "my pad's buttons are in the wrong places" was the name of a program it
# does not ship.
#
# End to end this is mapwiz_rig.sh, which builds a uinput pad, presses all
# twenty-one controls and then hands the result back to SDL — the only check
# that distinguishes a plausible-looking mapping from one that works, because
# SDL refusing a mapping is silent. These pin what a later edit could undo.
WIZC=src/sdlwiz.c

says "$SA" map bogus-verb 2>&1 | grep -q "unknown map command"
check "map still refuses a verb it does not have" $?

"$SA" 2>&1 | has "map learn"
check "the wizard is in the help, where somebody with a bad pad will look" $?

# ⚠ dlopen, NOT a link. meson.build argues that libSDL has no business being
# linked into a tool that must work over SSH, and the wizard does not break
# that: SDL is opened at run time by the one command that needs it, so `pads
# list` on a machine without SDL still works. A plain dependency() would put
# libSDL3 in DT_NEEDED and nothing would warn.
[ "$(ldd "$SA" | grep -c SDL)" = 0 ]
check "the binary does not link SDL — the wizard opens it at run time" $?

grep -q 'dlopen(SDL_SONAME' "$WIZC"
check "...through dlopen" $?

grep -q '#define SDL_SONAME "libSDL3.so.0"' "$WIZC"
check "...by SONAME, so a machine without SDL's headers still has the library" $?

# ⚠ SDL COMPUTES THE GUID AND THE INDICES. A mapping names controls by SDL's
# joystick indices and is keyed on SDL's GUID; neither is derivable from evdev,
# and getting either wrong fails SILENTLY — SDL declines the mapping and the
# pad behaves exactly as before, which is the fault the person is escaping.
grep -q 'GetJoystickGUID' "$WIZC"
check "the GUID comes from SDL rather than being worked out here" $?

# ⚠ THE RELEASE IS NOT THE NEXT ANSWER. A release does not arrive with its
# press — it lands a moment later, while the next question is already up. The
# first build recorded a trigger correctly and then took that trigger's return
# to rest as the answer to the control after it, and everything from there was
# one press behind: a mapping that is well-formed, loads fine, and plays wrong.
# Nobody doing this by hand can see it happen.
grep -q 'wiz_settle(j, bind\[i\])' "$WIZC"
check "a control is not asked for until the last press was let go" $?

# An ended stdin polls READY for ever. `map learn </dev/null` would spin a core
# with a wizard on screen that looks like it is politely waiting.
grep -q 'static bool ended = false' "$WIZC"
check "an ended stdin is remembered instead of polled for ever" $?

# The wizard assembles the line; map_add writes it — the same refusals, the
# same replace-don't-append rule, the same file header as the paste path.
grep -q 'return map_add_line(line)' "$WIZC"
check "the wizard writes through the same path as a pasted mapping" $?

grep -q 'platform:Linux' "$WIZC"
check "...and puts platform:Linux on it, without which SDL ignores it" $?

grep -q '"Set up with the controller"' "$GUIQML"
check "the window can set a mapping up itself" $?

grep -q 'map", "learn", "--rec"' "$GUIQML"
check "...through the same stream the terminal wizard uses" $?

# ⚠ The prompts come from the binary. A window with its OWN list of controls
# would be a second list to fall out of step with the one deciding the order,
# and the symptom is every binding one control out.
grep -q 'root.wizAsk.detail' "$GUIQML"
check "...and the control being asked for is the binary's word, not the window's" $?

# A refusal test, and it looks at the VISIBLE strings rather than the file:
# the comment above this code in the QML explains what it used to say, and a
# plain grep would fail on the explanation for the fix.
! grep -E '^\s*(text|value):' "$GUIQML" | has 'antimicrox'
check "the window no longer sends anybody to another program" $?

! grep -E '^\s*\+ "' "$GUIQML" | has 'antimicrox'
check "...not on a continuation line either" $?

grep -q '"Show in games"' "$GUIQML"
check "the window's button says where the overlay will appear" $?

grep -q 'cannot draw on the' "$GUIQML"
check "...and the panel says why the desktop stays empty" $?

# ⚠ `no_display` is strtol()-parsed by MangoHud, and a BARE `no_display` line
# carries the value 1. A reader that treated "the key is present" as "hidden"
# would report a visible overlay as hidden forever.
printf 'no_display=0\nfps\n' > "$MANGOHUD_CONFIGFILE"
says "$SA" hud state | grep -q "^state     visible"
check "no_display=0 reads as VISIBLE, not as 'the key is set'" $?

printf 'no_display\nfps\n' > "$MANGOHUD_CONFIGFILE"
says "$SA" hud state | grep -q "^state     hidden"
check "a bare no_display line carries the value 1" $?

# ⚠ MangoHud strips from the first '#' anywhere in a line before parsing, so a
# commented-out setting is genuinely inert. Reading one as live would report a
# state MangoHud does not have.
printf '#no_display=1\nfps\n' > "$MANGOHUD_CONFIGFILE"
says "$SA" hud state | grep -q "^state     visible"
check "a commented-out no_display is not a setting" $?

# The last occurrence wins, because that is what MangoHud's parser does.
printf 'no_display=1\nfps\nno_display=0\n' > "$MANGOHUD_CONFIGFILE"
says "$SA" hud state | grep -q "^state     visible"
check "the LAST occurrence of a key wins" $?

# ── the config is the user's file, and stays that way ───────────────────────

cat > "$MANGOHUD_CONFIGFILE" <<'EOF'
# somebody's carefully tuned overlay
fps_limit=144
custom_text=hello
no_display=1
gpu_temp
EOF

"$SA" hud show >/dev/null 2>&1
grep -q "^fps_limit=144" "$MANGOHUD_CONFIGFILE" &&
grep -q "^custom_text=hello" "$MANGOHUD_CONFIGFILE" &&
grep -q "^gpu_temp$" "$MANGOHUD_CONFIGFILE" &&
grep -q "somebody's carefully tuned" "$MANGOHUD_CONFIGFILE"
check "changing one setting preserves every other line" $?

"$SA" hud set font_size 32 >/dev/null 2>&1
grep -q "^font_size=32" "$MANGOHUD_CONFIGFILE"
check "hud set adds a key that was not there" $?

"$SA" hud set font_size 40 >/dev/null 2>&1
[ "$(grep -c '^font_size=' "$MANGOHUD_CONFIGFILE")" = 1 ]
check "...and setting it twice leaves ONE line" $?

# ⚠ '#' would be swallowed as a comment by MangoHud's parser and silently
# truncate the value; a newline would forge an extra setting.
says "$SA" hud set custom_text 'a#b' | grep -q "cannot appear"
check "a '#' in a value is refused" $?

says "$SA" hud set custom_text "$(printf 'a\nno_display=1')" | grep -q "cannot appear"
check "a newline in a value is refused" $?

# ── position ────────────────────────────────────────────────────────────────

"$SA" hud position top-right >/dev/null 2>&1
says "$SA" hud position | grep -q "^top-right$"
check "position can be set by name" $?

says "$SA" hud position nowhere | grep -q "unknown position"
check "an unknown position is refused" $?

"$SA" hud position nowhere >/dev/null 2>&1
[ $? = 2 ]
check "...with exit status 2" $?

"$SA" hud position top-left >/dev/null 2>&1
"$SA" hud cycle >/dev/null 2>&1
says "$SA" hud position | grep -q "^top-center$"
check "cycle walks clockwise from the top-left" $?

"$SA" hud cycle-back >/dev/null 2>&1
says "$SA" hud position | grep -q "^top-left$"
check "cycle-back walks the other way" $?

# The wrap is the interesting end: (0 - 1) % 8 is -1 in C, and an unguarded
# modulo indexes off the front of the array.
"$SA" hud position top-left >/dev/null 2>&1
"$SA" hud cycle-back >/dev/null 2>&1
says "$SA" hud position | grep -q "^middle-left$"
check "cycling back from the first position wraps to the last" $?

# ⚠ Cycling also UNHIDES: pressing the position key on a hidden overlay and
# seeing nothing happen reads as a broken key.
"$SA" hud hide >/dev/null 2>&1
"$SA" hud cycle >/dev/null 2>&1
says "$SA" hud state | grep -q "^state     visible"
check "cycling a hidden overlay shows it" $?

# An unrecognised position must not be preserved into an index that means
# nothing — a newer MangoHud with more positions, or a typo.
printf 'position=somewhere-odd\nno_display=0\n' > "$MANGOHUD_CONFIGFILE"
"$SA" hud cycle >/dev/null 2>&1
says "$SA" hud position | grep -q "^top-left$"
check "cycling from an unknown position restarts the walk" $?

says "$SA" hud choices hud-position | grep -q "bottom-right"
check "choices lists the positions" $?

# ── records ─────────────────────────────────────────────────────────────────

echo
echo "records"

"$SA" hud ensure >/dev/null 2>&1
says "$SA" hud --rec | head -1 | grep -q "^field	value	action$"
check "the first record names the columns" $?

says "$SA" hud --rec | grep -q "^state	"
check "hud --rec reports the state" $?

# Encoding itself is asserted in the pads section, where the fixture can carry
# a name with characters worth escaping. A path and a position are all
# unreserved characters, so `hud --rec` is the wrong place to look for a '%'.

# ── controllers, from a described machine ───────────────────────────────────

echo
echo "pads"

# Nothing attached at all: an ANSWER, not a failure.
export SYN_ARCADE_SYSFS="$T/sys-empty"
mkdir -p "$SYN_ARCADE_SYSFS"
says "$SA" pads list | grep -q "no game controllers"
check "an empty machine says so" $?

"$SA" pads list >/dev/null 2>&1
[ $? = 100 ]
check "...with exit status 100, not a failure" $?

# Describe a machine. The capability masks are the whole test: sysfs prints them
# MOST SIGNIFICANT WORD FIRST, so the last word holds bits 0-63. Reading them
# left to right — the obvious way — puts every bit in the wrong place and
# decides that nothing is a gamepad.
mkpad() {
    local dir="$SYN_ARCADE_SYSFS/$1/device"
    mkdir -p "$dir/id" "$dir/capabilities"
    printf '%s\n' "$2"        > "$dir/name"
    printf '%s\n' "$3"        > "$dir/id/vendor"
    printf '%s\n' "$4"        > "$dir/id/product"
    printf '0001\n'           > "$dir/id/version"
    printf '%s\n' "${6:-0003}" > "$dir/id/bustype"
    printf '%s\n' "$5"        > "$dir/capabilities/key"
    printf '3\n'              > "$dir/capabilities/abs"   # ABS_X | ABS_Y
    printf '%s\n' "${7:-0}"   > "$dir/capabilities/ff"
}

export SYN_ARCADE_SYSFS="$T/sys"
mkdir -p "$SYN_ARCADE_SYSFS"

# BTN_SOUTH is 0x130 = 304. 304/64 = word 4 counting from the RIGHT, bit 48 —
# so with five words the leftmost carries it: 1<<48 = 0x1000000000000.
PAD_KEY="1000000000000 0 0 0 0"
# FF_RUMBLE is 0x50 = 80: word 1 from the right, bit 16.
FF_RUMBLE="10000 0"

mkpad event20 "Microsoft X-Box 360 pad" 045e 028e "$PAD_KEY" 0003 "$FF_RUMBLE"
mkpad event3  "Sony Interactive Entertainment DualSense" 054c 0ce6 "$PAD_KEY"

# A keyboard: plenty of key bits, no gamepad button, and no stick axes.
kbdir="$SYN_ARCADE_SYSFS/event1/device"
mkdir -p "$kbdir/id" "$kbdir/capabilities"
printf 'Logitech G513 Keyboard\n' > "$kbdir/name"
printf '046d\n' > "$kbdir/id/vendor"
printf 'c33a\n' > "$kbdir/id/product"
printf '0001\n' > "$kbdir/id/version"
printf '0003\n' > "$kbdir/id/bustype"
printf '1000000000007 ff9f207ac14057ff febeffdfffefffff fffffffffffffffe\n' \
    > "$kbdir/capabilities/key"
printf '0\n' > "$kbdir/capabilities/abs"
printf '0\n' > "$kbdir/capabilities/ff"

says "$SA" pads list | grep -q "X-Box 360"
check "a gamepad is listed" $?

says "$SA" pads list | grep -q "DualSense"
check "a second gamepad is listed" $?

# ⚠ The discriminating half. A keyboard has BTN_ bits set and no sticks; a
# graphics tablet has sticks and no gamepad buttons. Either test alone lists
# the wrong devices as controllers.
says "$SA" pads list | grep -qv "Keyboard" && ! says "$SA" pads list | grep -q "G513"
check "a keyboard is NOT a controller" $?

# Kernel order, not readdir order — "event10" sorts before "event2" as a string.
says "$SA" pads list | grep -E "^1\." | grep -q "DualSense"
check "the list is in kernel order (event3 before event20)" $?

says "$SA" pads list | grep -q "rumble"
check "rumble support is reported" $?

says "$SA" pads --rec | head -1 | grep -q "^id	name	kind	bus	rumble	node$"
check "pads --rec names its columns" $?

# The friendly-name table turns an unhelpful kernel name into something a
# person recognises; anything not in it shows its own name.
says "$SA" pads --rec | grep -q "DualSense"
check "a known vendor:product gets a friendly kind" $?

# ── naming one controller ───────────────────────────────────────────────────

says "$SA" pads info event20 | grep -q "X-Box 360"
check "a pad can be named by event id" $?

says "$SA" pads info 1 | grep -q "DualSense"
check "...by its number in the list" $?

says "$SA" pads info dualsense | grep -q "DualSense"
check "...or by a case-insensitive fragment of its name" $?

says "$SA" pads info nosuchpad | grep -q "no controller matches"
check "an unmatched name is refused" $?

# ⚠ Ambiguity must not silently pick the first: rumbling the wrong controller
# is confusing, and setting a deadzone on the wrong one is worse.
mkpad event21 "Microsoft X-Box 360 pad" 045e 028e "$PAD_KEY" 0003 "$FF_RUMBLE"
says "$SA" pads info "X-Box" | grep -q "matches 2 controllers"
check "an ambiguous fragment is refused, and lists the candidates" $?
rm -rf "$SYN_ARCADE_SYSFS/event21"

says "$SA" pads info event20 | grep -q "045e:028e"
check "info reports the USB ids" $?

# ── percent encoding ────────────────────────────────────────────────────────
#
# ⚠ The reason every field is encoded rather than "the ones that need it": a
# controller name is arbitrary bytes off a USB descriptor. The day a name
# contains a TAB, an unencoded field shifts every column of that row and the
# GUI attributes one pad's properties to another.

mkpad event30 "DualShock 4 (v2)" 054c 09cc "$PAD_KEY"
says "$SA" pads --rec | grep -q "%28v2%29"
check "characters outside the unreserved set are encoded" $?

# A literal tab inside the name. Without encoding this row would have one more
# column than the header and every field after the name would be misread.
mkpad "event31" "$(printf 'Evil\tPad')" 1234 5678 "$PAD_KEY"
[ "$(says "$SA" pads --rec | grep -c '	')" = "$(says "$SA" pads --rec | grep -c '^')" ]
check "a tab in a device name does not add a column" $?

says "$SA" pads --rec | grep -q "Evil%09Pad"
check "...because it is encoded" $?

# Every row must have exactly the same number of fields as the header.
hdr=$(says "$SA" pads --rec | head -1 | awk -F'\t' '{print NF}')
bad_rows=$(says "$SA" pads --rec | awk -F'\t' -v h="$hdr" 'NF != h' | wc -l)
[ "$bad_rows" = 0 ]
check "every record has the same field count as the header" $?

rm -rf "$SYN_ARCADE_SYSFS/event30" "$SYN_ARCADE_SYSFS/event31"

# ── saved deadzones ─────────────────────────────────────────────────────────

echo
echo "deadzones"

"$SA" pads save event20 --deadzone=12 >/dev/null 2>&1
grep -q "^045e:028e	12$" "$XDG_CONFIG_HOME/syn-arcade/deadzones.state"
check "a deadzone is saved against the USB ids, not the event number" $?

# ⚠ Keyed on vendor:product because "event20" is whatever number the kernel had
# free at plug-in time and means nothing across a reboot.
"$SA" pads save event20 --deadzone=8 >/dev/null 2>&1
[ "$(grep -c '^045e:028e' "$XDG_CONFIG_HOME/syn-arcade/deadzones.state")" = 1 ]
check "saving twice leaves ONE line for that pad" $?

grep -q "8" "$XDG_CONFIG_HOME/syn-arcade/deadzones.state"
check "...the second one" $?

says "$SA" pads save event20 | grep -q "deadzone=N"
check "save without a deadzone is a usage error" $?

# apply against fixtures cannot open a device node, and must not fail for it —
# it runs from /etc/profile.d at every login.
"$SA" pads apply >/dev/null 2>&1
[ $? = 0 ]
check "pads apply succeeds with nothing openable" $?

rm -f "$XDG_CONFIG_HOME/syn-arcade/deadzones.state"
"$SA" pads apply >/dev/null 2>&1
[ $? = 0 ]
check "...and with nothing saved at all" $?

# ── SDL mappings ────────────────────────────────────────────────────────────

echo
echo "mappings"

GOOD='030000005e0400008e02000010010000,X360 pad,a:b0,b:b1,x:b2,y:b3,platform:Linux,'

says "$SA" map list | grep -q "no mappings"
check "an empty database says so" $?

"$SA" map add "$GOOD" >/dev/null 2>&1
says "$SA" map list | grep -q "X360 pad"
check "a valid mapping is added" $?

says "$SA" map --rec | head -1 | grep -q "^guid	name	bindings$"
check "map --rec names its columns" $?

# ⚠ The one that actually bites. SDL only applies a mapping whose platform
# matches; every mapping copied off a forum says platform:Windows. It parses,
# it loads, it is never applied, and nothing anywhere says why.
says "$SA" map add '03000000000000000000000000000000,Pad,a:b0,platform:Windows,' |
    grep -q "another platform"
check "a Windows mapping is refused with the reason" $?

says "$SA" map add '03000000000000000000000000000000,Pad,a:b0,' | grep -q "platform"
check "a mapping with no platform field is refused" $?

says "$SA" map add 'abc,Pad,a:b0,platform:Linux,' | grep -q "32"
check "a short GUID is refused" $?

says "$SA" map add '030000000000000000000000000000zz,Pad,a:b0,platform:Linux,' |
    grep -q "hex digit"
check "a non-hex GUID is refused" $?

# ⚠ `xinput` is a real wildcard GUID, not a typo — it appears in the
# platform:Linux section of the community gamecontrollerdb, so a plain
# "32 hex characters" check rejects a mapping SDL would have used.
"$SA" map add 'xinput,XInput Controller,a:b0,b:b1,platform:Linux,' >/dev/null 2>&1
says "$SA" map list | grep -q "XInput"
check "the xinput wildcard GUID is accepted" $?

says "$SA" map add 'notamapping' | grep -q "not a mapping line"
check "a line that is not a mapping is refused" $?

# One mapping per GUID: SDL takes the last it reads, so two entries would work
# — but the file would disagree with itself and `map remove` would appear to do
# nothing.
"$SA" map add '030000005e0400008e02000010010000,Renamed,a:b1,platform:Linux,' >/dev/null 2>&1
[ "$(grep -c '^030000005e0400008e02000010010000' \
     "$XDG_CONFIG_HOME/syn-arcade/gamecontrollerdb.txt")" = 1 ]
check "re-adding the same GUID replaces rather than appends" $?

says "$SA" map list | grep -q "Renamed"
check "...with the new name" $?

"$SA" map remove 030000005e0400008e02000010010000 >/dev/null 2>&1
says "$SA" map list | grep -qv "Renamed"
check "a mapping can be removed by GUID" $?

says "$SA" map remove nosuchmapping | grep -q "no mapping matches"
check "removing something absent is refused" $?

# ⚠ The failure this whole feature has: a database no game is told about. It is
# invisible from `map list`, which happily prints a file SDL never reads.
says "$SA" map path | grep -q "unset"
check "map path warns when SDL is not pointed at the database" $?

# ── shortcuts ───────────────────────────────────────────────────────────────

echo
echo "binds"

# ⚠ NOTHING below passes --reload. That runs `synctl dispatch`, which reaches
# the LIVE compositor and would reload the real desktop's config mid-build.

RC="$XDG_CONFIG_HOME/synui/synuirc"

says "$SA" binds show | grep -q "not installed"
check "the shortcuts start uninstalled" $?

"$SA" binds install >/dev/null 2>&1
grep -q "^bind = super+F11 spawn syn-arcade hud toggle$" "$RC"
check "install writes the toggle bind" $?

grep -q "^bind = super+F12 spawn syn-arcade hud cycle$" "$RC"
check "...and the cycle bind" $?

says "$SA" binds show | grep -q "super+F11"
check "show reads the combos back out of the file" $?

# Re-installing must not stack blocks: synui would apply whichever it read
# last, and `binds remove` would take one away and appear to do nothing.
"$SA" binds install >/dev/null 2>&1
[ "$(grep -c 'syn-arcade hud toggle' "$RC")" = 1 ]
check "installing twice leaves ONE block" $?

"$SA" binds install --toggle=super+shift+H --cycle=super+shift+J >/dev/null 2>&1
grep -q "^bind = super+shift+H spawn syn-arcade hud toggle$" "$RC"
check "the combos can be changed" $?

[ "$(grep -c 'syn-arcade hud toggle' "$RC")" = 1 ]
check "...still leaving ONE block" $?

# ── the Media Center key ────────────────────────────────────────────────────
#
# The green button on an MCE remote sends KEY_MEDIA, which xkeyboard-config
# spells XF86AudioMedia. No modifier: a remote has none to hold.
grep -q "^bind = XF86AudioMedia spawn syn-arcade big toggle$" "$RC"
check "the Media Center key opens big screen mode" $?

# ⛔ AND IT MUST NOT BE READ BACK AS THE USER'S CHOSEN KEY. It runs the same
# command as super+F10, so binds_read — which recovers the chosen key by
# matching that command — would take the LAST such line and write it back as
# `big`, silently replacing super+F10 with a key most keyboards do not have.
"$SA" binds install >/dev/null 2>&1
"$SA" binds refresh >/dev/null 2>&1
"$SA" binds refresh >/dev/null 2>&1
says "$SA" binds show > "$T/binds-after-refresh.txt"
has 'super+F10' "$T/binds-after-refresh.txt"
check "...and two refreshes later big screen mode is still on super+F10" $?
[ "$(grep -c 'XF86AudioMedia' "$RC")" = 1 ]
check "...with exactly one media line, not one per refresh" $?

# ⛔ SOMEBODY ELSE'S BINDING WINS, AND THE REST STILL REFRESHES. The three
# configurable keys answer a collision by refusing, because the answer is to
# pick another key. There is no other key to pick for this one — so refusing
# would mean anybody who binds their music player to it can never refresh their
# gaming keys again, over a line they never asked for.
MEDIARC=$T/media-taken/synui/synuirc
mkdir -p "$(dirname "$MEDIARC")"
cat > "$MEDIARC" <<'RCX'
bind = XF86AudioMedia spawn some-music-player
# >>> syn-arcade  — the gaming shortcuts. Do not edit between the markers;
bind = super+F11 spawn syn-arcade hud toggle
bind = super+F12 spawn syn-arcade hud cycle
bind = super+F10 spawn syn-arcade big toggle
# <<< syn-arcade
RCX
XDG_CONFIG_HOME="$T/media-taken" "$SA" binds refresh >/dev/null 2>&1
check "refresh SUCCEEDS when the media key is already taken" $?
[ "$(grep -c 'XF86AudioMedia' "$MEDIARC")" = 1 ]
check "...leaving exactly their line, not ours as well" $?
has 'some-music-player' "$MEDIARC"
check "...and it is theirs that survived" $?

# ⚠ synui parses a bind it cannot understand by IGNORING the line, so every bad
# combo below is a key that silently does nothing.
says "$SA" binds install --toggle=F11 | grep -q "no modifier"
check "a combo with no modifier is refused" $?

says "$SA" binds install --toggle="super + F11" | grep -q "no spaces"
check "a combo with spaces is refused" $?

says "$SA" binds install --toggle=super+ | grep -q "no key"
check "a combo ending in '+' is refused" $?

# The XKB keysym rule, straight out of synuirc's own comments: `=` must be
# spelled `equal`, and `super+=` is silently dropped.
says "$SA" binds install --toggle=super+= | grep -q "equal"
check "'super+=' is refused with the keysym name to use" $?

says "$SA" binds install --toggle=super+F11 --cycle=super+F11 |
    grep -q "whichever it read last"
check "binding both shortcuts to one combo is refused" $?

# ⚠ THE MARKERS, not the string "syn-arcade" — which is what this asserted
# until `binds ensure` arrived. Remove now leaves one comment line behind
# saying the removal was deliberate, and that line names the command that puts
# the shortcuts back, so it contains the word.
"$SA" binds remove >/dev/null 2>&1
grep -qE "^# (>>>|<<<) syn-arcade" "$RC"
[ $? != 0 ]
check "remove takes the block back out" $?

grep -q "spawn syn-arcade" "$RC"
[ $? != 0 ]
check "...and every bind with it" $?

says "$SA" binds remove | grep -q "nothing to remove"
check "removing twice is not an error" $?

# ⚠ `binds reload` must refuse outside a session rather than reaching for a
# compositor that is not there.
says "$SA" binds reload | grep -q "no synui session"
check "reload refuses with no session" $?

# ── the trap that would eat a desktop ───────────────────────────────────────
#
# synui reads EXACTLY ONE synuirc: $SYNUI_CONFIG, then the user's, then
# /etc/synui/synuirc. There is no merging. So writing a fresh user file with
# only a bind block in it does not ADD two shortcuts — it silently replaces the
# entire system configuration, and the desktop comes back on stock defaults
# with the user's terminal, autostarts, gaps and theme gone.

rm -rf "$XDG_CONFIG_HOME/synui"
SYSRC="$T/etc-synuirc"
cat > "$SYSRC" <<'EOF'
# a system-wide config with real settings in it
terminal = syntty
gap = 8
theme = dendrite
EOF

SYNUI_CONFIG="$SYSRC" "$SA" binds install >/dev/null 2>&1

grep -q "^terminal = syntty$" "$RC" &&
grep -q "^gap = 8$" "$RC" &&
grep -q "^theme = dendrite$" "$RC"
check "installing over a system config COPIES it rather than replacing it" $?

grep -q "^bind = super+F11 spawn syn-arcade hud toggle$" "$RC"
check "...and still adds the shortcuts" $?

grep -q "^terminal = syntty$" "$SYSRC"
check "...leaving the system file untouched" $?

# Removing the block must not delete a file this package seeded: by now it is
# the only synui config on the machine, and the rest of it is the user's.
"$SA" binds remove >/dev/null 2>&1
grep -q "^terminal = syntty$" "$RC"
check "removing the block keeps the rest of the config" $?

# A hand-mangled block — somebody deleted the end marker — must still be
# removable, or the shortcut cannot be taken out by the command whose job that
# is.
{ cat "$SYSRC"; echo "# >>> syn-arcade"; echo "bind = super+F11 spawn x"; } > "$RC"
"$SA" binds remove >/dev/null 2>&1
grep -qE "^# (>>>|<<<) syn-arcade" "$RC" || has "spawn syn-arcade" "$RC"
[ $? != 0 ]
check "an unterminated block is still removed" $?

grep -q "^terminal = syntty$" "$RC"
check "...without eating the rest of the file" $?

# ── refresh: the only path an upgrade has into a user's config ──────────────
#
# The bug these pin, found 2026-08-15. 0.1.0-2 added super+F10 for big screen
# mode. Installing the package writes nothing into anybody's home, so every
# machine that had already run `binds install` under 0.1.0-1 kept the two-key
# block it was born with: the feature shipped, the docs named the key,
# `binds show` PRINTED the key — and the key was in no synuirc anywhere. There
# is no way to notice that from inside the package; only a command that
# re-renders an existing block can close it.

rm -rf "$XDG_CONFIG_HOME/synui"

says "$SA" binds refresh | grep -q "nothing to refresh"
check "refresh with no block installed does nothing" $?

[ ! -f "$RC" ]
check "...and does not create a config file (refresh is not install)" $?

mkdir -p "$XDG_CONFIG_HOME/synui"
cat > "$RC" <<'EOF'
terminal = syntty

# >>> syn-arcade  — the gaming shortcuts.
bind = super+shift+H spawn syn-arcade hud toggle
bind = super+F12 spawn syn-arcade hud cycle
# <<< syn-arcade
EOF

"$SA" binds refresh >/dev/null 2>&1
grep -q "^bind = super+F10 spawn syn-arcade big toggle$" "$RC"
check "refresh adds a key the older version never wrote" $?

grep -q "^bind = super+shift+H spawn syn-arcade hud toggle$" "$RC"
check "...keeping the combo the user chose" $?

grep -q "^terminal = syntty$" "$RC"
check "...and everything outside the block" $?

[ "$(grep -c 'syn-arcade hud toggle' "$RC")" = 1 ]
check "...in exactly ONE block" $?

says "$SA" binds refresh | grep -q "already up to date"
check "refreshing an up-to-date block says so" $?

# ⚠ Byte-identical, not just "says nothing". This runs from /etc/profile.d at
# every login, and a writer that rewrites the same content each time churns the
# mtime of the user's compositor config for nothing.
cp "$RC" "$T/rc-before"
"$SA" binds refresh --quiet >/dev/null 2>&1
cmp -s "$T/rc-before" "$RC"
check "...and does not touch the file" $?

# ── ensure: the keys have to work on a machine nobody configured ────────────
#
# ⚠ THE GAP `refresh` LEFT ON PURPOSE, and it made three shortcuts unreachable
# on every stock install. Nothing ever ran `binds install`: the package cannot
# write a home directory, the session profile only ever refreshed, and refresh
# deliberately does nothing when there is no block. So super+F10 — the ONLY key
# that opens big screen mode — was in the defaults, in `binds show` and in the
# documentation, and did nothing at all when pressed.
rm -rf "$XDG_CONFIG_HOME/synui"

"$SA" binds ensure >/dev/null 2>&1
grep -q "^bind = super+F10 spawn syn-arcade big toggle$" "$RC"
check "ensure writes a block where there is none" $?

grep -q "^bind = super+F11 spawn syn-arcade hud toggle$" "$RC" &&
    grep -q "^bind = super+F12 spawn syn-arcade hud cycle$" "$RC"
check "...with all three gaming keys" $?

cp "$RC" "$T/rc-ensure"
"$SA" binds ensure --quiet >/dev/null 2>&1
cmp -s "$T/rc-ensure" "$RC"
check "...and a second login does not touch the file" $?

# ⚠ AND A DELIBERATE REMOVAL STAYS REMOVED. This runs at every login: without a
# marker, `binds remove` would be undone by the next one, for ever — the same
# "a setting that will not stay set" the guide-button marker exists to prevent.
"$SA" binds remove >/dev/null 2>&1
grep -q "^# syn-arcade: shortcuts removed" "$RC"
check "remove leaves a line saying it was deliberate" $?

"$SA" binds ensure >/dev/null 2>&1
grep -q "syn-arcade big toggle" "$RC"
[ $? != 0 ]
check "...and ensure does not put the shortcuts back" $?

says "$SA" binds ensure | grep -q "removed on purpose"
check "...it says why it is doing nothing" $?

# Asking for them back is either the command or deleting that line.
"$SA" binds install >/dev/null 2>&1
grep -q "^# syn-arcade: shortcuts removed" "$RC"
[ $? != 0 ]
check "installing again clears the marker" $?

# ⚠ The session runs `binds ensure`, not `binds refresh` — the whole point of
# the verb. A profile that refreshes is a profile that does nothing on the
# machines this fixes.
grep -q "binds ensure --quiet" data/syn-arcade.sh
check "the login profile runs ensure" $?

rm -f "$RC"

# An autostart is in the same block and is re-rendered with it.
cat > "$RC" <<'EOF'
# >>> syn-arcade
bind = super+F11 spawn syn-arcade hud toggle
autostart = syn-arcade big start
# <<< syn-arcade
EOF
"$SA" binds refresh >/dev/null 2>&1
grep -q "^autostart = syn-arcade big start$" "$RC"
check "refresh keeps big screen mode at login turned on" $?

# ⚠ A key refresh would ADD must never land on top of one the user already
# bound: synui applies whichever `bind =` line it read LAST and logs nothing, so
# a duplicate is one of the two shortcuts silently not existing.
cat > "$RC" <<'EOF'
bind = super+F10 spawn firefox

# >>> syn-arcade
bind = super+F11 spawn syn-arcade hud toggle
bind = super+F12 spawn syn-arcade hud cycle
# <<< syn-arcade
EOF
says "$SA" binds refresh | grep -q "already bound"
check "refresh refuses a key that clashes with one the user set" $?

[ "$(grep -c 'super+F10' "$RC")" = 1 ]
check "...and leaves the file alone" $?

grep -q "^bind = super+F10 spawn firefox$" "$RC"
check "...with the user's own bind intact" $?

# A combo already IN the block is not a clash with itself.
cat > "$RC" <<'EOF'
# >>> syn-arcade
bind = super+F11 spawn syn-arcade hud toggle
bind = super+F12 spawn syn-arcade hud cycle
bind = super+F10 spawn syn-arcade big toggle
# <<< syn-arcade
EOF
# ⚠ Not "already up to date": a hand-written block is missing the comments
# make_block() renders, so refresh rightly rewrites it. What is being pinned is
# that it does not see OUR OWN key as somebody else's and refuse.
says "$SA" binds refresh | grep -q "already bound"
[ $? != 0 ]
check "a key already in the block is not read as a clash" $?

[ "$(grep -c '^bind = super+F10 spawn syn-arcade big toggle$' "$RC")" = 1 ]
check "...and is still bound exactly once afterwards" $?

# ── big screen mode ─────────────────────────────────────────────────────────
#
# ⚠ NOTHING here launches anything. `big steam`, `big run steam-bpm` and
# `big launch <appid>` all start real programs — on a build machine that would
# open Steam, and on this developer's machine it would open Steam over whatever
# they were doing. Only the REFUSALS are exercised, plus the library scanner,
# which is pure reading and is the part with something to get wrong.
#
# `big start` is likewise never called: it takes a lock and execs quickshell.
# The suite runs with WAYLAND_DISPLAY unset, so it refuses on its own — which
# is itself worth an assertion.

echo
echo "big screen mode"

# ── a described Steam installation ──────────────────────────────────────────
#
# Two libraries, because one is the bug: a machine with games on a second drive
# keeps nothing for them in the home directory, and a scanner that reads only
# the Steam root finds the runtimes and none of the games.
STEAM="$T/steam"
LIB2="$T/fastdisk/SteamLibrary"
mkdir -p "$STEAM/steamapps" "$LIB2/steamapps" "$STEAM/appcache/librarycache"

cat > "$STEAM/steamapps/libraryfolders.vdf" <<EOF
"libraryfolders"
{
	"0"
	{
		"path"		"$STEAM"
		"label"		""
	}
	"1"
	{
		"path"		"$LIB2"
		"label"		""
	}
	"2"
	{
		"path"		"$T/unplugged-drive/SteamLibrary"
	}
}
EOF

manifest() {  # dir appid name stateflags lastplayed
    cat > "$1/steamapps/appmanifest_$2.acf" <<EOF
"AppState"
{
	"appid"		"$2"
	"name"		"$3"
	"StateFlags"		"$4"
	"LastPlayed"		"$5"
	"SizeOnDisk"		"1073741824"
}
EOF
}

manifest "$STEAM" 10 "Fixture Adventure"     4    100
manifest "$STEAM" 20 "Proton Experimental"   4    900
manifest "$STEAM" 30 "Still Downloading"     1026 800
manifest "$LIB2"  40 "Second Disk Game"      4    200
manifest "$LIB2"  50 "Hashed Art Game"       4    50

# The three cover-art layouts Steam has used, all of which are still on disk on
# a machine that has had it installed for a few years.
mkdir -p "$STEAM/appcache/librarycache/10"
: > "$STEAM/appcache/librarycache/10/library_600x900.jpg"          # current
: > "$STEAM/appcache/librarycache/40_library_600x900.jpg"          # legacy flat
mkdir -p "$STEAM/appcache/librarycache/50/deadbeefdeadbeef"
: > "$STEAM/appcache/librarycache/50/deadbeefdeadbeef/library_capsule.jpg"

export SYN_ARCADE_STEAM="$STEAM"

says "$SA" big games --rec | head -1 | grep -q "^appid.*name.*art"
check "games --rec names its columns first" $?

[ "$("$SA" big games --rec | tail -n +2 | wc -l)" = 3 ]
check "three installed games — not the tool, not the half-downloaded one" $?

# A space is NOT percent-encoded — the record separator is a tab, and
# encoding spaces would make every name in the suite unreadable.
"$SA" big games --rec | cut -f2 | has -x "Second Disk Game"
check "a game on the SECOND library is found (libraryfolders.vdf is read)" $?

"$SA" big games --rec | cut -f2 | has "Proton"
[ $? != 0 ]
check "Proton is not a game" $?

"$SA" big games --rec | cut -f2 | has "Still Downloading"
[ $? != 0 ]
check "a manifest without StateFlags 4 is not installed" $?

"$SA" big games --all --rec | cut -f2 | has "Proton"
check "--all puts the tools back" $?

# Most recently played first. On a gamepad every row down the list is a
# physical press, so alphabetical order costs eleven of them to reach the game
# somebody was playing yesterday.
[ "$("$SA" big games --rec | sed -n 2p | cut -f2)" = "Second Disk Game" ]
check "the most recently played game is first" $?

[ "$("$SA" big games --rec | sed -n 4p | cut -f2)" = "Hashed Art Game" ]
check "...and the least recent is last" $?

"$SA" big games --rec | has "10/library_600x900.jpg"
check "cover art: the current per-appid layout" $?

"$SA" big games --rec | has "40_library_600x900.jpg"
check "cover art: the legacy flat layout" $?

"$SA" big games --rec | has "deadbeefdeadbeef/library_capsule.jpg"
check "cover art: the content-hash layout Steam uses now" $?

# A library on a drive that is not plugged in is listed in the vdf and has no
# steamapps directory. Skipping it must not be an error.
says "$SA" big games | grep -q "Fixture Adventure"
check "a library on an absent drive is skipped, not fatal" $?

SYN_ARCADE_STEAM="$T/no-steam-here" says "$SA" big games --rec | head -1 |
    grep -q "^appid"
check "with no Steam at all the columns are still named" $?

SYN_ARCADE_STEAM="$T/no-steam-here" "$SA" big games --rec >/dev/null 2>&1
[ "$?" = 100 ]
check "...and it exits 100, which is 'nothing to list', not failure" $?

# ── the other tiles ─────────────────────────────────────────────────────────

says "$SA" big apps --rec | head -1 | grep -q "^id.*name.*exec"
check "apps --rec names its columns first" $?

"$SA" big apps --rec | cut -f1 | has "^desktop$"
check "there is always a way out of a full-screen surface" $?

# ⚠ AND A WAY TO CLOSE IT, WHICH IS NOT THE SAME THING. Stepping aside leaves
# the shell loaded — that is what makes Guide instant — so on its own it left
# big screen mode resident for the rest of the session. Super+F10 only ever
# hides it, and a layer-shell surface is not a window, so nothing in the dock
# or the switcher could close it either. Reported as "it runs in the background
# but is not a program I can close".
"$SA" big apps --rec | cut -f1 | has "^quit$"
check "...and a way to CLOSE it, not merely leave it" $?

"$SA" big apps --rec | awk -F'\t' '$1 == "quit" && $6 == "system" { f = 1 } END { exit !f }'
check "...on the system shelf, beside the machine's own switches" $?

# ⚠ The glyph is listed in meson.build by NAME — data/icons is not a glob — so
# a drawing added to the tree and not to the build ships nothing and the tile
# comes up blank on the television. Silent both ways: the build succeeds and
# the shell draws an empty box.
grep -q "data/icons/quit.svg" meson.build
check "...and its glyph is actually installed" $?

"$SA" big apps --rec | cut -f1 | has "^poweroff$"
check "...and the machine's own switches are there" $?

says "$SA" big run nosuchtile | grep -q "no tile called"
check "an unknown tile id is refused" $?

"$SA" big run nosuchtile >/dev/null 2>&1
[ "$?" = 2 ]
check "...with a usage status" $?

says "$SA" big launch "'; rm -rf /" | grep -q "not an appid"
check "an appid that is not a number is refused" $?

says "$SA" big launch | grep -q "needs an appid"
check "launch with no appid is refused" $?

# ⚠ A layer-shell surface needs a compositor. WAYLAND_DISPLAY is unset for this
# whole suite, so this must refuse rather than reach for whatever socket
# happens to be in XDG_RUNTIME_DIR — which on a developer's machine is their
# live desktop, and the failure mode is a full-screen window over their work.
says "$SA" big start | grep -q "no Wayland session"
check "big start refuses with no session" $?

says "$SA" big status --rec | head -1 | grep -q "^field"
check "status --rec names its columns first" $?

says "$SA" big | grep -q "not running"
check "nothing is running to begin with" $?

# ── the shelves, and the two columns that decide what a tile DOES ───────────
#
# `shelf` says which row of the television a tile belongs on and `pointer`
# whether launching it should turn the controller into a mouse. Both are read
# by name out of the header row, so the assertion is that they are THERE — a
# missing column arrives in QML as undefined, which reads as false, which is
# a browser you cannot move a cursor in and nothing anywhere saying why.
head1=$("$SA" big apps --rec | head -1)
printf '%s' "$head1" | has "shelf"
check "apps --rec carries the shelf column" $?

printf '%s' "$head1" | has "pointer"
check "...and whether a tile wants the controller as a mouse" $?

printf '%s' "$head1" | has "keys"
check "...and whether it wants the on-screen keyboard" $?

"$SA" big apps --rec | cut -f6 | has -x "system"
check "the power tiles are on the system shelf" $?

# The browser and the terminal are the two tiles the whole pointer/keyboard
# apparatus exists for. Neither is guaranteed installed on a build machine, so
# this asserts the RULE rather than the row: anything on the apps shelf that is
# not an action wants a pointer.
"$SA" big apps --rec | awk -F'\t' 'NR>1 && $6=="apps" && $7!="1" { bad=1 }
                                   END { exit bad?1:0 }'
check "every tile on the apps shelf wants a pointer" $?

# ── the one tile that is not on PATH ───────────────────────────────────────
#
# ⚠ THIS IS THE ONLY TILE THAT CAN BE INSTALLED AND STILL INVISIBLE, which is
# why it is the only one with assertions of its own rather than a rule. Every
# other row in apps_table() is `have("name")` — `command -v` — and Greenlight
# is distributed as a Flatpak, which puts nothing on PATH. Written the obvious
# way it would have drawn no tile, logged nothing and failed on exactly the
# install the package recommends, which is the shape of dead feature this
# project keeps writing memos about.
#
# ⚠ AND THE FOURTH CASE IS THE ONE THAT MATTERS. A probe that answers "yes" to
# `flatpak info <anything>` passes the first three of these while being wrong,
# so the stub answers for THAT ID ALONE and the last check proves a machine
# with Flatpak but without Greenlight grows no tile.
GLB=$T/gl-bin
mkdir -p "$GLB"

printf '#!/bin/sh\nexit 0\n' > "$GLB/greenlight"
chmod +x "$GLB/greenlight"
row=$(PATH="$GLB:$PATH" "$SA" big apps --rec | awk -F'\t' '$1 == "greenlight" { print $3 }')
[ "$row" = "greenlight" ]
check "a native greenlight on PATH is the tile's exec" $?
rm -f "$GLB/greenlight"

cat > "$GLB/flatpak" <<'STUB'
#!/bin/sh
[ "$1" = info ] && [ "$2" = io.github.unknownskl.greenlight ] && exit 0
exit 1
STUB
chmod +x "$GLB/flatpak"

row=$(PATH="$GLB:$PATH" "$SA" big apps --rec | awk -F'\t' '$1 == "greenlight" { print $3 }')
[ "$row" = "flatpak run io.github.unknownskl.greenlight" ]
check "...and a Flatpak-only install still draws one" $?

# The exec is three words where every other one is one or two. big_run splits
# on spaces into an argv; a launcher that passed the string to a shell, or that
# took only the first word, would run bare `flatpak` and open nothing.
words=$(printf '%s\n' "$row" | wc -w)
[ "$words" = 3 ]
check "...whose exec is the three words big_run splits into an argv" $?

cat > "$GLB/flatpak" <<'STUB'
#!/bin/sh
exit 1
STUB
chmod +x "$GLB/flatpak"
row=$(PATH="$GLB:$PATH" "$SA" big apps --rec | awk -F'\t' '$1 == "greenlight" { print $1 }')
[ -z "$row" ]
check "...and Flatpak WITHOUT greenlight grows no tile" $?
rm -f "$GLB/flatpak"

# ── GeForce NOW, which is a browser pointed at a service ────────────────────
#
# The tile exists only where syn-gfn does, like every other launcher on the
# play shelf — and unlike them it wants BOTH a pointer and a keyboard, because
# it is a web page until a game starts: choosing one takes a cursor and signing
# in takes letters. A tile that opened GeForce NOW with neither would be a page
# somebody can look at and not use.
GFB=$T/gfn-bin
mkdir -p "$GFB"

# ⚠ ASKED OF THE MACHINE RATHER THAN ASSUMED EITHER WAY. A build host has no
# syn-gfn and velle's desktop has one, and a check hard-coded to either answer
# is a check that fails on the other machine for no reason — which is how a
# suite teaches people to ignore it.
row=$("$SA" big apps --rec | awk -F'\t' '$1 == "syn-gfn" { print $1 }')
if command -v syn-gfn >/dev/null 2>&1; then
    [ "$row" = "syn-gfn" ]
    check "the GeForce NOW tile is there, because syn-gfn is installed" $?
else
    [ -z "$row" ]
    check "no syn-gfn installed grows no GeForce NOW tile" $?
fi

printf '#!/bin/sh\nexit 0\n' > "$GFB/syn-gfn"
chmod +x "$GFB/syn-gfn"

row=$(PATH="$GFB:$PATH" "$SA" big apps --rec \
      | awk -F'\t' '$1 == "syn-gfn" { print $2 "|" $3 "|" $6 "|" $7 $8 $9 }')
[ "$row" = "GeForce NOW|syn-gfn|play|111" ]
check "...and with it, a play-shelf tile that takes a pointer and a keyboard" $?

# ── what was opened recently ────────────────────────────────────────────────
#
# ⚠ ASKED OF SYNUI, and this is a rewrite of a block that tested the wrong
# thing thoroughly. It used to press tiles and read back a file this program
# kept, which is exactly the behaviour that had to go: a list of what was
# launched FROM THE TELEVISION is empty on a machine somebody has been using
# all day, and empty forever for anybody who opens big screen mode, looks at
# it, and goes back to the desktop.
#
# ⛔ SYNCTL IS STUBBED, AND IT IS NOT OPTIONAL HERE. synui exports SYNUI_SOCKET
# into every process it starts — including the shell running this suite — and
# synctl PREFERS it over XDG_RUNTIME_DIR. The real one would answer with the
# LIVE desktop's history, so this block would pass or fail depending on what
# velle had open at the time, and the fixtures below would prove nothing.
RCB=$T/recent-bin
mkdir -p "$RCB"

recent_stub() {   # recent_stub <json>
    cat > "$RCB/synctl" <<STUB
#!/bin/sh
[ "\$1" = recent ] || exit 1
printf '%s\n' '$1'
STUB
    chmod +x "$RCB/synctl"
}

# Three applications, and the third is the one that matters: an app_id with no
# .desktop file behind it, which synui reports with known:false because its
# "command" would be an app_id rather than a command.
recent_stub '[{"app_id":"fixture-writer","name":"Fixture Writer","exec":"/bin/true","icon":"accessories-text-editor","known":true},{"app_id":"retroarch","name":"RetroArch","exec":"retroarch","icon":"retroarch","known":true},{"app_id":"some-window","name":"some-window","exec":"","icon":"","known":false}]'

hdr=$(PATH="$RCB:$PATH" "$SA" big recent --rec | head -1)

# ⚠ THE SAME COLUMNS `big apps` EMITS, plus one on the end. A recent
# application is a tile like any other and the shell must not need a second
# way to draw one.
printf '%s\n' "$hdr" | awk -F'\t' '{ exit ($1 == "id" && $2 == "name" && $10 == "iconfile" && $NF == "iconname") ? 0 : 1 }'
check "recent --rec is a tile row, with the icon NAME on the end" $?

rows=$(PATH="$RCB:$PATH" "$SA" big recent --rec | tail -n +2)

[ "$(printf '%s\n' "$rows" | wc -l)" = 2 ]
check "...and an app with no .desktop behind it is not offered as a tile" $?

printf '%s\n' "$rows" | awk -F'\t' 'NR==1 { exit ($1 == "app:fixture-writer") ? 0 : 1 }'
check "...with an id prefixed so it cannot collide with a tile of ours" $?

printf '%s\n' "$rows" | awk -F'\t' 'NR==1 { exit ($6 == "recent" && $7 $8 $9 == "111") ? 0 : 1 }'
check "...on its own shelf, taking a pointer, a keyboard and the screen" $?

# ⚠ The app_id in the `icon` column, which is the identity column for every
# tile. It is what the shell matches on to prefer a drawing of ours: RetroArch
# opened from the desktop is still RetroArch.
printf '%s\n' "$rows" | awk -F'\t' 'NR==2 { exit ($4 == "retroarch") ? 0 : 1 }'
check "...and the app_id kept where a tile keeps its identity" $?

printf '%s\n' "$rows" | awk -F'\t' 'NR==1 { exit ($12 == "accessories-text-editor") ? 0 : 1 }'
check "...and the .desktop Icon= name passed through for the theme to resolve" $?

recent_stub '[]'
PATH="$RCB:$PATH" "$SA" big recent | has -i "nothing opened on this desktop"
check "...and says so plainly when nothing has been" $?

# ── running one ─────────────────────────────────────────────────────────────
#
# ⚠ ONLY WHAT SYNUI LISTS. An app_id that is not in the recently-opened list is
# refused rather than run — otherwise `big run app:<anything>` would be a
# general-purpose "run this string through a shell" verb, reachable by whatever
# can hand the shell an id.
recent_stub '[{"app_id":"fixture-writer","name":"Fixture Writer","exec":"touch '"$T"'/ran-it","icon":"x","known":true}]'

# ⚠ `says`, not a pipe. Refusing is an EX_USAGE exit, and `set -o pipefail`
# reports that rather than grep's — so a pipeline here fails the check on the
# very status that proves the refusal happened.
says env PATH="$RCB:$PATH" "$SA" big run app:not-listed |
    grep -qi "not among the recently opened"
check "an app that is not in the list is refused, not run" $?

rm -f "$T/ran-it"
PATH="$RCB:$PATH" "$SA" big run app:fixture-writer --wait >/dev/null 2>&1
[ -f "$T/ran-it" ]
check "...and one that is runs the command synui resolved" $?

# ── the drawn tile glyphs ───────────────────────────────────────────────────
#
# An app tile is a word on a rounded rectangle, so a shelf of them is a list of
# names with nothing to aim at from four metres away. The `icon` column has
# always said which drawing a tile wants; `iconfile` is that name resolved
# against this installation, and the shell draws whatever comes back.
#
# ⚠ THREE LISTS THAT HAVE TO AGREE, and not one of them fails loudly on its
# own: the names in apps_table(), the files in data/icons, and the install
# list in meson.build. Add a tile and forget the drawing — a blank tile, which
# looks like a rendering fault. Draw one and forget meson — it works in the
# source tree, where every developer runs the rig, and ships an empty tile to
# the television. Both are silent, and both are one grep to catch.

apps=$("$SA" big apps --rec)

# ⚠ THE GLYPH PATH IS COLUMN TEN AND NO LONGER THE LAST ONE. A new column goes
# on the END — that is the rule this record has followed since `full`, and it
# is what keeps `cut -f10` working for anybody who wrote one — so `transient`
# went after `iconfile` and the shape has to be pinned by POSITION now rather
# than by "the last field is the icon".
printf '%s\n' "$apps" | awk -F'\t' 'NR==1 { exit ($10 == "iconfile") ? 0 : 1 }'
check "apps --rec has the resolved glyph path in column ten" $?

printf '%s\n' "$apps" | awk -F'\t' 'NR==1 { exit ($NF == "transient") ? 0 : 1 }'
check "...and the newest column on the end, where new columns go" $?

# ⚠ The PATH, checked for existence, not merely for being non-empty. An
# unreadable path is what a wrong prefix produces, and it arrives in the shell
# as an Image that quietly fails to load — indistinguishable on screen from a
# tile that was never given a glyph at all.
missing=
while IFS= read -r f; do
    [ -n "$f" ] && [ -f "$f" ] || missing="$missing [$f]"
done < <(printf '%s\n' "$apps" | awk -F'\t' 'NR>1 { print $10 }')
[ -z "$missing" ]
check "every tile's glyph resolves to a file that exists$missing" $?

# The names in the C table, whether or not that application happens to be
# installed on the machine running this suite — which is the whole point, since
# a build host has none of them and would otherwise assert nothing.
#
# ⚠ Read out of the STRUCT INITIALISERS, not by grepping for a name next to
# "app". The looser pattern also matched `rec_row(3, "field", "value",
# "action")` — a column header three thousand lines away — and reported that
# the drawing for a tile called "value" was missing. A pattern that can match
# something which is not a tile will eventually match something which is not a
# tile.
#
# ⚠ …and matched by PATTERN inside those initialisers, not by counting quoted
# strings. Three rows take their exec from a helper — music_prog(),
# browser_prog(), terminal_prog() — so the icon is the third quoted string in
# those and the fourth everywhere else. Counting reported "app" as an icon name
# and never looked at music, firefox or terminal at all: an assertion that
# passes because it is testing nothing.
undrawn=
while IFS= read -r name; do
    [ -f "data/icons/$name.svg" ] || undrawn="$undrawn $name"
done < <(awk '/\(struct row\)\{/ { buf = ""; inrow = 1 }
              inrow          { buf = buf $0 }
              inrow && /\};/ { print buf; inrow = 0 }' \
              "$(dirname "$0")/../src/big.c" |
         grep -oE '"[a-z0-9-]+",[[:space:]]*"(app|action)"' |
         sed -E 's/^"([^"]+)".*/\1/' | sort -u)
[ -z "$undrawn" ]
check "every icon name in apps_table has a drawing:$undrawn" $?

unshipped=
for f in data/icons/*.svg; do
    grep -q "'$f'" meson.build || unshipped="$unshipped $f"
done
[ -z "$unshipped" ]
check "every drawing is in meson's install list:$unshipped" $?

# ⚠ SVG TINY, because that is what QtSvg renders. A gradient, a filter or a
# <style> block is not an error — it simply does not draw, so the glyph looks
# perfect in a browser and the television shows a blank tile. Checked here
# rather than trusted to review: this is exactly the kind of thing a later
# edit adds without knowing.
rich=$(grep -lE '<style|filter=|Gradient|<use' data/icons/*.svg 2>/dev/null | tr '\n' ' ')
[ -z "$rich" ]
check "the glyphs stay inside SVG Tiny: $rich" $?

# A Plex server found on the network and Plex installed on this machine are the
# same thing on the shelf, so they get the same drawing — which means the media
# records carry a glyph too.
#
# ⚠ ASKED FOR BY NAME, not by position. This used to assert `iconfile` was the
# LAST column, which was true when it was the newest one and stopped being true
# the moment `pointer` and `keys` went on after it — and the rule for this file
# is that new columns go on the END. A test that pins the final field forbids
# the one change the format is designed for, and it fails on the correct fix
# rather than on a mistake.
printf '%s\n' "$(says "$SA" big media --rec)" |
    awk -F'\t' 'NR==1 { for (i = 1; i <= NF; i++) if ($i == "iconfile") exit 0; exit 1 }'
check "media --rec carries a glyph for what it found" $?

# ── filling the screen, and the toggle that can undo it ─────────────────────
#
# A tile press asks synui to fullscreen what it opened: from a sofa, a titlebar
# and a strip of wallpaper around the edge are the whole difference between an
# appliance and somebody's computer left switched on.
#
# ⚠ The compositor offers a TOGGLE and nothing else — there is no "set
# fullscreen" verb. So every assertion here is about one of the two ways a
# toggle goes wrong: sent to a window that is already full it puts it back in
# a box, and sent at the wrong moment it fullscreens whatever the tile was
# covering. `full` is the column that keeps it away from the first case.

printf '%s' "$head1" | has "full"
check "...and whether it needs help filling the screen" $?

# An action opens no window, so a toggle sent on its behalf can only land on
# something else.
"$SA" big apps --rec | awk -F'\t' 'NR>1 && $5=="action" && $9=="1" { bad=1 }
                                   END { exit bad?1:0 }'
check "no action tile asks for a fullscreen it has no window for" $?

# The launchers that arrive full-screen by themselves must NOT be helped —
# helping them is what undoes it. Steam is the worst of them: it maps a startup
# splash before Big Picture, and a helped splash fills a wall on the way in.
# Neither is guaranteed installed here, so this asserts the rule, not the row.
"$SA" big apps --rec | awk -F'\t' '
    NR>1 && ($1=="steam-bpm" || $1=="retroarch" || $1=="kodi") && $9=="1" { bad=1 }
    END { exit bad?1:0 }'
check "nothing that opens full-screen by itself is helped into it" $?

BIGC=src/big.c

# Sampling the state once is not enough: an app that fullscreens itself does it
# a moment AFTER mapping, and a toggle that wins that race turns fullscreen off.
grep -q "FULL_SETTLE_MS" "$BIGC"
check "the window's state is re-read after a settle, not sampled once" $?

grep -q "if (now.fullscreen)" "$BIGC"
check "...and a window that got there on its own is left alone" $?

# ⚠ Waiting for a window must not delay waiting for the APPLICATION. The shell
# reads an exit under three seconds as a single-instance hand-off (a browser
# already running takes the URL and quits); fifteen seconds of polling in front
# of the waitpid would turn every hand-off into a "close" and throw the
# television back over the browser somebody just opened. These two are one
# contract, written in two languages.
awk '/^static void fullscreen_after_launch/,/^}/' "$BIGC" | has "fork()"
check "the window wait is forked, so it cannot delay the app's exit" $?

grep -q "lived < 3000" data/syn-arcade-big.qml
check "...which is the hand-off rule that delay would have broken" $?

# ── more than one application, and a way to close one ──────────────────────
#
# ⚠ Big screen mode could open exactly ONE application and did not say so. The
# shell had a single `Process`; `big run --wait` lives as long as the
# application, so the second tile press set `running = true` on a process that
# was already running — a SILENT no-op in quickshell. Everything else still
# happened, so the television stepped aside to reveal the application already
# on screen, and with no close on the controller the only way out was a
# keyboard.
BIGQML=data/syn-arcade-big.qml

! grep -q "id: appProc" "$BIGQML"
check "the one shared launch process is gone" $?

grep -q "readonly property var procs: \[proc0" "$BIGQML"
check "...replaced by a pool, so a second launch has somewhere to go" $?

# The pool is only a fix if a full pool SAYS so rather than dropping the press.
# ⚠ MATCHED ON THE TAIL ONLY. The sentence is one msgid now and I18n.tr()
# wraps it across two source lines, so the whole thing is never on one line.
grep -q "close one first" "$BIGQML"
check "...and a full pool says so instead of doing nothing" $?

# The register is the compositor's, not a tally kept in the shell — a private
# list drifts from the screen the moment anything is opened or closed
# elsewhere, and every close aimed at a stale row lands on nothing.
grep -q '"big", "windows", "--rec"' "$BIGQML"
check "what is open is ASKED of synui, never remembered" $?

# ── the Recent bar, and the wheel ───────────────────────────────────────────
#
# ⚠ ASKED OF THE COMPOSITOR, and the first version of this asked the wrong
# thing entirely. It recorded what `big run --wait` launched, which made
# "recently opened" mean "recently opened FROM THE TELEVISION" — empty on a
# machine somebody had been using all day, and empty forever for anybody who
# opened big screen mode, looked at it and went back to the desktop. synui
# writes the list now, on the map of every window, because a window turning up
# is the one thing every way of launching something has in common.
grep -q '"big", "recent", "--rec"' "$BIGQML"
check "the Recent bar is asked for, not remembered" $?

grep -q '"synctl", (char \*)"recent"' "$BIGC"
check "...and big.c asks SYNUI for it, not a file of its own" $?

# ⛔ THE SECOND WRITER HAD TO GO. A note kept here as well would be a second
# list to disagree with synui's within one launch.
! grep -q "recent_note" "$BIGC"
check "...with nothing recording presses on the side" $?

# An application with no .desktop file cannot be started again from a tile —
# its "command" would be an app_id, which is a guess with a shell behind it.
grep -q "r->known" "$BIGC"
check "...and an app it cannot start again is not drawn" $?

# ⚠ A BAR, not a shelf of its own. A row of the television spent on four tiles
# is the mistake the system switches were moved behind Start to undo, and a
# shelf here would push Games down — the change the shelf order was rearranged
# to make in the first place.
# ⚠ `label:` SITS BETWEEN THEM NOW — a shelf carries an English `title` as its
# identity and a translated `label` for the screen, so the two fields this cares
# about are no longer adjacent. Both are asserted, on the one line that has them.
grep -q 'title: "Recent".*kind: "app"' "$BIGQML" ||
    grep -qz 'title: "Recent".*kind: "app"' "$BIGQML"
check "...and it is a bar, so it costs no row of its own" $?

# Sleep, restart and power off are actions somebody takes, not applications to
# go back to. One of them at the front of this row after an evening's use would
# be a power button where a game was.
grep -q 'hit.shelf === "system"' "$BIGQML"
check "...with the machine's own switches kept out of it" $?

# A desktop application has no drawing in this package — its picture is an
# Icon= name that only the thing drawing it can resolve.
grep -q "Quickshell.iconPath" "$BIGQML"
check "...and a recent app's icon comes from the icon THEME" $?

# ⚠ PREFIXED. `retroarch` is already a tile with its own exec and fullscreen
# flag; an app_id that collided with one would make `big run` ambiguous in
# favour of whichever branch was written first.
grep -q 'strncmp(id, "app:", 4)' "$BIGC"
check "a recent app is run under an id that cannot collide" $?

# ── the wheel ───────────────────────────────────────────────────────────────
#
# ⛔ EVERY CHECK IN THIS BLOCK USED TO PASS AGAINST A HANDLER THAT NEVER RAN.
# WheelHandler defaults acceptedDevices to PointerDevice.Mouse, and QtWayland
# calls every pointer on every Wayland session a TOUCHPAD — so the handler
# rejected every event a real mouse produced, silently, and six greps said it
# was fine. The greps below are worth what they always were, which is not much
# on their own: tests/bigscreen_rig.sh drives a REAL wheel into a nested synui
# and compares screenshots, and that is what actually answers this.
grep -q "WheelHandler" "$BIGQML"
check "a mouse wheel browses the shelves" $?

grep -q "acceptedDevices: PointerDevice.AllDevices" "$BIGQML"
check "...accepting the touchpad Wayland says every mouse is" $?

# ⚠ TWO HANDLERS. `orientation` defaults to Qt.Vertical and a vertical handler
# discards a horizontal event outright, so the sideways wheel needs its own.
grep -q "orientation: Qt.Horizontal" "$BIGQML"
check "...and a sideways wheel has a handler of its own" $?

# ⚠ ALONG THE ROW, which is the direction the shelves actually run in. Mapping
# the wheel to up/down moved the selection between shelves — the one direction
# a thumb already does easily, and the one a row of fifty games does not go in.
grep -A4 "function wheelWords" "$BIGQML" | has 'return \["right", "left"\]'
check "...and a turn of it runs ALONG the row, not between rows" $?

grep -A4 "function wheelWords" "$BIGQML" | has "Qt.ShiftModifier"
check "...with shift for the rows, for the hand that has a keyboard" $?

# The Start menu is a column, so there the wheel is its list.
grep -A4 "function wheelWords" "$BIGQML" | has 'shell.menuOpen)          return \["down", "up"\]'
check "...except in the Start menu, which is a column" $?

grep -A24 "function wheelTurn" "$BIGQML" | has "shell.nav("
check "...through nav(), like every other input" $?

# ⚠ A notch is 120 units and a touchpad sends a stream of small ones. Acting on
# every event tears through a shelf; rounding each one to a step moves nothing
# on a fine wheel. The remainder has to be KEPT.
grep -A24 "function wheelTurn" "$BIGQML" | has "acc += acc > 0 ? -notch : notch"
check "...accumulating, so a trackpad neither flies nor stalls" $?

# ⚠ AND THE RIG HAS TO ACTUALLY DRIVE ONE. A rig that only renders cannot tell
# a working wheel from a handler that is never called.
grep -q "scroll 3" tests/bigscreen_rig.sh
check "the rig drives a real wheel through a virtual pointer" $?

says "$SA" big focus | grep -q "needs an app-id"
check "big focus refuses without an app-id" $?

says "$SA" big close | grep -q "needs an app-id"
check "big close refuses without an app-id" $?

# Closing is confirmed, always. This is a gamepad on a sofa and X is easy to
# catch with a sleeve.
grep -q "property var closing: null" "$BIGQML"
check "a close is a question before it is an action" $?

# ⚠ A dialog that only LOOKS modal is worse than none: the selection would go
# on moving behind it, and A would then close whatever it had wandered onto.
grep -q "if (shell.closing) {" "$BIGQML"
check "...and it owns every button until it is answered" $?

# X is meaningful on exactly one shelf; a close that worked on a tile would be
# a close that closes something nobody chose.
grep -q 'sh.kind !== "running"' "$BIGQML"
check "X closes only on the Running shelf" $?

# ⚠ The shelf was correct and USELESS until this: coming back left the
# selection where it was, the rows scroll to keep it in view, and Running —
# being at the top — sat off the screen. The rig caught it.
grep -q 'shell.rowTitle = "Running"' "$BIGQML"
check "coming back from an app selects the Running shelf" $?

# ⚠ Shelves appear and disappear underneath the selection — media from a
# broadcast, news from the network, and now Running on every launch. A column
# remembered against a row NUMBER belongs to whatever shelf is at that number
# at the time.
grep -q "function colKey" "$BIGQML"
check "the scroll position of a shelf is remembered by NAME, not row" $?

# ⚠ A seatbelt, and it was missing. synui exports SYNUI_SOCKET into every
# process it starts, and synctl PREFERS it over XDG_RUNTIME_DIR — so the rig
# was talking to the live compositor no matter what else it redirected. It was
# harmless while that was one read. `big close` made it not harmless.
grep -q "^unset SYNUI_SOCKET" tests/bigscreen_rig.sh
check "the rig cannot reach the live compositor through SYNUI_SOCKET" $?

# ── URLs, which are the one thing here that comes from OUTSIDE ──────────────
#
# A headline's link is written by whoever wrote the feed and a server's address
# by whatever answered a broadcast. Neither is trusted: `big open` takes http
# and https and nothing else. The dash cases are the ones that matter — a
# "URL" starting with a dash is an OPTION to the browser it would be handed.
says "$SA" big open "--version" | grep -q "not an http"
check "a URL that is really an option is refused" $?

says "$SA" big open "file:///etc/shadow" | grep -q "not an http"
check "a file:// URL is refused" $?

says "$SA" big open "javascript:alert(1)" | grep -q "not an http"
check "a javascript: URL is refused" $?

says "$SA" big open "http://example.com/ -kiosk" | grep -q "not an http"
check "a URL with a space in it is refused" $?

says "$SA" big open | grep -q "not an http"
check "no URL at all is refused" $?

# ── news ────────────────────────────────────────────────────────────────────
#
# SYN_ARCADE_NO_NET is set for the whole suite, so nothing here fetches. What
# is tested is the shape and the CACHE RULE, which is the part with a bug in
# it worth catching: a machine that is offline must show yesterday's headlines
# rather than an empty shelf.
says "$SA" big news --rec | head -1 | grep -q "^id.*title.*source.*link"
check "news --rec names its columns even with no network" $?

"$SA" big news --rec >/dev/null 2>&1
[ "$?" = 100 ]
check "...and exits 100, which is 'nothing to list', not failure" $?

mkdir -p "$XDG_CACHE_HOME/syn-arcade"
printf 'id\ttitle\tsource\tlink\tfeed\nnews-0\tOld%%20News\tSomewhere\thttps://example.com/1\tnews\n' \
    > "$XDG_CACHE_HOME/syn-arcade/news.tsv"

"$SA" big news --rec | has "Old%20News"
check "a cached headline is served without touching the network" $?

# The refresh path with no network must NOT flatten the cache. Old news beats
# no news, and the machine most likely to have no network is a television that
# has just been switched on.
"$SA" big news --rec --refresh >/dev/null 2>&1
grep -q "Old%20News" "$XDG_CACHE_HOME/syn-arcade/news.tsv"
check "a failed fetch leaves the cached headlines alone" $?

# ── media servers ───────────────────────────────────────────────────────────

says "$SA" big media --rec | head -1 | grep -q "^id.*name.*url.*source"
check "media --rec names its columns" $?

# ── a server tile gets a mouse and a keyboard ───────────────────────────────
#
# ⚠ THE COLUMNS WERE MISSING AND NOTHING SAID SO. The shell gates the
# controller-as-mouse and the on-screen keyboard on `activeApp.pointer === "1"`
# and `.keys === "1"`, read BY NAME out of these records. A media server tile
# was written with six columns and neither of those was among them, so the
# lookup returned `undefined`, which is not "1" — and pressing Plex opened a
# browser on a television with no way to move the cursor and no way to type,
# while every app tile on the same shelf had both.
#
# It reads as a Plex problem or a pad problem and it is neither. That is why
# the assertion is on the COLUMNS rather than on anything about Plex: the tile
# behind them is a URL opened in a browser, which is the one thing on the whole
# interface that CANNOT be driven by words on a pipe.
says "$SA" big media --rec | head -1 | grep -q 'pointer.*keys'
check "media --rec carries the pointer and keys columns" $?

# Hermetic, so no server answers and there are no rows to check here — the
# value written per row is pinned structurally instead.
grep -q 'icon_file(found\[i\].source), "1", "1")' src/big.c
check "...and every server tile is given both" $?

# ── a cache from before those columns existed ───────────────────────────────
#
# ⚠ READABLE IS NOT CORRECT. Columns go on the END of this file and are read by
# name precisely so an older cache keeps working, and a six-column one does —
# it just has no `pointer`, which silently means "no mouse". Age cannot tell
# that apart from a good cache, so the header is asked directly.
#
# Without this the fix would look like it had not worked: the update lands, the
# television comes on, and the FIRST press of Plex is still served out of the
# file the old build left behind — for up to MEDIA_TTL, which is ten minutes of
# looking exactly like the bug that was just fixed.
printf 'id\tname\turl\tsource\tkind\ticonfile\nplex-1\tOld\thttps://example.com:32400/web\tplex\tserver\t\n' \
    > "$XDG_CACHE_HOME/syn-arcade/media.tsv"
says "$SA" big media --rec | head -1 | grep -q 'pointer'
check "a cache written before the pointer column is not trusted" $?

# The other direction, and it is the half that keeps the guard honest: a cache
# that HAS the columns must still be used, or every television would rediscover
# the network on every draw and the cache would be decorative.
printf 'id\tname\turl\tsource\tkind\ticonfile\tpointer\tkeys\nplex-7\tKept\thttps://example.com:32400/web\tplex\tserver\t\t1\t1\n' \
    > "$XDG_CACHE_HOME/syn-arcade/media.tsv"
says "$SA" big media --rec | grep -q '^plex-7	Kept'
check "...but a cache that has them is still served from" $?

# ── the same server, found twice ────────────────────────────────────────────
#
# ⚠ A SERVER THAT ANSWERS ITS OWN BROADCAST DESCRIBED ITSELF TWICE. The
# localhost probe exists because a server does not RELIABLY answer itself — but
# plenty do, and then the same Plex is added once as 192.168.x.x from the GDM
# reply and once as 127.0.0.1 from the probe. servers_add() deduplicates on the
# URL, and those are two different strings, so the Media shelf carried Plex
# twice: once under the server's own name and once as "Plex (this machine)".
#
# From the sofa that does not read as a bug — it reads as two servers, and
# pressing either one works. That is why it shipped.
#
# ⚠ STRUCTURAL, and it has to be here: this suite is hermetic
# (SYN_ARCADE_NO_NET=1), so no assertion in it can make a server answer. The
# behaviour was proven against the live network, where `big media --refresh`
# went from two rows to one and kept the server's OWN name.
grep -q 'static bool addr_is_local(const char \*ip)' src/big.c
check "discovery can tell this machine's own addresses" $?

grep -q '!have_local_server(out, n, "plex") &&' src/big.c
check "...and does not probe localhost for a Plex it already found here" $?

grep -q '!have_local_server(out, n, "jellyfin") &&' src/big.c
check "...nor for a Jellyfin" $?

printf 'id\tname\turl\tsource\tkind\nplex-1\tLiving%%20Room\thttps://example.com:32400/web\tplex\tserver\n' \
    > "$XDG_CACHE_HOME/syn-arcade/media.tsv"

says "$SA" big run plex-99 | grep -q "no media server called"
check "a media tile id that is not in the cache is refused" $?

# ⚠ Deliberately NOT `big run plex-1`: that would open a browser on whatever
# display this build is running next to. The lookup is proven by the refusal
# above and by the URL check below, both of which stop before spawning.
# ⚠ Not `says` — it captures the output and always exits 0, so the status
# being checked here would be its own rather than the binary's.
"$SA" big run plex-99 >/dev/null 2>&1
[ "$?" = 2 ]
check "...with a usage status" $?

# ── stepping aside, and the way back ────────────────────────────────────────

says "$SA" big hide | grep -q "not running"
check "hiding what is not running is refused" $?

# ⚠ `big show` with nothing running means START it, which needs a compositor.
# With WAYLAND_DISPLAY unset it must refuse — and that refusal is the proof
# that this suite cannot open a full-screen window on the developer's desktop.
says "$SA" big show | grep -q "no Wayland session"
check "showing what is not running starts it, and needs a session" $?

# The control channel. `big listen` is what the shell runs; `big show` is what
# the keybind sends. Proven end to end here with the lock HELD by flock, so
# nothing has to be started: the binary's own big_running() sees a locked pid
# file exactly as it would with a real shell behind it.
CTLPID="$XDG_RUNTIME_DIR/syn-arcade-big.pid"
: > "$CTLPID"
flock -x "$CTLPID" -c 'sleep 6' &
FLOCKER=$!
sleep 0.4

timeout 6 "$SA" big listen > "$T/ctl.out" 2>/dev/null &
LISTENER=$!
sleep 0.5

"$SA" big show >/dev/null 2>&1
"$SA" big toggle >/dev/null 2>&1
sleep 0.5
kill "$LISTENER" 2>/dev/null
wait "$LISTENER" 2>/dev/null

grep -qx "show" "$T/ctl.out"
check "a word sent to a running shell arrives on its listener" $?

grep -qx "toggle" "$T/ctl.out"
check "...and the keybind sends toggle rather than killing it" $?

kill "$FLOCKER" 2>/dev/null
wait "$FLOCKER" 2>/dev/null
rm -f "$CTLPID"

# ── the controller as a mouse, and the guide watcher ────────────────────────
#
# Both need a session and must say so rather than reaching for whatever socket
# is lying around. ⚠ This is the assertion that keeps `big mouse` off the live
# seat: it moves a REAL pointer, and a test that accidentally started one would
# be moving the cursor on the machine running the build.
says "$SA" big mouse | grep -q "no Wayland session"
check "big mouse refuses with no session" $?

says "$SA" big guard | grep -q "no Wayland session"
check "big guard refuses with no session" $?

says "$SA" --help | grep -q "big mouse"
check "the controller-as-mouse is documented" $?

says "$SA" --help | grep -q "big keys"
check "...and so is the on-screen keyboard's typist" $?

# ── controller navigation, without a controller ─────────────────────────────
#
# `big nav` is the one thing here that turns hardware into behaviour, and the
# hardware is exactly what a build machine does not have. It is testable anyway,
# because the code opens the event node by path and reads input_event structs
# out of it — so a FIFO in a fake /dev/input is a controller as far as this is
# concerned, and the whole translation runs for real: which button is which
# word, how a d-pad hat becomes a direction, that a release emits nothing, and
# that a held direction repeats.
#
# ⚠ TWO things this deliberately does not claim to cover.
#
#   The analogue sticks. Their thresholds come from EVIOCGABS, an ioctl a FIFO
#   cannot answer, so stick navigation needs a real pad on a real seat.
#
#   The writing end is held OPEN for the whole run, with `exec`, and that is
#   not a detail. Close a FIFO and the reader gets POLLHUP — which this code
#   correctly treats as "the controller was unplugged" and responds to by
#   reopening every device from scratch, forgetting which direction was held.
#   A test that wrote with a fresh `>` per batch would be testing a pad being
#   yanked out between every button press.

echo
echo "controller navigation"

NAVSYS="$T/navsys"
NAVDEV="$T/navdev"
mkdir -p "$NAVSYS" "$NAVDEV"

navdir="$NAVSYS/event9/device"
mkdir -p "$navdir/id" "$navdir/capabilities"
printf 'Fixture Pad\n'   > "$navdir/name"
printf '045e\n'          > "$navdir/id/vendor"
printf '028e\n'          > "$navdir/id/product"
printf '0001\n'          > "$navdir/id/version"
printf '0003\n'          > "$navdir/id/bustype"
printf '%s\n' "$PAD_KEY" > "$navdir/capabilities/key"
printf '3\n'             > "$navdir/capabilities/abs"
printf '0\n'             > "$navdir/capabilities/ff"

mkfifo "$NAVDEV/event9"

# One input_event per triple: 16 bytes of timeval (which nothing here reads),
# then u16 type, u16 code, s32 value. Written by python because a struct with
# NUL bytes in it is not something printf can be trusted to emit.
#
# EV_SYN=0/SYN_REPORT=0 ends a frame, and every batch below sends one — that is
# what the kernel does, and it is what tells the reader "this is one complete
# state now" rather than leaving a press and its release to cancel out.
ev() {  # type code value [type code value ...] — appended to the open fd 9
    python3 -c '
import struct, sys
out = sys.stdout.buffer
a = sys.argv[1:]
for i in range(0, len(a), 3):
    out.write(struct.pack("qqHHi", 0, 0, int(a[i]), int(a[i+1]), int(a[i+2])))
out.write(struct.pack("qqHHi", 0, 0, 0, 0, 0))     # SYN_REPORT
out.flush()
' "$@" >&9
}

NAVOUT="$T/nav.out"

# The reader first: opening a FIFO for writing BLOCKS until there is one.
SYN_ARCADE_SYSFS="$NAVSYS" SYN_ARCADE_DEV="$NAVDEV" timeout 6 "$SA" big nav \
    > "$NAVOUT" 2>/dev/null &
NAVPID=$!
sleep 0.5

exec 9> "$NAVDEV/event9"

# EV_KEY=1, EV_ABS=3. BTN_SOUTH=304, BTN_EAST=305, BTN_TR=311, BTN_START=315,
# BTN_MODE=316. ABS_HAT0X=16, ABS_HAT0Y=17.
ev 1 304 1;  ev 1 304 0            # press and release the bottom face button
ev 1 305 1;  ev 1 305 0
ev 1 316 1;  ev 1 316 0
ev 1 311 1;  ev 1 311 0
ev 1 315 1;  ev 1 315 0

# ⚠ Press and release in ONE write, in two frames. This is the case that used
# to emit nothing at all: the direction was decided once per read(), so a tap
# fast enough to land in a single batch cancelled itself out.
ev 3 17 -1  0 0 0  3 17 0
ev 3 16 1   0 0 0  3 16 0
sleep 0.4

# Held: one press, then nothing. What arrives after it is the repeat.
ev 3 17 1
sleep 1.2
ev 3 17 0

exec 9>&-
wait "$NAVPID" 2>/dev/null

navout=$(cat "$NAVOUT")

printf '%s\n' "$navout" | has -x "accept"
check "the bottom face button is 'accept'" $?

printf '%s\n' "$navout" | has -x "back"
check "the right face button is 'back'" $?

# A press and its release are one word, not two. A stream reporting both halves
# would move the selection twice per press.
[ "$(printf '%s\n' "$navout" | grep -c '^accept$')" = 1 ]
check "a button RELEASE emits nothing" $?

printf '%s\n' "$navout" | has -x "guide"
check "the guide button is its own word" $?

printf '%s\n' "$navout" | has -x "page-right"
check "a shoulder button pages" $?

printf '%s\n' "$navout" | has -x "menu"
check "start is 'menu'" $?

printf '%s\n' "$navout" | has -x "up"
check "a d-pad tap arriving in ONE read still becomes a direction" $?

printf '%s\n' "$navout" | has -x "right"
check "...on both axes" $?

# Two taps, so exactly two directions before the held one. Returning to centre
# must emit nothing, or letting go of the d-pad would move one square more
# every time.
[ "$(printf '%s\n' "$navout" | grep -cE '^(up|left|right)$')" = 2 ]
check "returning to centre emits nothing" $?

# The held press plus its repeats. Asserted as a range, not a count: an exact
# number would make this a test of the build machine's scheduler.
held=$(printf '%s\n' "$navout" | grep -c '^down$')
[ "$held" -gt 1 ]
check "a HELD direction repeats" $?

[ "$held" -lt 30 ]
check "...at a walking pace, not as fast as the loop spins" $?

# ── at login ────────────────────────────────────────────────────────────────

rm -rf "$XDG_CONFIG_HOME/synui"

says "$SA" big autostart status | grep -qx "off"
check "big screen at login starts off" $?

# Turning it OFF when nothing is installed must not WRITE anything. "Make sure
# this is not happening" is satisfied by a file that never mentioned it, and
# creating a block to say so would install three keybinds nobody asked for.
"$SA" big autostart off >/dev/null 2>&1
[ ! -f "$RC" ]
check "turning it off when it was never on writes no config" $?

"$SA" big autostart on >/dev/null 2>&1
grep -q "^autostart = syn-arcade big start$" "$RC"
check "turning it on writes the autostart line" $?

# ⚠ synui does NOT implement the XDG autostart spec — nothing in a synui
# session reads ~/.config/autostart — so a .desktop there would be silently
# ignored. The synuirc line is the only thing that runs.
grep -q "^bind = super+F10 spawn syn-arcade big toggle$" "$RC"
check "...and the key that opens and closes it" $?

says "$SA" big autostart status | grep -qx "on"
check "status reads it back" $?

says "$SA" binds show | grep -q "big screen mode at login"
check "the shortcuts listing reports it too" $?

# One managed block, read-modify-written. Two writers each appending their own
# would leave synui applying whichever it read last, with neither command able
# to remove the other's.
[ "$(grep -c '>>> syn-arcade' "$RC")" = 1 ]
check "there is exactly ONE managed block" $?

"$SA" binds install --toggle=super+shift+H >/dev/null 2>&1
grep -q "^autostart = syn-arcade big start$" "$RC"
check "installing keybinds does not silently turn the autostart off" $?

says "$SA" binds install --big=super+F11 |
    grep -q "whichever it read last"
check "binding big screen to the overlay's key is refused" $?

"$SA" big autostart off >/dev/null 2>&1
grep -q "^autostart = syn-arcade big start$" "$RC"
[ $? != 0 ]
check "turning it off takes the line back out" $?

grep -q "spawn syn-arcade big toggle" "$RC"
check "...and leaves the key that opens it" $?

# ── which screen it opens on ────────────────────────────────────────────────
#
# The regression this covers: big screen mode used to open on "wherever the
# pointer is", unconditionally, and the case it is most used in — `big autostart
# on`, opening AT LOGIN — has no pointer position worth reading. The ten-foot
# interface opened on whatever monitor the cursor was parked on, which on a desk
# with a portrait side panel is routinely not the television.
#
# synctl is STUBBED rather than called. The real one would reach the live
# compositor (and answer with the developer's own monitors, which is a suite
# that passes or fails depending on what is plugged in). The stub prints the
# shape ipc.c prints, with the primary and the focused output deliberately
# DIFFERENT — a fixture where they agree cannot tell the two rules apart.

echo
echo "which screen"

STUB="$T/bin"
mkdir -p "$STUB"
cat > "$STUB/synctl" <<'EOF'
#!/bin/sh
[ "$1" = outputs ] || exit 1
printf '%s\n' '[{"name":"DP-3","at":[1080,1080],"size":[2560,1440],"scale":1.00,"primary":true,"focused":false},{"name":"DP-2","at":[1080,0],"size":[1920,1080],"scale":1.00,"primary":false,"focused":false},{"name":"HDMI-A-1","at":[0,1080],"size":[1080,1920],"scale":1.00,"primary":false,"focused":true}]'
EOF
chmod +x "$STUB/synctl"
REAL_PATH=$PATH
PATH="$STUB:$PATH"
export PATH

BIGCONF="$XDG_CONFIG_HOME/syn-arcade/big.conf"
mkdir -p "$(dirname "$BIGCONF")"
rm -f "$BIGCONF"

says "$SA" big | grep -q "screen         DP-3 (primary)"
check "with no config it takes the PRIMARY screen, not the focused one" $?

# The old behaviour, still available to anybody who wants it — this is the one
# assertion proving the setting is a choice rather than a rename.
printf 'output = focused\n' > "$BIGCONF"
says "$SA" big | grep -q "screen         HDMI-A-1 (focused)"
check "output = focused restores follow-the-pointer" $?

printf 'output = DP-2\n' > "$BIGCONF"
says "$SA" big | grep -q "screen         DP-2 (DP-2)"
check "a connector name pins it to that screen" $?

# Comments and whitespace, because this file is meant to be edited by hand.
printf '# which screen\n\n   output   =   DP-2   \n' > "$BIGCONF"
says "$SA" big | grep -q "screen         DP-2"
check "comments and loose whitespace parse" $?

# ⚠ A monitor named six months ago may be unplugged today. Falling back
# silently would put the interface on the first screen with nothing anywhere
# saying why — the exact failure this whole section exists to stop.
printf 'output = DP-99\n' > "$BIGCONF"
says "$SA" big | grep -q "no output called 'DP-99'"
check "a name that matches no connector says so" $?

says "$SA" big | grep -q "screen         DP-3"
check "...and falls back to the primary screen" $?

# No synui, no synctl: no preference, and the QML takes its first screen. A
# launcher that refused to open because it could not decide which monitor to be
# on would be a worse answer than being on the wrong one.
#
# ⚠ In a SUBSHELL, not `PATH=… says …`. An assignment preceding a shell
# FUNCTION persists after the call in bash, so the inline spelling would take
# the stub off the path for everything below this line — and the stub is what
# keeps the rest of the suite off the live compositor.
rm -f "$BIGCONF"
( PATH=$REAL_PATH; export PATH; says "$SA" big ) | grep -q "screen         first screen"
check "with no compositor to ask it falls back rather than failing" $?

# The stub stays on the path for the remainder of the run, deliberately: it
# answers `outputs` and exits 1 for everything else, which is what "no synctl"
# already looks like to this binary. Anything added below that needs the real
# one is a test that would be reaching the live desktop.

# ── which music player ──────────────────────────────────────────────────────
#
# ⚠ THE SAME READER AS `output` ABOVE, which is the point of testing it here:
# one key was hard-coded until this, and generalising the parser is exactly the
# kind of change that keeps working for the key it was written for and silently
# stops working for the other one.
#
# Named against `sh`, because the assertion has to hold on any machine — the
# suite runs on CI, on a fresh install, and on a desktop with a dozen players
# installed, and "whichever of these twelve is present" is not something a test
# can know. sh is not a music player; it is a program that certainly exists.
printf 'music = sh\n' > "$BIGCONF"
says "$SA" big apps | grep -qE '^music +media +Music +sh( |$)'
check "music = <program> in big.conf picks the Music tile's player" $?

# ⚠ SAID OUT LOUD, and it still falls back. A tile that quietly opens a
# different program from the one named in the config file is the failure nobody
# thinks to check for: they read the config, see the right name, and go looking
# somewhere else entirely.
printf 'music = not-a-real-player-xyz\n' > "$BIGCONF"
says "$SA" big apps 2>&1 | grep -q "big.conf says music = not-a-real-player-xyz"
check "...and a named player that is not installed says so" $?

# Both keys in one file, because that is what a config file looks like after
# somebody has set two things — and a parser that takes the last assignment can
# take the last assignment of the WRONG key if it is careless.
printf 'output = DP-2\nmusic = sh\n' > "$BIGCONF"
says "$SA" big | grep -q "screen         DP-2"
check "two keys in one file do not read each other" $?

says "$SA" big apps | grep -qE '^music +media +Music +sh( |$)'
check "...both ways round" $?

rm -f "$BIGCONF"

# ── music as something DRIVEN rather than launched ──────────────────────────
#
# ⚠ Only cliamp can be driven, so the useful assertion on an arbitrary machine
# is the REFUSAL: `big music` must say what to do rather than fail silently or,
# worse, try to drive a player that has no socket. Whether cliamp is installed
# on the machine running this suite is not something the suite gets to decide,
# so the `music = sh` above is what makes the answer deterministic.
printf 'music = sh\n' > "$BIGCONF"
says "$SA" big music status | grep -q "cannot drive"
check "a player that cannot be driven is refused, by name" $?

says "$SA" big music status | grep -q "music = cliamp"
check "...and the refusal says what to put in the config" $?

"$SA" big music status >/dev/null 2>&1
[ "$?" = 1 ]
check "...with a failing status, not a silent zero" $?

says "$SA" big music wobble | grep -q "takes status, play, pause"
check "an unknown transport verb lists the real ones" $?

rm -f "$BIGCONF"

# ── the visualizer, and the flag that must never come back ──────────────────
#
# ⚠ `--daemon` IS THE OBVIOUS WAY TO START A HEADLESS PLAYER AND IT IS WRONG
# HERE. cliamp computes its visualizer bands inside the TUI's own draw loop, so
# in headless mode `vis` answers "visualizer not available in headless mode" and
# `visstream` yields nothing but {"ok":false,"error":"bands timeout"} for ever.
#
# The failure is the dangerous kind: the music plays, the menu grows a Now
# Playing row, every transport button works, and the meter is silently flat —
# which reads as a bug in the drawing code, not in how the player was started.
# So the player is run on a pty with no window (`script`), and this is the
# assertion standing between that and somebody tidying it back.
! grep -q '"--daemon"' src/big.c
check "the player is never started headless — the visualizer needs its TUI" $?

# ⚠ The command is BUILT now (the source picker appends --provider), so the
# assertion is on the shape rather than on the literal: script, -qfc, a command,
# and /dev/null for the typescript.
grep -q '"script", "-qfc", cmd, "/dev/null"' src/big.c
check "...it gets a pty and no window instead" $?

grep -q 'snprintf(cmd, sizeof(cmd), "cliamp%s%s"' src/big.c
check "...and the source is a --provider flag on that command" $?

grep -q 'execlp("cliamp", "cliamp", "visstream"' src/big.c
check "big music vis streams cliamp's own bands" $?

# ⚠ `play` is RESUME, and resume does nothing from `stopped` — which is the
# state a player that has just started is in. Sending it there starts the
# player and plays nothing: the tile responds, the row appears, and there is
# silence.
awk '/if \(!strcmp\(verb, "play"\)\)/,/^	}/' src/big.c |
    awk '/strcmp\(state, "playing"\)/ { seen = 1 }
         seen && /music_cmd\("toggle"\)/ { good = 1 }
         END { exit !good }'
check "a cold start is toggled into playing, not merely resumed" $?

# The stream is bounded by the menu being open. Twenty frames a second behind a
# full-screen game is the thing this file's header warns about.
grep -q 'running: shell.menuOpen && shell.musicLive' "$BIGQML"
check "the visualizer runs only while the menu is open" $?

# A parser pointed at another program's output, inside a try. cliamp answers
# {"ok":false} when it has no bands, and one throw here takes the menu down.
grep -q 'try {' "$BIGQML"
check "...and a bad frame cannot take the menu down with it" $?

# ⚠ …AND ONLY WHEN CLIAMP IS THE PLAYER. The bands come out of cliamp's own
# stream, so with Spotify playing the meter is a subprocess answering
# {"ok":false} twenty times a second for as long as the menu is open.
grep -q 'shell.musicLive && shell.musicIsPlayer' "$BIGQML"
check "...and never against a player that is not cliamp" $?

grep -q 'onExited: shell.musicBands = \[\]' "$BIGQML"
check "...and the bars go rather than freezing on the last frame" $?

# ── the shortcut: both stick clicks, or V ───────────────────────────────────
#
# The visualizer is the one tile somebody turns on WHILE something else is
# already playing, so three rows into the Start menu is the wrong distance for
# it. L3+R3 on the pad, V on a keyboard.
#
# ⚠ A CHORD, AND SINGLY THEY STILL SAY NOTHING. The nav stream deliberately
# drops every button a menu has no meaning for — the header on nav_button()
# explains why a stream carrying spare events is one where a new button
# silently becomes a navigation command — so L3 and R3 must not appear in that
# map. Only the pair speaks.
#
# ⚠ SCOPED TO nav_button(), not to the file. pad.c also has
# pad_button_name(), which is the HUMAN-READABLE list `pads` and the mapping
# wizard print — "left stick click" belongs there and always has. A file-wide
# grep matched it and failed against correct code.
navmap=$(awk '/^static const char \*nav_button/,/^}/' src/pad.c)
case "$navmap" in
    *BTN_THUMB*) false ;;
    *) true ;;
esac
check "a single stick click is still not a navigation word" $?

grep -q 'nav_say("visualizer")' src/pad.c
check "...and both together say one word" $?

# ⚠ LATCHED, or a hold that wobbles toggles twice. Both sticks going down is
# two events and whichever lands second completes the chord; re-pressing one
# thumb while the other stayed down would fire again and read as the press
# having been ignored.
grep -q 'if (p->chorded)' src/pad.c
check "...once per hold, however the thumbs wobble" $?

# ⚠ PER PAD. Two controllers on a sofa are two people, and a chord is one pair
# of thumbs — a global latch would let one pad's L3 arm another's R3.
grep -q 'bool l3, r3;' src/pad.c
check "...and per controller, not per machine" $?

# ⚠ THE CHORD SEES BOTH EDGES. Everything else in that switch acts on the
# press; this one has to see the RELEASE to clear its latch, which is why it is
# offered the code before nav_button() and is handed `down` rather than a
# press-only filter.
grep -q 'if (nav_chord(p, code, down))' src/pad.c
check "...and it is the one thing there that watches releases too" $?

# ⚠ AND IT ONLY *STARTS* FROM THE TELEVISION'S OWN SCREEN.
#
# `big nav` keeps reading the pad while the interface is stepped aside — that
# is how Guide comes back from inside a game — so the chord is live in the game
# too, and L3+R3 is a REAL BINDING in plenty of them. Ungated, a shortcut meant
# for a launcher throws a full-screen visualizer over somebody's game mid-fight.
#
# ⚠ `away` IS BOTH CONDITIONS AT ONCE, which is why it is one test: true while
# an application is in front, and true when Guide has put the interface away.
# Turning it OFF is above this guard and deliberately ungated — that press
# always arrives while stepped aside, because the visualizer is what is on
# screen.
awk '/function toggleVisualizer/,/^    }/' "$BIGQML" | has 'if (shell.away) return'
check "...but it does not launch over a game, or with the interface away" $?

awk '/function toggleVisualizer/,/^    }/' "$BIGQML" |
    awk '/visualizerRunning/ { seen = 1 } /shell.away/ { exit !seen }'
check "...while stopping it stays ungated, above that test" $?

# The shell half. ⚠ BEFORE EVERY GUARD in nav(), because this addresses the
# machine rather than the screen: it means the same thing with the Start menu
# open, with a close dialog up, and with the selection parked on a media
# button.
grep -q 'if (cmd === "visualizer") { shell.toggleVisualizer(); return }' "$BIGQML"
check "the shell acts on it wherever the selection happens to be" $?

# ⚠ AND ABOVE THE KEYBOARD BRANCH when stepped aside, or it would be swallowed
# as a letter whenever the on-screen keyboard was open — which is the half that
# matters most, since the visualizer is turned off from in front of it.
awk '/function navAway/,/^    }/' "$BIGQML" | has 'cmd === "visualizer"'
check "...including while it is the thing on screen" $?

# ⚠ COMING BACK IS HOW IT STOPS, which is not a second mechanism: the tile is
# `transient`, so endTransients() ends it exactly as Guide would — and that is
# the path 0.1.0-28 made actually let go of projectM.
grep -q 'if (shell.visualizerRunning()) { shell.comeBack(); return }' "$BIGQML"
check "...and stopping it is the same path Guide already uses" $?

# ⚠ ASKED OF THE SLOTS rather than a flag of its own. A second answer would be
# a second thing to keep in step, and would be wrong the first time somebody
# closed the visualizer from the Running shelf.
grep -q "rec.tile.id === \"visualizer\"" "$BIGQML"
check "...deciding it is running from what is running" $?

# ⚠ projectM IS OPTIONAL, so on a machine without it there is no tile at all —
# and a shortcut answering a press with nothing is the dead button this file
# keeps warning about.
grep -q 'The visualizer needs projectM' "$BIGQML"
check "...and a machine without projectM is told so, not ignored" $?

grep -q 'case Qt.Key_V:' "$BIGQML"
check "V is the keyboard's spelling of the chord" $?

# ⚠ ADVERTISED, but only where it does something — the same rule the X button
# in that legend already follows.
grep -q 'k: "L3+R3", v: I18n.tr("Visualizer")' "$BIGQML"
check "...and the legend names it when there is one to toggle" $?

# ── and ASKING it to end is not the same as it ENDING ───────────────────────
#
# ⚠ THE ASSERTION THE LAST RELEASE WAS MISSING, AND THE REASON IT SHIPPED A
# FIX THAT DID NOT FIX ANYTHING.
#
# 0.1.0-26 added the signal chain — the shell signals `big run --wait`, which
# passes it on to the process group — and the big screen rig agreed it worked.
# It agreed because the rig STUBS `big run` with `exec sleep 300`, and sleep
# dies of SIGTERM the way the manual says. The real tile does not: projectM
# imports `signal` and `pa_signal_new` (`nm -D` on projectM-pulseaudio shows
# both), so SIGTERM lands in a handler and is answered by an event loop — and
# while the interface is over the top of it, occlusion-culled and getting no
# frame callbacks, that loop is not turning. Reported from the sofa exactly as
# before: Guide, and the frozen visualizer is still there.
#
# So this stands in for projectM with the property that MATTERS — it catches
# the signal and carries on — and asserts on the process being GONE, not on
# the signal having been sent. Against the polite-signal-only code the
# stand-in outlives the waiter and this fails, which is what the rig could
# not do.
# ⚠ THE STUBS ARE BUILT HERE AND ONLY THE LAUNCH IS A SUBSHELL. A `check`
# inside a subshell increments a copy of the counters and the parent's totals
# never see it — a failing assertion would print FAIL and the suite would still
# exit 0. Everything counted below runs in this shell.
VT="$T/insist"
mkdir -p "$VT/bin"

# A monitor to listen to, so big_visualizer gets past its refusal. Stubbed
# rather than real: the suite must not ask the running machine what its audio
# devices are, and `pactl list sources short` is tab-separated.
cat > "$VT/bin/pactl" <<'STUB'
#!/bin/sh
case " $* " in
    *" sources "*) printf '0\tfixture.monitor\tPipeWire\ts16le 2ch 48000Hz\tIDLE\n' ;;
    *" get-default-sink "*) printf 'fixture\n' ;;
esac
exit 0
STUB

# ⚠ The stand-in, and the trap IS the fixture. Without it this is the rig's
# `sleep` again and passes against code that fixes nothing.
cat > "$VT/bin/projectM-pulseaudio" <<STUB
#!/bin/bash
trap 'echo caught >> "$VT/caught.log"' TERM INT
printf '%s\n' "\$\$" > "$VT/vis.pid"
while :; do sleep 0.2; done
STUB

# Nothing may reach a compositor. The suite already unsets SYNUI_SOCKET and
# moves XDG_RUNTIME_DIR, so there is no live socket to find; this is the third
# layer, and it also records any attempt.
cat > "$VT/bin/synctl" <<STUB
#!/bin/sh
printf '%s\n' "\$*" >> "$VT/synctl.log"
exit 1
STUB

chmod +x "$VT/bin/pactl" "$VT/bin/projectM-pulseaudio" "$VT/bin/synctl"

# The tile's exec is `syn-arcade big visualizer` — a word off a table, resolved
# through PATH. It has to find the binary under test.
ln -sf "$SA" "$VT/bin/syn-arcade"

# ⚠ `exec`, so $! names the WAITER and not a shell that happens to be its
# parent. The signal below has to land on the process holding waited_pid.
( export PATH="$VT/bin:$PATH"; exec "$SA" big run visualizer --wait ) \
    >/dev/null 2>&1 &
WAITER=$!

VIS=""
for _ in $(seq 60); do
    [ -s "$VT/vis.pid" ] && { VIS=$(cat "$VT/vis.pid"); break; }
    sleep 0.1
done

if [ -z "$VIS" ]; then
    bad "the stand-in visualizer never started"
else
    ok "the visualizer tile starts through the waiter"

    # What coming back does: one SIGTERM, to the waiter.
    kill -TERM "$WAITER" 2>/dev/null

    # ⚠ Longer than INSIST_AFTER and not much longer. The grace is two seconds;
    # five is room for a loaded build machine and still short enough that a
    # regression is a failure rather than a slow suite.
    gone=1
    for _ in $(seq 50); do
        kill -0 "$VIS" 2>/dev/null || { gone=0; break; }
        sleep 0.1
    done
    check "a visualizer that IGNORES the polite signal is ended anyway" $gone

    # And the waiter itself, because a waiter still running is big screen mode
    # still believing the tile is open.
    wgone=1
    for _ in $(seq 30); do
        kill -0 "$WAITER" 2>/dev/null || { wgone=0; break; }
        sleep 0.1
    done
    check "...and the waiter goes with it" $wgone

    # The polite signal is still asked FIRST. Enforcement that skipped straight
    # to SIGKILL would take a save file with it on any tile that ever grew one.
    [ -s "$VT/caught.log" ]
    check "...having been ASKED first, not merely killed" $?
fi

kill -KILL "$WAITER" ${VIS:-} 2>/dev/null
wait "$WAITER" 2>/dev/null

# The seatbelt, asserted rather than assumed — and it is `dispatch` that is
# forbidden, not synctl. Asking `activewindow` is the FIRST thing spawn_wait
# does and its failure is the answer that turns the fullscreen step off; a
# dispatch getting through would be this suite toggling fullscreen on whatever
# the developer was looking at.
! grep -q dispatch "$VT/synctl.log" 2>/dev/null
check "...and nothing in that path dispatched to a compositor" $?

grep -q activewindow "$VT/synctl.log" 2>/dev/null
check "...the fullscreen step having been skipped by ASKING, not by luck" $?

# ⚠ AND ONLY FOR THE TILES THAT ASKED FOR IT. `hard` comes off the same
# `transient` column as the signalling itself, so there is no second list to
# fall out of step — and Ctrl+C at a prompt on `big run steam-bpm --wait` still
# means "stop waiting" rather than "SIGKILL Steam in two seconds".
grep -q 'spawn_wait(argv, rows\[i\].full, rows\[i\].transient)' src/big.c
check "insisting is the transient column's decision, not a new list" $?

grep -q 'sigaction(SIGALRM, &sa, NULL)' src/big.c
check "...and SIGALRM is HANDLED, so enforcement cannot kill the waiter" $?

# ── where the music comes from ──────────────────────────────────────────────
#
# The source picker. cliamp is STUBBED and lives in a directory of its own,
# entered only inside subshells: a cliamp on the suite's main PATH would change
# which player music_prog() picks for every assertion after this point, and
# `big apps` is checked in half a dozen places above.
#
# ⚠ The stub answers `status` and LOGS everything, because most of what matters
# here is which cliamp commands were run and in what order — a queue that ends
# without a `toggle` is a television that says it is playing and is silent.
MSTUB="$T/music-bin"
mkdir -p "$MSTUB"
#
# ⚠ CLIAMP_STATE is what lets "there is no player" be tested at all. The stub
# used to answer `{"ok":true,"state":"playing"}` unconditionally, so every
# assertion here ran against a machine where music was already playing — which
# is the one case where starting the player looks harmless, and is why a source
# picker that STARTED the music passed this suite. "off" answers the way cliamp
# answers when nothing is bound to the socket.
#
# ⚠ IT HAS TO BE ABLE TO COME UP, or the assertions below prove nothing. A stub
# that answers "not running" for ever makes music_ensure_running() give up and
# return false, so the caller bails out BEFORE the transport verb — and a
# picker that starts the music then passes a test written to catch it. Measured:
# with a stub that could not start, three of the four assertions below passed
# against the very code they were written against. So `--provider` marks the
# player as up, exactly as starting cliamp really does.
#
# ⚠ And it comes up STOPPED, which is the whole reason `toggle` starts music.
# See the note on `play` being RESUME in big.c.
cat > "$MSTUB/cliamp" <<'EOF'
#!/bin/sh
echo "cliamp $*" >> "$CLIAMP_LOG"
if [ "$1" = status ]; then
    st="${CLIAMP_STATE:-playing}"
    [ "$st" = off ] && [ -f "$CLIAMP_UP" ] && st=stopped
    # ⚠ A TOGGLE THAT ACTUALLY STARTS THE MUSIC. Until this the stub could
    # answer "stopped" for ever however many transport commands it was sent,
    # which is the one thing a real player never does — and a player that can
    # never begin playing is a fixture that cannot tell "it started" from "it
    # is stuck", the exact distinction the code below this line exists for.
    [ "$st" = stopped ] && [ -s "${CLIAMP_PLAYING:-}" ] && st=playing
    if [ "$st" = off ]; then
        printf '{"ok":false,"error":"not running"}'
    else
        # ⚠ TITLE AND PATH ARE SEPARATE, and the default is that they are the
        # same string — which is cliamp saying it has no name for a queued
        # track, and was the only case this stub could produce. It is NOT the
        # only case that happens: for a YouTube URL cliamp invents a title from
        # the last path segment ("watch"), and a stub that could not say that
        # is a stub that passed while every song on a station was called watch.
        # ⚠ `total` IS ABSENT ON AN EMPTY QUEUE, which is what cliamp really
        # does — measured, a player with nothing queued prints no such key at
        # all rather than a zero. It is the difference between a player that
        # can be resumed and one that has nothing to resume, and a stub that
        # always claimed a queue could not tell the two apart.
        printf '{"ok":true,"state":"%s","track":{"title":"%s","path":"%s"}%s}' \
            "$st" "${CLIAMP_TITLE:-$CLIAMP_TRACK}" "$CLIAMP_TRACK" \
            "${CLIAMP_TOTAL:+,\"total\":$CLIAMP_TOTAL}"
    fi
    exit 0
fi
# Anything carrying --provider IS a start: that is the only way the flag is
# ever passed, and music_ensure_running() is the only thing that passes it.
[ "$1" = "--provider" ] && : > "$CLIAMP_UP"

# ⚠ A TRACK THAT WILL NOT PLAY, which is a real and silent thing: a queued
# YouTube URL that cannot be resolved leaves cliamp `stopped` for ever, with no
# error on any stream and no skip of its own. While $CLIAMP_STUCK exists this
# stub is on such a track and `toggle` does nothing at all; `next` moves off it.
# ⚠ NOTHING IN HERE MAY CALL A PROGRAM. The runners below hand this stub a
# PATH that is a REPLACEMENT — three or four stub directories and no /bin — so
# `cat`, `rm` and friends are "command not found" and their failure is SILENT.
# This cost an afternoon: `rm -f "$CLIAMP_STUCK"` on the `next` line below
# never once removed anything, so the fixture could not model a player
# RECOVERING, and a new assertion failed against code that was correct.
#
# So state is a FILE'S EMPTINESS, tested with -s and cleared with `: >`, both
# of which are shell builtins.
[ "$1" = next ] && : > "${CLIAMP_STUCK:-/dev/null}"

# ⚠ AND A TOGGLE THAT IS SIMPLY LOST, which is a different thing again and is
# the one that was actually happening. A player that has just come up answers
# its first toggle by doing nothing — measured against the real cliamp: stuck
# at 12s, one plain re-toggle, playing at 16s. $CLIAMP_DEAF holds how many
# toggles are swallowed before one lands. Without this the fixture could only
# model a DEAD TRACK, so "skip it" looked like the right answer to a stall and
# a perfectly good song was thrown away.
if [ "$1" = toggle ] && [ -n "${CLIAMP_DEAF:-}" ] && [ -s "$CLIAMP_DEAF" ]; then
    read n < "$CLIAMP_DEAF"
    if [ "${n:-0}" -gt 0 ]; then
        n=$((n - 1))
        printf '%s' "$n" > "$CLIAMP_DEAF"
        exit 0
    fi
fi
[ "$1" = toggle ] && [ ! -s "${CLIAMP_STUCK:-}" ] && printf 1 > "$CLIAMP_PLAYING"
[ "$1" = stop ] && : > "${CLIAMP_PLAYING:-/dev/null}"
exit 0
EOF
chmod +x "$MSTUB/cliamp"
export CLIAMP_LOG="$T/cliamp.log" CLIAMP_TRACK="" CLIAMP_STATE="playing"
export CLIAMP_TITLE="" CLIAMP_TOTAL=""
export CLIAMP_UP="$T/cliamp.up"
export CLIAMP_PLAYING="$T/cliamp.playing" CLIAMP_STUCK="$T/cliamp.stuck"
export CLIAMP_DEAF="$T/cliamp.deaf"

# ⚠ THE WAITS ARE CUT DOWN FOR THE WHOLE RUN. music_start_insist() waits
# fifteen seconds for the player to settle before it decides a track will not
# play — right on a television, and a suite meson kills at 120s cannot afford
# it a dozen times over. The BEHAVIOUR under test is unchanged; only the clock
# is.
export SYN_ARCADE_MUSIC_WAIT_MS=1000

: > "$CLIAMP_LOG"; rm -f "$CLIAMP_UP" "$CLIAMP_PLAYING" "$CLIAMP_STUCK"

# ⚠ In a SUBSHELL for the same reason the synctl stub note above gives: an
# assignment in front of a shell FUNCTION persists after the call in bash.
music() { ( PATH="$MSTUB:$PATH"; export PATH; says "$SA" big music "$@" ); }

rm -f "$BIGCONF"
music source | has -E '^plex +Plex'
check "the source picker lists Plex first" $?

music source | has -E '^radio +Radio +· current'
check "...and an unset config reads as radio, which is what cliamp does" $?

music source --rec |
    awk -F'\t' '$1 == "plex" && $4 == "albums" { f = 1 } END { exit !f }'
check "...with an action column saying Plex has a library to pick from" $?

# ⚠ THE TWO STREAMING SERVICES NO LONGER SAY `browse` UNCONDITIONALLY, and
# that is the change rather than a broken assertion: on a machine with no
# yt-dlp and no Spotify account, "opens cliamp" was true and the outcome was an
# empty library. Which action each one gets, and both sides of it, are asserted
# in the streaming section further down; this only pins that the column is
# still answered for them.
music source --rec |
    awk -F'\t' '$1 == "spotify" && $4 != "" { f = 1 } END { exit !f }'
check "...and that the streaming services answer the column too" $?

# ⚠ NOT through says(), which always exits 0. An exit STATUS has to come from
# the binary itself, and every refusal in this file that checks one runs it
# directly for exactly that reason.
( PATH="$MSTUB:$PATH"; export PATH
  "$SA" big music source wobble >/dev/null 2>&1 )
[ "$?" = 2 ]
check "an unknown source is a usage error, not a silent no-op" $?

# ⚠ THE KEY IS WRITTEN ONCE, and every earlier copy of it goes. big_conf_get
# takes the LAST assignment in the file, so a writer that replaced the first
# one would leave a config that looks changed and reads unchanged — the
# setting would appear not to take, on exactly the machines where somebody had
# already edited the file by hand.
printf 'output = DP-2\nmusic_source = radio\n# a comment\nmusic_source = local\n' \
    > "$BIGCONF"
music source plex >/dev/null 2>&1
[ "$(grep -c '^music_source' "$BIGCONF")" = 1 ]
check "choosing a source leaves exactly one line for it" $?

grep -q '^music_source = plex$' "$BIGCONF"
check "...and it is the one that was chosen" $?

grep -q '^output = DP-2$' "$BIGCONF" && grep -q '^# a comment$' "$BIGCONF"
check "...with every other key and comment kept" $?

printf 'music_source = wobble\n' > "$BIGCONF"
music source 2>&1 | grep -q "big.conf says music_source = wobble"
check "a source nobody implements says so and falls back" $?

# ── choosing a source must not START the music ──────────────────────────────
#
# ⚠ THE REGRESSION THIS SECTION EXISTS FOR. Changing Plex → Radio on a silent
# machine started playing: the picker restarted the player unconditionally,
# which STARTS one that was not running, and then sent `toggle` for radio —
# which from `stopped` is not a toggle, it is `play`. Reported from the sofa,
# and reproduced here: `script -qfc cliamp --provider radio` was left running
# and audible after a press that only asked for a preference.
#
# Asserted on the LOG rather than on exit status, because the picker "worked"
# throughout — the setting was always written correctly. It is what it did
# BESIDES writing it that was the bug.
rm -f "$BIGCONF"
: > "$CLIAMP_LOG"; rm -f "$CLIAMP_UP"
( PATH="$MSTUB:$PATH"; export PATH; CLIAMP_STATE=off; export CLIAMP_STATE
  says "$SA" big music source radio ) >/dev/null

grep -q '^music_source = radio$' "$BIGCONF"
check "choosing a source with no player running still records it" $?

# ⚠ Both halves, and they fail separately. `toggle` is what actually made
# sound; the start is what created a player to make it with. Checking only the
# verb would miss a version that leaves cliamp running and silent, which is
# still a program somebody did not ask to start.
[ ! -f "$CLIAMP_UP" ]
check "...and does not START a player that was not running" $?

grep -q 'cliamp toggle' "$CLIAMP_LOG"
[ $? != 0 ]
check "...nor plays anything" $?

# The other side of it: a player that WAS playing keeps playing, on the new
# source. Changing source is not a stop button any more than it is a play one.
: > "$CLIAMP_LOG"
( PATH="$MSTUB:$PATH"; export PATH; CLIAMP_STATE=playing; export CLIAMP_STATE
  says "$SA" big music source radio ) >/dev/null
grep -q 'cliamp toggle' "$CLIAMP_LOG"
check "music that was playing carries on after a source change" $?

# ⚠ And `paused` is neither. Resuming here would start music somebody had
# deliberately stopped — the same complaint, one state along.
: > "$CLIAMP_LOG"
( PATH="$MSTUB:$PATH"; export PATH; CLIAMP_STATE=paused; export CLIAMP_STATE
  says "$SA" big music source radio ) >/dev/null
grep -q 'cliamp toggle' "$CLIAMP_LOG"
[ $? != 0 ]
check "...but a PAUSED player is not resumed by changing source" $?

rm -f "$BIGCONF"
: > "$CLIAMP_LOG"

rm -f "$BIGCONF"

# ⚠ A SOURCE THAT WOULD PLAY NOTHING HAS TO SAY SO ON ITS ROW. Choosing Local
# files on a machine with no music directory stops whatever is playing and
# queues nothing — silence, from a menu that looked fine. This machine is one
# of them: everything is on the Plex server and there is no ~/Music.
#
# ⚠ HOME is redirected for these two and nowhere else in this suite: whether
# the developer has a Music folder is not something an assertion may depend on.
mkdir -p "$T/nohome" "$T/withmusic/Music"
( HOME="$T/nohome"; export HOME; PATH="$MSTUB:$PATH"; export PATH
  says "$SA" big music source ) | grep -q "no music folder"
check "a machine with no music folder says so on the Local row" $?

( HOME="$T/withmusic"; export HOME; PATH="$MSTUB:$PATH"; export PATH
  says "$SA" big music source ) | grep -q "no music folder"
[ $? != 0 ]
check "...and a machine with one does not" $?

rm -f "$BIGCONF"

# ── the two rows that used to dead-end ──────────────────────────────────────
#
# ⚠ REPORTED FROM THE SOFA: both rows opened cliamp, and cliamp appeared to
# support neither service. Both halves were true, for reasons that have nothing
# to do with each other:
#
#   YouTube Music  needs yt-dlp, which is an OPTDEPEND of the cliamp package.
#                  Without it the provider is there, is selectable, and
#                  returns nothing — cliamp's own package says so.
#   Spotify        needs a [spotify] section in cliamp's config and an account
#                  signed in. With no section there is nothing to open.
#
# So the `action` column answers "install" and "setup" rather than "browse",
# and the row says which. Both are facts about THIS machine, which is why the
# PATH below is cut down to the stubs: whether the developer happens to have
# yt-dlp installed is not something an assertion may depend on. It is the same
# rule the "no music folder" pair above redirects HOME for.
NOYT="$T/no-yt"
mkdir -p "$NOYT"
srcpath() { ( PATH="$MSTUB:$STUB"; export PATH; says "$SA" big music "$@" ); }

CLIAMPCONF="$XDG_CONFIG_HOME/cliamp/config.toml"
mkdir -p "$(dirname "$CLIAMPCONF")"
rm -f "$CLIAMPCONF"

srcpath source --rec |
    awk -F'\t' '$1 == "ytmusic" && $4 == "install" { f = 1 } END { exit !f }'
check "YouTube Music offers to install yt-dlp on a machine without it" $?

srcpath source | has "needs yt-dlp"
check "...and the row says so rather than describing the button" $?

srcpath source --rec |
    awk -F'\t' '$1 == "spotify" && $4 == "setup" { f = 1 } END { exit !f }'
check "Spotify offers to sign in when cliamp has no [spotify] section" $?

# ⚠ THE OTHER SIDE OF BOTH, and without it these assertions would pass against
# a version that answered "install" and "setup" unconditionally — which is a
# television that offers to install yt-dlp every time somebody who already has
# it presses the row.
printf '#!/bin/sh\nexit 0\n' > "$NOYT/yt-dlp"; chmod +x "$NOYT/yt-dlp"
yt() { ( PATH="$MSTUB:$STUB:$NOYT"; export PATH; says "$SA" big music "$@" ); }

# ── ⚠ AND yt-dlp WAS NEVER THE WHOLE STORY, WHICH IS THE SECOND DEAD END ─────
#
# Reported from the sofa after the install row worked: "it just says open with
# cliamp but cliamp never got setup and I don't see how".
#
# yt-dlp is what PLAYS a YouTube URL; a Google OAuth desktop client is what
# BROWSES for one, and `browse` was the second of those. cliamp v1.63.2 ships an
# EMPTY fallback credential pool (external/ytmusic/fallback.go:
# `var fallbackCredentials []oauthCreds`), so with no client_id/client_secret in
# config.toml it prints
#
#     YouTube: no credentials available (configure client_id/client_secret …)
#
# to a stderr nobody on a sofa reads, and registers no YouTube provider at all.
# Measured against the installed binary in a config directory of its own.
#
# ⚠ SO THE ROW STOPPED DEPENDING ON THAT ALTOGETHER. Since 0.1.0-29 YouTube
# Music has its own STATIONS here — yt-dlp enumerates any URL and `cliamp queue`
# takes what comes out, neither of which ever sees a credential — so the action
# is `yt` and the only thing it needs is yt-dlp. What the OAuth client still
# gates is cliamp's own search, which is what the note says.
rm -f "$CLIAMPCONF"
yt source --rec |
    awk -F'\t' '$1 == "ytmusic" && $4 == "yt" { f = 1 } END { exit !f }'
check "with yt-dlp the row is this package's own stations, not cliamp" $?

# ⚠ AND IT DOES NOT DEPEND ON CREDENTIALS, in either direction. The wizard's own
# default writes exactly this against an empty pool, and it changes nothing here
# — which is the point: stations play without an account at all.
printf '[ytmusic]\nenabled = true\n' > "$CLIAMPCONF"
yt source --rec |
    awk -F'\t' '$1 == "ytmusic" && $4 == "yt" { f = 1 } END { exit !f }'
# ⚠ NO BACKTICKS IN A CHECK LABEL. They are a command substitution inside the
# double quotes, and the arguments to `check` expand LEFT TO RIGHT — so the
# substitution runs and replaces $? before the second argument is expanded.
# Written as "`enabled = true` alone", this assertion reported the exit status
# of "enabled: command not found" (127) and failed against passing code.
check "...whatever enabled = true does or does not mean to cliamp" $?

# ⚠ THE NOTE ANSWERS "CAN I GET AT MY OWN MUSIC", which is the question
# somebody actually has. Signed out it says what signing in would buy; signed
# in it says what the row now holds. The OAuth client is a narrower thing and
# has its own row on the page rather than a warning on this one.
rm -f "$CLIAMPCONF" "$BIGCONF"
yt source | has "sign in for your own playlists"
check "...and a signed-out row says what signing in would buy" $?

printf 'yt_cookies = vivaldi\n' > "$BIGCONF"
yt source | has "your playlists and your stations"
check "...and a signed-in one says what it now holds" $?
rm -f "$BIGCONF"

# ⚠ AND THE NOTE IS PER SOURCE, NOT PER ACTION. It was keyed on the action, and
# `setup` was briefly both services — so setting up YouTube Music said "needs
# Spotify Premium".
#
# ⚠ Captured rather than piped into `grep -q`. This is the negative half, and
# `grep -q` exits the instant it matches — which closes the pipe under the
# writer and reports SIGPIPE (141) through `set -o pipefail`. The file header
# has the long version; a NEGATIVE assertion is where it bites hardest,
# because 141 is not 0 and the row would read as correct.
rm -f "$CLIAMPCONF"
ytrow=$(yt source | grep '^ytmusic')
case "$ytrow" in
    *"Spotify Premium"*) false ;;
    *) true ;;
esac
check "...and no row tells YouTube Music it needs Spotify Premium" $?

printf '[spotify]\nbitrate = 320\n' > "$CLIAMPCONF"
srcpath source --rec |
    awk -F'\t' '$1 == "spotify" && $4 == "browse" { f = 1 } END { exit !f }'
check "...and a configured Spotify opens cliamp rather than the wizard" $?
rm -f "$CLIAMPCONF"

# ⚠ `--noconfirm` BEFORE THE VERB. synpkg stops parsing global options at the
# first non-option argument, so `install --noconfirm` is a flag it never sees —
# and a front-end that cannot answer a prompt then authenticates through
# polkit, declines itself, and installs nothing while reporting success. It has
# bitten this project twice. The permissive shape `synpkg( --[a-z]+)* install`
# is what let it through last time, so this pins the literal.
grep -q '"synpkg --noconfirm install yt-dlp"' src/big.c
check "the installer passes --noconfirm BEFORE the verb" $?

# Nothing is installed twice, and nothing opens a terminal to say so.
( PATH="$MSTUB:$STUB:$NOYT"; export PATH
  says "$SA" big music install ytmusic ) | grep -q "already installed"
check "installing what is already there is a sentence, not a terminal" $?

# ── and the terminal it opens is not always the television's ────────────────
#
# ⚠ THESE VERBS HAVE A SECOND CALLER NOW: the desktop music widget's source
# picker, which reaches `setup`, `install` and `browse` from a 268px card on the
# wallpaper. `fill` is the television's rule — an application launched from four
# metres away takes the whole display — and applied unconditionally it means
# pressing Sign in on that card throws a fullscreen terminal over whatever
# somebody was working in.
#
# Source-only, and it has to be: `fill` decides whether a fullscreen_toggle is
# dispatched at a compositor, which is the one thing this suite is forbidden to
# do (see the seatbelt above). big_running() is the shell's presence rather than
# a guess at it — the lock is held for exactly as long as that process lives.
[ "$(grep -c 'return spawn_wait(argv, big_running(NULL), false);' src/big.c)" = 2 ]
check "an errand fills the screen only when the television is up" $?

# ⚠ SCOPED TO THE TWO FUNCTIONS. `big web --wait` fullscreens unconditionally
# and should: a headline is a browser window opened from the television and
# nothing about a web page fills a screen by itself. A file-wide grep here
# fails on that one and says nothing about music.
mfill=$(sed -n '/^static int big_music_browse(void)/,/^}/p;/^static int term_run_and_hold(/,/^}/p' src/big.c |
        grep -c 'spawn_wait(argv, true, false)')
[ "$mfill" = 0 ]
check "...and neither music path fullscreens unconditionally any more" $?

# ⚠ THE ROWS ARE THE DESKTOP'S TOO, so a note that names one surface is wrong on
# the other. This said "type a search on the television" to somebody looking at
# their wallpaper; what actually happens is the same on both — a terminal comes
# up to type into, which on the television is the one the on-screen keyboard is
# pointed at.
#
# ⚠ Captured rather than piped into `grep -q`, for the reason spelled out on the
# Spotify Premium check above: this is the NEGATIVE half, `grep -q` exits the
# instant it matches, and the SIGPIPE that kills awk comes back through
# `set -o pipefail` as 141 — which is not 0, so the row would report a pass at
# exactly the moment the note was wrong again.
findnote=$(srcpath yt --rec | awk -F'\t' '$1 == "find" { print $3 }')
case "$findnote" in
    ""|*television*) false ;;
    *) true ;;
esac
check "the Search row's note is true on a desktop as well as a sofa" $?

# ── quitting has to let go of the music ─────────────────────────────────────
#
# ⚠ REPORTED: Quit, and the music is still playing. The player this interface
# starts is HEADLESS — `script -qfc cliamp …`, a TUI on a pty with no terminal
# — so it has no window, it is not a toplevel for the dock or the switcher to
# reach, and synui's bar has no MPRIS controls. Quitting and leaving it running
# is music with NO WAY TO STOP IT short of opening a terminal.
#
# ⚠ AND "ALWAYS STOP IT" IS THE WRONG FIX. A cliamp somebody has open in a
# terminal is not headless and not ours — big screen mode drives it happily
# over the same socket while it is up, and ending it on the way out would be
# this launcher reaching over somebody's music. So the marker records the one
# thing that tells them apart: whether THIS package started it.
MARK="$XDG_RUNTIME_DIR/syn-arcade-music.ours"
rm -f "$MARK"

# Nothing started, nothing to do — and the overwhelmingly common Quit, so it
# must be silent and successful rather than an error nobody caused.
out=$( PATH="$MSTUB:$STUB"; export PATH; says "$SA" big music release )
[ -z "$out" ]
check "releasing with no player of ours says nothing" $?

( PATH="$MSTUB:$STUB"; export PATH; "$SA" big music release >/dev/null 2>&1 )
[ "$?" = 0 ]
check "...and is a success, not a failure nobody caused" $?

# ⚠ NOT OURS, SO NOT TOUCHED. The stub answers as a live player throughout;
# the only thing that changes is the marker.
CLIAMP_STATE=playing
( PATH="$MSTUB:$STUB"; export PATH
  CLIAMP_LOG="$T/release.log"; export CLIAMP_LOG
  : > "$CLIAMP_LOG"
  "$SA" big music release >/dev/null 2>&1 )
relmsg=$(cat "$T/release.log" 2>/dev/null)
case "$relmsg" in *stop*) false ;; *) true ;; esac
check "a player this package did not start is left alone" $?

# ⚠ THE MARKER IS WRITTEN WHERE THE LOCK LIVES — a tmpfs logind wipes at
# logout — so a claim cannot outlive the session that made it and be believed
# by the next one.
grep -q 'syn-arcade-music.ours' src/big.c
check "the claim is recorded in the runtime directory" $?

grep -q 'XDG_RUNTIME_DIR' src/big.c
check "...which logind wipes, so it cannot outlive the session" $?

# ⚠ MARKED ONLY WHERE ONE WAS ACTUALLY STARTED. music_ensure_running() returns
# early when a player is already up, and that path must NOT claim it.
awk '/static bool music_ensure_running/,/^}/' src/big.c |
    awk '/music_socket_live\(\)\)/ { early = 1 } /music_mark_ours\(true\)/ { exit !early }'
check "...and claimed only where a player was really started" $?

# The claim is dropped with the player, on every path that ends one.
awk '/static bool music_stop_player/,/^}/' src/big.c | has 'music_mark_ours(false)'
check "...and dropped again when it stops" $?

# ⚠ AND WHEN THERE IS NOTHING LEFT TO CLAIM. A player that already went — quit
# from its own interface, or crashed — would otherwise leave a marker that
# outlived it and made every later Quit report a failure it could do nothing
# about.
awk '/static int big_music_release/,/^}/' src/big.c |
    grep -q 'music_mark_ours(false)'
check "...and when the player has already gone by itself" $?

# The shell half: every way out goes through one function.
grep -q 'function quitNow' "$BIGQML"
check "the shell has one way out, not three" $?

[ "$(grep -c 'shell.quitNow()' "$BIGQML")" -ge 3 ]
check "...used by the tile, the keyboard and \`big stop\` alike" $?

grep -q '"big", "music", "release"' "$BIGQML"
check "...and it lets go of the music before it goes" $?

# ⚠ A TIMER AS WELL AS onExited. Quitting is the one action with no way back,
# and a Quit that hung on a music player refusing to die would be a television
# nobody can get out of.
awk '/id: quitTimer/,/^    }/' "$BIGQML" | has 'Qt.quit()'
check "...and cannot hang on a player that refuses to stop" $?

# ── YouTube Music stations, which is what "plays like the radio does" means ──
#
# ⚠ THE POINT OF THE WHOLE THING: no account, no OAuth client, no cliamp TUI.
# yt-dlp enumerates a URL and `cliamp queue` takes what comes out — measured on
# this machine with no credentials anywhere, a search returned titles and watch
# URLs and a queued URL took the player's total from 11 to 12.
#
# ⚠ yt-dlp IS STUBBED HERE, and it is not only about the network being off. A
# suite that really searched YouTube would be asserting on somebody else's
# search results, which change hourly. The stub answers in yt-dlp's own shape:
# one line per --print flag, title then URL.
YTB="$T/yt-bin"
mkdir -p "$YTB"
cat > "$YTB/yt-dlp" <<'STUB'
#!/bin/sh
# The spec is the last argument. Two lines per result, title then URL, exactly
# as `--print "%(title)s" --print "%(webpage_url)s"` prints them.
for last in "$@"; do :; done
case "$last" in
    ytsearch*) printf 'Found One\nhttps://www.youtube.com/watch?v=aaaaaaaaaaa\n'
               printf 'Found Two\nhttps://www.youtube.com/watch?v=bbbbbbbbbbb\n' ;;
    *list=RD*) printf 'Seed Track\nhttps://www.youtube.com/watch?v=ccccccccccc\n'
               printf 'Second Of The Mix\nhttps://www.youtube.com/watch?v=ddddddddddd\n' ;;
    *)         printf 'One Single Track\nhttps://www.youtube.com/watch?v=eeeeeeeeeee\n' ;;
esac
exit 0
STUB
chmod +x "$YTB/yt-dlp"

# ⚠ SYN_ARCADE_NO_NET IS TURNED OFF FOR THESE, and only these. yt_enumerate
# refuses to run at all while it is set — the same seatbelt the news and the
# server discovery have — so a station test needs it off, and the stub above is
# what makes that safe.
ytrun() { ( PATH="$YTB:$MSTUB:$STUB"; export PATH
            SYN_ARCADE_NO_NET=0; export SYN_ARCADE_NO_NET
            says "$SA" big music "$@" ); }

YTLIST="$XDG_CONFIG_HOME/syn-arcade/ytmusic.list"
rm -f "$YTLIST"

# An empty list is not an error, it is a machine nobody has added one to — and
# it has to say HOW, because an empty panel on a television is a broken button.
ytrun yt | has 'big music yt add'
check "an empty station list says how to add one" $?

# ⚠ THE NAME IS RESOLVED, NOT TYPED. A station you have to name is one nobody
# adds, and yt-dlp already knows what the thing is called.
ytrun yt add "https://www.youtube.com/watch?v=eeeeeeeeeee" >/dev/null 2>&1
grep -q 'One Single Track' "$YTLIST"
check "adding a station resolves its name from yt-dlp" $?

grep -q '^https://www.youtube.com/watch?v=eeeeeeeeeee	' "$YTLIST"
check "...and stores the URL first, tab, then the name" $?

# ⚠ A MIX IS NAMED AFTER ITS SEED, so without this a track and the endless
# station it seeds are two rows with the same name and very different
# behaviour. `list=RD…` is YouTube's own marker for the generated station.
ytrun yt add "https://www.youtube.com/watch?v=ccccccccccc&list=RDccccccccccc" >/dev/null 2>&1
grep -q 'Seed Track — mix' "$YTLIST"
check "...and a mix is not given the same name as its seed track" $?

ytrun yt --rec | awk -F'\t' '$1 == "2" { f = 1 } END { exit !f }'
check "stations are numbered by their position in the file" $?

# ⚠ A URL IS NOT A STATION ID, and both have to work: a search result's id IS
# its URL, so the shell plays one with the same command it plays a station
# with. This is the refusal for neither.
( PATH="$YTB:$MSTUB:$STUB"; export PATH; "$SA" big music yt 99 >/dev/null 2>&1 )
[ "$?" = 2 ]
check "a station number nobody has is a usage error, not silence" $?

# ⚠ COMMENTS AND BLANK LINES SKIPPED — it is a file people edit by hand, and a
# commented-out station that still counted would renumber every row under it.
printf '# a note\n\nhttps://www.youtube.com/watch?v=fffffffffff\tKept\n' > "$YTLIST"
ytrun yt --rec | awk -F'\t' '$1 == "1" && $2 == "Kept" { f = 1 } END { exit !f }'
check "a hand-edited list may have comments and blank lines" $?

# The refusal that keeps the network off in the rest of the suite.
#
# ⚠ Captured, not piped into `grep -q`. Negative assertion, same trap as the
# note above: `grep -q` closes the pipe the moment it matches and the writer
# dies of SIGPIPE, which `set -o pipefail` reports as 141 — and 141 is not 0,
# so a MATCH would read here as "it refused".
ytoff=$( PATH="$YTB:$MSTUB:$STUB"; export PATH
         says "$SA" big music yt search "anything" )
case "$ytoff" in
    *"Found One"*) false ;;
    *) true ;;
esac
check "a search refuses while SYN_ARCADE_NO_NET is set" $?

ytrun yt search "anything" --rec |
    awk -F'\t' '$2 == "Found One" { f = 1 } END { exit !f }'
check "...and finds things with it off, keyed by URL so playing one is one path" $?

ytrun yt search 2>&1 | has "takes something to search for"
check "...and a search with nothing to search for says so" $?

# ── ⚠ THE FRONT OF THE QUEUE IS ASKED ABOUT BEFORE IT IS PLAYED ─────────────
#
# `--flat-playlist` is what makes reading a station fast enough to be a button,
# and its entries carry a real title, duration and view count for videos that
# answer "Video unavailable" the moment anything plays them. `%(availability)s`
# is null for EVERY entry there, so there is nothing in that listing to filter
# on — measured on velle's own playlist, where entry 0 is dead and entries 1
# and 2 are fine and all three look identical on the way in.
#
# Asked about ONE video, yt-dlp answers in about a second and run_capture()
# turns its non-zero exit into NULL:
#
#     yt-dlp --simulate --print "%(id)s" <dead>  → rc 1
#     yt-dlp --simulate --print "%(id)s" <good>  → rc 0
grep -q '"--simulate"' src/big.c
check "the head of a station is asked whether it will play" $?

# ⚠ THE FUNCTION IS CUT OUT INTO A FILE, not piped into `grep -q`. That is the
# SIGPIPE trap this file has already paid for twice: grep exits the instant it
# matches, awk dies of SIGPIPE, and `set -o pipefail` reports 141 — so four
# assertions went red against source that matched perfectly. The ones whose
# pattern happens to sit near the END of the extract get away with it, which is
# what makes it look like only some of them are broken.
YTPLAY="$T/yt_play.c"
awk '/static int yt_play/,/^}/' src/big.c > "$YTPLAY"
awk '/static bool yt_playable/,/^}/' src/big.c > "$T/yt_playable.c"

grep -q 'while (head < n && head < YT_VERIFY && !yt_playable' "$YTPLAY"
check "...walking past the ones that say no" $?

# ⚠ FROM `head`, NOT FROM ZERO. Queueing the dead ones anyway would put one
# back at position 0 and undo the whole check.
grep -q 'for (int i = head; i < n; i++)' "$YTPLAY"
check "...and the queue starts at the first one that said yes" $?

grep -q 'if (i == head)' "$YTPLAY"
check "...which is also the one that gets the first toggle" $?

# ⚠ BOUNDED IN BOTH DIRECTIONS: at most YT_VERIFY questions, and never past the
# end of the list — `head < n` is what stops an all-dead playlist queueing
# nothing at all and reporting success.
grep -q 'if (head >= n)' "$YTPLAY"
check "...and a list where nothing will play says so" $?

# ⚠ WITH THE SESSION. A members-only track is playable for the signed-in person
# and not for anybody else; asking without the cookies would drop tracks that
# would have played perfectly.
grep -q 'cookies-from-browser' "$T/yt_playable.c"
check "...asked as the signed-in person, where there is a session" $?

# ── signing in, which is a BROWSER and not a Google Cloud project ───────────
#
# ⚠ "Log in to use my own playlists" has two possible answers and they are not
# the same amount of work. An OAuth client unlocks SEARCH INSIDE CLIAMP'S TUI
# and takes a Google Cloud project; browser cookies unlock somebody's own
# PRIVATE playlists and Liked Music, which is what puts their music on the
# television with a d-pad. Both are offered; this is the one the row leads with.
#
# ⚠ THE STUB ANSWERS 401 WITHOUT COOKIES, exactly as YouTube does — measured
# here against the real thing, Vivaldi with no session gave 7 cookies, all
# decrypted, and a 401. A stub that answered the same either way could not tell
# a working sign-in from a broken one.
# ⚠ THE BROWSER NAME IS WHAT DECIDES, not merely whether the flag was passed.
# `yt login` WRITES the setting before it checks it — so the check does carry
# `--cookies-from-browser <that browser>`, and a stub keyed on the flag alone
# answers "signed in" for every browser on earth. It did, and both refusal
# assertions passed against a sign-in that could never fail.
# Here only `vivaldi` has a session, which is the shape of a real machine.
cat > "$YTB/yt-dlp" <<'STUB'
#!/bin/sh
browser=""
prev=""
for a in "$@"; do
    [ "$prev" = "--cookies-from-browser" ] && browser="$a"
    prev="$a"
done
for last in "$@"; do :; done
case "$last" in
    */feed/playlists)
        [ "$browser" = vivaldi ] || exit 1
        printf 'Late Night Drive\nhttps://www.youtube.com/playlist?list=PLaaa\n'
        printf 'Liked Music\nhttps://www.youtube.com/playlist?list=LM\n' ;;
    ytsearch*)
        printf 'Found One\nhttps://www.youtube.com/watch?v=aaaaaaaaaaa\n'
        printf 'Found Two\nhttps://www.youtube.com/watch?v=bbbbbbbbbbb\n' ;;
    *list=RD*)
        printf 'Seed Track\nhttps://www.youtube.com/watch?v=ccccccccccc\n' ;;
    *)  printf 'One Single Track\nhttps://www.youtube.com/watch?v=eeeeeeeeeee\n' ;;
esac
exit 0
STUB
chmod +x "$YTB/yt-dlp"
rm -f "$BIGCONF"

# ⚠ A SIGNED-OUT MACHINE GETS A SENTENCE, NOT AN EMPTY LIST. "You have no
# playlists" is a lie somebody would reasonably act on by making some.
ytrun yt mine 2>&1 | has "not signed in"
check "reading playlists while signed out says so, rather than showing none" $?

# ⚠ VERIFIED, NOT MERELY WRITTEN DOWN — every failure here is silent by nature,
# and yt-dlp says so on a stderr a television never shows.
ytrun yt login vivaldi | has "Late Night Drive"
check "signing in reports what the session can actually see" $?

grep -q '^yt_cookies = vivaldi$' "$BIGCONF"
check "...and remembers the browser" $?

ytrun yt mine --rec |
    awk -F'\t' '$2 == "Liked Music" { f = 1 } END { exit !f }'
check "...so a signed-in machine can list its own playlists" $?

# ⚠ THE ID IS THE PLAYLIST URL, so playing one is the same command as playing a
# station — there is no second play path to keep in step.
ytrun yt mine --rec | has 'playlist%3Flist%3DLM'
check "...keyed by URL, so playing one is the station path" $?

# ⚠ THE COOKIES GO ON EVERY ENUMERATION, not only the library one: a private
# playlist is private at PLAY time too, and without them a station that lists
# perfectly resolves to nothing when it is pressed.
grep -q '"--cookies-from-browser", browser' src/big.c
check "...and every enumeration carries the session, not just that one" $?

# The refusal, and ⚠ THE SETTING IS TAKEN BACK OUT. Leaving it would put
# --cookies-from-browser on every enumeration from then on and keep answering
# "no playlists" as though the account were empty.
( PATH="$YTB:$MSTUB:$STUB"; export PATH
  SYN_ARCADE_NO_NET=0; export SYN_ARCADE_NO_NET
  "$SA" big music yt login chromium >/dev/null 2>&1 )
[ "$?" != 0 ]
check "a browser with no YouTube session is refused" $?

ytnow=$(cat "$BIGCONF" 2>/dev/null)
case "$ytnow" in *"yt_cookies = chromium"*) false ;; *) true ;; esac
check "...and the machine is not left claiming to be signed in" $?

# A browser yt-dlp cannot read is a usage error before anything is written.
( PATH="$YTB:$MSTUB:$STUB"; export PATH
  "$SA" big music yt login netscape >/dev/null 2>&1 )
[ "$?" = 2 ]
check "...and a browser yt-dlp cannot read is refused outright" $?

# ── ⚠ THE PROMPT TAKES A NUMBER, because the answer is typed with a d-pad ────
#
# Reported the first time it was used: the list was printed unnumbered and the
# prompt asked for a name, so somebody read it, typed `1`, and got "yt-dlp
# cannot read cookies from '1'". The search picker two rows away takes a
# number, every other list on this system takes a number, and spelling out
# `vivaldi` on an on-screen keyboard is the errand the terminal trick exists to
# keep short.
#
# ⚠ DRIVEN ON A PTY, and it has to be: the prompt only appears when there IS a
# terminal (see can_be_asked), so a piped stdin makes the verb go off and OPEN
# one instead of answering. `script` is enough here — this is a line at a time,
# not a full-screen interface.
#
# ⚠ `script` IS RESOLVED BEFORE THE PATH IS CUT DOWN. The stub PATH is a
# REPLACEMENT — three directories, so that which yt-dlp and which cliamp get
# found is decided by this file — and /usr/bin is not one of them. Calling
# `script` by name inside it is "command not found", the pipeline produces
# nothing, and all four of these assertions fail against working code. They
# did.
#
# ⚠ AND THE ANSWER GOES TO A FILE, NOT INTO `grep -q`. This is the SIGPIPE
# trap the file header describes, and it bites hardest exactly here: `grep -q`
# exits the instant it matches, closing the pipe under a writer that is still
# going, and `set -o pipefail` reports the writer's 141. The three assertions
# whose text appears at the END of the output got away with it; the one that
# matches the FOURTH LINE of a nine-line list failed every time, against code
# that was correct. Greping a FILE has no writer to kill.
SCRIPT=$(command -v script)
YTASK="$T/ytask.out"
rm -f "$BIGCONF"
ytask() { ( PATH="$YTB:$MSTUB:$STUB"; export PATH
            SYN_ARCADE_NO_NET=0; export SYN_ARCADE_NO_NET
            printf '%s\n' "$1" |
            "$SCRIPT" -qfc "$SA big music yt login" /dev/null > "$YTASK" 2>&1 )
          return 0; }

ytask 2
grep -q "signed in with vivaldi" "$YTASK"
check "the browser prompt takes the NUMBER beside the name" $?

# ⚠ INSTALLED FIRST, so the numbers mean something on a machine with one
# browser rather than pointing at wherever it sat in a fixed list.
#
# ⚠ CHROMIUM, and the choice is the assertion. It is THIRD in big.c's fixed
# list, so a version that simply printed that list in order would put it at 3
# and this would fail — which is what makes the reordering the thing being
# tested rather than the printing.
#
# ⚠ And it is stubbed HERE because `have()` shells out with the PATH it was
# given, which for these is the cut-down stub PATH. On the real machine firefox
# and vivaldi are installed; inside this suite NOTHING is, so an assertion
# looking for any "installed here" at all failed against correct code.
printf '#!/bin/sh\nexit 0\n' > "$YTB/chromium"; chmod +x "$YTB/chromium"
ytask ""
grep -qE '^ *1 +chromium +· installed here' "$YTASK"
check "...numbered with what is installed at the top" $?

# The other side: something NOT installed keeps its place further down, so the
# list is still the whole list rather than only what is here.
#
# ⚠ `[[:space:]]*$` RATHER THAN `$`. This came off a PTY, so every line ends
# CR-LF and an anchor placed straight after the word cannot match — the CR is
# still to come. Written as `firefox$` this failed against a list that was
# exactly right.
grep -qE '^ *[2-9] +firefox[[:space:]]*$' "$YTASK"
check "...with the rest still offered below it" $?
rm -f "$YTB/chromium"

# A name still works, because from a shell prompt that is what somebody types.
rm -f "$BIGCONF"
ytask vivaldi
grep -q "signed in with vivaldi" "$YTASK"
check "...and a name still works, for whoever is at a keyboard" $?

# ⚠ ALL digits, not "starts with a digit" — `chrome` must not be read as a
# number, and neither must `2fast`.
grep -q 'numeric = \*p >= .0. && \*p <= .9.' src/big.c
check "...and a name that begins with a digit is still a name" $?

rm -f "$BIGCONF"
ytask 99
grep -q "there is no 99 on that list" "$YTASK"
check "...while a number nobody offered is a refusal, not a browser called 99" $?
rm -f "$BIGCONF"

# ── the page is not all stations ────────────────────────────────────────────
#
# ⚠ WHICH ERRANDS EXIST IS big.c's ANSWER. Whether this machine has a YouTube
# session, and whether cliamp has an OAuth client, are facts about the machine —
# and a copy of that reasoning in QML is how the sign-in route vanished from the
# television in 0.1.0-29: the C answer changed and nothing in the shell noticed.
rm -f "$BIGCONF" "$CLIAMPCONF"
ytrun yt --rec |
    awk -F'\t' '$1 == "find" && $4 == "action" { f = 1 } END { exit !f }'
check "the stations page offers Search" $?

ytrun yt --rec |
    awk -F'\t' '$1 == "login" && $4 == "action" { f = 1 } END { exit !f }'
check "...Sign in, while there is no session" $?

# ⚠ THE OAUTH ROUTE, RESTORED. It was the row's action in 0.1.0-28 and 0.1.0-29
# replaced that action with this page, leaving `cliamp setup` reachable from
# nowhere on the television. It is a row on the page now.
ytrun yt --rec |
    awk -F'\t' '$1 == "setup" && $4 == "action" { f = 1 } END { exit !f }'
check "...and cliamp's own search, which is the OAuth route" $?

printf '[ytmusic]\nclient_id     = "1234.apps.googleusercontent.com"\nclient_secret = "s3cr3t"\n' \
    > "$CLIAMPCONF"
ytsetup=$(ytrun yt --rec | awk -F'\t' '$1 == "setup"')
[ -z "$ytsetup" ]
check "...offered only while it is missing" $?

# ⚠ A KEY THAT IS PRESENT AND EMPTY IS NOT A CREDENTIAL. Cheap to get wrong —
# a check for the key rather than for its value passes on this file.
printf '[ytmusic]\nclient_id     = ""\nclient_secret = ""\n' > "$CLIAMPCONF"
ytrun yt --rec |
    awk -F'\t' '$1 == "setup" { f = 1 } END { exit !f }'
check "...and empty keys are not credentials either" $?

# ⚠ `[yt]` and `[youtube]` are the SAME SECTION to cliamp (config.go normalises
# all three), so a machine set up under either name must be read as set up.
printf '[youtube]\nclient_id     = "1234.apps.googleusercontent.com"\nclient_secret = "s3cr3t"\n' \
    > "$CLIAMPCONF"
ytsetup=$(ytrun yt --rec | awk -F'\t' '$1 == "setup"')
[ -z "$ytsetup" ]
check "...under [ytmusic], [youtube] or [yt], as cliamp reads them" $?
rm -f "$CLIAMPCONF"

# Signed in, the Sign in row becomes the playlists row.
printf 'yt_cookies = vivaldi\n' > "$BIGCONF"
ytrun yt --rec |
    awk -F'\t' '$1 == "mine" && $4 == "action" { f = 1 } END { exit !f }'
check "signed in, the page offers Your playlists instead" $?

ytlogin=$(ytrun yt --rec | awk -F'\t' '$1 == "login"')
[ -z "$ytlogin" ]
check "...and stops asking for a sign-in it already has" $?
rm -f "$BIGCONF"

# ⚠ THE STATIONS ARE STILL DISTINGUISHABLE FROM THE ERRANDS, or the shell would
# try to play "Search…" as a URL.
printf 'https://www.youtube.com/watch?v=fffffffffff\tKept\n' > "$YTLIST"
ytrun yt --rec |
    awk -F'\t' '$2 == "Kept" && $4 == "station" { f = 1 } END { exit !f }'
check "...and a station still says it is one" $?

# ── typing, which is a TERMINAL and not a text field ────────────────────────
#
# ⚠ THE ON-SCREEN KEYBOARD CANNOT TYPE INTO THE SHELL. It types through wtype
# into whatever holds keyboard focus, and the shell's surface deliberately holds
# none — a menu that grabbed the keyboard to draw a keyboard would type into
# itself. So the two verbs that read stdin get a TERMINAL when there is no tty,
# which is the same mechanism the install and sign-in rows already use.
#
# ⚠ Without this they are launched by the television as a plain process with no
# tty at all: fgets returns on EOF immediately and the command flashes and exits
# having done nothing, which is exactly the shape of a dead button.
grep -q 'static bool can_be_asked' src/big.c
check "a verb that reads stdin asks whether there is anybody to ask" $?

grep -q 'term_run_and_hold("syn-arcade big music yt find")' src/big.c
check "...and gets a terminal on the television when there is not" $?

grep -q 'term_run_and_hold("syn-arcade big music yt login")' src/big.c
check "...for signing in as well as for searching" $?

# ⚠ It cannot loop: the command inside the terminal HAS a tty.
grep -q 'isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)' src/big.c
check "...and the terminal it opens takes the interactive branch" $?

# ── the key a queued track is remembered under ──────────────────────────────
#
# ⚠ `?v=` IS THE IDENTITY ON YOUTUBE, and the rule used to be "strip everything
# from the ?" — which is right for Plex, where the query is a token nothing here
# may write into a cache, and wrong for YouTube, where it keyed every song on
# the site to `https://www.youtube.com/watch`. Measured: the first station that
# played showed every track called "watch".
grep -q 'static void music_key' src/big.c
check "there is exactly one home for how a queued track is keyed" $?

grep -q 'music_key(raw, keyed, sizeof(keyed))' src/big.c
check "...and the reader goes through it" $?

# ⚠ AND THE BEHAVIOUR, not only the shape. Both halves of the regression are
# here: the KEY has to keep `?v=`, and the lookup has to be tried even though
# cliamp DID give the track a title of its own ("watch", off the end of the
# URL). Either half alone leaves the television naming every song `watch`.
TITLES="$XDG_CACHE_HOME/syn-arcade/music-titles.rec"
mkdir -p "$(dirname "$TITLES")"
printf 'https://www.youtube.com/watch?v=zzzzzzzzzzz\tThe Real Song Name\n' > "$TITLES"
( CLIAMP_TRACK="https://www.youtube.com/watch?v=zzzzzzzzzzz&list=RDzzzzzzzzzzz"
  CLIAMP_TITLE="watch"
  export CLIAMP_TRACK CLIAMP_TITLE
  music status ) | has 'The Real Song Name'
check "a queued YouTube track is named from what queued it, not 'watch'" $?

# ⚠ THE OTHER SIDE: a track nothing here queued keeps cliamp's own answer. A
# radio station has a real name and must not be replaced by a cache miss.
( CLIAMP_TRACK="http://radio.cliamp.stream/lofi/stream"
  CLIAMP_TITLE="Lofi Stream"
  export CLIAMP_TRACK CLIAMP_TITLE
  music status ) | has 'Lofi Stream'
check "...and a station cliamp DOES know keeps the name it gave" $?
rm -f "$TITLES"


# ── the PICTURE, which cliamp does not publish either ───────────────────────
#
# ⚠ MEASURED: a playing YouTube track's MPRIS metadata has FOUR keys —
# xesam:title, xesam:url, mpris:length, mpris:trackid. There is no
# `mpris:artUrl` at all, so the widget's cover tile had nothing to load and
# drew its placeholder for every song. The same video played through Firefox
# filled the tile in, because Firefox publishes a thumbnail — which is what
# made a missing field read as a broken widget.
#
# It is DERIVED, not fetched: a YouTube thumbnail is a pure function of the
# video id, and the key already is the video id. yt-dlp cannot supply one
# anyway — under `--flat-playlist`, `%(thumbnail)s` prints `NA` exactly as
# `%(url)s` does.
art_of() {
    ( CLIAMP_TRACK="$1"; CLIAMP_TITLE="${2:-watch}"
      export CLIAMP_TRACK CLIAMP_TITLE
      music status --rec ) | awk -F'\t' 'NR==2 {print $4}'
}

music status --rec | head -1 | has 'art'
check "the records name a column for the track's picture" $?

[ "$(art_of 'https://www.youtube.com/watch?v=zzzzzzzzzzz&list=RDzzzzzzzzzzz')" \
    = 'https://i.ytimg.com/vi/zzzzzzzzzzz/mqdefault.jpg' ]
check "a YouTube track's cover is derived from its video id" $?

# ⚠ `mqdefault` AND NOTHING ELSE, and this assertion is the measurement:
# maxresdefault 404s on older videos (an Image that sits at Error for ever,
# showing nothing — the very bug being fixed), and hqdefault is 4:3 with BLACK
# BARS baked in, which a square cropping tile would crop INTO the picture.
# (Asserted on the URL that is BUILT, not on the prose above it — the comment
# has to be free to name the sizes it rejected and why.)
[ "$(grep -c 'i\.ytimg\.com/vi/' src/big.c)" = 1 ] &&
    grep -q 'i\.ytimg\.com/vi/%s/mqdefault\.jpg' src/big.c
check "...at the one size that is always present and 16:9" $?

# ⚠ THE OTHER SHAPES ANSWER NOTHING, and an empty column is a real answer. A
# Plex stream's art would need the token that must never be written down here,
# and a local file's lives in tags nothing on this path reads. Both keep the
# placeholder the tile has always drawn.
[ -z "$(art_of 'http://192.168.1.50:32400/library/parts/1/2/f.flac?X-Plex-Token=SECRETVALUE')" ]
check "a Plex stream is given no picture rather than a guessed one" $?

( CLIAMP_TRACK='http://192.168.1.50:32400/library/parts/1/2/f.flac?X-Plex-Token=SECRETVALUE'
  export CLIAMP_TRACK; music status --rec ) | has SECRETVALUE
[ $? != 0 ]
check "...and the new column leaks no more of a token than the old ones" $?

[ -z "$(art_of 'http://radio.cliamp.stream/lofi/stream' 'Lofi Stream')" ]
check "...and a radio station is given none either" $?

# ⚠ THE ID IS VALIDATED. This string is printed as a record and handed to
# another program to fetch, and the key it comes from was built out of somebody
# else's URL. Anything outside [A-Za-z0-9_-] means the shape assumed here does
# not hold, and no picture is the honest answer.
[ -z "$(art_of 'https://www.youtube.com/watch?v=not an id!')" ]
check "...nor is a malformed video id turned into a URL to chase" $?
[ "$(grep -c 'music_key(' src/big.c)" -ge 4 ]
check "...as do the writers, rather than spelling the rule out again" $?

# ⚠ AND THE PLEX RULE SURVIVED IT. The token must still never reach the cache.
grep -q 'X-Plex-Token' src/big.c && ! grep -q 'X-Plex-Token=%s".*titles' src/big.c
check "...and a Plex token still never reaches the titles cache" $?

# ── the Music tile has to have something to PLAY ────────────────────────────
#
# ⚠ REPORTED AFTER 0.1.0-33: music could not be started at all any more. Two
# facts that are harmless apart:
#
#   · THE QUEUE DOES NOT SURVIVE THE PLAYER. `--provider` is a start-up flag
#     and what it preloads is all a fresh player has — eleven stations on
#     radio, NOTHING on ytmusic, plex or local, whose queues this file fills a
#     track at a time over the socket.
#   · SINCE 0.1.0-33 THE PLAYER DOES NOT SURVIVE QUIT, because a headless
#     player left running is music with no way to stop it.
#
# Together: press Quit, press Music, and the tile starts a bare player, sends
# `toggle` to an empty queue, and plays silence. It looked like a working
# button on a broken machine. Measured on this one — `big music play` left
# cliamp answering `"state":"stopped","index":-1` with no `total` at all.
#
# So whatever fills a queue writes down what filled it, and the tile puts it
# back.
LAST="$XDG_CACHE_HOME/syn-arcade/music-last.rec"

# ⚠ `script` IS STUBBED, AND ONLY HERE. music_ensure_running() starts the
# player through `script -qfc cliamp… /dev/null` — a pty with no window, see
# the note on --daemon above — and the runner PATHs in this file are
# REPLACEMENTS with no /usr/bin in them. Without this the start is "command
# not found", every replay below returns "cliamp did not come up", and the
# assertions fail against working code. It is kept out of $MSTUB so that the
# sections above, which assert that a player is NOT started, keep testing that
# against the same missing-binary conditions they were written under.
LSTUB="$T/last-bin"
mkdir -p "$LSTUB"
cat > "$LSTUB/script" <<'EOF'
#!/bin/sh
# The pty is what the real one is for, and a suite has no use for it. What
# matters is that the command inside really runs, so the cliamp stub sees
# --provider and marks the player up — a stub that could not come up would
# make music_ensure_running() give up and every caller bail out BEFORE the
# thing under test. That has cost this file four assertions before.
#
# ⚠ `eval`, NOT `sh -c`. The runner PATH here is a REPLACEMENT of four
# directories and /bin is not one of them, so `sh` by name is not found, the
# command inside never runs, and the player never comes up — which reads as
# "cliamp did not come up" and fails every assertion below against working
# code. It did. The shebang works because it is an absolute path; nothing
# inside may be.
cmd=""
while [ $# -gt 0 ]; do
    case "$1" in
        -*c) cmd="$2"; shift 2 ;;
        *)   shift ;;
    esac
done
[ -n "$cmd" ] && eval "$cmd"
exit 0
EOF
chmod +x "$LSTUB/script"

lastrun() { ( PATH="$LSTUB:$YTB:$MSTUB:$STUB"; export PATH
              SYN_ARCADE_NO_NET=0; export SYN_ARCADE_NO_NET
              "$SA" big music "$@" ); }

printf 'music_source = ytmusic\n' > "$BIGCONF"
rm -f "$LAST" "$MARK"
: > "$CLIAMP_LOG"

# Playing a station is what makes the record — and it is written where the
# queue really filled, not where the press arrived.
lastrun yt "https://www.youtube.com/watch?v=eeeeeeeeeee" >/dev/null 2>&1
grep -q 'ytmusic' "$LAST"
check "playing a station writes down what was playing" $?

# ⚠ THE STATION, NOT THE TRACKS IT ENUMERATED TO. A `list=RD…` mix answers
# differently every time it is asked — that is what a mix IS — so remembering
# the tracks would resume a station that no longer exists.
grep -q 'watch%3Fv%3Deeeeeeeeeee' "$LAST"
check "...as the station that was asked for, not the tracks it resolved to" $?

# THE BUG ITSELF: player gone, queue gone with it, and the tile has to put the
# music back rather than start a silent player.
: > "$CLIAMP_LOG"; rm -f "$CLIAMP_UP" "$MARK"
( CLIAMP_STATE=off; export CLIAMP_STATE; lastrun play ) >/dev/null 2>&1
grep -q 'cliamp queue https://www.youtube.com/watch?v=eeeeeeeeeee' "$CLIAMP_LOG"
check "pressing Music with no player puts the last station back on" $?

# ⚠ BOTH HALVES, and they fail separately — this is the older lesson in this
# file. A queue with no `toggle` is a television that says it is playing and is
# silent; a `toggle` with no queue is the bug being fixed.
grep -q 'cliamp toggle' "$CLIAMP_LOG"
check "...and starts it, rather than filling a queue nobody plays" $?

# ⚠ THE DISCRIMINATING ONE. A player that is up WITH A QUEUE must be resumed,
# not reloaded: reloading would restart the station from its first track every
# time somebody pressed Music after a pause, which is a worse bug than the one
# being fixed and would look exactly like a working button.
: > "$CLIAMP_LOG"
( CLIAMP_STATE=paused; CLIAMP_TOTAL=11; export CLIAMP_STATE CLIAMP_TOTAL
  lastrun play ) >/dev/null 2>&1
grep -q 'cliamp queue' "$CLIAMP_LOG"
[ $? != 0 ]
check "a player with something queued is resumed, not loaded again" $?

grep -q 'cliamp toggle' "$CLIAMP_LOG"
check "...with the verb that starts a paused player" $?

# ⚠ AND ONLY OUR OWN PLAYER IS RESTARTED. Putting a station back means
# music_restart() — `--provider` is a start-up flag and there is no other way —
# and a cliamp somebody has open in a terminal is not this launcher's to
# restart. Same marker, and the same argument, as `release` above.
: > "$CLIAMP_LOG"; rm -f "$MARK"
( CLIAMP_STATE=stopped; export CLIAMP_STATE; lastrun play ) >/dev/null 2>&1
grep -q 'cliamp queue' "$CLIAMP_LOG"
[ $? != 0 ]
check "a player this package did not start is not reloaded under somebody" $?

# The twin: the same empty player, claimed. Without this the assertion above
# passes for a version that never resumes anything at all.
: > "$CLIAMP_LOG"; : > "$MARK"
( CLIAMP_STATE=stopped; export CLIAMP_STATE; lastrun play ) >/dev/null 2>&1
grep -q 'cliamp queue' "$CLIAMP_LOG"
check "...and one it did start, with an empty queue, is filled again" $?
rm -f "$MARK"

# ⚠ A RECORD BELONGS TO ITS SOURCE. Replaying a station goes through yt_play(),
# which WRITES `music_source` — so resuming one after somebody deliberately
# moved the picker to Plex would silently undo the choice they just made.
printf 'music_source = plex\n' > "$BIGCONF"
: > "$CLIAMP_LOG"; rm -f "$CLIAMP_UP"
( CLIAMP_STATE=off; export CLIAMP_STATE; lastrun play ) >/dev/null 2>&1
grep -q 'cliamp queue' "$CLIAMP_LOG"
[ $? != 0 ]
check "a station is not resumed over the source somebody chose instead" $?

grep -q '^music_source = plex$' "$BIGCONF"
check "...so the source they chose is still the source" $?

# A machine that has never played anything is not an error — it is every
# machine on its first day, and the tile still starts the player.
printf 'music_source = ytmusic\n' > "$BIGCONF"
rm -f "$LAST" "$CLIAMP_UP"
( CLIAMP_STATE=off; export CLIAMP_STATE; lastrun play >/dev/null 2>&1 )
[ "$?" = 0 ]
check "a machine that has never played anything is not a failure" $?

# The other two queueing paths write the same record, and ⚠ the Plex one
# records the RATING KEY. The URLs cliamp is handed carry the token; nothing
# here writes one into a cache file, which is the rule music_key() exists for.
awk '/static int plex_play_album/,/^}/' src/big.c |
    grep -q 'music_last_remember("plex", key)'
check "a Plex album is remembered by its key, never by its track URLs" $?

awk '/static int local_queue/,/^}/' src/big.c | has 'music_last_remember'
check "...and the local library remembers its directory too" $?

# ⚠ AFTER THE BAIL-OUT, not before it. An album that turned out to have no
# playable tracks is not something to resume, and a record written on the way
# in would make the tile replay a failure for the rest of the session.
awk '/static int plex_play_album/,/^}/' src/big.c |
    awk '/no playable tracks/ { bail = 1 }
         /music_last_remember/ && bail { good = 1 }
         END { exit !good }'
check "...and only where a queue really filled" $?

# ── a track that will not play, and the silence it makes ───────────────────
#
# ⚠ MEASURED AGAINST A REAL PLAYER, and it is the second half of the same
# report: a queued YouTube URL that cannot be resolved leaves cliamp `stopped`
# FOR EVER. It does not skip it, it does not end the queue, it says nothing on
# any stream this program can read. Watched for 24 seconds: no movement.
#
# ⚠ AND THE TRACK LOOKS PERFECT ON THE WAY IN. Enumeration is
# `--flat-playlist` — the playlist's own listing, which is what makes it fast
# enough to press a button for — and that listing gives a real title, duration
# and view count for a video that answers "Video unavailable" the moment
# anything tries to play it, cookies or not. `%(availability)s` is NA there, so
# there is nothing to filter on. The playlist this was found in had its dead
# track FIRST, and pressing Music queued fifty-four tracks perfectly and played
# silence.
#
# Reported as: it will not play unless you skip and then play, and even then
# it is inconsistent. Which is exactly this — and which is what the fix does
# on somebody's behalf.
printf 'music_source = ytmusic\n' > "$BIGCONF"
: > "$CLIAMP_LOG"; rm -f "$CLIAMP_PLAYING" "$CLIAMP_DEAF"; echo 1 > "$CLIAMP_STUCK"
( CLIAMP_STATE=stopped; CLIAMP_TOTAL=54; export CLIAMP_STATE CLIAMP_TOTAL
  lastrun play ) >/dev/null 2>&1
grep -q 'cliamp next' "$CLIAMP_LOG"
check "a queue that will not start is stepped past, not left in silence" $?

# ── ⚠ BUT A LOST TOGGLE IS NOT A DEAD TRACK, AND IT IS THE COMMON CASE ──────
#
# Measured against the real player: a cliamp that has just come up answers its
# first toggle by doing NOTHING, and sits at `stopped` indefinitely. A second
# toggle — on the SAME track — starts it within four seconds. Watched
# directly: stuck at 12s, one plain re-toggle, playing at 16s.
#
# 0.1.0-35 answered that stall with `next`, so it threw away a song that was
# never broken: velle's entry 1 reports `public`, plays on its own in two
# seconds, and was skipped anyway — twice, at fifteen seconds apiece, for
# thirty-four seconds of silence before a note was heard. Reported as "the
# music isn't starting, and when I load a playlist I have to skip to get it to
# play". This is that, and the assertion is that NOTHING IS SKIPPED.
: > "$CLIAMP_LOG"; rm -f "$CLIAMP_PLAYING" "$CLIAMP_STUCK"; echo 1 > "$CLIAMP_DEAF"
( CLIAMP_STATE=stopped; CLIAMP_TOTAL=54; export CLIAMP_STATE CLIAMP_TOTAL
  lastrun play ) >/dev/null 2>&1
deaf=$(cat "$CLIAMP_LOG")
case "$deaf" in *"cliamp next"*) false ;; *) true ;; esac
check "a lost toggle is asked again, not answered by skipping a good track" $?

[ "$(grep -c 'cliamp toggle' "$CLIAMP_LOG")" -ge 2 ]
check "...by re-toggling the same track" $?

[ -s "$CLIAMP_PLAYING" ]
check "...and the music really is playing afterwards" $?

# ⚠ AND THE SKIP IS STILL THERE BEHIND IT. Re-asking cannot rescue a track that
# genuinely will not play, so a stuck one must still be stepped past once the
# re-asks are spent — otherwise this fix would trade one silence for another.
: > "$CLIAMP_LOG"; rm -f "$CLIAMP_PLAYING" "$CLIAMP_DEAF"; echo 1 > "$CLIAMP_STUCK"
( CLIAMP_STATE=stopped; CLIAMP_TOTAL=54; export CLIAMP_STATE CLIAMP_TOTAL
  lastrun play ) >/dev/null 2>&1
grep -q 'cliamp next' "$CLIAMP_LOG"
check "...while a track that truly will not play is still stepped past" $?

# ⚠ BOTH HALVES. `next` moves the track but does NOT start it — measured: the
# player sits at the new index, still stopped, and `play` (resume) does nothing
# from there. A skip with no toggle behind it is a queue moved one along and
# still silent, which is the bug wearing a different hat.
[ "$(grep -c 'cliamp toggle' "$CLIAMP_LOG")" -ge 2 ]
check "...and started again on the track it moved to" $?

# ⚠ THE ASSERTION THIS FILE ALREADY PAID FOR ONCE. An "insurance toggle" was
# written here before and turned a reliable station into one that started about
# half the time: the state LAGS the command, the check ran two seconds early
# every time, and `toggle` from `playing` is PAUSE. Nothing may touch a player
# that is playing.
: > "$CLIAMP_LOG"; rm -f "$CLIAMP_STUCK"; printf 1 > "$CLIAMP_PLAYING"
( CLIAMP_STATE=stopped; CLIAMP_TOTAL=54; export CLIAMP_STATE CLIAMP_TOTAL
  lastrun play ) >/dev/null 2>&1
grep -q 'cliamp next' "$CLIAMP_LOG"
[ $? != 0 ]
check "a player that really did start is never skipped or toggled again" $?

# ⚠ NOR A PAUSED ONE. Pausing is a decision somebody made while this was
# watching, and pressing on through it would be the program playing music over
# the top of a person.
: > "$CLIAMP_LOG"; echo 1 > "$CLIAMP_STUCK"
( CLIAMP_STATE=paused; CLIAMP_TOTAL=54; export CLIAMP_STATE CLIAMP_TOTAL
  lastrun play ) >/dev/null 2>&1
grep -q 'cliamp next' "$CLIAMP_LOG"
[ $? != 0 ]
check "...and a player somebody paused is left paused" $?

# ⚠ AN EMPTY QUEUE IS NOT A TRACK THAT WILL NOT PLAY, it is no track at all,
# and skipping through it would be a minute of pressing `next` against nothing.
: > "$CLIAMP_LOG"; rm -f "$CLIAMP_PLAYING"; echo 1 > "$CLIAMP_STUCK"; rm -f "$LAST"
( CLIAMP_STATE=stopped; export CLIAMP_STATE; lastrun play ) >/dev/null 2>&1
grep -q 'cliamp next' "$CLIAMP_LOG"
[ $? != 0 ]
check "...and an empty queue is not skipped through either" $?

# The Now Playing row's A button is the other way somebody asks a stalled queue
# to start, and it sends the bare verb.
: > "$CLIAMP_LOG"; rm -f "$CLIAMP_PLAYING"; echo 1 > "$CLIAMP_STUCK"
( CLIAMP_STATE=stopped; CLIAMP_TOTAL=54; export CLIAMP_STATE CLIAMP_TOTAL
  lastrun toggle ) >/dev/null 2>&1
grep -q 'cliamp next' "$CLIAMP_LOG"
check "the Now Playing button rescues a stalled queue too" $?

# ⚠ …AND ONLY WHEN IT IS A START. `toggle` is two verbs wearing one name: from
# `playing` it is PAUSE, and a pause that then insisted on playing again would
# be a button that cannot turn the music off.
: > "$CLIAMP_LOG"
( CLIAMP_STATE=playing; CLIAMP_TOTAL=54; export CLIAMP_STATE CLIAMP_TOTAL
  lastrun toggle ) >/dev/null 2>&1
grep -q 'cliamp next' "$CLIAMP_LOG"
[ $? != 0 ]
check "...but a toggle that means PAUSE is left alone" $?

# ⚠ ONE RESCUE AT A TIME. Waiting is long enough that the natural response is
# to press again, and two of these are two processes sending `next` and
# `toggle` on their own timers — where the second's toggle lands on the music
# the first just started and PAUSES it.
grep -q 'LOCK_EX | LOCK_NB' src/big.c && grep -q 'syn-arcade-music.start' src/big.c
check "a second press cannot fight the first for the queue" $?

# And the escape hatch that keeps this suite inside meson's timeout is a
# documented fixture knob, not a behaviour change.
grep -q 'SYN_ARCADE_MUSIC_WAIT_MS' src/big.c
check "the settle can be shortened for a fixture, and only for one" $?

rm -f "$LAST" "$MARK" "$CLIAMP_UP" "$BIGCONF" "$CLIAMP_STUCK" "$CLIAMP_PLAYING"
: > "$CLIAMP_LOG"
CLIAMP_STATE=playing; export CLIAMP_STATE

# ── and the SHELL has to know about all of it ───────────────────────────────
#
# ⚠ THE SECOND-ROSTER TRAP, which is what hid the last one. big.c can answer
# `yt` in the action column all day; if the QML has no page for it the row
# does nothing at all, and nothing warns. Each of these is one end of a wire
# whose other end is asserted above.
grep -q 'sourceSetProc.next === "yt"' "$BIGQML"
check "choosing YouTube Music opens the stations page" $?

grep -q 'shell.menuPage === "yt") return shell.ytItems' "$BIGQML"
check "...which is a page of the same menu, not a second panel" $?

grep -q '"big", "music", "yt", "--rec"' "$BIGQML"
check "...listed by big.c rather than by a copy of the list here" $?

grep -q 'it.kind === "yt"' "$BIGQML"
check "...and pressing A on a station plays it" $?

grep -q '"big", "music", "yt", it.id' "$BIGQML"
check "...through the same verb the id came from" $?

# ⚠ GUARDED ON `running`, like chooseSource and playAlbum. A station takes a
# few seconds to load, which is exactly long enough to press A twice — and
# `running = true` on an already-running quickshell Process is a SILENT no-op.
grep -q 'if (ytPlayProc.running) return' "$BIGQML"
check "...once, however many times A is pressed" $?

# ⚠ THE ERRAND ROWS ARE DISPATCHED BY KIND, not by a list of ids here — so a
# row added in big.c needs no change in this file unless it needs a NEW kind of
# handling. The one id this file does know about is `mine`, because that one is
# another PAGE rather than something launched.
grep -q 'it.kind === "ytaction"' "$BIGQML"
check "the shell dispatches the errand rows by kind" $?

grep -q '"big", "music", "yt", "mine", "--rec"' "$BIGQML"
check "...and Your playlists is a page of the same menu" $?

# ⚠ `setup` IS CLIAMP'S WIZARD, not one of ours — the OAuth route, restored to
# the television after 0.1.0-29 left it reachable from nowhere.
grep -q '"big", "music", "setup"' "$BIGQML"
check "...and the OAuth row runs cliamp's own wizard" $?

# ⚠ `keys: "1"` — both of the errands that end in typing need the on-screen
# keyboard pointed at the terminal they open.
grep -q 'id: "music-yt-" + it.id' "$BIGQML"
check "...and the two that type open with the keyboard enabled" $?

# The empty state is the first thing most people will see on that page — and
# ⚠ IT IS ITS OWN TEXT, because the page is never EMPTY: Search and Sign in are
# always on it, so the shared "nothing here" line can never fire.
grep -q 'No saved stations yet' "$BIGQML"
check "a page with no stations still says stations are a thing" $?

# ── the media buttons: whatever is playing, not just cliamp ─────────────────
#
# ⚠ `big transport` IS NOT `big music`, and the difference is whose music it
# is. `big music` drives cliamp over its socket; this speaks MPRIS, which is
# what makes a play/pause button on a television work on Spotify, a film, or a
# video in a browser tab. busctl is STUBBED here — the real one would find the
# session bus of the desktop running this suite and pause somebody's album.
BSTUB="$T/bus-bin"
mkdir -p "$BSTUB"
cat > "$BSTUB/busctl" <<'EOF'
#!/bin/sh
printf 'busctl %s\n' "$*" >> "$BUSCTL_LOG"

# Which player this call is about: the argument that looks like a bus name.
#
# ⚠ THE INTERFACE NAME LOOKS EXACTLY LIKE ONE. `org.mpris.MediaPlayer2.Player`
# is the last argument of every GetAll, so a loop that keeps the LAST match
# answers for a player called "Player" — which is to say it answers with the
# fallback for every call, and the fixture then has one player in it however
# many the list printed. It cost twenty minutes here.
bus=
for a in "$@"; do
    case $a in
        org.mpris.MediaPlayer2.Player) ;;
        org.mpris.MediaPlayer2.*) [ -z "$bus" ] && bus=$a ;;
    esac
done

case " $* " in
    *" list "*)
        # ⚠ WITH SOMETHING ELSE ON THE BUS. A list of nothing but media
        # players cannot show that the prefix is what filters them.
        printf 'org.freedesktop.systemd1 1 systemd velle :1.1 - - -\n'
        [ -n "${BUS_FIREFOX:-}" ] &&
            printf 'org.mpris.MediaPlayer2.firefox 222 firefox velle :1.5 - - -\n'
        [ -n "${BUS_CLIAMP:-}" ] &&
            printf 'org.mpris.MediaPlayer2.cliamp 333 cliamp velle :1.6 - - -\n'
        exit 0 ;;
    *GetAll*)
        case $bus in
            *firefox) st=$BUS_FIREFOX; title=$BUS_FTITLE; artist="Someone" ;;
            *)        st=$BUS_CLIAMP;  title="Cliamp Track"; artist="" ;;
        esac
        printf '{"type":"a{sv}","data":[{"CanPause":{"type":"b","data":true},'
        printf '"Metadata":{"type":"a{sv}","data":{"xesam:title":{"type":"s","data":"%s"},' "$title"
        printf '"xesam:artist":{"type":"as","data":["%s"]}}},' "$artist"
        printf '"CanPlay":{"type":"b","data":true},'
        printf '"CanGoNext":{"type":"b","data":true},'
        printf '"PlaybackStatus":{"type":"s","data":"%s"},' "$st"
        printf '"CanGoPrevious":{"type":"b","data":false}}]}\n'
        exit 0 ;;
    *Identity*)
        case $bus in
            *firefox) printf 's "Firefox"\n' ;;
            *)        printf 's "Cliamp"\n' ;;
        esac
        exit 0 ;;
esac
exit 0
EOF
chmod +x "$BSTUB/busctl"
export BUSCTL_LOG="$T/busctl.log" BUS_FIREFOX="" BUS_CLIAMP="" BUS_FTITLE="A Video"
: > "$BUSCTL_LOG"

# ⚠ The MUSIC stub is on this path too: when the chosen player is cliamp the
# transport goes over its socket rather than over D-Bus, and without cliamp
# here that branch would be tested against a player that is not installed.
tport() { ( PATH="$BSTUB:$MSTUB:$STUB"; export PATH; says "$SA" big transport "$@" ); }

( PATH="$BSTUB:$MSTUB:$STUB"; export PATH
  "$SA" big transport status >/dev/null 2>&1 )
[ "$?" != 0 ]
check "with nothing on the bus, transport status is not a success" $?

BUS_FIREFOX=Playing BUS_CLIAMP=Paused
export BUS_FIREFOX BUS_CLIAMP
tport status --rec | awk -F'\t' 'NR == 2 && $1 == "firefox" { f = 1 } END { exit !f }'
check "whatever is PLAYING wins, over a paused player big screen owns" $?

tport status --rec | awk -F'\t' 'NR == 2 && $2 == "Firefox" { f = 1 } END { exit !f }'
check "...and the player is named the way it names itself" $?

# ⚠ `as`, A LIST WITH ONE ENTRY — which is how every MPRIS player publishes an
# artist, and which the JSON reader had to learn to step over. Without it the
# artist is silently always empty.
tport status --rec | has "Someone"
check "the artist is read out of a one-element D-Bus array" $?

# ⚠ THE TIE-BREAK, and it is what decides whose buttons these are on a machine
# with a browser tab paused in another workspace.
BUS_FIREFOX=Paused; export BUS_FIREFOX
tport status --rec | awk -F'\t' 'NR == 2 && $1 == "cliamp" { f = 1 } END { exit !f }'
check "with both paused, the player big screen mode drives wins" $?

# ⚠ AND CLIAMP IS DRIVEN OVER ITS OWN SOCKET, not over D-Bus. Its MPRIS title
# is the file path and `play` from `stopped` does nothing there — both already
# solved in the music path, and rediscovering them through a second interface
# is how a television ends up with two answers about one player.
: > "$CLIAMP_LOG"; : > "$BUSCTL_LOG"
( PATH="$BSTUB:$MSTUB:$STUB"; export PATH; says "$SA" big transport next ) >/dev/null
grep -q 'cliamp next' "$CLIAMP_LOG"
check "a transport command for cliamp goes over its socket" $?

grep -q 'busctl.*Next' "$BUSCTL_LOG"
[ $? != 0 ]
check "...and not over D-Bus as well" $?

# The other way round: a player that is not cliamp gets the D-Bus method.
: > "$CLIAMP_LOG"; : > "$BUSCTL_LOG"
BUS_FIREFOX=Playing; export BUS_FIREFOX
( PATH="$BSTUB:$MSTUB:$STUB"; export PATH; says "$SA" big transport toggle ) >/dev/null
grep -q 'org.mpris.MediaPlayer2.firefox' "$BUSCTL_LOG" &&
    grep -q 'PlayPause' "$BUSCTL_LOG"
check "a transport command for anything else is an MPRIS method call" $?

# ⚠ SAID, NOT SENT. The stub answers CanGoPrevious false, which is what a radio
# stream looks like. A button that reports success and does nothing is how
# somebody learns the interface is broken.
( PATH="$BSTUB:$MSTUB:$STUB"; export PATH
  "$SA" big transport prev >/dev/null 2>&1 )
[ "$?" != 0 ]
check "a skip the player says it cannot do is refused, not faked" $?

# ⚠ THE MOST IMPORTANT ASSERTION IN THIS SECTION, and it is the same one the
# Plex section below makes about `big music`: MPRIS is a SECOND DOOR onto the
# same fact, and cliamp publishes the file path as the track title. For a Plex
# stream that path carries the account token in its query — four metres wide on
# a television, in the footer, on every screen.
BUS_FTITLE='http://192.168.1.50:32400/library/parts/1/2/file.flac?X-Plex-Token=SECRETVALUE'
export BUS_FTITLE
tport status --rec | has "SECRETVALUE"
[ $? != 0 ]
check "a token in a track title never reaches the media buttons" $?

tport status --rec | has "file.flac"
check "...and what is left is the name of the track" $?

BUS_FTITLE="A Video"; export BUS_FTITLE
BUS_FIREFOX=""; BUS_CLIAMP=""; export BUS_FIREFOX BUS_CLIAMP

( PATH="$BSTUB:$MSTUB:$STUB"; export PATH
  "$SA" big transport wobble >/dev/null 2>&1 )
[ "$?" = 2 ]
check "an unknown transport verb is a usage error" $?

# ── the visualizer is ENDED on the way back, not left running ───────────────
#
# ⚠ THE BUG: open the visualizer, press Guide, go back to it — and it is
# frozen until the window is resized twice with a mouse. A surface fully
# covered by an opaque one is occlusion-culled and gets no frame callbacks, and
# projectM does not idle without them (measured: 100% of a core while covered).
#
# ⚠ AND KILLING THE SHELL'S PROCESS IS NOT ENOUGH, which is the whole reason
# this is a behavioural test and not a grep. `big run --wait` gives the
# application its own SESSION, so a SIGTERM to the waiter leaves the program
# it started drawing away behind the television. The waiter has to pass the
# signal on to the whole process group.
grep -qE 'rows\[n\+\+\] = \(struct row\)\{ "visualizer".*|^\t\t\t"system", false, false, true, true \};' src/big.c
check "the visualizer is marked as ending on return" $?

says "$SA" big apps | grep -q "ends on return"
check "...and says so in plain text as well" $?

says "$SA" big apps --rec | awk -F'\t' '
    NR == 1 { for (i = 1; i <= NF; i++) if ($i == "transient") c = i; next }
    $1 == "web" && $c == "1" { bad = 1 }
    END { exit !!bad }'
check "...and the browser is NOT — Guide is meant to come back to it" $?

KILLBIN="$T/kill-bin"
mkdir -p "$KILLBIN" "$T/killhome"
ln -sf "$SA" "$KILLBIN/syn-arcade"
printf '#!/bin/sh\nexec sleep 941\n' > "$KILLBIN/projectM-pulseaudio"
chmod +x "$KILLBIN/projectM-pulseaudio"
# The visualizer refuses to start without a monitor source to listen to — it
# would open a MICROPHONE otherwise, which on a Bluetooth headset takes the
# machine's audio down. Two answers is the whole fixture.
cat > "$KILLBIN/pactl" <<'EOF'
#!/bin/sh
case "$1" in
    get-default-sink) echo fixture_sink ;;
    list) printf '0\tfixture_sink.monitor\tmodule-null-sink.c\ts16le\tSUSPENDED\n' ;;
esac
exit 0
EOF
chmod +x "$KILLBIN/pactl"

(
    # ⚠ HOME redirected as well: the visualizer writes projectM's own config
    # before starting it, and that file belongs to the person running this.
    HOME="$T/killhome"; export HOME
    PATH="$KILLBIN:$STUB:$PATH"; export PATH
    syn-arcade big run visualizer --wait &
    waiter=$!
    sleep 1
    app=$(pgrep -P "$waiter" | head -1)
    printf '%s\n' "${app:-none}" > "$T/vis.pid"
    kill -TERM "$waiter" 2>/dev/null
    sleep 1
    if [ -n "$app" ] && kill -0 "$app" 2>/dev/null; then
        kill -9 "$app" 2>/dev/null
        echo still > "$T/vis.alive"
    fi
    wait "$waiter" 2>/dev/null
)
app=$(cat "$T/vis.pid" 2>/dev/null)
[ -n "$app" ] && [ "$app" != none ]
check "a tile launch really does start something" $?

[ ! -f "$T/vis.alive" ]
check "…and a SIGTERM to the waiter ends the program it started" $?

# ⚠ THE GROUP, not the pid. The application was given its own session by
# spawn_detached_pid, so anything IT started in turn is in that group too — and
# killing one pid leaves a wrapper's real program running with nothing left
# holding a handle on it.
grep -q 'kill(-(pid_t)waited_pid, sig)' src/big.c
check "...and the signal goes to the whole process group" $?

# The shell's half: coming back is what ends them, and it signals the WAITER
# (there is no other pid it has) rather than trying to find the application.
grep -q 'shell.endTransients()' "$BIGQML"
check "coming back ends the applications marked as transient" $?

grep -q 'shell.procs\[i\].signal(15)' "$BIGQML"
check "...by signalling the process it started, which passes it on" $?

grep -q 'rec.tile.transient' "$BIGQML"
check "...and WHICH ones is big.c's column, not a list in the QML" $?

# ⚠ Coming back a second time would move the selection to the Running shelf —
# from a press of Guide meant to leave it where it was.
grep -q 'if (rec.ended)' "$BIGQML"
check "...and the exit that follows does not bounce the selection" $?

# ── the media buttons, on screen ────────────────────────────────────────────

grep -q 'big", "transport", "status", "--rec"' "$BIGQML"
check "the footer asks what is playing rather than assuming cliamp" $?

# ⚠ `transport`, NOT `media`: `media` is already the Plex and Jellyfin servers
# found on the network, and a duplicated property in QML is a warning nobody
# reads and a Media shelf that empties itself one day.
grep -q 'property var transport: ({})' "$BIGQML"
check "...into a property that does not collide with the media SERVERS" $?

grep -q 'shell.mediaState === "playing" || shell.mediaState === "paused"' "$BIGQML"
check "the buttons are drawn only while something is playing or paused" $?

grep -q 'if (!shell.mediaLive) shell.mediaFocus = false' "$BIGQML"
check "...and the selection cannot be left on them when they go" $?

grep -q 'shell.mediaFocus = true' "$BIGQML"
check "down from the last shelf lands on the media buttons" $?

# Every input path, because a button reachable only one way is a button half
# the room cannot press.
grep -q 'Qt.Key_MediaTogglePlayPause' "$BIGQML" &&
    grep -q 'Qt.Key_MediaNext' "$BIGQML" &&
    grep -q 'Qt.Key_MediaPrevious' "$BIGQML"
check "the media keys on a keyboard or a remote work too" $?

grep -q 'shell.mediaPress()' "$BIGQML"
check "...and so do A on the pad and a mouse click" $?

# ⚠ ⏮ ⏯ ⏭ ARE NOT IN EVERY FONT. On this machine they resolve through Noto
# Sans Symbols 2 and Noto Color Emoji; a fresh install is promised neither, and
# a missing glyph is an empty box four metres wide with nothing said anywhere.
grep -q 'ctx.fillRect' "$BIGQML"
check "the glyphs are drawn rather than typed, so no font can lose them" $?

# The rig has to be able to see all of this without reaching the real bus.
grep -q '^unset DBUS_SESSION_BUS_ADDRESS' tests/bigscreen_rig.sh
check "the rig cannot reach the live desktop's music player over D-Bus" $?

# ⚠ AND THE QML HALF IS PROVEN BY A PROCESS THAT REALLY GOES AWAY. A grep can
# show that comeBack() calls endTransients(); only the rig can show that the
# thing the tile started is gone afterwards. Confirmed discriminating: with the
# call removed the rig reports the pid still running.
grep -q 'STILL RUNNING after Guide' tests/bigscreen_rig.sh
check "...and it says whether Guide really ended the visualizer" $?

# ── a Plex token is not something to draw on a television ───────────────────
#
# ⚠ THE MOST IMPORTANT ASSERTION IN THIS SECTION. cliamp reports a queued
# track's PATH as its title — it reads no tags — so a track streamed from Plex
# comes back as a URL with `?X-Plex-Token=…` on the end. Left alone, that is
# somebody's credential drawn four metres wide in the Start menu, in every
# screenshot of it, and in the records this command prints.
CLIAMP_TRACK='http://192.168.1.50:32400/library/parts/1/2/file.flac?X-Plex-Token=SECRETVALUE'
music status --rec | has SECRETVALUE
[ $? != 0 ]
check "a Plex token never reaches the records the shell reads" $?

music status | has SECRETVALUE
[ $? != 0 ]
check "...nor the line a person sees" $?

music status | has 'file.flac'
check "...and what is left still names the track" $?

# The map that gives it a real name. Written by whatever queued the track,
# keyed on the path WITHOUT its query — which is the same string both sides
# have to agree on, and the reason the token is not in the cache either.
mkdir -p "$XDG_CACHE_HOME/syn-arcade"
printf '%s\t%s\n' \
    'http://192.168.1.50:32400/library/parts/1/2/file.flac' \
    'Linkin%20Park%20%E2%80%94%20With%20You' \
    > "$XDG_CACHE_HOME/syn-arcade/music-titles.rec"
music status | has 'Linkin Park — With You'
check "a queued track is drawn with the name it was queued under" $?

rm -f "$XDG_CACHE_HOME/syn-arcade/music-titles.rec"
CLIAMP_TRACK=''

# The suite runs with SYN_ARCADE_NO_NET=1, so this is the refusal rather than a
# library: what matters is that it FAILS rather than hanging or pretending.
( PATH="$MSTUB:$PATH"; export PATH; "$SA" big music plex >/dev/null 2>&1 )
[ "$?" != 0 ]
check "with no network the Plex library says so instead of drawing nothing" $?

music plex 2>&1 | has "cliamp setup"
check "...and names the command that would give it a server" $?

# ── projectM, and the microphone it must not listen to ──────────────────────
#
# ⚠ THE DEFAULT CAPTURE DEVICE IS A MICROPHONE. A visualizer that opens it
# reacts to the room and sits still through the music, which reads as a broken
# visualizer rather than as the wrong device. What is wanted is the MONITOR of
# whatever sink the music is going to, and it is asked for at launch because
# this machine's default output changes with a Bluetooth headset.
VSTUB="$T/vis-bin"
mkdir -p "$VSTUB"

# A machine with a Bluetooth headset connected: its output, its MONITOR, and —
# the one that matters — its microphone, which is what the shipped code used to
# open. The analog card is here too, so "the first monitor" is a real choice
# rather than the only row.
cat > "$VSTUB/pactl" <<'EOF'
#!/bin/sh
case "$1 $2 $3" in
    "get-default-sink  ")
        echo bluez_output.F4_B6_2D_DA_0E_BD.1 ;;
    "list sources short")
        printf '60\talsa_output.pci-0000_0a_00.4.analog-stereo.monitor\tPipeWire\ts32le 2ch 48000Hz\tSUSPENDED\n'
        printf '61\talsa_input.pci-0000_0a_00.4.analog-stereo\tPipeWire\ts32le 2ch 48000Hz\tSUSPENDED\n'
        printf '63930\tbluez_input.F4:B6:2D:DA:0E:BD\tPipeWire\tfloat32le 1ch 48000Hz\tRUNNING\n'
        printf '65138\tbluez_output.F4_B6_2D_DA_0E_BD.1.monitor\tPipeWire\ts16le 2ch 48000Hz\tRUNNING\n' ;;
    *) exit 1 ;;
esac
EOF
cat > "$VSTUB/projectM-pulseaudio" <<'EOF'
#!/bin/sh
echo "PULSE_SOURCE=$PULSE_SOURCE"
echo "SDL_AUDIO_INCLUDE_MONITORS=$SDL_AUDIO_INCLUDE_MONITORS"
EOF
chmod +x "$VSTUB/pactl" "$VSTUB/projectM-pulseaudio"

PMCONF="$XDG_CONFIG_HOME/projectM/qprojectM-pulseaudio.conf"
rm -f "$PMCONF"

( PATH="$VSTUB:$PATH"; export PATH; says "$SA" big visualizer ) |
    grep -q '^PULSE_SOURCE=bluez_output.F4_B6_2D_DA_0E_BD.1.monitor$'
check "the visualizer listens to the monitor of the sink in use" $?

( PATH="$VSTUB:$PATH"; export PATH; says "$SA" big visualizer ) |
    grep -q '^SDL_AUDIO_INCLUDE_MONITORS=1$'
check "...and the SDL build is told monitors exist at all" $?

# ── the bug that took a desktop's audio down with it ────────────────────────
#
# ⚠ projectM does NOT read PULSE_SOURCE. It enumerates sources itself, connects
# BY NAME, and remembers the choice in its own Qt config — where the saved value
# was `bluez_input.…`, the MICROPHONE of a Bluetooth headset. A Bluetooth device
# cannot do high-fidelity playback and microphone input at once, so opening it
# switched the card to the HSP/HFP profile: the output dropped to 16kHz mono
# (quiet and muffled, with the volume control making no difference) and the sink
# was destroyed and rebuilt, killing the music the visualizer was drawing.
grep -q '^pulseAudioDeviceName=bluez_output.F4_B6_2D_DA_0E_BD.1.monitor$' "$PMCONF"
check "the device is written into the config projectM actually reads" $?

# ⚠ FALSE, which is the opposite of what the name suggests: while it is true,
# projectM runs its own scan INSTEAD of opening the device named beside it, and
# that scan is what picks a microphone. Measured both ways round.
grep -q '^tryFirstAvailablePlaybackMonitor=false$' "$PMCONF"
check "...with projectM's own device scan turned OFF, or the name is ignored" $?

grep -qv 'bluez_input' "$PMCONF" && ! grep -q 'bluez_input' "$PMCONF"
check "...and a microphone is never written into it" $?

# Keys that are projectM's own — a window position, a preset playlist — survive.
printf '[General]\npulseAudioDeviceName=wrong\nplaylistPath=/home/somebody\n' > "$PMCONF"
( PATH="$VSTUB:$PATH"; export PATH; says "$SA" big visualizer ) >/dev/null
grep -q '^playlistPath=/home/somebody$' "$PMCONF"
check "...while everything else in projectM's config is preserved" $?

# ⚠ REFUSES rather than falling back. With no monitor to be found the only
# device left is a microphone, and opening one is what broke the machine — so
# "no monitor" has to mean "do not start", not "start on whatever is there".
cat > "$VSTUB/pactl" <<'EOF'
#!/bin/sh
case "$1 $2 $3" in
    "get-default-sink  ") echo some_sink ;;
    "list sources short")
        printf '61\talsa_input.pci-0000_0a_00.4.analog-stereo\tPipeWire\ts32le 2ch 48000Hz\tSUSPENDED\n' ;;
    *) exit 1 ;;
esac
EOF
chmod +x "$VSTUB/pactl"

( PATH="$VSTUB:$PATH"; export PATH; "$SA" big visualizer >/dev/null 2>&1 )
[ "$?" != 0 ]
check "with no monitor to listen to, the visualizer refuses to start" $?

( PATH="$VSTUB:$PATH"; export PATH; says "$SA" big visualizer ) |
    grep -qi "microphone"
check "...and says why, in terms of what it would have opened" $?

# Put the working stub back for the assertions below, which only care that
# projectM is on PATH.
cat > "$VSTUB/pactl" <<'EOF'
#!/bin/sh
case "$1 $2 $3" in
    "get-default-sink  ") echo sink ;;
    "list sources short") printf '1\tsink.monitor\tPipeWire\ts16le\tRUNNING\n' ;;
    *) exit 1 ;;
esac
EOF
chmod +x "$VSTUB/pactl"

( PATH="$VSTUB:$PATH"; export PATH; says "$SA" big apps ) |
    grep -qE '^visualizer +system +Visualizer +syn-arcade big visualizer'
check "an installed projectM is a row behind Start, not a shelf tile" $?

( PATH="$VSTUB:$PATH"; export PATH; says "$SA" big apps ) |
    grep -qE '^visualizer .*\[fullscreen\]'
check "...and it is asked to fill the screen, being a 512x512 window" $?

# ⚠ ON AN EMPTY PATH, not on this machine's. Whether projectM is installed
# where the suite runs is not something the suite gets to decide, and an
# assertion about its ABSENCE that reads the developer's own PATH is one that
# passes today and fails on the machine that installs it.
mkdir -p "$T/empty"
( PATH="$T/empty"; export PATH; says "$SA" big apps ) | grep -q '^visualizer'
[ $? != 0 ]
check "...and there is no such row on a machine without it" $?

( PATH="$T/empty"; export PATH; says "$SA" big visualizer ) |
    grep -q "projectM is not installed"
check "...where the command says what to install" $?

# ── the menu has pages now ──────────────────────────────────────────────────
grep -q 'property string menuPage: "main"' "$BIGQML"
check "the Start menu draws a page rather than a second panel" $?

grep -q 'if (shell.menuPage !== "main")' "$BIGQML"
check "...and B goes UP a page before it closes anything" $?

# ⚠ 130 albums in a Column is a panel taller than the television with its first
# row off the top of the screen.
grep -q 'ListView {' "$BIGQML" && grep -q 'positionViewAtIndex' "$BIGQML"
check "...the rows scroll and keep the selection on screen" $?

# The gap that arrived with the visualizer: menuActivate() handled the switches
# and the way out, and every `kind: "app"` row reaching it did nothing at all.
grep -q 'shell.launchApp(it, \["big", "run", it.id, "--wait"\])' "$BIGQML"
check "an application row in the Start menu actually launches" $?

# Setting `running = true` on a quickshell Process that is already running is a
# silent no-op, and these are the two rows somebody can press A on twice.
grep -q 'if (sourceSetProc.running) return' "$BIGQML"
check "a second press while a source is being switched is refused, not lost" $?

grep -q 'if (albumPlayProc.running) return' "$BIGQML"
check "...and the same on an album" $?

# ── the guide button ────────────────────────────────────────────────────────
#
# The watcher that makes the pad's GUIDE button open big screen mode from the
# desktop. It is an autostart line in the same managed block, and it is ON by
# default — which is what makes the "off" case need a marker of its own.

grep -q "^autostart = syn-arcade big guard$" "$RC"
check "an installed block watches the guide button" $?

says "$SA" big guide | grep -qx "on"
check "…and says so" $?

"$SA" big guide off >/dev/null 2>&1
grep -q "^autostart = syn-arcade big guard$" "$RC"
[ $? != 0 ]
check "turning the guide button off takes the line out" $?

# ⚠ The whole point of the marker. An absent autostart line is ALSO what every
# block written before this feature looks like, so without something in the
# file saying "off out loud", `binds refresh` — which runs at every login —
# would put the guard back for the one person who deliberately turned it off.
# A setting that will not stay set is worse than the setting not existing.
grep -q "syn-arcade: guide button off" "$RC"
check "...and records the decision, so refresh cannot undo it" $?

"$SA" binds refresh >/dev/null 2>&1
grep -q "^autostart = syn-arcade big guard$" "$RC"
[ $? != 0 ]
check "a login-time refresh leaves it off" $?

says "$SA" big guide status | grep -qx "off"
check "status reads it back" $?

# The upgrade path, which is the failure this whole marker scheme exists
# alongside: a block written by an OLDER syn-arcade has no guard line and no
# marker, and refresh must ADD the line — while keeping the keys the user
# chose. A default that arrives only for new installs reaches nobody.
cat > "$RC" <<'OLDBLOCK'
gaps = 8
# >>> syn-arcade  — the gaming shortcuts.
bind = super+F11 spawn syn-arcade hud toggle
bind = super+F12 spawn syn-arcade hud cycle
bind = super+F9 spawn syn-arcade big toggle
# <<< syn-arcade
OLDBLOCK

"$SA" binds refresh >/dev/null 2>&1
grep -q "^autostart = syn-arcade big guard$" "$RC"
check "refreshing a block from an older version adds the guide watcher" $?

grep -q "^bind = super+F9 spawn syn-arcade big toggle$" "$RC"
check "...without touching the key that person chose" $?

grep -q "^gaps = 8$" "$RC"
check "...and without touching the rest of their desktop config" $?

"$SA" binds remove >/dev/null 2>&1
unset SYN_ARCADE_STEAM

# ── about ───────────────────────────────────────────────────────────────────

echo
echo "about"

says "$SA" about | grep -q "GPL-2.0-or-later"
check "about names the licence" $?

says "$SA" about --rec | head -1 | grep -q "^field	value	action$"
check "about --rec names its columns" $?

# ── big screen: the two settings that could only be spelled by hand ─────────
#
# The screen it opens on and the music player were `output =` and `music =` in
# big.conf and nothing else — no verb, no window — which is no use at all to the
# case they exist for: somebody sitting in front of a television that the
# interface just opened on the wrong monitor.
#
# ⚠ synctl is STUBBED. `big output` asks the compositor which screens are
# attached, and the one in this session is the LIVE one.

echo
echo "big screen settings"

OSTUB="$T/out-bin"
mkdir -p "$OSTUB"
cat > "$OSTUB/synctl" <<'EOF'
#!/bin/sh
[ "$1" = outputs ] || exit 1
printf '[{"name":"DP-3","at":[0,0],"size":[2560,1440],"scale":1.00,"primary":true,"focused":true},'
printf '{"name":"HDMI-A-1","at":[2560,0],"size":[1920,1080],"scale":1.00,"primary":false,"focused":false}]\n'
EOF
chmod +x "$OSTUB/synctl"

BIGCONF2="$XDG_CONFIG_HOME/syn-arcade/big.conf"
rm -f "$BIGCONF2"

( PATH="$OSTUB:$PATH"; export PATH; says "$SA" big output --rec ) |
    grep -q '^primary	Main screen	current$'
check "the screen defaults to primary, and says so" $?

( PATH="$OSTUB:$PATH"; export PATH; says "$SA" big output --rec ) |
    grep -q '^HDMI-A-1	HDMI-A-1	-$'
check "...and every attached connector is offered by name" $?

( PATH="$OSTUB:$PATH"; export PATH; "$SA" big output HDMI-A-1 >/dev/null 2>&1 )
grep -q '^output = HDMI-A-1$' "$BIGCONF2"
check "choosing one writes it to big.conf" $?

# ⚠ A connector that is not plugged in is REFUSED. The shell falls back to its
# first screen for a name that matches nothing, which is indistinguishable from
# the setting being ignored.
( PATH="$OSTUB:$PATH"; export PATH; says "$SA" big output DP-9 ) |
    grep -q "no screen called"
check "a screen that is not attached is refused" $?

grep -q '^output = HDMI-A-1$' "$BIGCONF2"
check "...and the refusal leaves the old choice alone" $?

# The player. cliamp is the only one that can be DRIVEN, and the picker has to
# say so — fourteen equal-looking chips would hide the one fact that matters.
PSTUB="$T/player-bin"
mkdir -p "$PSTUB"
printf '#!/bin/sh\nexit 0\n' > "$PSTUB/cliamp"
printf '#!/bin/sh\nexit 0\n' > "$PSTUB/vlc"
chmod +x "$PSTUB/cliamp" "$PSTUB/vlc"

( PATH="$PSTUB:$PATH"; export PATH; says "$SA" big player --rec ) |
    grep -q '^cliamp	cliamp	current	played without a window$'
check "cliamp is the player, and the row says it needs no window" $?

( PATH="$PSTUB:$PATH"; export PATH; says "$SA" big player --rec ) |
    grep -q '^vlc	vlc	-	opens its own window$'
check "...and the others say they open one" $?

( PATH="$PSTUB:$PATH"; export PATH; "$SA" big player vlc >/dev/null 2>&1 )
grep -q '^music = vlc$' "$BIGCONF2"
check "choosing a player writes it to big.conf" $?

says "$SA" big player nosuchplayer | grep -q "not installed"
check "a player that is not installed is refused" $?

( PATH="$PSTUB:$PATH"; export PATH; says "$SA" big player cliamp ) |
    grep -q "without a window"
check "...and switching back to cliamp says what changes" $?

rm -f "$BIGCONF2"

# ── fit: the gamescope wrappers ─────────────────────────────────────────────
#
# ⚠ HOME AND XDG_DATA_HOME ARE REDIRECTED FOR THIS WHOLE SECTION, and unlike
# everything above that is not belt and braces — it is the only thing standing
# between this suite and the live desktop's own menu. `fit` writes
# ~/.local/share/applications/syn-fit-*.desktop and ~/Desktop/syn-fit-*.desktop,
# both of which appear in the running session the moment they exist. A run
# without this leaves fixture games in velle's start menu.
#
# XDG_CONFIG_HOME is redirected for the whole file already; it is set again here
# so the three stay together and a future edit cannot separate them.

echo
echo "fit"

FITHOME="$T/fithome"
mkdir -p "$FITHOME/Desktop"
export HOME="$FITHOME" XDG_DATA_HOME="$FITHOME/.local/share"
export XDG_CONFIG_HOME="$T/config"

case "$HOME" in
    "$T"/*) : ;;
    *) echo "REFUSING: fit's HOME is not sandboxed" >&2; exit 1 ;;
esac

says "$SA" fit | grep -q "No gamescope wrappers"
check "there are no wrappers to start with" $?

"$SA" fit >/dev/null 2>&1
[ "$?" = 100 ]
check "an empty list exits 100 rather than failing" $?

says "$SA" fit new --exec="wine Sims.exe" --name="The Sims (Fullscreen)" \
    --game=1024x768 --screen=2560x1440 --sharpness=2 |
    grep -q "gamescope -w 1024 -h 768 -W 2560 -H 1440"
check "the game size is -w/-h and the screen size is -W/-H" $?

# ⚠ THE assertion of this whole feature. Lower case is the size the GAME
# renders at and upper case the size of the SCREEN; swapped, the game renders at
# the monitor's resolution — which is the thing being avoided, and which most of
# these games cannot do at all. Nothing warns, and it looks like a game bug.
says "$SA" fit command the-sims-fullscreen |
    grep -q -- "-w 1024 -h 768 -W 2560 -H 1440"
check "...and the same way round when the command is read back" $?

[ -f "$XDG_DATA_HOME/applications/syn-fit-the-sims-fullscreen.desktop" ]
check "a menu entry is written" $?

grep -q "^Exec=syn-arcade fit run the-sims-fullscreen$" \
    "$XDG_DATA_HOME/applications/syn-fit-the-sims-fullscreen.desktop"
check "the entry runs \`fit run\`, so editing it needs no menu rewrite" $?

grep -q "gamescope -w 1024 -h 768" \
    "$XDG_DATA_HOME/applications/syn-fit-the-sims-fullscreen.desktop"
check "...with the assembled command written in it as a comment" $?

[ ! -e "$FITHOME/Desktop/syn-fit-the-sims-fullscreen.desktop" ]
check "no desktop icon unless one was asked for" $?

"$SA" fit edit the-sims-fullscreen --desktop=yes >/dev/null 2>&1
[ -x "$FITHOME/Desktop/syn-fit-the-sims-fullscreen.desktop" ]
check "the desktop icon appears, and is executable" $?

"$SA" fit edit the-sims-fullscreen --desktop=no >/dev/null 2>&1
[ ! -e "$FITHOME/Desktop/syn-fit-the-sims-fullscreen.desktop" ]
check "...and goes away again" $?

# Editing changes the command without touching the entry that runs it.
"$SA" fit edit the-sims-fullscreen --game=640x480 >/dev/null 2>&1
says "$SA" fit command the-sims-fullscreen | grep -q -- "-w 640 -h 480"
check "an edit changes the command" $?

grep -q "^Exec=syn-arcade fit run the-sims-fullscreen$" \
    "$XDG_DATA_HOME/applications/syn-fit-the-sims-fullscreen.desktop"
check "...and the menu entry's Exec is unchanged by it" $?

# Refusals. Each of these is a value that would otherwise reach gamescope or
# the shell, and be wrong somewhere nobody is watching.
says "$SA" fit edit the-sims-fullscreen --game=1024 | grep -q "WxH"
check "a size that is not WxH is refused" $?

says "$SA" fit edit the-sims-fullscreen --filter=bilinear | grep -q "unknown filter"
check "a filter gamescope does not have is refused" $?

says "$SA" fit edit the-sims-fullscreen --sharpness=40 | grep -q "0"
check "a sharpness outside 0-20 is refused" $?

says "$SA" fit edit the-sims-fullscreen --env=WINEPREFIX | grep -q "NAME=VALUE"
check "an env setting with no = is refused" $?

# ⚠ A newline would forge a second setting in the config file, a second key in
# the .desktop, and a second command in the shell line.
says "$SA" fit edit the-sims-fullscreen --name="$(printf 'a\nExec=rm -rf x')" |
    grep -q "newline"
check "a newline in a value is refused" $?

# Quoting. A path with a space in it is the normal case for wine games, and an
# unquoted `cd` would run the game in the wrong directory or not at all.
"$SA" fit new --id=spaced --name="Spaced" --exec="wine Game.exe" \
    --workdir="/home/you/Program Files/A Game" >/dev/null 2>&1
says "$SA" fit command spaced | grep -q "cd '/home/you/Program Files/A Game' &&"
check "a working directory with spaces is quoted" $?

# ── adopting a gamescope line that is already there ─────────────────────────
#
# ⚠ Without this, `--from` on a shortcut somebody had already made by hand
# produced a wrapper AROUND a wrapper — two nested micro-compositors — and the
# entries most worth wrapping are exactly the ones that already carry a
# hand-written gamescope line.

cat > "$T/adopt.desktop" <<'ADOPT'
[Desktop Entry]
Type=Application
Name=Gangsters (Fullscreen)
Exec=env WINEPREFIX=/home/you/Games/gangsters gamescope -w 800 -h 600 -W 2560 -H 1440 -f -F fsr --fsr-sharpness 2 -- wine gangsters.exe
Path=/home/you/Games/gangsters
Categories=Game;
ADOPT

out=$("$SA" fit inspect "$T/adopt.desktop" 2>&1)
printf '%s\n' "$out" | has "^game      800x600$"
check "an existing gamescope line gives up its game size" $?

printf '%s\n' "$out" | has "^screen    2560x1440$"
check "...and its screen size" $?

printf '%s\n' "$out" | has "^env       WINEPREFIX=/home/you/Games/gangsters$"
check "...and its environment" $?

printf '%s\n' "$out" | has -- "-- wine gangsters.exe$"
check "...and the game's own command, from after the --" $?

[ "$(printf '%s\n' "$out" | grep -c gamescope)" = 1 ]
check "...exactly ONE gamescope in the result, not two" $?

printf '%s\n' "$out" | has "^name      Gangsters (Fullscreen)$"
check "a name that already says Fullscreen is not given a second one" $?

# --fsr-sharpness is gamescope's own alias for --sharpness; a line using it
# would otherwise lose the setting silently.
printf '%s\n' "$out" | has -- "--sharpness 2"
check "--fsr-sharpness is read as sharpness" $?

# inspect must not CREATE anything: the window calls it to fill a form, and a
# picker that made a wrapper out of every entry looked at would be a menu full
# of them.
[ ! -e "$XDG_CONFIG_HOME/syn-arcade/fit/gangsters-fullscreen.conf" ]
check "inspect creates nothing" $?

# A plain entry, with the field codes the launcher is supposed to substitute.
cat > "$T/plain.desktop" <<'PLAIN'
[Desktop Entry]
Type=Application
Name=Quake
Exec=wine Quake.exe %U
Categories=Game;
PLAIN
says "$SA" fit inspect "$T/plain.desktop" | grep -q "wine Quake.exe$"
check "a .desktop field code is stripped from the command" $?

says "$SA" fit inspect "$T/plain.desktop" | grep -q "^name      Quake (Fullscreen)$"
check "...and a plain name gains the suffix" $?

# The applications list, which is what the picker draws. The fixture goes into
# the sandbox's own applications directory — the one the picker searches first —
# rather than being read from where it was written above.
mkdir -p "$XDG_DATA_HOME/applications"
cp "$T/plain.desktop" "$XDG_DATA_HOME/applications/quake.desktop"
says "$SA" fit apps --rec | grep -q "Quake"
check "fit apps finds an entry in XDG_DATA_HOME" $?

# ⚠ Not `grep -qv`, which succeeds the moment ANY line fails to match and is
# therefore true of every list. The question is whether the wrapper made above
# is in there at all.
says "$SA" fit apps --rec | grep -q "The%20Sims"
[ "$?" != 0 ]
check "...and leaves this tool's own wrappers out of it" $?

# ── removal takes all three files ───────────────────────────────────────────
"$SA" fit edit the-sims-fullscreen --desktop=yes >/dev/null 2>&1
"$SA" fit remove the-sims-fullscreen >/dev/null 2>&1
[ ! -e "$XDG_CONFIG_HOME/syn-arcade/fit/the-sims-fullscreen.conf" ] &&
    [ ! -e "$XDG_DATA_HOME/applications/syn-fit-the-sims-fullscreen.desktop" ] &&
    [ ! -e "$FITHOME/Desktop/syn-fit-the-sims-fullscreen.desktop" ]
check "remove takes the config, the menu entry and the desktop icon" $?

says "$SA" fit remove nosuchwrapper | grep -q "no wrapper called"
check "removing something absent is refused" $?

says "$SA" fit show nosuchwrapper | grep -q "no wrapper called"
check "showing something absent is refused" $?

# ⚠ The live desktop's own menu, which this section is redirected away from.
# ⛔ COMPARED WITH THE SNAPSHOT taken at the top, not with the empty set: see
# the note beside REAL_FIT_MENU. What is asserted is that this run ADDED
# nothing, which is the actual claim.
[ "$(find "$REAL_HOME/.local/share/applications" -name 'syn-fit-*' \
     2>/dev/null | sort)" = "$REAL_FIT_MENU" ]
check "no wrapper reached the real applications menu" $?

[ "$(find "$REAL_HOME/Desktop" -name 'syn-fit-*' 2>/dev/null | sort)" \
  = "$REAL_FIT_DESK" ]
check "no wrapper reached the real desktop" $?

unset XDG_DATA_HOME
export HOME="$REAL_HOME"

# ── the sandbox held ────────────────────────────────────────────────────────
#
# The last and most important assertion: prove that nothing in this run reached
# outside $T. If any of the above wrote to the real config, this is how it gets
# noticed on the machine it happened on rather than three releases later.

echo
echo "sandbox"

[ ! -e "$HOME/.config/syn-arcade/deadzones.state" ] ||
    [ -n "$(find "$HOME/.config/syn-arcade" -newer "$T" 2>/dev/null)" ] && true
find "$T" -name deadzones.state | has . || true

# The real synuirc must not have been touched. Compare against its own mtime
# captured before anything ran would be better still, but its ABSENCE from the
# set of files newer than $T is the cheap version and catches a write.
if [ -f "$HOME/.config/synui/synuirc" ]; then
    [ -z "$(find "$HOME/.config/synui/synuirc" -newer "$T" 2>/dev/null)" ]
    check "the real synuirc was not written" $?
else
    ok "the real synuirc was not created"
fi

if [ -f "$HOME/.config/MangoHud/MangoHud.conf" ]; then
    [ -z "$(find "$HOME/.config/MangoHud/MangoHud.conf" -newer "$T" 2>/dev/null)" ]
    check "the real MangoHud config was not written" $?
else
    ok "the real MangoHud config was not created"
fi

# ── the selection model in the QML ──────────────────────────────────────────
#
# A grep, because there is no QML engine in this suite and pulling one in would
# make the build depend on qt6-declarative for a five-line check. It is worth
# having anyway: this exact line cost big screen mode every horizontal movement
# it had, in the shipped 0.1.0-2.
#
# `cols` is a `var` property holding one column index per shelf. The obvious
# spelling — take the object, set a key, assign it back — does NOT notify:
# Qt compares the incoming QVariant against the stored one, finds the identical
# JS object and drops the write, so no binding reading `cols` is re-evaluated.
# Nothing warns. The result was that left, right, the shoulder-button page
# jumps, Home, and the mouse moving along one shelf were all dead, while up and
# down worked perfectly because `row` is an int — which read from the sofa as
# "the controller is half wired up" rather than as a QML bug.
#
# It even looked intermittent: `selected` also reads `row`, so the next up or
# down press published every swallowed column move at once and the selection
# jumped several tiles. Verified both ways on Qt 6.11 by driving the real file
# under quickshell — mutate-and-reassign read back 0 after a write of 7.
BIGQML=data/syn-arcade-big.qml

grep -q "Object.assign({}, shell.cols" "$BIGQML"
check "setCol assigns a COPY of cols (a mutated object emits no change)" $?

! grep -qE '^\s*shell\.cols = c\s*$' "$BIGQML"
check "...and never reassigns the same object reference" $?

# ── which shelf it OPENS on ────────────────────────────────────────────────
#
# The shelves arrive as separate queries and land in whatever order they
# finish: the cached ones (media, news) in a millisecond, the Steam library
# last, because it reads every manifest on disk. Whichever answered first was
# shelves[0] for an instant, `rowTitle` adopted it as though somebody had
# chosen it, and when Games was inserted ABOVE it the name-matching faithfully
# moved the selection down to keep it there. The rows scroll to keep the
# selection in view — so big screen mode opened with the library off the top of
# the screen and Media selected, on a machine with a large library every time.
#
# ⚠ An adoption is not a choice, and `rowChosen` is the whole of the fix: until
# a button is actually pressed there is no selection to preserve, so the top
# shelf is re-taken every time another one lands. Both movers have to set it,
# or the first press is forgotten and the next arriving shelf overrides it.
grep -q "property bool rowChosen" "$BIGQML"
check "the selection knows whether anybody has chosen it" $?

grep -q "if (!shell.rowChosen)" "$BIGQML"
check "...and an unchosen selection follows the top shelf" $?

[ "$(grep -c 'shell.rowChosen = true' "$BIGQML")" -ge 3 ]
check "...and every deliberate move claims it" $?

# ── the two things big.c ships that the shell used to drop ──────────────────
#
# Both were being computed, percent-encoded and written into every record while
# this file read neither: `logo` is the game's own title art, and `iconfile`
# the drawn glyph for an app or an action. A column nothing reads is invisible
# — no warning, no error, and the interface merely looks plainer than the data
# it was handed.
grep -q "it.logo" "$BIGQML"
check "the banner draws the game's own title art" $?

grep -q "tile.modelData.iconfile" "$BIGQML"
check "a tile with no cover draws its glyph" $?

# ── hover must not be able to move the selection on its own ─────────────────
#
# The second bug of this family, and it read as three unrelated faults from the
# sofa: up and down "not taking", the selection "jumping" several shelves, and
# launching "one app behind" what was highlighted.
#
# Qt re-delivers a hover event at the LAST KNOWN cursor position on every frame
# where the scene graph is dirty (QQuickDeliveryAgentPrivate::
# flushFrameSynchronousEvents). Every selection move here animates — the shelf
# column for 200ms, the strip under ApplyRange for 200ms, the tile scale for
# 140ms — so ONE d-pad press is a dozen frames of tiles being dragged past a
# cursor nobody is touching. `onEntered` fired on each tile that arrived under
# it and wrote row and col, which scrolled the strip, which dragged another tile
# under the cursor: a feedback loop with the pad on one side and the animation
# on the other. Launching landed exactly one behind because ApplyRange scrolls
# by exactly one tile width, so A activated the state while the screen still
# showed the picture from before the move.
#
# ⚠ The headless rig CANNOT catch this and neither can any screenshot it takes:
# the fault needs the surface to have seen a pointer at least once, and nothing
# in the rig ever moves one. On a television it needs nothing at all — coming
# back from an app leaves the cursor wherever the stick left it, over the tiles.
#
# So the fix is a gate on the cursor's SCENE position actually changing, and
# entered() cannot be part of it because entered() carries no coordinates.
grep -q "function pointerMoved(g)" "$BIGQML"
check "hover is gated on the pointer's scene position changing" $?

! grep -qE '^\s*onEntered:' "$BIGQML"
check "...and no onEntered handler moves the selection" $?

grep -q "shell.pointerMoved(" "$BIGQML"
check "...with the tile's hover routed through that gate" $?

# ── stepping aside rather than quitting ─────────────────────────────────────
#
# The behaviour this release exists for. Launching anything used to call
# Qt.quit(), so opening the controller window or the browser CLOSED the
# television interface and getting back meant finding a keyboard. Each of the
# greps below is one half of the replacement.

grep -q "visible: chosen && !shell.away" "$BIGQML"
check "the main surface is UNMAPPED while away, not just transparent" $?

grep -q '"big", "run", it.id, "--wait"' "$BIGQML"
check "an app tile is launched with --wait, so its exit is the way back" $?

grep -q 'case "guide":      shell.stepAside()' "$BIGQML"
check "guide steps aside instead of quitting" $?

grep -q "shell.comeBack()" "$BIGQML"
check "...and something brings it back" $?

# ⚠ AND THE TWO TILES ARE NOT THE SAME TILE. Desktop used to be the one that
# quit, which left the interface with a way to END it and no way to leave it
# loaded on purpose — and, because Super+F10 only hides, the ordinary way out
# left it resident with nothing in the dock to close. Asserted as a PAIR: two
# tiles that both quit is the state this replaced, and it looks correct from
# every angle except the one that matters.
grep -A2 'if (it.id === "desktop")' "$BIGQML" | has "shell.stepAside()"
check "the Desktop tile steps aside and stays loaded" $?

#
# ⚠ THROUGH quitNow() SINCE 0.1.0-33, not Qt.quit() directly — the way out has
# something to do first (let go of the headless music player) and there are
# three doors onto it. The pair is still the assertion: Desktop steps aside,
# Quit ends the process.
grep -A2 'if (it.id === "quit")' "$BIGQML" | has "shell.quitNow()"
check "...and the Quit tile is the one that ends the process" $?

awk '/function quitNow/,/^    }/' "$BIGQML" | has 'releaseProc.running = true'
check "...which really does end it, once the music is let go" $?

grep -A2 'if (it.id === "desktop")' "$BIGQML" | has "Qt.quit()"
[ $? != 0 ]
check "...so Desktop is not a second Quit" $?

# ⚠ The single-instance trap. Firefox and Steam exit IMMEDIATELY when one is
# already running — the second process hands its arguments to the first over a
# socket. Treating that as "they closed it" throws the television back over a
# browser somebody just opened, on exactly the machines where the browser was
# already up.
grep -q "lived < 3000" "$BIGQML"
check "a launcher that returns at once is a hand-off, not a close" $?

# ── the on-screen keyboard ──────────────────────────────────────────────────
#
# ⚠ Keyboard focus NONE, and this is the assertion that matters most in the
# file. The keyboard's whole job is to type into the window UNDERNEATH it; a
# surface that took keyboard focus to draw a keyboard would be typing into
# itself, and every key would go nowhere with nothing saying why.
grep -q "WlrLayershell.keyboardFocus: WlrKeyboardFocus.None" "$BIGQML"
check "the on-screen keyboard never takes keyboard focus" $?

grep -q '"big", "keys"' "$BIGQML"
check "it types through the binary rather than spawning wtype per key" $?

grep -q "stdinEnabled: true" "$BIGQML"
check "...over a stream, so a fast press cannot be dropped" $?

# ⛔ AND IT OPENS ON A HELD START, NOT A PRESSED ONE.
#
# `big nav` keeps reading the pad while the interface is stepped aside — that is
# how Guide comes back — and it grabs nothing, so every button reaches the
# application as well. A bare Start opened this keyboard, and in GeForce NOW
# Start is the button that opens the GAME's menu: one press put a keyboard over
# the thing somebody was reaching for. A press belongs to the application; a
# hold does not.
# ⚠ $T, not $BGH: this section runs a good thousand lines before the QML block
# that makes $BGH, and an unbound variable under `set -u` ends the suite here
# with everything after it unrun.
awk '/if \(!shell.oskOpen\) \{/,/^        \}/' "$BIGQML" > "$T/osk.qml"
has 'cmd === "keyboard"' "$T/osk.qml" \
    && ok "the keyboard opens on the held-Start word" \
    || bad "the on-screen keyboard does not open on the keyboard word"
has 'cmd === "menu"' "$T/osk.qml" \
    && bad "a bare Start still opens the keyboard over the application" \
    || ok "…and a bare Start is left to the application in front"

# ── the controller as a mouse ───────────────────────────────────────────────
#
# Bounded in one place: the Process's `running` condition. All three clauses
# matter — out of the way, something that wants a pointer, and no keyboard up
# (A cannot be both a click and a keypress).
grep -q 'running: shell.away && shell.activeApp !== null' "$BIGQML"
check "the mouse runs only while the interface is out of the way" $?

grep -q 'shell.activeApp.pointer === "1" && !shell.oskOpen' "$BIGQML"
check "...only for a tile that wants one, and never under the keyboard" $?

# ── the three rows, in order ────────────────────────────────────────────────
#
# Games, then Play/Media/Apps across one row, then the headlines. The order is
# compared by LINE NUMBER, because the shelves are pushed in display order and
# there is no QML engine here to ask.
grep -q 'title: "News"' "$BIGQML"
check "there is a news shelf" $?

gamesline=$(grep -n 'title: "Games"' "$BIGQML" | head -1 | cut -d: -f1)
playline=$(grep -n 'title: "Play"' "$BIGQML" | head -1 | cut -d: -f1)
mediline=$(grep -n 'title: "Media"' "$BIGQML" | head -1 | cut -d: -f1)
appsline=$(grep -n 'title: "Apps"' "$BIGQML" | head -1 | cut -d: -f1)
newsline=$(grep -n 'title: "News"' "$BIGQML" | head -1 | cut -d: -f1)

# ⚠ Every one of these has to be a NUMBER before they are compared. A renamed
# shelf makes its variable empty, and `[ "" -gt 5 ]` is a shell ERROR, not a
# false — which prints a diagnostic beside a check that then reports whatever
# the previous command left in $?. That is how two of these looked like passes
# on the run that renamed System.
numeric=1
for v in "$gamesline" "$playline" "$mediline" "$appsline" "$newsline"; do
	case $v in ''|*[!0-9]*) numeric=0 ;; esac
done
[ "$numeric" -eq 1 ]
check "every shelf the order is asserted on still exists" $?

# ⚠ THE LIBRARY IS THE FIRST ROW. Play used to sit above it — two launcher
# tiles across the top of a television, with the covers somebody turned it on
# for pushed a row down.
[ "$gamesline" -lt "$playline" ]
check "the library is the first row, above the launchers" $?

[ "$playline" -lt "$mediline" ] && [ "$mediline" -lt "$appsline" ]
check "...then Play, Media and Apps, in that order along one row" $?

[ "$newsline" -gt "$appsline" ]
check "...and the headlines are last" $?

# ── the machine's own switches are NOT a shelf ──────────────────────────────
#
# ⚠ THE INVERSE ASSERTION, and it is the one worth having. Four buttons pressed
# once a day cost a whole row of the television and one more scroll on the way
# to the news. They are still `shelf = system` in big.c — where a tile goes is
# still decided there — so what this pins is that THIS file stopped pushing
# them into `shelves`, which is a thing a later edit could put back in one line
# without anything warning.
! grep -q 'title: "System"' "$BIGQML"
check "the system switches are no longer a row of the screen" $?

grep -q 'out.concat(shell.byShelf("system"))' "$BIGQML"
check "...they are what is behind Start" $?

# One implementation of what Sleep does. Reachable from the menu now and from a
# shelf tile still, and two copies is how one of them stops restarting the
# launch overlay — which reads as a button that did nothing.
grep -q 'function runAction(it)' "$BIGQML"
check "an action has one implementation, not one per way in" $?

grep -q 'if (shell.runAction(it)) return' "$BIGQML"
check "...and the tile path goes through it" $?

# The menu owns every button while it is up, exactly as the close question
# does. Without the guard the d-pad keeps moving the selection behind the
# overlay and A means something other than what is drawn under the cursor.
grep -q 'if (shell.menuOpen) {' "$BIGQML"
check "the Start menu is modal to the controller" $?

grep -q 'case "menu":       shell.menuToggle(); break' "$BIGQML"
check "Start opens it" $?

grep -q 'case Qt.Key_S:        shell.nav("menu"); break' "$BIGQML"
check "...and S is the keyboard's spelling of Start" $?

# A switch nobody can find is a switch that is not there, and on a television
# the legend is the only place it can be advertised.
grep -q 'k: "Start", v: I18n.tr("System")' "$BIGQML"
check "...and the legend says so" $?

# Closed BEFORE the action runs: sleep comes back to this screen, and coming
# back to a menu left open half an hour ago is the interface having remembered
# the wrong thing.
#
# ⚠ SCOPED TO THE FUNCTION, and the whole-file version of this was wrong. There
# are four `menuOpen = false` in the file — the toggle, the nav guard, the
# backdrop's click — and the last of them is in the OVERLAY, hundreds of lines
# below menuActivate. A `tail -1` therefore compared a line in the drawing
# against a line in the logic and reported the order backwards.
awk '/function menuActivate\(\)/,/^    }$/' "$BIGQML" > "$T/menuactivate.qml"
menuclose=$(grep -n 'shell.menuOpen = false' "$T/menuactivate.qml" | head -1 | cut -d: -f1)
menurun=$(grep -n 'shell.runAction(it)' "$T/menuactivate.qml" | head -1 | cut -d: -f1)
case ${menuclose:-x}${menurun:-x} in
	*[!0-9]*) false ;;
	*) [ "$menuclose" -lt "$menurun" ] ;;
esac
check "...and it closes before the switch it chose is thrown" $?

# ── a BAR: a shelf that would rather scroll than own a row ──────────────────
#
# ⚠ THIS IS WHAT RESERVES ROOM FOR HEROIC AND LUTRIS. Both are already in
# apps_table() behind a have() check, so they join the Play bar the day they
# are installed — and without this rule that arrival is a silent RELAYOUT:
# four launchers no longer fit beside Media and Apps at the 15% squeeze, the
# packer breaks the row in three, and installing a game launcher rearranges the
# whole television.
grep -q 'function isBar(sh) { return sh.kind === "app" }' "$BIGQML"
check "Play, Media and Apps are bars" $?

grep -q 'if (bar && cur.length && shell.isBar(shell.shelves\[cur\[0\]\])) {' "$BIGQML"
check "...so they share a row whatever the arithmetic says" $?

grep -q 'if (!bar && shell.bandScale(\[sh\]) < shell.bandSqueeze) close()' "$BIGQML"
check "...and a bar is never sent to a row of its own for not fitting" $?

# Past the squeeze the row is SHARED OUT instead of overflowing the screen. A
# band whose widths add up to more than the row is a Row drawn off the edge of
# the television, and the last bar simply is not there.
grep -q 'units = w.map(x => x \* shell.rowUnits / used)' "$BIGQML"
check "a band that cannot fit shares the row out in proportion" $?

grep -q 'Math.max(shell.bandSqueeze, shell.bandScale(shs))' "$BIGQML"
check "...with the tiles floored at the same 15% the strip allows" $?

# ── the dendrite mark ───────────────────────────────────────────────────────
#
# Resolved in C through icon_file(), like every tile glyph, so the header draws
# from the source tree and the installed tree without knowing which it is.
grep -q 'SYN_BIG_LOGO' "$BIGQML"
check "the header takes its emblem from a path C resolved" $?

grep -q 'setenv("SYN_BIG_LOGO", icon_file("synapse"), 1)' src/big.c
check "...and big.c is what resolves it" $?

[ -f data/icons/synapse.svg ]
check "...and the drawing is in the tree" $?

grep -q "data/icons/synapse.svg" meson.build
check "...and ships" $?

# ── the Settings gear ───────────────────────────────────────────────────────
#
# ⛔ THE SECOND GLYPH THAT IS NOT A TILE, and the checks above it are not
# enough for it: apps_table() emits no row for Settings — it is a PAGE of the
# System menu rather than something `big run` can run — so the three lists that
# have to agree never mention it, and none of them noticed that the only row on
# a menu of eight with an empty icon column was the one the shell draws itself.
grep -q 'SYN_BIG_SETTINGS_ICON' "$BIGQML"
check "the Settings row takes its gear from a path C resolved" $?

grep -q 'setenv("SYN_BIG_SETTINGS_ICON", icon_file("settings"), 1)' src/big.c
check "...and big.c is what resolves it" $?

[ -f data/icons/settings.svg ]
check "...and the drawing is in the tree" $?

grep -q "data/icons/settings.svg" meson.build
check "...and ships" $?

# ⚠ AND THE ROW ACTUALLY CARRIES IT. The three checks above pass on a build
# where the environment variable is set, read into a property, and never
# reaches the row — which is the whole of the bug they exist to prevent.
grep -q 'iconfile: shell.settingsIcon' "$BIGQML"
check "...and the Settings row is given it" $?

# ⚠ ANCHORED TO THE WORDMARK, not placed in a Row with it. A Row refuses to
# position a child that anchors itself, so the two would silently overlap at
# the left margin.
grep -q 'anchors.left: wordmark.right' "$BIGQML"
check "the mark sits beside the wordmark" $?

# ── the layout answers to the screen's SHAPE, not only its height ───────────
#
# ⚠ WHAT MADE THIS BUG INVISIBLE: scaling off height alone gives the identical
# layout for 1080p, 1440p and 4K, so "it works at every resolution" was true
# and hid that it was wrong at every ASPECT RATIO. The leftover tile at the
# right-hand edge ran from 10% visible on 4:3 to 92% on 21:9; only 16:9, the
# shape it was drawn on, looked deliberate.
#
# Rendered proof is bigscreen_rig.sh, which can now be given a SIZE. These
# guard the two things a later edit could quietly undo.
grep -q 'win.width / 96' "$BIGQML"
check "the unit is clamped by WIDTH as well as height" $?

# 54 x 16/9 = 96 exactly, so the clamp is a no-op on 16:9 and can only ever
# shrink the unit on a proportionally narrower screen. If someone "tidies" that
# constant the televisions this is for change size, which is the one thing it
# must never do.
grep -q 'win.height / 54' "$BIGQML"
check "...and 16:9 still lands on exactly the height-derived unit" $?

# The tile takes the SNAPPED width from the strip. A delegate that went back to
# a fixed multiple of u would put the ragged edge straight back, and nothing
# would warn.
grep -q 'width: strip.slotW' "$BIGQML"
check "a tile takes the width the shelf snapped for it" $?

grep -q 'height: strip.slotH' "$BIGQML"
check "...and the height that keeps Steam's 2:3 art square" $?

# The keyboard is a second window and cannot read win.u, so it carries its own
# copy of the clamp — the two drifting apart is a keyboard at a different scale
# from the interface it types into.
[ "$(grep -c '/ 96' "$BIGQML")" -ge 2 ]
check "the on-screen keyboard clamps the same way" $?

# ── shelves that share a row ────────────────────────────────────────────────
#
# ⚠ THE EMPTY HALF OF ONE ROW AND THE MISSING ROW BELOW IT WERE THE SAME SPACE.
# Media has three tiles and Apps has three, and each of them used to own a full
# row of the television — so two thirds of both rows was nothing, while System
# and the headlines sat off the bottom edge where only a scroll could reach
# them. Consecutive shelves that fit across are packed into a BAND now.
#
# Rendered proof is bigscreen_rig.sh at three aspect ratios; these guard the
# parts an edit could undo without anything warning.
grep -q 'readonly property var bands' "$BIGQML"
check "shelves are packed into bands" $?

grep -q 'model: shell.bands' "$BIGQML"
check "...and a band, not a shelf, is one row of the Column" $?

# ⚠ ONE PLACE FOR THE TILE WIDTH. The packer reserves a shelf's width from this
# and the strip draws its tiles from it; two copies of the number is a band
# whose arithmetic disagrees with its own contents, which clips the last tile
# of a shelf that was promised to fit whole. Nothing about that warns.
grep -q 'function idealUnits' "$BIGQML"
check "the ideal tile width has exactly one home" $?

grep -q 'win.u \* shell.idealUnits' "$BIGQML"
check "...and the strip reads it from there rather than repeating it" $?

# ── a band is ONE SHAPE of tile ─────────────────────────────────────────────
#
# ⚠ REPORTED FROM A 1080p LAPTOP WITH THREE GAMES: the whole interface arrived
# in the top half of the screen with a hole in the middle of it. The packer was
# right by its own rules — three covers FIT, so Games was packed into a band
# with Play, Media and Apps — but a cover strip is about fourteen units tall
# and a bar is about eight, and a Row aligns its children at the TOP. Half that
# row was empty by construction, and the screen was a row short below it.
#
# ⚠ INVISIBLE ON A REAL LIBRARY, which is why it shipped and why this suite is
# the wrong place to catch it on its own: fifty games overflow, an overflowing
# shelf keeps its own row, and the machine it was written on has fifty-three.
# `GAMES=3 SIZE=1920x1080 tests/bigscreen_rig.sh …` is the picture of it.
grep -q 'function isPortrait' "$BIGQML"
check "the shape of a tile has exactly one home too" $?

# ⚠ ONE definition, and every consumer must READ it rather than ask the kind
# again. There are two `portrait` properties — the strip's, which decides how
# tall a row is, and the tile's, which decides the shape of the art — and the
# packer now decides which shelves may share a row from the same answer. Any of
# the three drifting is a row reserved at one height and drawn at another.
[ "$(grep -A1 'property bool portrait:' "$BIGQML" | grep -c 'shell.isPortrait')" = 2 ]
check "...and both drawing paths take that answer rather than asking again" $?

grep -q 'shell.isPortrait(sh) !== shell.isPortrait(shell.shelves\[cur\[0\]\])' "$BIGQML"
check "a shelf of covers never shares a row with a shelf of app tiles" $?

# The screen decides the packing, not a count of tiles — the same six shelves
# land differently on 4:3, 16:9 and 21:9. Assuming a shape here is the bug
# 0.1.0-8 was, one layer up.
grep -q 'property: "rowUnits"' "$BIGQML"
check "the packer is told how wide the row really is" $?

grep -q 'when: win.chosen' "$BIGQML"
check "...by the screen that is actually showing it, and no other" $?

# A shelf that has to scroll keeps its own row: half a row is half the tiles
# per press, so narrowing a fifty-game library doubles how far somebody pushes
# a stick to cross it. The squeeze is the same 15% the strip already allows
# itself when it snaps a row to whole tiles.
grep -q 'readonly property real bandSqueeze: 0.85' "$BIGQML"
check "a band may squeeze its tiles, but only as far as the strip already does" $?

# The peek is a promise that the row runs on past the edge. On a shelf whose
# tiles all fit it is a promise of nothing — and the stretch that pays for it
# would make Media's tiles a different size from Apps's on the same row.
grep -q 'if (!strip.overflows) return idealW' "$BIGQML"
check "a shelf that fits keeps its tiles at size, with no peek to promise" $?

# ⚠ UP AND DOWN MOVE A BAND. Two shelves on one row are ONE row to somebody
# holding a controller, and a d-pad that needed two presses to leave a row it
# had visibly already left is a broken d-pad.
grep -q 'const at = shell.place(shell.row)' "$BIGQML"
check "up and down step between bands, not between shelves" $?

# …and left and right run along the whole band, crossing into the shelf drawn
# beside it rather than stopping at a wall with three tiles visible past it.
grep -q 'shell.setCol(over.row' "$BIGQML"
check "...and running off a shelf steps into the one beside it" $?

# The rows scroll by BAND too. Counting shelves would scroll a row too far for
# every band holding more than one — by an amount that depends on the screen's
# width, which is exactly the shape of bug this file has been bitten by.
grep -q 'const band = shell.place(shell.row)\[0\]' "$BIGQML"
check "the stage scrolls by band as well" $?

# B steps aside from the TOP BAND. `row > 0` is still true on a first row that
# holds two shelves, and B would move sideways instead of getting out of the
# way — from the one place it has nowhere else to go.
grep -q 'if (shell.place(shell.row)\[0\] > 0) shell.moveRow(-1)' "$BIGQML"
check "B steps aside from the top band, not merely from the top shelf" $?

# ── the lock belongs to the shell, and to nothing the shell spawned ─────────
#
# ⚠ THE BUG THIS PINS LEFT BIG SCREEN MODE PERMANENTLY UNUSABLE. `big start`
# clears FD_CLOEXEC on the flock descriptor on purpose, so the lock survives
# the exec into quickshell and is held for exactly as long as the shell lives.
# What it also did was hand that descriptor to every helper quickshell spawns —
# `big nav`, `big keys`, `big mouse`, `pads hold` — because an inherited fd
# keeps being inherited until something stops it.
#
# So the lock came to be held by the longest-lived of that family. Found in the
# wild with the shell long gone and an orphaned `big nav` still holding the
# file 51 minutes later: every `big start` answered "already running" with no
# big screen anywhere on any output, and `big stop` could not clear it either,
# because it kills the pid in the file and that process had already exited.
#
# ⚠ AND IT OUTLIVED A LOGOUT — logind ships KillUserProcesses=no — so it was
# still there at the next login. Nothing about it was self-clearing.
#
# The shell is stood in for by a plain `flock`, which is all it is to the
# kernel; `pads hold` stands in for the helper because it is the one that
# genuinely runs for hours.
if ! command -v flock >/dev/null 2>&1; then
    echo "  skip  the inherited lock (flock is not installed)"
else
    LOCKF="$XDG_RUNTIME_DIR/syn-arcade-big.pid"
    rm -f "$LOCKF"
    # 9<> so the descriptor is a NUMBER we know and can announce, exactly as
    # big.c announces whatever open() gave it.
    printf '99999\n' > "$LOCKF"
    (
        exec 9<>"$LOCKF"
        flock -x 9
        SYN_BIG_LOCK_FD=9 "$SA" pads hold >/dev/null 2>&1 &
        helper=$!
        sleep 1
        exec 9<&-              # the shell dies; the helper lives on
        echo "$helper" > "$T/helper.pid"
        sleep 4
        kill "$helper" 2>/dev/null
    ) &
    outer=$!
    sleep 2

    # With the shell's own descriptor closed, nothing should be holding this.
    out=$("$SA" big status 2>&1 | head -1)
    case "$out" in
        *"not running"*) ok "a helper outliving the shell does not hold the lock" ;;
        *) bad "a helper outliving the shell does not hold the lock ($out)" ;;
    esac

    wait "$outer" 2>/dev/null
    rm -f "$LOCKF"
fi

# ── big screen refuses a compositor that cannot show it ─────────────────────
#
# Big screen mode is two quickshell PanelWindows — wlr-layer-shell surfaces —
# and layer-shell is a wlroots protocol that mutter has never implemented.
# Under GNOME the shell started, mapped nothing and exited: an app-grid entry
# that did nothing when clicked, with no error anywhere. Confirmed on a GNOME
# session 2026-08-18, which is also when the guard went in.
#
# Checked at the SOURCE rather than by running it. Exercising the real path
# needs a compositor without layer-shell, and there is no way to conjure one
# in a build that would not also risk driving the live seat — which this
# suite's own rigs have done before. What can rot silently is the ORDER: the
# probe has to run before quickshell is spawned, or the refusal is printed
# after the thing it was meant to prevent has already started.
BIGSRC="$(dirname "$0")/../src/big.c"
if [ -f "$BIGSRC" ]; then
    grep -q 'zwlr_layer_shell_v1' "$BIGSRC" \
        && ok "big start asks the registry for layer-shell" \
        || bad "big.c no longer probes for zwlr_layer_shell_v1"

    # The probe is asked of the REGISTRY, not of a desktop name. A list of
    # desktop names is wrong about every wlroots compositor not on it, and
    # wrong about both names synui itself logs in under.
    if grep -n 'have_layer_shell()' "$BIGSRC" | has 'XDG_CURRENT_DESKTOP'; then
        bad "the layer-shell test reads XDG_CURRENT_DESKTOP instead of the registry"
    else
        ok "the layer-shell test asks the compositor, not the desktop name"
    fi

    guard=$(grep -n 'have_layer_shell() == 0' "$BIGSRC" | head -1 | cut -d: -f1)
    spawn=$(grep -n 'quickshell' "$BIGSRC" | awk -F: -v g="${guard:-0}" '$1 > g {print $1; exit}')
    if [ -n "$guard" ] && [ -n "$spawn" ] && [ "$guard" -lt "$spawn" ]; then
        ok "the refusal comes before quickshell is started"
    else
        bad "the layer-shell guard does not precede the quickshell spawn"
    fi

    # -1 means "no display to ask", which must NOT be a refusal: a machine
    # that cannot answer has to be allowed to try.
    grep -q 'have_layer_shell() == 0' "$BIGSRC" \
        && ok "only a definite no refuses — an unanswerable probe does not" \
        || bad "the guard refuses on anything but a definite no"
fi

# ── the shape this file has now been fixed for TWICE ────────────────────────
#
# Once by adding says(), which covered the 139 assertions that capture their
# output, and once by adding has(), which covered the 74 that did not. A third
# round is a certainty without a gate.
#
# ⚠ `says` IN FRONT IS THE EXEMPTION. It captures into a variable and ignores
# SIGPIPE, so its own printf cannot be killed and nothing upstream of grep is
# left in the pipe.
selfgrep=$(grep -nE '\| *grep -q' "$0" | grep -v 'says ' |
           grep -vE '^[0-9]+:[[:space:]]*#' || true)
if [ -n "$selfgrep" ]; then
    bad "this suite pipes into 'grep -q' — use 'has', or 'says' in front"
    printf '%s\n' "$selfgrep" | sed 's/^/        /' >&2
else
    ok "no producer in this file pipes into 'grep -q' (141 on a MATCH)"
fi

# ── the television's background, and the web app tiles ──────────────────────
#
# ⚠ THE PIXELS ARE bigscreen_rig.sh's JOB, not this suite's. What is checked
# here is everything a grep CAN answer: that the setting round-trips through
# big.conf, that the guards refuse what they say they refuse, and that the tile
# rows carry the two columns a browser on a television cannot work without.
echo
echo "── background ──"

BGH=$(mktemp -d); trap 'rm -rf "$BGH"' EXIT
mkdir -p "$BGH/syn-arcade" "$BGH/synui"
bg() { XDG_CONFIG_HOME="$BGH" HOME="$BGH" "$SA" big background "$@" 2>&1; }

# Unset means `desktop`, not empty. A default that only exists when somebody
# has already chosen it is not a default.
case "$(bg)" in
    desktop*) ok "background defaults to following the desktop" ;;
    *) bad "the default is not 'desktop': $(bg)" ;;
esac

# ⚠ synui's OWN FILE, in its own format — a bare token line and a `mode` line.
# Reading it is the whole feature: a path copied into big.conf at install time
# would be right once and wrong the first time somebody pressed Super+W.
magick -size 8x8 xc:'#123456' "$BGH/wp.png" 2>/dev/null ||
    convert -size 8x8 xc:'#123456' "$BGH/wp.png" 2>/dev/null || true
if [ -f "$BGH/wp.png" ]; then
    printf '%s\nmode fill\n' "$BGH/wp.png" > "$BGH/synui/wallpaper.state"
    [ "$(bg --path)" = "$BGH/wp.png" ] \
        && ok "…and follows synui's wallpaper.state" \
        || bad "wallpaper.state was not read: $(bg --path)"

    # ⛔ THE STATE FILE BEATS synuirc, which is the order synui itself applies
    # them in (config.c applies the state last). Getting this backwards would
    # mean the television showed the wallpaper somebody replaced months ago
    # while the desktop showed the one they picked this morning.
    printf 'wallpaper = /nonexistent-on-purpose.png\n' > "$BGH/synui/synuirc"
    [ "$(bg --path)" = "$BGH/wp.png" ] \
        && ok "…and the state file beats synuirc, as synui applies them" \
        || bad "synuirc won over wallpaper.state: $(bg --path)"

    # `matrix` is a live GL surface, not a file. There is no still of it to
    # hand a QML Image, so it has to resolve to nothing rather than to a
    # wrong picture — or to a broken file:// URL that draws blank.
    printf 'matrix\n' > "$BGH/synui/wallpaper.state"
    [ -z "$(bg --path)" ] \
        && ok "…and the kanji rain resolves to nothing, not a broken path" \
        || bad "matrix produced a path: $(bg --path)"

    printf '%s\nmode fill\n' "$BGH/wp.png" > "$BGH/synui/wallpaper.state"
else
    ok "(skipped the wallpaper reads: no ImageMagick to make a picture with)"
fi

# ⚠ A PATH THAT DOES NOT RESOLVE IS REFUSED AT THE VERB. Written through, it
# would produce a television that looks exactly as it did before — the setting
# silently ignored — with nothing on that screen able to explain itself.
XDG_CONFIG_HOME="$BGH" HOME="$BGH" "$SA" big background /no/such/picture.png \
    >/dev/null 2>&1 \
    && bad "an unreadable background path was accepted" \
    || ok "an unreadable background path is refused"

XDG_CONFIG_HOME="$BGH" HOME="$BGH" "$SA" big background none >/dev/null 2>&1
[ -z "$(bg --path)" ] \
    && ok "'none' draws nothing — the plain colour" \
    || bad "'none' still produced a path: $(bg --path)"

# ⚠ NO OUTPUT IS THE ANSWER, NOT A FAILURE. The shell runs this and reads one
# line; exiting non-zero for the plain colour would put an error in its log on
# every start for a setting working exactly as asked.
XDG_CONFIG_HOME="$BGH" HOME="$BGH" "$SA" big background --path >/dev/null 2>&1 \
    && ok "…and still exits 0, because empty IS the answer" \
    || bad "--path exited non-zero for the plain colour"

echo
echo "── web apps ──"

wa() { XDG_CONFIG_HOME="$BGH" HOME="$BGH" "$SA" big webapps "$@" 2>&1; }
apps() { XDG_CONFIG_HOME="$BGH" HOME="$BGH" "$SA" big apps --rec 2>/dev/null; }

rm -f "$BGH/syn-arcade/big.conf"
for id in twitch youtube spotify; do
    case "$(wa)" in
        *"$id"*on*) ok "$id is on by default" ;;
        *) bad "$id is not on with no setting written: $(wa)" ;;
    esac
done

# ⛔ THE LAST ONE OFF MUST WRITE `none`, NEVER AN EMPTY VALUE. big_conf_get
# ignores an assignment with nothing after the `=`, so an empty list reads back
# as UNSET — which is all three ON, the exact opposite of what switching the
# last one off asked for.
for id in twitch youtube spotify; do wa "$id" off >/dev/null; done
has "^webapps = none$" "$BGH/syn-arcade/big.conf" \
    && ok "switching the last one off writes 'none', not an empty value" \
    || bad "the empty list was written back: $(cat "$BGH/syn-arcade/big.conf")"
wastate() { printf '%s' "$(wa)" | awk -v i="$1" '$1 == i {print $2}'; }
[ "$(wastate twitch)" = off ] \
    && ok "…and it reads back as off rather than defaulting on again" \
    || bad "'none' read back as the unset default"

# A list must not match by substring: `webapps = youtube` answering for a
# `youtube-music` that does not exist yet is the shape of the bug.
wa youtube on >/dev/null
[ "$(wastate youtube)" = on ] && ok "one back on is on" || bad "youtube did not come back on"
[ "$(wastate twitch)" = off ] && ok "…and the others stayed off" \
    || bad "turning one on turned the others on"

# ⛔ pointer AND keys ON EVERY WEB TILE. This is the Plex tile's bug: a row
# without those columns is a browser on a television with no mouse and no
# keyboard, which looks like the site being broken rather than the tile.
for id in twitch youtube spotify; do wa "$id" on >/dev/null; done
APPSREC=$(apps)
if printf '%s' "$APPSREC" | cut -f1 | grep -Fxq twitch; then
    for id in twitch youtube spotify; do
        row=$(printf '%s\n' "$APPSREC" | awk -F'\t' -v i="$id" '$1 == i')
        if [ -z "$row" ]; then
            bad "$id has no row in \`big apps\` with a browser installed"
            continue
        fi
        p=$(printf '%s' "$row" | cut -f7); k=$(printf '%s' "$row" | cut -f8)
        [ "$p" = 1 ] && [ "$k" = 1 ] \
            && ok "$id carries pointer and keys" \
            || bad "$id has pointer=$p keys=$k — a browser with neither"
        # The shelf is the media one, beside Plex and Kodi: on a sofa Twitch is
        # where a stream is, not "a web browser pointed at Twitch".
        [ "$(printf '%s' "$row" | cut -f6)" = media ] \
            && ok "…and sits on the media shelf" \
            || bad "$id is not on the media shelf"
        # And a glyph, or the tile draws a blank square on the television.
        f=$(printf '%s' "$row" | cut -f10)
        [ -n "$f" ] && [ -f "$f" ] \
            && ok "…and has a drawing" \
            || bad "$id has no iconfile ($f)"
    done
else
    ok "(skipped the tile columns: no browser installed to build a command with)"
fi

# Switched off is GONE from the table, not a dead tile on the shelf.
wa spotify off >/dev/null
if apps | cut -f1 | grep -Fxq spotify; then
    bad "a switched-off web app still has a tile"
else
    ok "a switched-off web app has no tile at all"
fi
rm -f "$BGH/syn-arcade/big.conf"

echo
echo "── settings: what the television shows, and the power row ──"
#
# The page behind Start ▸ Settings. The shell draws whatever this table says,
# presses `next` and reads it again, so everything about what a setting IS
# lives here — which is what these assertions are for.

st() { XDG_CONFIG_HOME="$BGH" HOME="$BGH" "$SA" big settings "$@" 2>&1; }
strec() { XDG_CONFIG_HOME="$BGH" HOME="$BGH" "$SA" big settings --rec 2>/dev/null; }
sysrows() { XDG_CONFIG_HOME="$BGH" HOME="$BGH" "$SA" big apps --rec 2>/dev/null \
            | awk -F'\t' '$6 == "system" {print $1}'; }

rm -f "$BGH/syn-arcade/big.conf"

# ⚠ EVERY SHELF ON, WITH NOTHING WRITTEN. A launcher whose first run hides half
# of itself is one nobody finds the settings page to fix.
offbydefault=$(strec | tail -n +2 | awk -F'\t' '$1 ~ /^show_/ && $5 != "on"')
[ -z "$offbydefault" ] \
    && ok "every shelf and menu row is on with no config written" \
    || bad "something is off by default: $offbydefault"

[ "$(st keep_awake)" = playing ] \
    && ok "keep_awake defaults to 'playing', not 'always'" \
    || bad "keep_awake defaulted to $(st keep_awake)"

# ⛔ THE IDS AND THE VALUES STAY ENGLISH; the two label columns are what gets
# drawn. i18n_test.sh proves the record does not move under a foreign locale;
# this proves the record carries BOTH halves, because a shell given only the
# label has nothing to send back and one given only the id has nothing to draw.
head1=$(strec | head -1)
[ "$head1" = "$(printf 'id\tlabel\tgroup\tgrouplabel\tvalue\tvaluelabel')" ] \
    && ok "the record names all six columns" \
    || bad "unexpected header: $head1"

# ── cycling ────────────────────────────────────────────────────────────────
#
# ⚠ `next` IS WHAT THE TELEVISION PRESSES. The shell does not know that
# keep_awake has three values and show_news two — it sends `next` and redraws —
# so a cycle that ran off the end would be a setting somebody could not get out
# of without a keyboard.
[ "$(st show_news next)" = off ] && ok "next flips a switch off" \
    || bad "show_news next did not go off"
[ "$(st show_news next)" = on ] && ok "…and round again" \
    || bad "show_news did not come back on"

[ "$(st keep_awake next)" = always ] || bad "keep_awake: playing → always"
[ "$(st keep_awake next)" = never ] || bad "keep_awake: always → never"
[ "$(st keep_awake next)" = playing ] \
    && ok "a three-value row cycles round rather than stopping at the end" \
    || bad "keep_awake did not come back round to playing"

# ⚠ A VALUE THE TABLE DOES NOT KNOW READS AS THE DEFAULT. big.conf is a file
# people edit; the alternative to falling back is a shelf that is neither on
# nor off, which draws as nothing and cannot be switched back on from a sofa.
printf 'show_news = yes\n' > "$BGH/syn-arcade/big.conf"
[ "$(st show_news)" = on ] \
    && ok "a hand-edited value nothing recognises reads as the default" \
    || bad "show_news = yes read back as $(st show_news)"
rm -f "$BGH/syn-arcade/big.conf"

st nonsense on >/dev/null 2>&1; [ $? = 2 ] \
    && ok "an unknown setting is refused with status 2" \
    || bad "an unknown setting was not refused"
# ⚠ CAPTURED FIRST. This command is EXPECTED to exit 2, so `st … | grep` would
# report the refusal's own status rather than grep's verdict — which is what
# says() exists for. See the header.
says st keep_awake sometimes > "$T/setting-bad.txt"
has 'never, playing, always' "$T/setting-bad.txt" \
    && ok "…and a bad value names the legal ones" \
    || bad "a bad value did not list what is allowed: $(cat "$T/setting-bad.txt")"

# ── the Start menu's own rows ───────────────────────────────────────────────
#
# ⛔ THE WAY OUT HAS NO SWITCH. Desktop and Quit are how somebody leaves a
# full-screen surface that owns the keyboard, and on a gamepad there is no key
# combination to fall back on — so they must survive every setting being off.
st show_power off >/dev/null
st show_visualizer off >/dev/null
sysrows > "$T/sysrows-off.txt"
if has -Fx desktop "$T/sysrows-off.txt" && has -Fx quit "$T/sysrows-off.txt"; then
    ok "Desktop and Quit survive every switch being off"
else
    bad "the way out went away: $(tr '\n' ' ' < "$T/sysrows-off.txt")"
fi
if has -E '^(sleep|restart|poweroff)$' "$T/sysrows-off.txt"; then
    bad "show_power off left a power row on the menu: $(tr '\n' ' ' < "$T/sysrows-off.txt")"
else
    ok "show_power off takes all three power rows off the menu"
fi

st show_power on >/dev/null
sysrows > "$T/sysrows-on.txt"
has -Fx poweroff "$T/sysrows-on.txt" \
    && ok "…and switching it back on brings them back" \
    || bad "the power rows did not come back"

# ── `big choices`, which the DESKTOP window needs and the television does not ─
#
# A gamepad has one button and cycles; a mouse has a row of chips and picks. So
# the desktop window is the only caller that needs every value at once.
ch() { XDG_CONFIG_HOME="$BGH" HOME="$BGH" "$SA" big choices "$@" 2>&1; }

ch keep_awake --rec > "$T/choices.txt"
[ "$(head -1 "$T/choices.txt")" = "$(printf 'id\tlabel\tcurrent')" ] \
    && ok "big choices names an id, a label and which is current" \
    || bad "unexpected choices header: $(head -1 "$T/choices.txt")"
[ "$(tail -n +2 "$T/choices.txt" | wc -l)" = 3 ] \
    && ok "…and keep_awake offers all three" \
    || bad "keep_awake offered $(tail -n +2 "$T/choices.txt" | wc -l) values"
# ⛔ EXACTLY ONE CURRENT. A row set that marks none leaves the window with no
# chip lit and nothing saying which is in force; one that marks two is a
# one-of-many pick that is not one.
[ "$(awk -F'\t' 'NR>1 && $3 == "current"' "$T/choices.txt" | wc -l)" = 1 ] \
    && ok "…with exactly one marked current" \
    || bad "not exactly one current value"
# A switch is the same shape, which is what lets the window draw both from one
# delegate.
[ "$(ch show_news --rec | tail -n +2 | wc -l)" = 2 ] \
    && ok "a switch answers the same way, with two" \
    || bad "show_news did not offer two values"
ch nonsense >/dev/null 2>&1; [ $? = 2 ] \
    && ok "…and there are no choices for a setting that does not exist" \
    || bad "big choices accepted an unknown setting"

rm -f "$BGH/syn-arcade/big.conf"

echo
echo "── the remote ──"
#
# ⛔ BACK IS NOT ESCAPE. Escape quits deliberately — somebody at a keyboard has
# a way back that somebody on a sofa does not. A remote's Back is its
# most-pressed button and means "up one level"; folded in with Escape, the first
# press would close the interface and the way back in is a key combination
# nobody holding a remote can press.
BIGQML=data/syn-arcade-big.qml
has 'case Qt.Key_Back:' "$BIGQML" \
    && ok "the remote's Back key is handled" \
    || bad "Qt.Key_Back is not handled — a remote's Back does nothing"
awk '/case Qt.Key_Back:/,/break/' "$BIGQML" > "$BGH/back.qml"
has 'shell.nav("back")' "$BGH/back.qml" \
    && ok "…as Back, not as quit" \
    || bad "Back does not reach nav(\"back\")"
awk '/case Qt.Key_Escape:/,/break/' "$BIGQML" > "$BGH/esc.qml"
has 'quitNow' "$BGH/esc.qml" \
    && ok "…and Escape still quits, as it always did" \
    || bad "Escape stopped quitting"

for k in Key_Select Key_Menu Key_Guide Key_HomePage Key_ChannelUp Key_ChannelDown \
         Key_AudioRewind Key_AudioForward Key_Play Key_Stop Key_Cancel Key_Settings \
         Key_Pause Key_Close Key_LaunchMedia Key_PowerOff Key_Sleep \
         Key_MediaRecord; do
    has "Qt.$k" "$BIGQML" \
        && ok "$k reaches the shell" \
        || bad "Qt.$k is unhandled — that button does nothing on a remote"
done

# ⛔ PAUSE IS Qt.Key_Pause AND NOT Qt.Key_MediaPause, which is the whole reason
# Play worked and Pause did nothing. The kernel's rc6_mce keytable sends
# KEY_PAUSE — evdev 119, the same code as a keyboard's Pause/Break — so xkb
# gives it the plain `Pause` keysym. XF86AudioPause, which IS Key_MediaPause,
# is what a keyboard sends and no MCE remote does.
awk '/case Qt.Key_Pause:/,/break/' "$BIGQML" > "$BGH/pause.qml"
has 'mediaCmd' "$BGH/pause.qml" \
    && ok "the remote's Pause reaches the transport" \
    || bad "Qt.Key_Pause does not drive the player — Play works and Pause does not"

# ⛔ AND Qt.Key_Cancel IS STOP, NOT BACK. It was folded in with Back, which made
# the one button that means "stop playing" navigate up a shelf. `Cancel` is what
# xkeyboard-config puts on <STOP> (evdev KEY_STOP, 128) and nothing else on any
# evdev keyboard produces it.
awk '/case Qt.Key_Cancel:/,/break/' "$BIGQML" > "$BGH/cancel.qml"
has 'mediaCmd' "$BGH/cancel.qml" \
    && ok "…and the remote's Stop stops rather than going back" \
    || bad "Qt.Key_Cancel is still wired to navigation"

# ⛔ THREE BUTTONS ARRIVE AS key === 0 AND NO case CAN CATCH THEM. Qt has no
# Qt::Key for XF86OK, XF86Info or XF86MediaSelectProgramGuide, and KEY_NEXT and
# KEY_PREVIOUS have no keysym at all — xkeyboard-config maps nothing to those
# keycodes. The scancode is the only thing left, and QML's KeyEvent does not
# expose nativeVirtualKey, so it is the only thing available too.
has 'event.key === 0' "$BIGQML" \
    && ok "the keys Qt cannot name are matched by scancode" \
    || bad "nothing handles key === 0 — OK, Info and Guide do nothing"
awk '/event.key === 0/,/^ *}$/' "$BIGQML" > "$BGH/scan.qml"
for pair in "360:accept" "366:menu" "373:guide"; do
    code=${pair%%:*}; verb=${pair##*:}
    has "case $code:" "$BGH/scan.qml" \
        && ok "scancode $code reaches nav(\"$verb\")" \
        || bad "scancode $code is unhandled"
done
# ── the Power button asks rather than acts ──────────────────────────────────
#
# ⛔ SLEEP, RESTART AND POWER OFF ARE THREE IRREVERSIBLE THINGS AND A REMOTE HAS
# ONE BUTTON. A key wired straight to any of them is a key that does the wrong
# one, from four metres away, with somebody's game still running. So it opens a
# selector, and the page is the rows big.c already marks as the machine's own
# switches rather than a second list here.
for k in Key_PowerOff Key_Sleep; do
    awk -v k="$k" '$0 ~ ("case Qt." k ":"),/break/' "$BIGQML" > "$BGH/pwr.qml"
    has 'nav("power")' "$BGH/pwr.qml" \
        && ok "$k opens the power selector rather than acting" \
        || bad "Qt.$k does not open a selector — it may act on one press"
done
# ⚠ THROUGH nav() AND NOT AROUND IT. A button wired straight to a function is
# one the FIFO cannot send and therefore one the rig cannot drive — and the
# remote is exactly the device nobody has on the desk to try by hand.
has 'case "power":' "$BIGQML" \
    && ok "…as a nav word, so the pad and the rig can reach it too" \
    || bad "power is not a nav word — only a key can open it" 
has 'menuPage === "power"' "$BIGQML" \
    && ok "…and there is a power page for it to open" \
    || bad "no power page exists"
# ⛔ THE WAY OUT IS ON IT, so the page can never be empty — `show_power` off
# takes the three switches away and leaves Desktop and Quit.
awk '/menuPage === "power"/,/^$/' "$BIGQML" > "$BGH/pwrpage.qml"
has 'kind === "action"' "$BGH/pwrpage.qml" \
    && ok "…drawn from big.c's own action rows, so Desktop and Quit stay" \
    || bad "the power page builds its own list"

# ── Record is synui's recorder, not a second one ────────────────────────────
awk '/case Qt.Key_MediaRecord:/,/break/' "$BIGQML" > "$BGH/rec.qml"
has 'recordToggle' "$BGH/rec.qml" \
    && ok "the remote's Record key reaches the recorder" \
    || bad "Qt.Key_MediaRecord is unhandled"
says "$SA" --help > "$T/help.txt"
has 'big record' "$T/help.txt" \
    && ok "…and 'big record' is documented" \
    || bad "big record is undocumented"
# ⚠ synui OWNS screen recording: it knows the output, whether audio was asked
# for, and it draws the indicator. A recorder of this package's own would be a
# second answer to "am I recording", and the two would disagree the first time
# somebody used super+shift+r instead of the remote.
has 'dispatch' <(grep -A6 'static int big_record' src/big.c) \
    && ok "…by dispatching synui's own record action" \
    || bad "big record does not go through synui"

# ⚠ KEY_OK + 8 = 360, KEY_INFO + 8 = 366, KEY_EPG + 8 = 373. The +8 is the X
# convention Qt's Wayland plugin keeps, and the evdev codes are kernel ABI — so
# these numbers are checked against the header rather than remembered.
IEC=/usr/include/linux/input-event-codes.h
if [ -f "$IEC" ]; then
    for pair in "KEY_OK:360" "KEY_INFO:366" "KEY_EPG:373" \
                "KEY_NEXT:415" "KEY_PREVIOUS:420" "KEY_POWER2:364" \
                "KEY_DVD:397" "KEY_EJECTCD:169"; do
        name=${pair%%:*}; want=${pair##*:}
        raw=$(awk -v n="$name" '$1=="#define" && $2==n {print $3}' "$IEC" | head -1)
        got=$(printf '%d' "$raw" 2>/dev/null)
        [ -n "$got" ] && [ "$((got + 8))" = "$want" ] \
            && ok "$name is $got, so the shell's $want is right" \
            || bad "$name is $got — the shell says $want, expected $((got + 8))"
    done
else
    ok "(skipped the evdev code check: no linux headers installed)"
fi

# ⛔ EVERY ONE OF THOSE MUST BE A REAL Qt ENUM. An unknown Qt.Key_Foo in QML is
# `undefined`, and `case undefined:` simply never matches an integer — a dead
# branch that raises no error, logs nothing, and looks exactly like a handled
# key. Checked against Qt's own header rather than trusted.
QNS=/usr/include/qt6/QtCore/qnamespace.h
if [ -f "$QNS" ]; then
    unknown=$(grep -oE 'Qt\.Key_[A-Za-z0-9_]+' "$BIGQML" | sort -u |
              sed 's/^Qt\.//' | while read -r k; do
                  grep -qE "^ *$k( |=)" "$QNS" || echo "$k"
              done)
    [ -z "$unknown" ] \
        && ok "every Qt.Key_* in the shell is a real Qt enum" \
        || bad "these are not Qt enums, so their case never matches: $unknown"
else
    ok "(skipped the Qt enum check: no qt6 headers installed)"
fi

# ── the disc in the drive ───────────────────────────────────────────────────
#
# ⚠ EVERY BYTE OF THIS IS FIXTURED, and it has to be: the machine this is
# written on has no optical drive at all, most build machines have none, and
# the ones that do cannot be asked to have a Blu-ray in them. `udevadm` is
# stubbed to say what udev would say, /sys/block is a directory of empty
# folders, and mpv and eject are stubs that write down what they were asked to
# do rather than doing it — a suite that ran the real `eject` would open the
# tray of the machine running the build.
echo
echo "the disc in the drive"

DFX="$T/disc"
mkdir -p "$DFX/sys/sr0" "$DFX/bin" "$DFX/lib"
cat > "$DFX/bin/udevadm" <<'EOF'
#!/bin/sh
# What udev last probed about the media. $DISC_PROPS is the fixture.
case "$*" in
    */dev/sr0*) printf '%s\n' "$DISC_PROPS" | tr '|' '\n' ;;
esac
EOF
printf '#!/bin/sh\nprintf "%%s\\n" "$*" >> "$MPV_LOG"\n'   > "$DFX/bin/mpv"
printf '#!/bin/sh\nprintf "%%s\\n" "$*" >> "$EJECT_LOG"\n' > "$DFX/bin/eject"
chmod +x "$DFX/bin/udevadm" "$DFX/bin/mpv" "$DFX/bin/eject"
export MPV_LOG="$DFX/mpv.log" EJECT_LOG="$DFX/eject.log" DISC_PROPS=""
: > "$MPV_LOG"; : > "$EJECT_LOG"

# ⚠ PREPENDED to the real PATH rather than replacing it: `have()` shells out to
# `command -v`, and a PATH with no /bin is the trap that made an earlier
# fixture in this file model a state it could not reach.
disc() { ( PATH="$DFX/bin:$PATH"
           SYN_ARCADE_SYS_BLOCK="$DFX/sys" SYN_ARCADE_LIBDIR="$DFX/lib"
           export PATH SYN_ARCADE_SYS_BLOCK SYN_ARCADE_LIBDIR
           says "$SA" big disc "$@" ) }
tile() { ( PATH="$DFX/bin:$PATH"
           SYN_ARCADE_SYS_BLOCK="$DFX/sys" SYN_ARCADE_LIBDIR="$DFX/lib"
           export PATH SYN_ARCADE_SYS_BLOCK SYN_ARCADE_LIBDIR
           says "$SA" big apps --rec ) }

# A machine with no optical drive is the common one, and it must answer rather
# than fail: no drive, no rows, no tile.
# ⚠ NOT THROUGH says(), which always exits 0 — it is there to capture OUTPUT.
# An exit code is asked of the binary directly, as every other one here is.
SYN_ARCADE_SYS_BLOCK="$DFX/none" "$SA" big disc >/dev/null 2>&1
[ "$?" = 100 ] && ok "a machine with no optical drive says so (100)" \
               || bad "no drive did not report EX_EMPTY"

DISC_PROPS='ID_CDROM=1|ID_CDROM_MEDIA=1|ID_CDROM_MEDIA_DVD=1|ID_FS_LABEL=THE_LONG_GOOD_FRIDAY|ID_CDROM_MEDIA_STATE=complete'
[ "$(disc --rec | awk -F'\t' 'NR==2 {print $2}')" = dvd ] \
    && ok "a DVD reads as a DVD" \
    || bad "the DVD read as '$(disc --rec | awk -F'\t' 'NR==2 {print $2}')'"

# ⚠ THE DISC'S OWN NAME, with the underscores ISO 9660 forces on it taken back
# out. A tile reading THE LONG GOOD FRIDAY is what somebody is looking for; a
# fourth tile reading "DVD" beside Plex, Kodi and Music is not.
[ "$(disc --rec | awk -F'\t' 'NR==2 {print $3}')" = "THE LONG GOOD FRIDAY" ] \
    && ok "…and the tile is named after the disc" \
    || bad "the tile name is '$(disc --rec | awk -F'\t' 'NR==2 {print $3}')'"

tile | awk -F'\t' '$1=="disc"' | has 'media' \
    && ok "…and it is on the media shelf" \
    || bad "the disc tile is not on the media shelf"

# ⛔ THE TRAP THIS WHOLE READER IS WRITTEN AROUND. ID_CDROM_BD says the DRIVE
# can read Blu-ray; every Blu-ray drive on earth reports it with an empty tray.
# Matching it instead of ID_CDROM_MEDIA_BD puts a Blu-ray tile on the
# television of everybody who owns the drive.
DISC_PROPS='ID_CDROM=1|ID_CDROM_BD=1|ID_CDROM_DVD=1|ID_CDROM_MEDIA=0'
[ "$(disc --rec | awk -F'\t' 'NR==2 {print $2}')" = none ] \
    && ok "an empty Blu-ray drive is an empty drive, not a Blu-ray" \
    || bad "ID_CDROM_BD on an EMPTY drive was read as a disc"
tile | awk -F'\t' '$1=="disc"' | has . \
    && bad "an empty drive still drew a tile" \
    || ok "…and it draws no tile"

# A blank disc is media — every "is there a disc" test says yes — and there is
# nothing on it to play.
DISC_PROPS='ID_CDROM_MEDIA=1|ID_CDROM_MEDIA_CD_R=1|ID_CDROM_MEDIA_STATE=blank'
[ "$(disc --rec | awk -F'\t' 'NR==2 {print $2","$5}')" = "blank,0" ] \
    && ok "a blank disc is not something to play" \
    || bad "a blank disc read as '$(disc --rec | awk -F'\t' 'NR==2 {print $2","$5}')'"

DISC_PROPS='ID_CDROM_MEDIA=1|ID_CDROM_MEDIA_CD=1|ID_CDROM_MEDIA_TRACK_COUNT_AUDIO=12'
[ "$(disc --rec | awk -F'\t' 'NR==2 {print $2","$3}')" = "cd,Audio CD" ] \
    && ok "an audio CD is an audio CD" \
    || bad "the audio CD read as '$(disc --rec | awk -F'\t' 'NR==2 {print $2","$3}')'"

# A data disc has a label and a filesystem and is not a film.
DISC_PROPS='ID_CDROM_MEDIA=1|ID_CDROM_MEDIA_DVD=1|ID_FS_LABEL=BACKUPS|ID_CDROM_MEDIA_TRACK_COUNT_DATA=1'
: # (a data DVD is indistinguishable from a video one without reading it —
  #  see the note in big.c; it is offered, and mpv says so if it is not a film)

# ── what mpv is asked to do ─────────────────────────────────────────────────
#
# ⛔ THE DEVICE IS AN OPTION, NEVER PART OF THE URL. `dvd://[title][/device]`
# splits on the first slash, so `dvd:///dev/sr0` hands mpv a device of
# `dev/sr0` — a relative path, from whatever directory the shell was started
# in, which on the desktop launcher is `/`.
# ⚠ SPLIT ON `|`, not on a colon: every URL here ENDS in two of them, so a
# colon-separated field list names the disc kind "//" in the report.
for pair in "ID_CDROM_MEDIA_DVD=1|--dvd-device=/dev/sr0|dvd://" \
            "ID_CDROM_MEDIA_BD=1|--bluray-device=/dev/sr0|bd://" \
            "ID_CDROM_MEDIA_TRACK_COUNT_AUDIO=9|--cdda-device=/dev/sr0|cdda://"; do
    prop=${pair%%|*}; rest=${pair#*|}; devopt=${rest%%|*}; url=${rest##*|}
    : > "$MPV_LOG"
    DISC_PROPS="ID_CDROM_MEDIA=1|$prop"
    disc play >/dev/null 2>&1
    line=$(cat "$MPV_LOG")
    case "$line" in
        *"$devopt"*"$url"*) ok "$url is played with $devopt" ;;
        *) bad "$url was started as: $line" ;;
    esac
done

# ⚠ --force-window, on every kind. An audio CD has no video, so without it mpv
# maps no window at all — and the tile press has already stepped the television
# aside to make room for one.
has -- '--force-window' <<<"$(cat "$MPV_LOG")" \
    && ok "…and always with a window to step aside for" \
    || bad "mpv is started without --force-window"

# The socket the media buttons then drive it over.
has -- '--input-ipc-server' <<<"$(cat "$MPV_LOG")" \
    && ok "…and with the socket the transport drives it over" \
    || bad "mpv is started with no IPC socket"

# ⚠ SAID, NOT REFUSED. An unencrypted disc plays perfectly without either
# library and a home-made DVD is the kind somebody puts in first.
DISC_PROPS='ID_CDROM_MEDIA=1|ID_CDROM_MEDIA_DVD=1'
disc --rec | awk -F'\t' 'NR==2 {print $6}' | has 'libdvdcss' \
    && ok "an encrypted DVD names the package it needs" \
    || bad "the DVD row does not name libdvdcss"
: > "$DFX/lib/libdvdcss.so.2"
[ -z "$(disc --rec | awk -F'\t' 'NR==2 {print $6}')" ] \
    && ok "…and says nothing once it is installed" \
    || bad "libdvdcss is installed and the row still asks for it"
rm -f "$DFX/lib/libdvdcss.so.2"
DISC_PROPS='ID_CDROM_MEDIA=1|ID_CDROM_MEDIA_BD=1'
disc --rec | awk -F'\t' 'NR==2 {print $6}' | has 'libaacs' \
    && ok "a Blu-ray names its own" \
    || bad "the Blu-ray row does not name libaacs"

# ⛔ AND THE TILE NEEDS A PLAYER. mpv is UNTICKED in the installer's software
# list, so a stock machine can genuinely have a drive, a disc and nothing to
# play it with — and this table's rule is that a tile you can press is a tile
# that works. Posed with a PATH holding the fixture's udevadm and no mpv;
# `have()` asks the shell's `command -v`, which is a builtin, so a PATH with
# nothing else in it is a machine with nothing else installed.
mkdir -p "$DFX/nompv"
cp "$DFX/bin/udevadm" "$DFX/nompv/udevadm"
DISC_PROPS='ID_CDROM_MEDIA=1|ID_CDROM_MEDIA_DVD=1|ID_FS_LABEL=A_FILM'
nomp=$( PATH="$DFX/nompv" SYN_ARCADE_SYS_BLOCK="$DFX/sys" \
        SYN_ARCADE_LIBDIR="$DFX/lib" DISC_PROPS="$DISC_PROPS" \
        "$SA" big apps --rec 2>/dev/null | awk -F'\t' '$1=="disc"' )
[ -z "$nomp" ] \
    && ok "with no mpv there is no disc tile to press" \
    || bad "a disc tile was drawn on a machine with no player"
needs=$( PATH="$DFX/nompv" SYN_ARCADE_SYS_BLOCK="$DFX/sys" \
         SYN_ARCADE_LIBDIR="$DFX/lib" DISC_PROPS="$DISC_PROPS" \
         "$SA" big disc --rec 2>/dev/null | awk -F'\t' 'NR==2 {print $6}' )
[ "$needs" = mpv ] \
    && ok "…and the row says mpv is what is missing, before any decrypter" \
    || bad "the row asks for '$needs' on a machine with no player"

: > "$EJECT_LOG"
disc eject >/dev/null 2>&1
has '/dev/sr0' <<<"$(cat "$EJECT_LOG")" \
    && ok "eject opens the drive the disc is in" \
    || bad "eject was called as: $(cat "$EJECT_LOG")"

says "$SA" --help > "$T/help.txt"
has 'big disc' "$T/help.txt" \
    && ok "…and all of it is documented" \
    || bad "big disc is undocumented"

# ── the remote, while an application is in front ────────────────────────────
#
# ⛔ THE PROBLEM THIS SOLVES IS INVISIBLE FROM A DESK. Press a tile and big
# screen mode steps aside; keyboard focus belongs to the film or the browser,
# and five of the remote's buttons then reach NOTHING — OK is XF86OK, which no
# application binds; ⏭ and ⏮ have no keysym at all; Guide and Power likewise.
# So `big nav` reads them from the device and the shell acts on them itself.
#
# ⚠ DRIVEN THROUGH A FIFO, not through uinput. SYN_ARCADE_SYSFS and
# SYN_ARCADE_DEV are the same seams the pad fixtures use: a directory of
# capability masks, and a named pipe where the event node would be. Nothing
# here needs a device, a permission or a compositor.
echo
echo "the remote, with an application in front"

RFX="$T/remote"
mkdir -p "$RFX/sys/event99/device/capabilities" "$RFX/dev"

# The key mask, written the way sysfs writes one: 64-bit hex words, MOST
# SIGNIFICANT FIRST, so the LAST word holds bits 0..63.
rmask() {
    python3 -c '
import sys
words = [0] * 8
for b in (int(x) for x in sys.argv[1:]):
    words[b // 64] |= 1 << (b % 64)
print(" ".join("%x" % w for w in reversed(words)))' "$@"
}

KEY_OK=352 KEY_INFO=358 KEY_POWER2=356 KEY_EPG=365 KEY_NEXT=407 KEY_PREVIOUS=412
KEY_A=30 KEY_Z=44 KEY_PLAY=207 KEY_UP=103

remote_mask() { rmask "$@" > "$RFX/sys/event99/device/capabilities/key"; }
remote_say() {
    # Every argument is an evdev code: press, release, one frame each.
    #
    # ⛔ O_NONBLOCK, AND THE FAILURE IS AN ANSWER. Opening a FIFO for writing
    # BLOCKS UNTIL SOMETHING READS IT — for ever, with no timeout — and the
    # case this fixture exists to prove is exactly the one where nothing does:
    # a device that is not a remote is never opened, so this open has to fail
    # with ENXIO rather than hang the suite. It hung it, once.
    python3 -c '
import os, struct, sys, time
fd = -1
for _ in range(30):      # …and RETRIED, because the reader may still be starting
    try:
        fd = os.open(sys.argv[1], os.O_WRONLY | os.O_NONBLOCK)
        break
    except OSError:
        time.sleep(0.1)
if fd < 0:
    sys.exit(0)          # nothing opened the node, so there is nothing to say
for code in (int(c) for c in sys.argv[2:]):
    for value in (1, 0):
        os.write(fd, struct.pack("qqHHi", 0, 0, 1, code, value))
        os.write(fd, struct.pack("qqHHi", 0, 0, 0, 0, 0))
    time.sleep(0.05)
time.sleep(0.4)' "$RFX/dev/event99" "$@"
}
nav_words() {
    rm -f "$RFX/dev/event99"; mkfifo "$RFX/dev/event99"
    ( SYN_ARCADE_SYSFS="$RFX/sys" SYN_ARCADE_DEV="$RFX/dev" \
      timeout 6 "$SA" big nav > "$RFX/out.txt" 2>/dev/null ) &
    local navpid=$!
    sleep 0.7
    remote_say "$@" 2>/dev/null || true
    wait $navpid 2>/dev/null
    cat "$RFX/out.txt"
}

if command -v python3 >/dev/null 2>&1; then
    remote_mask $KEY_OK $KEY_EPG $KEY_NEXT $KEY_PREVIOUS $KEY_POWER2 $KEY_PLAY $KEY_UP
    out=$(nav_words $KEY_OK $KEY_NEXT $KEY_PREVIOUS $KEY_EPG $KEY_POWER2)
    want="remote:accept remote:next remote:prev remote:guide remote:power"
    [ "$(printf '%s' "$out" | tr '\n' ' ' | sed 's/ *$//')" = "$want" ] \
        && ok "the five buttons no application can receive arrive as words" \
        || bad "the remote said: $(printf '%s' "$out" | tr '\n' ' ')"

    # ⛔ AND NOTHING ELSE, WHICH IS THE WHOLE RULE. Play, pause, stop, ⏪ and ⏩
    # DO reach the application — mpv binds every one of them out of the box —
    # so reading them here as well would act on one press twice: the film would
    # pause and unpause on a single button.
    out=$(nav_words $KEY_PLAY $KEY_UP)
    [ -z "$out" ] \
        && ok "…and a key the application can receive is left to it" \
        || bad "a key with a keysym was also read as a word: $out"

    # ⛔ A KEYBOARD MUST NEVER MATCH. Reading one here would mean every key
    # pressed at the desk arriving as a word as well as reaching what is
    # focused — a double action on a machine somebody is using normally.
    remote_mask $KEY_OK $KEY_NEXT $KEY_A $KEY_Z
    out=$(nav_words $KEY_OK)
    [ -z "$out" ] \
        && ok "a keyboard is not a remote, whatever else is in its key map" \
        || bad "a device with letters on it was read as a remote: $out"
else
    ok "(skipped the remote fixture: no python3)"
fi

# ── a tap of Start is the game's, a hold is ours ────────────────────────────
#
# The same FIFO fixture, with a PAD in it this time. In GeForce NOW, Start opens
# the game's own menu — and while big screen mode is stepped aside it was also
# opening the on-screen keyboard, over the menu somebody was reaching for. The
# press still says `menu` (which is the Start MENU while the interface is on
# screen); only a hold says `keyboard`.
if command -v python3 >/dev/null 2>&1; then
    PFX="$T/holdpad"
    mkdir -p "$PFX/sys/event98/device/capabilities" "$PFX/dev"
    # A gamepad, by the same test udev's input_id makes: a gamepad BUTTON and
    # the two axes of a stick. BTN_GAMEPAD 304, BTN_START 315, ABS_X/ABS_Y 0/1.
    rmask 304 315 316 317 > "$PFX/sys/event98/device/capabilities/key"
    rmask 0 1                > "$PFX/sys/event98/device/capabilities/abs"
    echo 0                   > "$PFX/sys/event98/device/capabilities/ff"
    echo "Fixture Pad"       > "$PFX/sys/event98/device/name"

    rm -f "$PFX/dev/event98"; mkfifo "$PFX/dev/event98"
    ( SYN_ARCADE_SYSFS="$PFX/sys" SYN_ARCADE_DEV="$PFX/dev" \
      timeout 6 "$SA" big nav > "$PFX/out.txt" 2>/dev/null ) &
    holdpid=$!
    sleep 0.7
    python3 -c '
import os, struct, sys, time
fd = -1
for _ in range(30):
    try:
        fd = os.open(sys.argv[1], os.O_WRONLY | os.O_NONBLOCK)
        break
    except OSError:
        time.sleep(0.1)
if fd < 0:
    sys.exit(0)
def ev(t, c, v): os.write(fd, struct.pack("qqHHi", 0, 0, t, c, v))
START = 315
ev(1, START, 1); ev(0, 0, 0); time.sleep(0.2)      # a TAP, well under the hold
ev(1, START, 0); ev(0, 0, 0); time.sleep(0.6)
ev(1, START, 1); ev(0, 0, 0); time.sleep(1.0)      # …and a HOLD
ev(1, START, 0); ev(0, 0, 0); time.sleep(0.5)' "$PFX/dev/event98" 2>/dev/null || true
    wait $holdpid 2>/dev/null
    said=$(tr '\n' ' ' < "$PFX/out.txt" | sed 's/ *$//')
    [ "$said" = "menu menu keyboard" ] \
        && ok "a tap says menu, a hold says menu then keyboard" \
        || bad "the pad said: $said"
    # ⚠ ONCE PER HOLD. A word per poll would open and close the keyboard for as
    # long as the button was down.
    [ "$(grep -c '^keyboard$' "$PFX/out.txt")" = 1 ] \
        && ok "…and the hold says it exactly once" \
        || bad "a held Start said it $(grep -c '^keyboard$' "$PFX/out.txt") times"
else
    ok "(skipped the held-Start fixture: no python3)"
fi

# ⚠ THE WORDS ARE PREFIXED AND THE SHELL ACTS ON THEM ONLY WHILE IT IS STEPPED
# ASIDE. On screen this window has keyboard focus and the same press ALSO
# arrives as a key event, which is where it is handled — acting on both would
# take one press two shelves back.
grep -q 'if (shell.away) shell.navRemote(cmd.substring(7))' "$BIGQML"
check "a remote word is acted on only while an application is in front" $?
grep -q 'function navRemote' "$BIGQML"
check "…by turning each one into something that application understands" $?
awk '/function navRemote/,/^    }/' "$BIGQML" > "$BGH/navremote.qml"
has 'shell.key("Return")' "$BGH/navremote.qml" \
    && ok "OK becomes a Return, which a menu and a browser both take" \
    || bad "the remote's OK types nothing"
has 'mediaCmd("next")' "$BGH/navremote.qml" \
    && ok "…and skip becomes a transport press" \
    || bad "the remote's skip does nothing while away"
has 'comeBack' "$BGH/navremote.qml" \
    && ok "…and Guide is still the way back" \
    || bad "the remote cannot get back from an application"

# ── fast forward is not skip ────────────────────────────────────────────────
#
# ⛔ These two sent `next` and `prev` — the buttons either side of them on the
# same remote — so a film could be skipped out of and never wound through,
# which is the one thing ⏪ and ⏩ are for on the one device with no other way
# to do it.
awk '/case Qt.Key_AudioForward:/,/break/' "$BIGQML" > "$BGH/ff.qml"
has 'mediaCmd("forward")' "$BGH/ff.qml" \
    && ok "fast forward seeks rather than skipping a track" \
    || bad "Qt.Key_AudioForward is still a track skip"
awk '/case Qt.Key_AudioRewind:/,/break/' "$BIGQML" > "$BGH/rw.qml"
has 'mediaCmd("rewind")' "$BGH/rw.qml" \
    && ok "…and so does rewind" \
    || bad "Qt.Key_AudioRewind is still a track skip"

# ⛔ THE ROW AND THE PRESS ARE TWO SPELLINGS OF ONE LIST. When the array held
# three and the row drew five, every press landed one button to the left of the
# one that lit up.
n_model=$(awk '/Repeater \{/,/\]/' "$BIGQML" | grep -c 'act: "')
# ⚠ COUNT THE QUOTES, not the -F'"' fields: five quoted words make ELEVEN
# fields, and NF/2 is 5.5 — a number that matches nothing and reads as a fault
# in the thing being tested rather than in the test.
n_acts=$(awk '/property var mediaActs/ { print gsub(/"/, "") / 2 }' "$BIGQML")
[ "${n_model:-0}" = 5 ] && [ "${n_acts:-0}" = 5 ] \
    && ok "the media row and the presses behind it are the same five" \
    || bad "the row has $n_model buttons and the press list has $n_acts"

says "$SA" big transport nonsense > "$T/tv.txt" 2>&1
has 'forward' "$T/tv.txt" \
    && ok "a bad transport verb lists the ones there are, seek included" \
    || bad "the transport refusal does not mention forward"

# ── verdict ─────────────────────────────────────────────────────────────────

echo
echo "$pass passed, $fail failed"
[ "$fail" = 0 ]
