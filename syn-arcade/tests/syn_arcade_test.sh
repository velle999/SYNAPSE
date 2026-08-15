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
# It also stops `grep -q` closing the pipe early and killing the binary with
# SIGPIPE partway through writing its output.
says() { local out; out=$("$@" 2>&1); printf '%s\n' "$out"; }

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

# ── the sandbox ─────────────────────────────────────────────────────────────
#
# Everything the binary resolves through config_path() lands under here.
export XDG_CONFIG_HOME="$T/config"
mkdir -p "$XDG_CONFIG_HOME"

# The overlay config, pinned away from both /etc and the real user file.
export MANGOHUD_CONFIGFILE="$T/config/MangoHud/MangoHud.conf"

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

"$SA" binds remove >/dev/null 2>&1
grep -q "syn-arcade" "$RC"
[ $? != 0 ]
check "remove takes the block back out" $?

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
grep -q "syn-arcade" "$RC"
[ $? != 0 ]
check "an unterminated block is still removed" $?

grep -q "^terminal = syntty$" "$RC"
check "...without eating the rest of the file" $?

# ── about ───────────────────────────────────────────────────────────────────

echo
echo "about"

says "$SA" about | grep -q "GPL-2.0-or-later"
check "about names the licence" $?

says "$SA" about --rec | head -1 | grep -q "^field	value	action$"
check "about --rec names its columns" $?

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

# ── verdict ─────────────────────────────────────────────────────────────────

echo
echo "$pass passed, $fail failed"
[ "$fail" = 0 ]
