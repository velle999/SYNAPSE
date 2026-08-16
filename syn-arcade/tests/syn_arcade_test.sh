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

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

# The real one, kept so the assertions at the end can prove nothing reached it —
# the `fit` section redirects HOME, and $HOME by then is a temporary directory.
REAL_HOME=$HOME

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

"$SA" 2>&1 | grep -q "map learn"
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
! grep -E '^\s*(text|value):' "$GUIQML" | grep -q 'antimicrox'
check "the window no longer sends anybody to another program" $?

! grep -E '^\s*\+ "' "$GUIQML" | grep -q 'antimicrox'
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
grep -qE "^# (>>>|<<<) syn-arcade" "$RC" || grep -q "spawn syn-arcade" "$RC"
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
"$SA" big games --rec | cut -f2 | grep -qx "Second Disk Game"
check "a game on the SECOND library is found (libraryfolders.vdf is read)" $?

"$SA" big games --rec | cut -f2 | grep -q "Proton"
[ $? != 0 ]
check "Proton is not a game" $?

"$SA" big games --rec | cut -f2 | grep -q "Still Downloading"
[ $? != 0 ]
check "a manifest without StateFlags 4 is not installed" $?

"$SA" big games --all --rec | cut -f2 | grep -q "Proton"
check "--all puts the tools back" $?

# Most recently played first. On a gamepad every row down the list is a
# physical press, so alphabetical order costs eleven of them to reach the game
# somebody was playing yesterday.
[ "$("$SA" big games --rec | sed -n 2p | cut -f2)" = "Second Disk Game" ]
check "the most recently played game is first" $?

[ "$("$SA" big games --rec | sed -n 4p | cut -f2)" = "Hashed Art Game" ]
check "...and the least recent is last" $?

"$SA" big games --rec | grep -q "10/library_600x900.jpg"
check "cover art: the current per-appid layout" $?

"$SA" big games --rec | grep -q "40_library_600x900.jpg"
check "cover art: the legacy flat layout" $?

"$SA" big games --rec | grep -q "deadbeefdeadbeef/library_capsule.jpg"
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

"$SA" big apps --rec | cut -f1 | grep -q "^desktop$"
check "there is always a way out of a full-screen surface" $?

"$SA" big apps --rec | cut -f1 | grep -q "^poweroff$"
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
printf '%s' "$head1" | grep -q "shelf"
check "apps --rec carries the shelf column" $?

printf '%s' "$head1" | grep -q "pointer"
check "...and whether a tile wants the controller as a mouse" $?

printf '%s' "$head1" | grep -q "keys"
check "...and whether it wants the on-screen keyboard" $?

"$SA" big apps --rec | cut -f6 | grep -qx "system"
check "the power tiles are on the system shelf" $?

# The browser and the terminal are the two tiles the whole pointer/keyboard
# apparatus exists for. Neither is guaranteed installed on a build machine, so
# this asserts the RULE rather than the row: anything on the apps shelf that is
# not an action wants a pointer.
"$SA" big apps --rec | awk -F'\t' 'NR>1 && $6=="apps" && $7!="1" { bad=1 }
                                   END { exit bad?1:0 }'
check "every tile on the apps shelf wants a pointer" $?

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

printf '%s\n' "$apps" | awk -F'\t' 'NR==1 { exit ($NF == "iconfile") ? 0 : 1 }'
check "apps --rec ends with the resolved glyph path" $?

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
printf '%s\n' "$(says "$SA" big media --rec)" |
    awk -F'\t' 'NR==1 { exit ($NF == "iconfile") ? 0 : 1 }'
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

printf '%s' "$head1" | grep -q "full"
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
awk '/^static void fullscreen_after_launch/,/^}/' "$BIGC" | grep -q "fork()"
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
grep -q "already open — close one first" "$BIGQML"
check "...and a full pool says so instead of doing nothing" $?

# The register is the compositor's, not a tally kept in the shell — a private
# list drifts from the screen the moment anything is opened or closed
# elsewhere, and every close aimed at a stale row lands on nothing.
grep -q '"big", "windows", "--rec"' "$BIGQML"
check "what is open is ASKED of synui, never remembered" $?

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

"$SA" big news --rec | grep -q "Old%20News"
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

printf '%s\n' "$navout" | grep -qx "accept"
check "the bottom face button is 'accept'" $?

printf '%s\n' "$navout" | grep -qx "back"
check "the right face button is 'back'" $?

# A press and its release are one word, not two. A stream reporting both halves
# would move the selection twice per press.
[ "$(printf '%s\n' "$navout" | grep -c '^accept$')" = 1 ]
check "a button RELEASE emits nothing" $?

printf '%s\n' "$navout" | grep -qx "guide"
check "the guide button is its own word" $?

printf '%s\n' "$navout" | grep -qx "page-right"
check "a shoulder button pages" $?

printf '%s\n' "$navout" | grep -qx "menu"
check "start is 'menu'" $?

printf '%s\n' "$navout" | grep -qx "up"
check "a d-pad tap arriving in ONE read still becomes a direction" $?

printf '%s\n' "$navout" | grep -qx "right"
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
grep -q 'music_cmd(strcmp(state, "playing") == 0 ? "play"' src/big.c
check "a cold start is toggled into playing, not merely resumed" $?

# The stream is bounded by the menu being open. Twenty frames a second behind a
# full-screen game is the thing this file's header warns about.
grep -q 'running: shell.menuOpen && shell.musicLive' "$BIGQML"
check "the visualizer runs only while the menu is open" $?

