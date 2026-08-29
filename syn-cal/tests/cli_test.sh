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

# ⛔ NEW CALENDARS START OFF. Discovery on a work account turns up every shared
# calendar in the building; syncing them because they exist is how a planner
# becomes unreadable on first use.
[ "$("$S" --rec calendars work | tail -n +2 | awk -F'\t' '$3==1' | wc -l)" = 0 ]
check "…switched off until asked for" $?

"$S" enable work Home >/dev/null
[ "$("$S" --rec calendars work | tail -n +2 | awk -F'\t' '$3==1' | wc -l)" = 1 ]
check "enable turns exactly one on" $?

"$S" discover work >/dev/null
[ "$("$S" --rec calendars work | tail -n +2 | awk -F'\t' '$3==1' | wc -l)" = 1 ]
check "…and running discover again does not undo the choice" $?

# ── up ──────────────────────────────────────────────────────────────────────
mkdir -p "$T/store/work/Home"
printf 'BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:cli-1@x\r\nDTSTAMP:20260101T000000Z\r\nDTSTART:20260910T140000Z\r\nSUMMARY:Ship syn-cal\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n' \
  > "$T/store/work/Home/cli-1%40x.ics"

out=$("$S" --rec sync)
[ "$(echo "$out" | awk -F'\t' 'NR==2{print $6}')" = 1 ]
check "sync pushes a new local event" $?

curl -s -u tester:secret -X PROPFIND "http://127.0.0.1:$PORT/tester/home/" -H 'Depth: 1' \
     --data '<?xml version="1.0"?><d:propfind xmlns:d="DAV:"><d:prop><d:getetag/></d:prop></d:propfind>' \
  | grep -q 'cli-1-x-'
check "…and the server really has it, under a safe href" $?

out=$("$S" --rec sync)
[ "$(echo "$out" | awk -F'\t' 'NR==2{print $4+$5+$6+$7+$8+$9}')" = 0 ]
check "…and syncing again moves nothing" $?

# ── down, into a cold store ─────────────────────────────────────────────────
rm -rf "$T/store/work" "$T/store/state"/*.idx
out=$("$S" --rec sync)
[ "$(echo "$out" | awk -F'\t' 'NR==2{print $3}')" = 1 ]
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
[ "$(echo "$out" | awk -F'\t' 'NR==2{print $8}')" = 1 ]
check "deleting a file locally deletes it on the server" $?

# ── failure is reported, not swallowed ──────────────────────────────────────
"$S" sync nosuchaccount >/dev/null 2>&1
[ $? -ne 0 ]
check "syncing an account that does not exist fails" $?

"$S" enable work "No Such Calendar" >/dev/null 2>&1
[ $? -ne 0 ]
check "enabling a calendar that does not exist fails" $?

# ── the OAuth kinds, without needing an account anywhere ────────────────────

"$S" account add-google gmail >/dev/null 2>&1
[ $? -eq 2 ]
check "add-google without a client id is refused" $?

# ⛔ CAPTURED FIRST, NOT PIPED. `set -o pipefail` is on, and syn-cal exits 2
# here on purpose — so `syn-cal ... | grep -q` reports 2 whether or not grep
# matched, and the check fails for a command that did exactly the right thing.
# See [[reference_pipefail_grep_q_sigpipe]]; this is the same trap.
out=$("$S" account add-google gmail 2>&1)
echo "$out" | grep -q "console.cloud.google.com"
check "…and says where to get one" $?

"$S" account add-google gmail --client-id 123.apps.googleusercontent.com >/dev/null
check "add-google with one succeeds" $?

# ⚠ AN OAUTH ACCOUNT HAS NO PASSWORD. Describing its token as one is a wrong
# instruction as much as a wrong word: it sends people to `login --user`.
"$S" accounts | grep -q "signed in:"
check "…and is described as signed in, not as having a password" $?

"$S" account add-microsoft work365 --client-id abc >/dev/null
check "add-microsoft succeeds" $?

# ⛔ AND SYNCING IT SAYS WHY. Microsoft removed CalDAV; falling through to the
# CalDAV client would 404 against a URL the user never typed. The message must
# also not be "not signed in", which is advice that leads nowhere — signing in
# would not have helped either.
out=$("$S" sync work365 2>&1)
[ $? -ne 0 ]
check "syncing a Microsoft account fails" $?
echo "$out" | grep -q "Graph backend"
check "…naming Graph as what is missing, not the credentials" $?

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
