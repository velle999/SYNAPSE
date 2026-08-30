#!/usr/bin/env bash
#
# cli_test.sh — the binary, against a real server, the way a person uses it.
#
# The C tests drive the engine directly. This drives the COMMANDS, because the
# GUI will be a front end over `syn-cal --rec` and the verbs are therefore an
# interface with two consumers, not an afterthought.
#
# ⛔ DBUS IS POINTED AT NOTHING. libsecret would otherwise write into the real
# login keyring of whoever is running the suite, and leave it there. Pointing it
# at a dead socket also exercises the fallback — the path that must say "file"
# out loud rather than claim a keyring saved something.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

S=${1:-./build/syn-cal}
[ -x "$S" ] || { echo "not executable: $S" >&2; exit 1; }

if ! command -v radicale >/dev/null 2>&1; then
    echo "  skip  radicale is not installed, cannot test the commands end to end"
    exit 0
fi

pass=0; fail=0
ok()  { pass=$((pass+1)); echo "  ok    $1"; }
bad() { fail=$((fail+1)); echo "  FAIL  $1"; }
check() { if [ "$2" = 0 ]; then ok "$1"; else bad "$1"; fi; }

T=$(mktemp -d); PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; [ -n "$PID" ] && wait "$PID" 2>/dev/null; rm -rf "$T"; }
trap cleanup EXIT

PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1]);s.close()')
mkdir -p "$T/collections"
printf 'tester:secret\n' > "$T/users"
radicale --storage-filesystem-folder="$T/collections" --server-hosts="127.0.0.1:$PORT" \
         --auth-type=htpasswd --auth-htpasswd-filename="$T/users" \
         --auth-htpasswd-encryption=plain --logging-level=error >"$T/radicale.log" 2>&1 &
PID=$!
for i in $(seq 1 100); do
    curl -s -o /dev/null --max-time 1 -u tester:secret "http://127.0.0.1:$PORT/" && break
    kill -0 "$PID" 2>/dev/null || { echo "  FAIL  radicale did not start"; tail -5 "$T/radicale.log"; exit 1; }
    sleep 0.1
done

MK='<?xml version="1.0"?><c:mkcalendar xmlns:d="DAV:" xmlns:c="urn:ietf:params:xml:ns:caldav"><d:set><d:prop><d:displayname>%s</d:displayname></d:prop></d:set></c:mkcalendar>'
for c in Home Work; do
    lc=$(echo "$c" | tr 'A-Z' 'a-z')
    curl -s -u tester:secret -o /dev/null -X MKCALENDAR "http://127.0.0.1:$PORT/tester/$lc/" \
         -H 'Content-Type: application/xml' --data "$(printf "$MK" "$c")"
done

export SYNCAL_HOME="$T/store"
export DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent-syncal-test
URL="http://127.0.0.1:$PORT/"

"$S" account add work "$URL" --user tester >/dev/null
check "account add" $?

printf 'secret\n' | "$S" login work >/dev/null 2>"$T/login.err"
check "login reads the password from stdin" $?

# ⛔ AND SAYS WHERE IT WENT. With no keyring the fallback is a file, and the one
# thing it must not do is stay quiet about that.
grep -q "no keyring is running" "$T/login.err"
check "…and says out loud that no keyring took it" $?

[ "$("$S" --rec accounts | awk -F'\t' 'NR==2{print $5}')" = "file" ]
check "…and reports the secret as living in a file, not claiming a keyring" $?

"$S" discover work >/dev/null
check "discover finds the calendars" $?

[ "$("$S" --rec calendars work | tail -n +2 | wc -l)" = 2 ]
check "…both of them" $?

# ⛔ AND THE FIRST DISCOVERY TURNS THEM ON. An account that is added, signed in,
# discovered and then still shows an empty calendar is a setup that completed
# successfully and produced nothing.
[ "$("$S" --rec calendars work | tail -n +2 | awk -F'\t' '$3==1' | wc -l)" = 2 ]
check "…and a new account's calendars are switched on" $?

"$S" disable work Home >/dev/null
[ "$("$S" --rec calendars work | tail -n +2 | awk -F'\t' '$3==1' | wc -l)" = 1 ]
check "disable turns exactly one off" $?

# ⛔ ONLY THE FIRST. Re-running discovery must not undo a choice — the whole
# point of being able to turn one off.
"$S" discover work >/dev/null
[ "$("$S" --rec calendars work | tail -n +2 | awk -F'\t' '$3==1' | wc -l)" = 1 ]
check "…and running discover again does not undo the choice" $?