# A parser pointed at another program's output, inside a try. cliamp answers
# {"ok":false} when it has no bands, and one throw here takes the menu down.
grep -q 'try {' "$BIGQML"
check "...and a bad frame cannot take the menu down with it" $?

grep -q 'onExited: shell.musicBands = \[\]' "$BIGQML"
check "...and the bars go rather than freezing on the last frame" $?

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
cat > "$MSTUB/cliamp" <<'EOF'
#!/bin/sh
echo "cliamp $*" >> "$CLIAMP_LOG"
[ "$1" = status ] && printf '{"ok":true,"state":"playing","track":{"title":"%s","path":"%s"}}' \
    "$CLIAMP_TRACK" "$CLIAMP_TRACK"
exit 0
EOF
chmod +x "$MSTUB/cliamp"
export CLIAMP_LOG="$T/cliamp.log" CLIAMP_TRACK=""
: > "$CLIAMP_LOG"

# ⚠ In a SUBSHELL for the same reason the synctl stub note above gives: an
# assignment in front of a shell FUNCTION persists after the call in bash.
music() { ( PATH="$MSTUB:$PATH"; export PATH; says "$SA" big music "$@" ); }

rm -f "$BIGCONF"
music source | grep -qE '^plex +Plex'
check "the source picker lists Plex first" $?

music source | grep -qE '^radio +Radio +· current'
check "...and an unset config reads as radio, which is what cliamp does" $?

music source --rec |
    awk -F'\t' '$1 == "plex" && $4 == "albums" { f = 1 } END { exit !f }'
check "...with an action column saying Plex has a library to pick from" $?

music source --rec |
    awk -F'\t' '$1 == "spotify" && $4 == "browse" { f = 1 } END { exit !f }'
check "...and that the two streaming services open cliamp instead" $?

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

# ── a Plex token is not something to draw on a television ───────────────────
#
# ⚠ THE MOST IMPORTANT ASSERTION IN THIS SECTION. cliamp reports a queued
# track's PATH as its title — it reads no tags — so a track streamed from Plex
# comes back as a URL with `?X-Plex-Token=…` on the end. Left alone, that is
# somebody's credential drawn four metres wide in the Start menu, in every
# screenshot of it, and in the records this command prints.
CLIAMP_TRACK='http://192.168.40.153:32400/library/parts/1/2/file.flac?X-Plex-Token=SECRETVALUE'
music status --rec | grep -q SECRETVALUE
[ $? != 0 ]
check "a Plex token never reaches the records the shell reads" $?

music status | grep -q SECRETVALUE
[ $? != 0 ]
check "...nor the line a person sees" $?

music status | grep -q 'file.flac'
check "...and what is left still names the track" $?

# The map that gives it a real name. Written by whatever queued the track,
# keyed on the path WITHOUT its query — which is the same string both sides
# have to agree on, and the reason the token is not in the cache either.
mkdir -p "$XDG_CACHE_HOME/syn-arcade"
printf '%s\t%s\n' \
    'http://192.168.40.153:32400/library/parts/1/2/file.flac' \
    'Linkin%20Park%20%E2%80%94%20With%20You' \
    > "$XDG_CACHE_HOME/syn-arcade/music-titles.rec"
music status | grep -q 'Linkin Park — With You'
check "a queued track is drawn with the name it was queued under" $?

rm -f "$XDG_CACHE_HOME/syn-arcade/music-titles.rec"
CLIAMP_TRACK=''

# The suite runs with SYN_ARCADE_NO_NET=1, so this is the refusal rather than a
# library: what matters is that it FAILS rather than hanging or pretending.
( PATH="$MSTUB:$PATH"; export PATH; "$SA" big music plex >/dev/null 2>&1 )
[ "$?" != 0 ]
check "with no network the Plex library says so instead of drawing nothing" $?

music plex 2>&1 | grep -q "cliamp setup"
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
printf '%s\n' "$out" | grep -q "^game      800x600$"
check "an existing gamescope line gives up its game size" $?

printf '%s\n' "$out" | grep -q "^screen    2560x1440$"
check "...and its screen size" $?

printf '%s\n' "$out" | grep -q "^env       WINEPREFIX=/home/you/Games/gangsters$"
check "...and its environment" $?

printf '%s\n' "$out" | grep -q -- "-- wine gangsters.exe$"
check "...and the game's own command, from after the --" $?

[ "$(printf '%s\n' "$out" | grep -c gamescope)" = 1 ]
check "...exactly ONE gamescope in the result, not two" $?

printf '%s\n' "$out" | grep -q "^name      Gangsters (Fullscreen)$"
check "a name that already says Fullscreen is not given a second one" $?

# --fsr-sharpness is gamescope's own alias for --sharpness; a line using it
# would otherwise lose the setting silently.
printf '%s\n' "$out" | grep -q -- "--sharpness 2"
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
[ -z "$(find "$REAL_HOME/.local/share/applications" -name 'syn-fit-*' \
        2>/dev/null)" ]
check "no wrapper reached the real applications menu" $?

[ -z "$(find "$REAL_HOME/Desktop" -name 'syn-fit-*' 2>/dev/null)" ]
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
find "$T" -name deadzones.state | grep -q . || true

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
grep -q 'k: "Start", v: "System"' "$BIGQML"
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

# ── verdict ─────────────────────────────────────────────────────────────────

echo
echo "$pass passed, $fail failed"
[ "$fail" = 0 ]
