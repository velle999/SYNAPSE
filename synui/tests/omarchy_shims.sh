#!/usr/bin/env bash
#
# omarchy_shims.sh — the commands Omarchy's plugins call by name.
#
# ⚠ THE CONTRACT IS WITH SOFTWARE THIS REPOSITORY DOES NOT CONTROL, which is
# the whole reason these are worth a test. eduardodallecort.weather-radar
# hardcodes `omarchy-weather-location --set <city> <lat,lon>` and
# `omarchy-notification-send …` into its own QML — no configuration, no
# fallback — and then reads the answer back out of a JSON file it does not
# write. Every link in that chain fails silently:
#
#   · a missing binary is a Process that never starts, reported to the shell
#     log and not to the widget, so the city picker takes a click and does
#     nothing;
#   · a malformed file is parseFloat returning NaN, which the plugin's own
#     parser turns into "no location set" — the same symptom as no file at all;
#   · a rejected notify-send is an alert that never appears, on a feature whose
#     whole job is to speak up unprompted.
#
# So what is checked here is the FILE and the ARGUMENT LIST, not the weather.
# Both shims are driven with a fake HOME and a stubbed notify-send, because the
# real ones would write the developer's own location and put toasts on the seat
# the suite is running on.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
LOC=${1:-$HERE/../systemd/omarchy-weather-location.sh}
NOTIFY=${2:-$HERE/../systemd/omarchy-notification-send.sh}
SHELL_SHIM=${3:-$HERE/../systemd/omarchy-shell.sh}
for s in "$LOC" "$NOTIFY" "$SHELL_SHIM"; do
    [ -r "$s" ] || { echo "not readable: $s" >&2; exit 1; }