"$S" enable work Home >/dev/null
[ "$("$S" --rec calendars work | tail -n +2 | awk -F'\t' '$3==1' | wc -l)" = 2 ]
check "enable turns it back on" $?

# ── up ──────────────────────────────────────────────────────────────────────
mkdir -p "$T/store/work/Home"
printf 'BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:cli-1@x\r\nDTSTAMP:20260101T000000Z\r\nDTSTART:20260910T140000Z\r\nSUMMARY:Ship syn-cal\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n' \
  > "$T/store/work/Home/cli-1%40x.ics"

out=$("$S" --rec sync)
[ "$(echo "$out" | awk -F'\t' '$2=="Home"{print $6}')" = 1 ]
check "sync pushes a new local event" $?

curl -s -u tester:secret -X PROPFIND "http://127.0.0.1:$PORT/tester/home/" -H 'Depth: 1' \
     --data '<?xml version="1.0"?><d:propfind xmlns:d="DAV:"><d:prop><d:getetag/></d:prop></d:propfind>' \
  | grep -q 'cli-1-x-'
check "…and the server really has it, under a safe href" $?

out=$("$S" --rec sync)
[ "$(echo "$out" | awk -F'\t' 'NR>1{t+=$4+$5+$6+$7+$8+$9} END{print t+0}')" = 0 ]
check "…and syncing again moves nothing" $?

