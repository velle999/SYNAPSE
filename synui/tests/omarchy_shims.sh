#!/usr/bin/env bash
#
# omarchy_shims.sh — the two commands Omarchy's weather widgets call by name.
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
for s in "$LOC" "$NOTIFY"; do
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

printf '\n  %d passed, %d failed\n' "$pass" "$fails"
[ "$fails" = 0 ] || exit 1
echo "omarchy_shims: PASS"