done
command -v python3 >/dev/null 2>&1 || { echo "SKIP: python3 not installed."; exit 77; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

pass=0 fails=0
ok()  { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  FAIL  %s\n' "$1" >&2; fails=$((fails + 1)); }
check() { # check <what> <want> <got>
    [ "$2" = "$3" ] && ok "$1" || bad "$1 (want '$2', got '$3')"
}

# ── omarchy-weather-location ────────────────────────────────────────────────

FAKE_HOME=$TMP/home
LOC_FILE=$FAKE_HOME/.local/state/omarchy/settings/weather.json
loc() { HOME=$FAKE_HOME bash "$LOC" "$@"; }

# What the plugin's own reader makes of the file, rather than what the file
# looks like: RadarModel.parseLocationFile is the only opinion that matters and
# it is stricter than JSON.parse — a location with unusable coordinates is
# reported as no location at all.
parsed() { # parsed <field>
    python3 - "$LOC_FILE" "$1" <<'PY'
import json, sys
try:
    with open(sys.argv[1]) as f:
        d = json.load(f)
except Exception:
    print("<unreadable>"); raise SystemExit
v = d.get(sys.argv[2])
print("" if v is None else v)
PY
}

mkdir -p "$FAKE_HOME"

loc --set "St Louis" "38.62727,-90.19789"
check "a set city is stored"        "St Louis" "$(parsed name)"
check "latitude survives as a number" "38.62727" "$(parsed latitude)"
check "longitude keeps its sign"    "-90.19789" "$(parsed longitude)"
check "and reads back for the menu" "St Louis" "$(loc)"

# ⛔ THE DIRECTORY DOES NOT EXIST ON A FRESH BOX. Omarchy's own header says a
# missing file means auto-detect, so nothing creates ~/.local/state/omarchy
# until the first city is chosen — a shim that assumed the directory would fail
# on exactly the machines that need it most.
rm -rf "$FAKE_HOME/.local"
loc --set "Reykjavík" "64.14,-21.94"
check "creates the state directory when it is absent" "Reykjavík" "$(parsed name)"

# A hand-written name with no coordinates is documented upstream, and the
# widgets geocode it themselves.
loc --set "Malibu"
check "a name alone is a valid location" "Malibu" "$(parsed name)"
check "and carries no coordinates"       ""       "$(parsed latitude)"

# ⚠ A CITY NAME IS SOMEBODY ELSE'S TEXT — the geocoder answers in the local
# spelling. An unescaped quote writes a file JSON.parse rejects, which the
# plugin reports as no location: the picker looks like it did nothing, which is
# the exact failure the shim exists to fix.
loc --set 'K"o\ln' "50.94,6.96"
check "a quote in the name does not break the file" 'K"o\ln' "$(parsed name)"
check "and round-trips back out"                    'K"o\ln' "$(loc)"

loc --set "Nowhere" "not,coords" 2>/dev/null
check "garbage coordinates are refused" "1" "$?"
check "and the previous location is left alone" 'K"o\ln' "$(parsed name)"

loc --clear
check "clear removes the file" "no" "$([ -e "$LOC_FILE" ] && echo yes || echo no)"

HOME=$FAKE_HOME bash "$LOC" --nonsense >/dev/null 2>&1
check "an unknown verb is a usage error" "1" "$?"

# ── omarchy-notification-send ───────────────────────────────────────────────

mkdir -p "$TMP/bin"
cat > "$TMP/bin/notify-send" <<'STUB'
#!/bin/sh
printf '%s\n' "$*"
STUB
chmod +x "$TMP/bin/notify-send"
notify() { PATH="$TMP/bin:$PATH" bash "$NOTIFY" "$@"; }

# The call weather-radar actually makes, verbatim from its Service.qml.
check "the storm alert reaches notify-send intact" \
      "-a Weather Radar -u critical -- Heavy rain Arriving in 40 minutes" \
      "$(notify --app-name "Weather Radar" -u critical "Heavy rain" "Arriving in 40 minutes")"

# ⚠ THEIR GLYPH IS A SEPARATE FIELD AND notify-send HAS NO SUCH THING. Dropping
# it would be a toast nobody recognises as the plugin's; forwarding -g would be
# no toast at all.
check "the glyph rides in the summary" \
      "-a omarchy-action -u low -- G  Rain Soon" \
      "$(notify -g G "Rain" "Soon")"

check "--flag=value is understood too" \
      "-a Foo -u critical -- H D" \
      "$(notify --app-name=Foo --urgency=critical "H" "D")"

check "an image becomes the icon" \
      "-a omarchy-action -u low -i /tmp/x.png -- H " \
      "$(notify --image /tmp/x.png "H")"

# ⛔ AN UNKNOWN OPTION IS AN ERROR, NOT A GUESS. Dropping the flag alone would
# leave its value standing where the headline goes, so `--sound chime` would
# post a notification that says "chime".
notify --sound chime "H" >/dev/null 2>&1
check "an unknown option is refused" "1" "$?"

notify -u normal >/dev/null 2>&1
check "no headline is a usage error" "1" "$?"

# --exec is offered as an action and run only when it is chosen.
cat > "$TMP/bin/notify-send" <<'STUB'
#!/bin/sh
echo default
STUB
check "a chosen action runs the program" "ran" \
      "$(notify "H" "D" --exec echo ran)"

cat > "$TMP/bin/notify-send" <<'STUB'
#!/bin/sh
echo ""
STUB
check "an unchosen action runs nothing" "" \
      "$(notify "H" "D" --exec echo ran)"

# ── omarchy-shell ───────────────────────────────────────────────────────────
#
# ⛔ THE ONE WHOSE ABSENCE WRITES NOTHING ANYWHERE. The three above are spawned
# as a Process, so Quickshell logs a missing binary to the shell log. This one
# is reached through `bar.run`, which is `sh -c` — exit 127 in a detached shell
# nobody is reading. YT Mini's bar button called it and the button was
# indistinguishable from a working one: no error, no log line, no clue.
#
# ⚠ WHAT IS CHECKED IS THE ARGUMENT LIST HANDED TO quickshell, because that IS
# the contract — the shim's whole job is to turn their verb into our IPC call,
# and the two failure modes are calling the wrong function and dropping the
# payload on the floor. quickshell is stubbed so no live shell is touched: the
# real one would drive the desktop the suite is running on.
cat > "$TMP/bin/quickshell" <<'STUB'
#!/bin/sh
echo "$@"
STUB
chmod +x "$TMP/bin/quickshell"

SHELL_TREE=$TMP/bar
mkdir -p "$SHELL_TREE"
: > "$SHELL_TREE/shell.qml"
shim() { PATH="$TMP/bin:$PATH" SYNUI_BAR="$SHELL_TREE" bash "$SHELL_SHIM" "$@"; }
# The tail of the call — everything after the config path, which is the part
# that says what the shell is being asked to do.
verb_of() { shim "$@" | sed 's|^-p [^ ]* ||'; }

check "toggle carries the payload to the shell" \
      "ipc call plugin toggleWith x.y {\"clipboard\":true}" \
      "$(verb_of shell toggle x.y '{"clipboard":true}')"
check "summon opens rather than toggling" \
      "ipc call plugin openWith x.y {\"url\":\"u\"}" \
      "$(verb_of shell summon x.y '{"url":"u"}')"
check "close needs no payload" \
      "ipc call plugin close x.y" \
      "$(verb_of shell close x.y)"

# ⚠ THE PAYLOAD-CARRYING SPELLING EVEN WHEN THERE IS NO PAYLOAD. quickshell
# matches an IPC call on arity: `toggleWith` with one argument is refused
# outright, so an empty payload has to be passed as an empty argument rather
# than left off. Dropping it would make the no-payload case the broken one.
check "an absent payload is still an argument" \
      "ipc call plugin toggleWith x.y " \
      "$(verb_of shell toggle x.y)"

# `shell` is their subcommand group and is accepted, not required.
check "the group name is optional by hand" \
      "ipc call plugin toggleWith x.y " \
      "$(verb_of toggle x.y)"

# ⛔ AND AN UNKNOWN VERB FAILS LOUDLY. A shim that swallowed a verb it does not
# implement would be the same dead button wearing a working command's name.
shim shell warp x.y >/dev/null 2>&1
check "an unimplemented verb is refused" "2" "$?"
shim shell toggle >/dev/null 2>&1
check "a missing plugin id is a usage error" "2" "$?"

printf '\n  %d passed, %d failed\n' "$pass" "$fails"
[ "$fails" = 0 ] || exit 1
echo "omarchy_shims: PASS"