# ── down, into a cold store ─────────────────────────────────────────────────
rm -rf "$T/store/work" "$T/store/state"/*.idx
out=$("$S" --rec sync)
[ "$(echo "$out" | awk -F'\t' '$2=="Home"{print $3}')" = 1 ]
check "a cold client pulls the event back down" $?

"$S" --rec events work Home | grep -q "Ship%20syn-cal"
check "…and events lists it, percent-encoded for a front end" $?

# ── a dry run must not touch anything ───────────────────────────────────────
printf 'BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:cli-2@x\r\nDTSTAMP:20260101T000000Z\r\nDTSTART:20260911T140000Z\r\nSUMMARY:Not really\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n' \
  > "$T/store/work/Home/cli-2%40x.ics"
"$S" --dry-run sync >/dev/null
curl -s -u tester:secret -X PROPFIND "http://127.0.0.1:$PORT/tester/home/" -H 'Depth: 1' \
     --data '<?xml version="1.0"?><d:propfind xmlns:d="DAV:"><d:prop><d:getetag/></d:prop></d:propfind>' \
  | grep -q 'cli-2-x-'
[ $? -ne 0 ]
check "--dry-run pushes nothing" $?

# ── the deletion path, through the commands ─────────────────────────────────
rm -f "$T/store/work/Home/cli-2%40x.ics"
"$S" sync >/dev/null 2>&1
rm -f "$T/store/work/Home/cli-1%40x.ics"
out=$("$S" --rec sync)
[ "$(echo "$out" | awk -F'\t' '$2=="Home"{print $8}')" = 1 ]
check "deleting a file locally deletes it on the server" $?

# ── failure is reported, not swallowed ──────────────────────────────────────
"$S" sync nosuchaccount >/dev/null 2>&1
[ $? -ne 0 ]
check "syncing an account that does not exist fails" $?

"$S" enable work "No Such Calendar" >/dev/null 2>&1
[ $? -ne 0 ]
check "enabling a calendar that does not exist fails" $?

# ── the agenda reads the store, expands rules, and groups by day ────────────

# ⛔ NAMED NOTHING LIKE ITS UID, deliberately. A vdir is a public format and a
# file may have been restored from a backup or written by khal; every consumer
# used to rebuild <uid>.ics from the UID and miss it, which showed up as an
# agenda with a silent hole in it.
python3 - "$SYNCAL_HOME" <<'PY'
import sys, os, datetime
root = sys.argv[1]
d = os.path.join(root, "work", "Home"); os.makedirs(d, exist_ok=True)
# ⛔ FROM LOCAL MIDNIGHT, THE SAME ANCHOR THE AGENDA USES. Hanging these off
# `now` instead makes the fixture drift through the window as the day goes on:
# `agenda --days=4` runs from the start of today, so the daily rule's third
# instance at +75h leaves the range once the clock passes 21:00, and this file
# fails for the last three hours of every day and passes every morning.
base = datetime.datetime.now().astimezone().replace(
    hour=0, minute=0, second=0, microsecond=0)
def ics(uid, off, summ, extra=""):
    utc = datetime.timezone.utc
    s = (base + datetime.timedelta(hours=off)).astimezone(utc).strftime("%Y%m%dT%H%M%SZ")
    e = (base + datetime.timedelta(hours=off + 1)).astimezone(utc).strftime("%Y%m%dT%H%M%SZ")
    return (f"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:{uid}\r\n"
            f"DTSTAMP:20260101T000000Z\r\nDTSTART:{s}\r\nDTEND:{e}\r\n"
            f"SUMMARY:{summ}\r\n{extra}END:VEVENT\r\nEND:VCALENDAR\r\n")
open(os.path.join(d, "not-the-uid.ics"), "w").write(
    ics("agenda-1@x", 3, "Coffee with Sam", "LOCATION:The kitchen\r\n"))
open(os.path.join(d, "series.ics"), "w").write(
    ics("agenda-2@x", 27, "Standup", "RRULE:FREQ=DAILY;COUNT=3\r\n"))
PY

rows=$("$S" --rec agenda --days=4 | tail -n +2 | wc -l)
[ "$rows" = 4 ]
check "agenda expands a one-off plus three of a daily rule" $?

"$S" --rec agenda --days=4 | grep -q "Coffee%20with%20Sam"
check "…with the summary percent-encoded for a front end" $?

"$S" --rec agenda --days=4 | grep -q "The%20kitchen"
check "…and the location" $?

# ⛔ THE FILE NAMED not-the-uid.ics IS THE ONE THAT WAS BROKEN.
"$S" --rec agenda --days=4 | grep -q "agenda-1%40x"
check "…including an event whose filename does not match its UID" $?

[ "$("$S" --rec agenda --days=4 | tail -n +2 | awk -F'\t' '$4==1' | wc -l)" = 3 ]
check "…and only the rule's instances are marked recurring" $?

"$S" agenda --days=4 | grep -qE "^[A-Z][a-z]+day"
check "the human agenda groups by day" $?

# ── the month grid ──────────────────────────────────────────────────────────
#
# ⚠ FIXED MONTHS FOR THE SHAPE, and today's month only for the things that are
# genuinely about today. Checking the grid against whatever month the build runs
# in is exactly how the agenda cases above came to pass every morning and fail a
# release at 21:01. February 2026 is twenty-eight days opening on a Sunday
# whenever anybody runs this.

[ "$("$S" --rec month --from=2026-02 | tail -n +2 | wc -l)" = 28 ]
check "the month grid answers one record per day" $?

[ "$("$S" --rec month --from=2028-02 | tail -n +2 | wc -l)" = 29 ]
check "…twenty-nine in a leap February" $?

# Weeks start on Sunday here — the block below changes it and changes it back.
"$S" --rec month --from=2026-02 | tail -n +2 | head -1 | grep -qP '^2026-02-01\t[0-9]+\t0\t0\t0\t'
check "…each in the cell its weekday says: 1 Feb 2026 is a Sunday" $?

[ "$("$S" --rec month --from=2026-02 | tail -n +2 | awk -F'\t' '{print $3}' | sort -n | tail -1)" = 3 ]
check "…so twenty-eight days opening in column 0 fit in four rows" $?

# ⛔ THE SHAPE A `days / 7` GETS WRONG IN THE OTHER DIRECTION. 1 Feb 2027 is a
# Monday, which is column 1 of a Sunday-first grid — and one column in is enough
# to push the same twenty-eight days into a fifth row.
[ "$("$S" --rec month --from=2027-02 | tail -n +2 | awk -F'\t' '{print $3}' | sort -n | tail -1)" = 4 ]
check "…and opening one column in, five" $?

[ "$("$S" --rec month | tail -n +2 | awk -F'\t' '$6==1' | wc -l)" = 1 ]
check "exactly one day is marked today" $?

# The fixture above put a one-off on today and a daily rule on the days after.
[ "$("$S" --rec month | tail -n +2 | awk -F'\t' '$6==1 {print $7}')" -ge 1 ]
check "…and today's cell counts the event that is on it" $?

# ⛔ THE MONTH ENDS WHERE THE NEXT ONE BEGINS. Loading the 1st plus 31 days
# pulled the following month's events into this month's grid.
[ "$("$S" --rec month --from=2026-02 | tail -n +2 | tail -1 | cut -f1)" = "2026-02-28" ]
check "…and no cell belongs to the month after" $?

"$S" --rec month --from=2026-02-14 | tail -n +2 | head -1 | grep -q '^2026-02-01'
check "a full date names the month it falls in" $?

"$S" month --from=nonsense >/dev/null 2>&1
[ $? -ne 0 ]
check "a --from that is not a month fails" $?

# ── which day the week starts on ────────────────────────────────────────────
#
# ⛔ SUNDAY UNLESS SOMEBODY SAYS OTHERWISE, and the heading moves with the grid.
# A grid drawn Sunday-first under a Monday-first heading is off by one all month
# and reads as the grid being broken.

[ "$("$S" weekstart)" = "sunday" ]
check "weeks start on Sunday until told otherwise" $?

"$S" month | grep -q "Su Mo Tu We Th Fr Sa"
check "…and the grid is headed from Sunday" $?

# 1 August 2026 is a Saturday: the last column Sunday-first, the second-to-last
# Monday-first. dow is 6 either way — a column is not a weekday.
"$S" --rec month --from=2026-08 | tail -n +2 | head -1 | grep -qP '^2026-08-01\t[0-9]+\t0\t6\t6\t'
check "…so the 1st sits in column 6, and says it is a Saturday" $?

"$S" weekstart mon >/dev/null
check "the week start can be changed" $?

[ "$("$S" weekstart)" = "monday" ]
check "…and it stays changed" $?

"$S" month | grep -q "Mo Tu We Th Fr Sa Su"
check "…the heading follows it" $?

"$S" --rec month --from=2026-08 | tail -n +2 | head -1 | grep -qP '^2026-08-01\t[0-9]+\t0\t5\t6\t'
check "…and so does the column, while the weekday does not" $?

# ⛔ A TYPO IS REFUSED, NOT ROUNDED TO A DEFAULT.
"$S" weekstart tuesday >/dev/null 2>&1
[ $? -ne 0 ]
check "a day that is not sun or mon is refused" $?

[ "$("$S" weekstart)" = "monday" ]
check "…and the setting is left alone" $?

"$S" weekstart sun >/dev/null
check "and back to Sunday" $?

"$S" month | grep -q "Su Mo Tu We Th Fr Sa"
check "the human grid is a week wide, headed by weekday" $?

# ── making an event, which is the point of the thing ────────────────────────

# ⚠ --in, BECAUSE TWO CALENDARS ARE ON HERE. With one it is inferred; with
# several, guessing would put an appointment on a calendar nobody named.
"$S" new "Untargeted" --at "2026-09-21 13:15" >/dev/null 2>&1
[ $? -ne 0 ]
check "a new event refuses to guess between two calendars" $?

"$S" new "Lunch, then dentist" --at "2026-09-21 13:15" --for 45m --remind 15m \
     --where "Clinic; room 2" --in work/Home >/dev/null
check "a new event can be made" $?

[ "$("$S" --rec agenda --from=2026-09-21 --days=1 | tail -n +2 | wc -l)" = 1 ]
check "…and it is there when the calendar is read" $?

"$S" --rec agenda --from=2026-09-21 --days=1 | grep -q "Lunch%2C%20then%20dentist"
check "…with the comma in its name intact, not truncated at it" $?

"$S" --rec agenda --from=2026-09-21 --days=1 | grep -q "Clinic%3B%20room%202"
check "…and the semicolon in its location" $?

grep -q "TRIGGER:-PT15M" "$SYNCAL_HOME/work/Home/"*.ics
check "…and a reminder that fires before it, not after" $?

EV=$("$S" --rec agenda --from=2026-09-21 --days=1 | tail -1 | cut -f9 \
     | python3 -c 'import sys,urllib.parse;print(urllib.parse.unquote(sys.stdin.read().strip()))')

# ⛔ AN EDIT THAT DOES NOT MENTION THE TIME MUST NOT MOVE IT. Recovering the
# start through the recurrence expander silently returned nothing and wrote the
# event at 1970 — an appointment that simply stops appearing.
"$S" edit "$EV" --title "Dentist" >/dev/null
[ "$("$S" --rec agenda --from=2026-09-21 --days=1 | tail -n +2 | wc -l)" = 1 ]
check "renaming an event leaves it on its day" $?

"$S" --rec agenda --from=2026-09-21 --days=1 | grep -q "Dentist"
check "…under its new name" $?

# ⛔ AND THE ESCAPES MUST NOT COMPOUND. ics_prop hands back the raw property, so
# re-composing without unescaping doubles every backslash — invisible until the
# second edit, by which point it is on the server too.
"$S" edit "$EV" --title "Dentist" >/dev/null
"$S" edit "$EV" --title "Dentist" >/dev/null
# -F: in a regex the backslash would escape the semicolon and match a line
# that never had one, which is exactly the corruption being tested for.
[ "$(grep -hoF 'LOCATION:Clinic\; room 2' "$SYNCAL_HOME/work/Home/"*.ics | wc -l)" = 1 ]
check "…and three edits leave the location exactly as typed" $?

grep -q "TRIGGER:-PT15M" "$SYNCAL_HOME/work/Home/"*.ics
check "…and keep a reminder the edit never mentioned" $?

"$S" edit "$EV" --at "2026-09-21 09:00" --for 2h >/dev/null
"$S" --rec agenda --from=2026-09-21 --days=1 | tail -1 | cut -f1,2 \
  | awk '{ if ($2 - $1 == 7200) exit 0; else exit 1 }'
check "moving an event sets its length too" $?

# ⛔ AN ALL-DAY EVENT LANDS ON ITS OWN DATE. libical resolves a VALUE=DATE to
# midnight UTC and every view here formats in local time, so west of the
# meridian a birthday drew on the day before — in all three views at once.
"$S" new "Birthday" --at 2026-09-22 --in work/Home >/dev/null
[ "$("$S" --rec agenda --from=2026-09-22 --days=1 | tail -n +2 | grep -c Birthday)" = 1 ]
check "an all-day event is on the day it was given" $?

[ "$("$S" --rec agenda --from=2026-09-21 --days=1 | tail -n +2 | grep -c Birthday)" = 0 ]
check "…and not on the day before it" $?

grep -q "DTSTART;VALUE=DATE:20260922" "$SYNCAL_HOME/work/Home/"*.ics
check "…written as a date rather than an instant" $?

"$S" new "No when" --in work/Home >/dev/null 2>&1
[ $? -ne 0 ]
check "an event with no time is refused" $?

"$S" new "Bad time" --at "sometime tuesday" --in work/Home >/dev/null 2>&1
[ $? -ne 0 ]
check "…and so is a date nothing can read" $?

"$S" delete "$EV" >/dev/null
[ "$("$S" --rec agenda --from=2026-09-21 --days=1 | tail -n +2 | wc -l)" = 0 ]
check "an event can be deleted" $?

"$S" delete "no-such-event@x" >/dev/null 2>&1
[ $? -ne 0 ]
check "…and deleting one that is not there fails rather than claiming success" $?

rm -rf "$SYNCAL_HOME/work/Home"

# ── the OAuth kinds, without needing an account anywhere ────────────────────

# ⚠ BOTH ANSWERS ARE CORRECT, AND WHICH ONE IS RIGHT IS A BUILD OPTION. A build
# with google_client_id set must add the account with no flag at all — that is
# the whole point of shipping an id. A build without one must still refuse and
# say where to get one. Asserting only the refusal would turn the fix into a
# test failure the day the id lands.
#
# ⛔ CAPTURED FIRST, NOT PIPED. `set -o pipefail` is on, and syn-cal exits 2 in
# the no-id case on purpose — so `syn-cal ... | grep -q` reports 2 whether or
# not grep matched, and the check fails for a command that did exactly the
# right thing. See [[reference_pipefail_grep_q_sigpipe]]; this is the same trap.
out=$("$S" account add-google gmail 2>&1); rc=$?
if [ "$rc" -eq 0 ]; then
	check "add-google needs no flag when this build ships an id" 0
	grep -q "googleusercontent.com" "$SYNCAL_HOME/accounts.conf"
	check "…and the shipped id was recorded" $?
	"$S" account remove gmail >/dev/null
else
	[ "$rc" -eq 2 ]
	check "add-google without a client id is refused" $?
	echo "$out" | grep -q "console.cloud.google.com"
	check "…and says where to get one" $?
fi

"$S" account add-google gmail --client-id 123.apps.googleusercontent.com >/dev/null
check "add-google with an explicit id succeeds" $?

# ⚠ THE OVERRIDE MUST WIN. A --client-id that is silently replaced by the
# shipped one sends somebody's calendar through the wrong project.
grep -q "client_id = 123.apps.googleusercontent.com" "$SYNCAL_HOME/accounts.conf"
check "…and an explicit id beats the shipped one" $?

# ⚠ AN OAUTH ACCOUNT HAS NO PASSWORD. Describing its token as one is a wrong
# instruction as much as a wrong word: it sends people to `login --user`.
"$S" accounts | grep -q "signed in:"
check "…and is described as signed in, not as having a password" $?

# ⚠ isatty IS THE WRONG QUESTION FOR A WINDOW, so login takes the decision as a
# flag. Both spellings must PARSE — an unrecognised option exits 2 from usage(),
# and the GUI would then look like it had failed to sign in when it had in fact
# failed to start.
#
# ⛔ AGAINST AN ACCOUNT THAT DOES NOT EXIST, on purpose. A real login opens a
# loopback listener and waits five minutes for a browser that is never coming.
# "no account called …" is exit 1 — reached only after the flag was accepted,
# which is exactly the thing being tested.
for flag in --browser --no-browser; do
    out=$("$S" login nosuchaccount "$flag" 2>&1); rc=$?
    [ "$rc" -eq 1 ] && ! echo "$out" | grep -qi "unknown option"
    check "login accepts $flag" $?
done

# ⚠ AND THE FLAG MUST DO SOMETHING, not merely parse. --no-browser prints the
# authorisation URL instead of opening one, which is the branch a machine with
# no browser and the whole test suite depend on.
#
# ⛔ CAPTURED UNDER `timeout`, NOT RUN TO COMPLETION. After printing the URL the
# command opens a loopback listener and waits five minutes for a redirect that
# is never coming, so the test reads what it needs and kills it. The timeout's
# own exit status is therefore meaningless here and is deliberately discarded.
# ⚠ AN EXPLICIT id, NOT THE SHIPPED ONE. These three checks are about the URL's
# SHAPE, which does not depend on whose project it names — and a build
# configured without google_client_id would otherwise fail to add the account at
# all and take the three of them down with it.
"$S" account add-google urltest --client-id 123.apps.googleusercontent.com >/dev/null 2>&1
url=$(timeout 6 "$S" login urltest --no-browser 2>/dev/null | head -1) || true
echo "$url" | grep -q '^https://accounts.google.com/o/oauth2/v2/auth?'
check "login --no-browser prints the authorisation URL instead of opening one" $?

# PKCE, and the parameter that makes a refresh token arrive at all: without
# access_type=offline Google returns one on the FIRST authorisation only, and
# the account silently stops working an hour after the second.
echo "$url" | grep -q 'code_challenge_method=S256'
check "…and the URL carries a PKCE challenge" $?

echo "$url" | grep -q 'access_type=offline'
check "…and asks for offline access, so a refresh token comes back" $?

"$S" account remove urltest >/dev/null 2>&1

"$S" account add-microsoft work365 --client-id abc >/dev/null
check "add-microsoft succeeds" $?

# ⛔ AND IT ASKS FOR CREDENTIALS, NOT FOR A BACKEND. Graph is built now, so an
# unsigned-in Microsoft account must fail on the sign-in — the honest reason —
# rather than on anything about CalDAV. The account has no token here, and
# nothing in this suite talks to Microsoft.
out=$("$S" sync work365 2>&1)
[ $? -ne 0 ]
check "syncing a Microsoft account with no token fails" $?
echo "$out" | grep -q "not signed in"
check "…because it is not signed in, which is the actual reason" $?

# The backend is chosen by the ACCOUNT KIND, not by the URL — a Graph calendar
# URL handed to the CalDAV client answers 404 to PROPFIND, which reads as "my
# account is broken".
grep -q "e->kind == ACC_MICROSOFT" "$(dirname "$0")/../src/main.c"
check "…and the backend follows the account kind" $?

# ⛔ AND A FAILED SYNC MUST NOT CLAIM SUCCESS. "Already up to date" after an
# error is the most reassuring possible way to say nothing happened.
! echo "$out" | grep -q "Already up to date"
check "…and does not report 'Already up to date'" $?

"$S" account remove gmail >/dev/null
"$S" account remove work365 >/dev/null

"$S" account remove work >/dev/null
[ ! -f "$T/store/state/secret.work.password" ]
check "removing an account takes its stored password with it" $?

echo
echo "$pass/$((pass+fail)) passed"
[ "$fail" = 0 ] || { echo "--- radicale log ---" >&2; tail -20 "$T/radicale.log" >&2; }
exit $([ "$fail" = 0 ] && echo 0 || echo 1)
