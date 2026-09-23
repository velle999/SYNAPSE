#!/usr/bin/env bash
#
# scan_test.sh — syn-scan's suite.
#
# ⛔ EVERY PATH THIS SUITE TOUCHES IS COMPOSED FROM $ROOT, and $ROOT is a mktemp
# directory. Not one hard-coded /tmp, /var or $HOME anywhere below. syn-clean's
# suite hard-coded two paths its own header promised it had not, ran `clean
# --all`, and deleted the real /tmp of whoever typed `meson test` — three times
# before anyone noticed. This program MOVES FILES. The same mistake here moves
# somebody's.
#
# ⛔ AND THE ENGINES ARE STUBS. The suite must not need clamav installed, must
# not depend on a signature database that changes weekly, and must never run a
# real rootkit check against the machine running the tests. Each stub prints a
# recorded sample of the real engine's output; what is under test is OUR
# parsing, quarantine and record protocol.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

BIN=${1:?usage: scan_test.sh /path/to/syn-scan}
[ -x "$BIN" ] || { echo "not executable: $BIN" >&2; exit 1; }

ROOT=$(mktemp -d "${TMPDIR:-/tmp}/syn-scan-test.XXXXXX") || exit 1
trap 'rm -rf "$ROOT"' EXIT

STUBS="$ROOT/stubs"
SCANME="$ROOT/scanme"
export SYNSCAN_HOME="$ROOT/state"
# ⛔ FORCE THE clamscan PATH. This box may be running clamd, and neither the
# real socket nor the real /usr/bin/clamdscan can be hidden with PATH. Pointing
# at a socket that cannot exist is what keeps the suite testing the stubs
# instead of scanning the developer's actual machine.
export SYNSCAN_CLAMD_SOCKET="$ROOT/no-such-clamd.sock"
# ⛔ AND THE SIGNATURE DATABASE IS A FIXTURE. syn-scan checks every file in
# clamscan's database directory before it runs; against the real
# /var/lib/clamav the suite's result would depend on the build machine's
# freshclam — and on a machine with no clamav, every scan would refuse.
export SYNSCAN_CLAMAV_DBDIR="$ROOT/clamav-db"
mkdir -p "$STUBS" "$SCANME" "$SYNSCAN_HOME" "$SYNSCAN_CLAMAV_DBDIR"
printf 'stub\n' > "$SYNSCAN_CLAMAV_DBDIR/main.cvd"

pass=0 fail=0
ok()   { pass=$((pass+1)); printf '  ok   %s\n' "$1"; }
bad()  { fail=$((fail+1)); printf '  FAIL %s\n' "$1"; [ $# -gt 1 ] && printf '       %s\n' "$2"; }

# ── stub engines ────────────────────────────────────────────────────────────
#
# ⚠ Real clamscan output, kept verbatim including the awkward parts: a
# signature name with a dot and a dash, and a path containing ": " — which is
# why the parser reads from the right and not the left.
cat > "$STUBS/clamscan" <<STUB
#!/bin/sh
echo "\$@" > "$STUBS/clamscan.args"
echo "$SCANME/eicar.com: Win.Test.EICAR_HDB-1 FOUND"
echo "$SCANME/clean.txt: OK"
echo "$SCANME/notes: chapter 2.txt: Win.Trojan.Agent-1 FOUND"
echo "$SCANME/big.zip: Heuristics.Limits.Exceeded ERROR"
exit 1
STUB

# ⚠ The stub records its argv so the suite can assert HOW clamscan is invoked,
# not just how its output is parsed.
cat > "$STUBS/clamscan.args" </dev/null
cat > "$STUBS/rkhunter" <<'STUB'
#!/bin/sh
echo "Warning: The file properties have changed:"
echo "Warning: Hidden directory found: /dev/.udev"
exit 1
STUB

cat > "$STUBS/chkrootkit" <<'STUB'
#!/bin/sh
echo "Checking \`lkm'... You have 2 process hidden for ps command"
exit 0
STUB

chmod +x "$STUBS"/*
# ⛔ syn-scan SEES ONLY THE STUBS. The stub dir used to go first on the normal
# PATH, which shadows a real engine only while the stub is there and runnable:
# the moment a test made one unrunnable, or asked for an engine with none on
# PATH, the machine's own rkhunter, chkrootkit or clamscan was found behind
# it. Five checks failed on every machine with the engines installed — and
# check() runs this suite, so syn-scan could not be BUILT on the very machines
# that use it. The script's own tools keep the normal PATH; only syn-scan's
# runs get the stubs alone. (A stub has no #! line; execvp falls back to
# /bin/sh by absolute path, so it needs nothing on PATH.)
sc() { PATH="$STUBS" "$BIN" "$@"; }

printf 'harmless\n' > "$SCANME/clean.txt"
printf 'X5O!P%%@AP[4\\PZX54(P^)7CC)7}$EICAR\n' > "$SCANME/eicar.com"

# ── 1. engines: an absent engine is reported, not hidden ────────────────────
out=$(sc --rec engines)
grep -q '^#engine	id	name	present	runnable	path$' <<<"$out" \
  && ok "engines --rec emits the header row the window keys off" \
  || bad "engines --rec header" "got: $(head -1 <<<"$out")"

grep -q '^engine	clamav	.*	1	1	' <<<"$out" \
  && ok "engines finds the stub clamscan on PATH" \
  || bad "engines clamav present"

# ⛔ PRESENT AND RUNNABLE ARE TWO FACTS. Arch ships /usr/bin/rkhunter as 0700
# root:root, so an ordinary user cannot execute an engine that IS installed.
# Reporting that as "not installed" sends somebody to reinstall a package they
# already have — while lynis, auditing as root, finds it and awards MALW-3276.
chmod 0600 "$STUBS/chkrootkit"
out=$(sc --rec engines)
grep -q '^engine	chkrootkit	.*	1	0	' <<<"$out" \
  && ok "an installed but unexecutable engine reads present=1 runnable=0" \
  || bad "present/runnable split" "$(grep chkrootkit <<<"$out")"

# ⚠ CAPTURE, THEN GREP. `set -o pipefail` is on and syn-scan exits 1 when it
# has findings, so `sc ... | grep -q` reports failure even when grep
# matched — the assertion would fail on a working program.
sysout=$(sc scan --system 2>&1)
grep -qi 'needs root' <<<"$sysout" \
  && ok "a scan says an engine needs root rather than 'not installed'" \
  || bad "needs-root message" "$sysout"
chmod 0755 "$STUBS/chkrootkit"

# ── 2. clamav parsing ───────────────────────────────────────────────────────
out=$(sc --rec scan "$SCANME")
n=$(grep -c '^finding	clamav	' <<<"$out")
[ "$n" = 3 ] && ok "clamscan: 2 FOUND + 1 ERROR parsed, OK line ignored" \
             || bad "clamscan finding count" "expected 3, got $n"

grep -q '^finding	clamav	infected	'"$SCANME"'/eicar.com	Win.Test.EICAR_HDB-1	' <<<"$out" \
  && ok "clamscan: path and signature split correctly" \
  || bad "clamscan eicar row" "$(grep clamav <<<"$out" | head -1)"

# ⛔ THE ONE THAT MATTERS. A path containing ": " parsed from the left gives a
# truncated path — and a truncated path is a file the user did not scan being
# named as infected.
grep -q "notes: chapter 2.txt	Win.Trojan.Agent-1" <<<"$out" \
  && ok "clamscan: a path containing ': ' survives the parse" \
  || bad "clamscan colon-in-path" "$(grep -i chapter <<<"$out")"

grep -q '^finding	clamav	error	' <<<"$out" \
  && ok "clamscan: ERROR becomes a verdict, not a silent drop" \
  || bad "clamscan ERROR row"

# ── 2a. archives are scanned ────────────────────────────────────────────────
#
# ⛔ THE REAL ARRIVAL PATH ON THIS DISTRO. A mod pack is a zip and the payload
# inside it is a Windows binary — data to Linux, and structurally invisible to
# synguard, which is the entire reason this program exists. Verified against
# real clamav 1.5.4 on 2026-09-10: EICAR stored inside modpack.zip as
# textures/readme.exe WAS detected. Dropping this flag would silently turn that
# case off and every test above would still pass.
grep -q -- '--scan-archive=yes' "$STUBS/clamscan.args" \
  && ok "clamscan is asked to look inside archives" \
  || bad "--scan-archive missing" "argv was: $(cat "$STUBS/clamscan.args")"

grep -q -- '--recursive' "$STUBS/clamscan.args" \
  && ok "clamscan is asked to recurse" || bad "--recursive missing"

# ── 2b. the clamd path, when there is a clamd ───────────────────────────────
#
# ⛔ --fdpass IS THE WHOLE TEST. clamd runs as the clamav user and cannot open
# a file in somebody's home; without that flag every scan of a user's own files
# comes back "File path check failure: Permission denied. ERROR" — once per
# file. Verified against a real clamd 2026-09-10, both with and without.
cat > "$STUBS/clamdscan" <<STUB
#!/bin/sh
echo "\$@" > "$STUBS/clamdscan.args"
echo "$SCANME/eicar.com: Eicar-Test-Signature FOUND"
STUB
chmod +x "$STUBS/clamdscan"
: > "$ROOT/fake-clamd.sock"

out=$(SYNSCAN_CLAMD_SOCKET="$ROOT/fake-clamd.sock" sc --rec scan "$SCANME")
grep -q -- '--fdpass' "$STUBS/clamdscan.args" \
  && ok "the clamd path passes --fdpass" \
  || bad "--fdpass missing" "argv was: $(cat "$STUBS/clamdscan.args" 2>/dev/null)"

grep -q '^finding	clamav	infected	.*Eicar-Test-Signature' <<<"$out" \
  && ok "the clamd path parses the same record shape" || bad "clamdscan parse"

# ⚠ And the fallback is what runs when there is no socket — the ordinary case.
rm -f "$STUBS/clamscan.args"
sc --rec scan "$SCANME" >/dev/null
grep -q -- '--scan-archive=yes' "$STUBS/clamscan.args" 2>/dev/null \
  && ok "with no clamd socket it falls back to clamscan" \
  || bad "clamscan fallback not taken"

# ── 3. system engines only run for --system ─────────────────────────────────
out=$(sc --rec scan "$SCANME")
grep -q 'rkhunter' <<<"$out" \
  && bad "rkhunter ran on a file scan" "a path list means nothing to it" \
  || ok "a file scan does not run the system engines"

out=$(sc --rec scan --system)
grep -q '^finding	rkhunter	suspect	' <<<"$out" \
  && ok "--system runs rkhunter and drops its 'Warning:' prefix" \
  || bad "rkhunter --system"

grep -q '^finding	chkrootkit	suspect	' <<<"$out" \
  && ok "--system runs chkrootkit" || bad "chkrootkit --system"

# ── 4. --only ───────────────────────────────────────────────────────────────
out=$(sc --rec --only clamav scan "$SCANME")
grep -q 'rkhunter' <<<"$out" && bad "--only clamav ran another engine" \
                             || ok "--only runs exactly the engine named"

sc --only nosuchengine scan "$SCANME" >/dev/null 2>&1 \
  && bad "--only accepted an engine that does not exist" \
  || ok "--only refuses an unknown engine"

# ── 5. quarantine round-trip ────────────────────────────────────────────────
victim="$SCANME/victim.bin"
printf 'original contents\n' > "$victim"
chmod 0644 "$victim"
before=$(sha256sum < "$victim")

sc --yes quarantine take "$victim" >/dev/null 2>&1
[ -e "$victim" ] && bad "quarantine take left the original in place" \
                 || ok "quarantine take removes the original"

id=$(sc --rec quarantine list | awk -F'\t' '/^quarantine/{print $2; exit}')
[ -n "$id" ] && ok "quarantine list names what it holds" || bad "quarantine list empty"

# ⛔ 0600 AND NOT EXECUTABLE. A quarantine full of files that are still +x has
# only moved the problem.
blob="$SYNSCAN_HOME/quarantine/$id"
perm=$(stat -c %a "$blob" 2>/dev/null)
[ "$perm" = 600 ] && ok "quarantined file is 0600, whatever it was before" \
                  || bad "quarantine permissions" "got $perm"

sc --yes quarantine restore "$id" >/dev/null 2>&1
after=$(sha256sum < "$victim" 2>/dev/null)
[ "$before" = "$after" ] && ok "restore returns the exact bytes" \
                         || bad "restore content mismatch"
perm=$(stat -c %a "$victim" 2>/dev/null)
[ "$perm" = 644 ] && ok "restore returns the original mode" \
                  || bad "restore mode" "expected 644, got $perm"

# ── 6. restore never clobbers ───────────────────────────────────────────────
printf 'replacement\n' > "$victim"
sc --yes quarantine take "$victim" >/dev/null 2>&1
id=$(sc --rec quarantine list | awk -F'\t' '/^quarantine/{print $2; exit}')
printf 'somebody put this back\n' > "$victim"
sc --yes quarantine restore "$id" >/dev/null 2>&1
grep -q 'somebody put this back' "$victim" \
  && ok "restore refuses to overwrite a file that reappeared" \
  || bad "restore clobbered a replacement"

# ── 7. the program has no delete ────────────────────────────────────────────
#
# ⛔ NOT A STYLE CHECK. The design promise is that a false positive can always
# be undone. A `delete` verb added later would break that silently.
sc scan --delete "$SCANME" >/dev/null 2>&1 \
  && bad "a --delete option exists" \
  || ok "there is no --delete"

sc --help 2>&1 | grep -qiE '^\s*syn-scan (delete|remove)' \
  && bad "help advertises a delete command" \
  || ok "help advertises no delete command"

# ── 8. --dry-run changes nothing ────────────────────────────────────────────
printf 'untouched\n' > "$SCANME/dry.bin"
sc --dry-run --yes quarantine take "$SCANME/dry.bin" >/dev/null 2>&1
[ -e "$SCANME/dry.bin" ] && ok "--dry-run leaves the file alone" \
                         || bad "--dry-run moved a file"

# ── 9. diagnostics stay off stdout in --rec ─────────────────────────────────
#
# ⚠ A warning printed on stdout is a parse error in the window, not a message.
out=$(sc --rec --only clamav scan "$ROOT/does-not-exist" 2>/dev/null)
grep -qv '^\(#\|finding\|engine\|quarantine\|status\)' <<<"${out:-}" && true
badline=$(grep -vE '^(#|finding|engine|quarantine|status)' <<<"${out:-}" | grep -v '^$' | head -1)
[ -z "$badline" ] && ok "--rec stdout carries records only" \
                  || bad "non-record on stdout in --rec" "$badline"

# ── 10. a scan that ran nothing never reports a clean machine ───────────────
#
# ⛔ THE WORST FAILURE THIS PROGRAM COULD HAVE. With no engine installed it
# printed "Nothing found.", exited 0, and wrote a clean result into the state
# file for `status` and the bar badge to repeat back for a week. A scan that
# did not happen must not look like a scan that found nothing.
emptypath="$ROOT/no-engines"; mkdir -p "$emptypath"
noeng_home="$ROOT/state-noeng"; mkdir -p "$noeng_home"
out=$(PATH="/nonexistent-for-tests" SYNSCAN_HOME="$noeng_home" \
      "$BIN" --only chkrootkit scan --system 2>&1)
rc=$?
grep -qi 'nothing found' <<<"$out" \
  && bad "reported 'Nothing found' with no engine installed" \
  || ok "a scan with no engine does not report a clean machine"
[ "$rc" -ne 0 ] && ok "a scan that ran nothing exits non-zero" \
                || bad "exit status" "expected non-zero, got $rc"
[ -e "$noeng_home/last-scan" ] \
  && bad "a scan that ran nothing wrote a clean result to the state file" \
  || ok "a scan that ran nothing records no status"

# ── 11. rkhunter: a warning, its detail, and rkhunter's own trouble ─────────
#
# ⛔ Every line rkhunter printed used to be a finding. The weekly sweep runs
# sandboxed, rkhunter could not write /var/log/rkhunter.log there, and that
# sentence was reported every week as the one Suspicious finding Settings
# counted. A "Warning:" line is a finding, an indented line belongs to it, and
# anything else is the engine's problem — listed, never counted.
cat > "$STUBS/rkhunter" <<STUB
echo "\$@" > "$STUBS/rkhunter.args"
echo "Warning: The file properties have changed:"
echo "         File: /usr/bin/foo"
echo "         Current hash: abc123"
echo "Warning: Hidden directory found: /dev/.udev"
echo "Logfile directory is not writable: /var/log/rkhunter.log"
exit 1
STUB
chmod +x "$STUBS/rkhunter"
rk_home="$ROOT/state-rk"; mkdir -p "$rk_home"
out=$(SYNSCAN_HOME="$rk_home" sc --rec --only rkhunter scan --system 2>/dev/null); rc=$?
[ "$(grep -c '^finding	rkhunter	suspect	' <<<"$out")" = 2 ] \
  && ok "two warnings are two findings" || bad "rkhunter warning count" "$out"
grep -q '^finding	rkhunter	suspect	[^	]*	The file properties have changed: File: /usr/bin/foo Current hash: abc123	' <<<"$out" \
  && ok "an indented line belongs to the warning above it" \
  || bad "rkhunter continuation" "$(grep properties <<<"$out")"
grep -q '^finding	rkhunter	incomplete	[^	]*	Logfile directory is not writable' <<<"$out" \
  && ok "rkhunter's own complaint is 'incomplete', not a finding" \
  || bad "rkhunter engine trouble" "$out"
[ "$rc" = 1 ] && ok "…and the exit status counts the two warnings" || bad "exit" "$rc"
grep -q -- "--logfile $rk_home/rkhunter.log" "$STUBS/rkhunter.args" \
  && ok "rkhunter logs into syn-scan's state, which the weekly unit can write" \
  || bad "rkhunter --logfile" "$(cat "$STUBS/rkhunter.args")"
human=$(SYNSCAN_HOME="$rk_home" sc --only rkhunter scan --system 2>&1)
grep -q '2 things need a look' <<<"$human" \
  && ok "the summary counts the warnings only" || bad "summary count" "$human"
grep -q 'rkhunter did not finish: Logfile directory is not writable' <<<"$human" \
  && ok "…and says rkhunter did not finish, and why" || bad "incomplete line" "$human"

# ── 12. what the last scan found is kept, and status says what it was ──────
#
# Settings showed "1 outstanding" and nothing said what: only the count was
# saved. Both halves of the weekly sweep — the system checks, then the files —
# are kept, and the count is both of them.
st_home="$ROOT/state-status"; mkdir -p "$st_home"
SYNSCAN_HOME="$st_home" sc --only rkhunter scan --system >/dev/null 2>&1
SYNSCAN_HOME="$st_home" sc --only clamav scan "$SCANME" >/dev/null 2>&1
grep -q '^findings=4$' "$st_home/last-scan" \
  && ok "the saved count is both halves: 2 system + 2 files (the unreadable one is not counted)" \
  || bad "saved count" "$(cat "$st_home/last-scan")"
out=$(SYNSCAN_HOME="$st_home" sc --rec status)
grep -q '^status	ran	[0-9]*	4$' <<<"$out" && ok "status --rec keeps its status row" \
  || bad "status row" "$out"
[ "$(grep -c '^finding	' <<<"$out")" = 6 ] \
  && ok "status --rec lists every finding it saved, the incomplete one too" \
  || bad "status finding rows" "$out"
grep -q "^finding	clamav	infected	$SCANME/eicar.com	" <<<"$out" \
  && ok "…with the path" || bad "status clamav row" "$out"
out=$(SYNSCAN_HOME="$st_home" sc status 2>&1)
grep -q "$SCANME/eicar.com" <<<"$out" && grep -q 'The file properties have changed' <<<"$out" \
  && ok "plain status names what was found, not just how many" \
  || bad "human status list" "$out"
grep -q '4 things need a look' <<<"$out" && ok "…and the count" || bad "human count" "$out"
wk=$(SYNSCAN_HOME="$st_home" sc status --weekly 2>&1)
grep -q 'Weekly scan:' <<<"$wk" && grep -q "$SCANME/eicar.com" <<<"$wk" \
  && ok "status --weekly reads the same kind of record" || bad "--weekly" "$wk"

old_home="$ROOT/state-old"; mkdir -p "$old_home"
printf 'started=1\nfinished=2\nfindings=1\n' > "$old_home/last-scan"
out=$(SYNSCAN_HOME="$old_home" sc status 2>&1)
grep -q 'does not say what' <<<"$out" \
  && ok "a record from before the list was kept says so, rather than nothing" \
  || bad "old record" "$out"

# ── 13. the window's app_id, which is how the dock finds its icon ───────────
cat > "$STUBS/quickshell" <<STUB
echo "QS_APP_ID=\$QS_APP_ID"
STUB
chmod +x "$STUBS/quickshell"
out=$(sc gui 2>&1)
grep -q '^QS_APP_ID=syn-scan$' <<<"$out" \
  && ok "gui runs quickshell as app_id syn-scan, matching syn-scan.desktop" \
  || bad "gui app_id" "$out"
# ⚠ AND AN INHERITED ONE IS REPLACED: opened from Settings, the window would
# otherwise wear Settings' id and share its dock entry.
out=$(QS_APP_ID=syn-settings sc gui 2>&1)
grep -q '^QS_APP_ID=syn-scan$' <<<"$out" \
  && ok "…even when started from another quickshell app" \
  || bad "gui kept an inherited app_id" "$out"

# ── 14. a signature file clamscan cannot read ───────────────────────────────
#
# ⛔ clamscan SKIPS AN UNREADABLE DATABASE FILE WITHOUT A WORD and scans on what
# is left — exit 0, "OK". On 2026-09-22 daily.cld was 0640 clamav:clamav and
# every scan a person ran used main.cvd alone. A stub cannot reproduce that
# silence; what is under test is that syn-scan no longer depends on clamscan
# to report it.
db="$ROOT/clamav-db-partial"; mkdir -p "$db"
printf 'stub\n' > "$db/main.cvd"
printf 'stub\n' > "$db/daily.cld"
printf 'stub\n' > "$db/freshclam.dat"
chmod 0000 "$db/daily.cld" "$db/freshclam.dat"
if [ -r "$db/daily.cld" ]; then
  # root reads a 0000 file, so there is nothing unreadable to find.
  printf '  skip the unreadable-database checks: running as root\n'
else
  db_home="$ROOT/state-db"; mkdir -p "$db_home"
  out=$(SYNSCAN_CLAMAV_DBDIR="$db" SYNSCAN_HOME="$db_home" sc --rec scan "$SCANME" 2>/dev/null)
  grep -q "^finding	clamav	incomplete	$db/daily.cld	could not read $db/daily.cld " <<<"$out" \
    && ok "an unreadable signature file is an 'incomplete' row naming it" \
    || bad "unreadable daily.cld" "$out"
  [ "$(grep -c '^finding	clamav	\(infected\|error\)	' <<<"$out")" = 3 ] \
    && ok "…and the scan still runs on the signatures that are there" \
    || bad "partial-database scan" "$out"
  grep -q 'freshclam\.dat' <<<"$out" \
    && bad "freshclam's state file was taken for a signature file" "$out" \
    || ok "freshclam.dat is not a signature file"
  human=$(SYNSCAN_CLAMAV_DBDIR="$db" SYNSCAN_HOME="$db_home" sc scan "$SCANME" 2>&1)
  grep -q "clamav did not finish: could not read $db/daily.cld" <<<"$human" \
    && ok "a person is told the scan ran without it" || bad "human incomplete line" "$human"
  grep -q '2 things need a look' <<<"$human" \
    && ok "…and it is not counted as a finding" || bad "summary count" "$human"
  grep -q '^findings=2$' "$db_home/last-scan" \
    && ok "…nor in the saved record" || bad "saved count" "$(cat "$db_home/last-scan")"

  # Nothing readable at all: clamscan would refuse to scan and print only to
  # stderr, and that empty stream must not become "Nothing found."
  chmod 0000 "$db/main.cvd"
  none_home="$ROOT/state-db-none"; mkdir -p "$none_home"
  out=$(SYNSCAN_CLAMAV_DBDIR="$db" SYNSCAN_HOME="$none_home" sc --only clamav scan "$SCANME" 2>&1); rc=$?
  grep -q 'cannot be read by this account' <<<"$out" && ! grep -qi 'nothing found' <<<"$out" \
    && ok "a database with nothing readable in it is refused, not scanned" \
    || bad "all-unreadable database" "$out"
  [ "$rc" -ne 0 ] && [ ! -e "$none_home/last-scan" ] \
    && ok "…exits non-zero and records nothing" \
    || bad "all-unreadable exit/record" "rc=$rc, record: $(cat "$none_home/last-scan" 2>/dev/null)"
  chmod 0644 "$db/main.cvd"
fi
chmod 0644 "$db/daily.cld" "$db/freshclam.dat"

# Before freshclam's first download the directory is empty.
empty="$ROOT/clamav-db-empty"; mkdir -p "$empty"
empty_home="$ROOT/state-db-empty"; mkdir -p "$empty_home"
out=$(SYNSCAN_CLAMAV_DBDIR="$empty" SYNSCAN_HOME="$empty_home" sc --only clamav scan "$SCANME" 2>&1); rc=$?
grep -q 'no signature database at .* yet' <<<"$out" && ! grep -qi 'nothing found' <<<"$out" \
  && [ "$rc" -ne 0 ] && [ ! -e "$empty_home/last-scan" ] \
  && ok "an empty database directory is an engine that could not run" \
  || bad "empty database" "rc=$rc: $out"

# ⚠ clamd reads the database as its own user; the client never opens it, so
# the check must not stand in the way of the daemon path.
out=$(SYNSCAN_CLAMD_SOCKET="$ROOT/fake-clamd.sock" SYNSCAN_CLAMAV_DBDIR="$empty" \
      sc --rec scan "$SCANME" 2>/dev/null)
grep -q '^finding	clamav	infected	.*Eicar-Test-Signature' <<<"$out" \
  && ok "the clamd path does not check a database it never reads" \
  || bad "clamd path blocked by the database check" "$out"

# ── 15. what an engine could not read is listed, not counted ────────────────
#
# ⛔ The first sweep under release 4 said "40 things need a look"; 26 were one
# Rust crate's deliberately corrupt xz test files, which ClamAV answers with
# "Can't allocate memory". Unreadable is a gap in the scan, not a finding: it
# is listed apart, by name, and the count, the exit status and the saved
# record hold only what an engine found.
ur_home="$ROOT/state-unread"; mkdir -p "$ur_home"
human=$(SYNSCAN_HOME="$ur_home" sc --only clamav scan "$SCANME" 2>&1)
grep -q '2 things need a look' <<<"$human" && grep -q '1 file could not be scanned:' <<<"$human" \
  && grep -q "$SCANME/big.zip" <<<"$human" \
  && ok "an unreadable file is listed apart, by name, and not counted" \
  || bad "unreadable listing" "$human"

cp "$STUBS/clamscan" "$ROOT/clamscan.orig"
cat > "$STUBS/clamscan" <<STUB
#!/bin/sh
echo "$SCANME/bad-1-lzma2-1.xz: Can't allocate memory ERROR"
exit 2
STUB
chmod +x "$STUBS/clamscan"
ur2_home="$ROOT/state-unread-only"; mkdir -p "$ur2_home"
human=$(SYNSCAN_HOME="$ur2_home" sc --only clamav scan "$SCANME" 2>&1); rc=$?
[ "$rc" = 0 ] && grep -q '1 file could not be scanned:' <<<"$human" \
  && grep -q '^findings=0$' "$ur2_home/last-scan" \
  && ok "a scan that found nothing but could not read a file exits 0 and saves 0" \
  || bad "unreadable-only scan" "rc=$rc $human $(cat "$ur2_home/last-scan" 2>/dev/null)"
cp "$ROOT/clamscan.orig" "$STUBS/clamscan"

# A record written before this rule counted the unreadable row; status counts
# the saved list by today's rule, so Settings is right before the next sweep.
old2="$ROOT/state-oldcount"; mkdir -p "$old2"
printf 'started=1\nfinished=2\nfindings=3\n' > "$old2/last-scan"
{ printf 'finding\tclamav\tinfected\t/x/a\tSig-1\t2\n'
  printf 'finding\tclamav\tinfected\t/x/b\tSig-2\t2\n'
  printf 'finding\tclamav\terror\t/x/c.xz\tCan'"'"'t allocate memory\t2\n'; } > "$old2/findings-files"
out=$(SYNSCAN_HOME="$old2" sc --rec status)
grep -q '^status	ran	2	2$' <<<"$out" \
  && ok "status recounts an older record's list by today's rule" \
  || bad "status recount" "$out"
out=$(SYNSCAN_HOME="$old2" sc status 2>&1)
grep -q '2 things need a look' <<<"$out" && grep -q '1 file could not be scanned:' <<<"$out" \
  && ok "…and says which file could not be scanned" || bad "status unread block" "$out"

# ── 16. rkhunter's baseline follows pacman, and only pacman ─────────────────
#
# ⛔ `rkhunter --propupd` re-records every file, blessing whatever changed —
# including a binary replaced behind pacman's back. The hook's helper takes
# only the entries for the paths the transaction touched. Stubs stand in for
# rkhunter (it "records" the system from a fixture) and pacman; the helper runs
# against a database directory of the suite's own.
SRC=$(cd "$(dirname "$0")/.." && pwd)
HELPER="$SRC/hooks/rkhunter-baseline"
rkdb="$ROOT/rkhdb"; mkdir -p "$rkdb"; printf 'x\n' > "$rkdb/programs_bad.dat"
entry() { printf 'File:0:%s:%s:1:0755:0:0:10:100::0:%s:\n' "$1" "$2" "${3:-}"; }
cat > "$ROOT/rkh-stub" <<STUB
#!/bin/sh
while [ \$# -gt 0 ]; do [ "\$1" = --dbdir ] && d=\$2; shift; done
[ -e "$ROOT/rkh-fail" ] && exit 1
cp "$ROOT/rkh-current.dat" "\$d/rkhunter.dat"
exit 1
STUB
cat > "$ROOT/pacman-stub" <<STUB
#!/bin/sh
case "\$1" in
-Qo)  shift; [ "\$1" = -- ] && shift
      for p; do
          if grep -qxF "\$p" "$ROOT/unowned"; then echo "error: No package owns \$p" >&2
          else echo "\$p is owned by pkg 1-1"; fi
      done ;;
-Qkk) while read -r p; do echo "warning: pkg: \$p (SHA256 checksum mismatch)"; done < "$ROOT/mismatch"
      echo "backup file: pkg: /etc/rkhunter.conf (Modification time mismatch)"
      echo "pkg: 9 total files, 1 altered file" ;;
esac
exit 1
STUB
chmod +x "$ROOT/rkh-stub" "$ROOT/pacman-stub"
runh() { SYNSCAN_RKH_DBDIR="$rkdb" SYNSCAN_RKHUNTER="${RKH:-$ROOT/rkh-stub}" \
         SYNSCAN_PACMAN="$ROOT/pacman-stub" bash "$HELPER"; }

# The first baseline: checked against pacman. b does not match its package,
# bb is a link to it, and x belongs to no package — none of the three is
# recorded. A changed config file is pacman's "backup file", and stays in.
{ printf 'Version:2026092200\nHost:box\nOS:SynapseOS 1\nFormatVersion:1\n'
  entry /usr/bin/a h2; entry /usr/bin/b h2; entry /usr/bin/bb h2 /usr/bin/b
  entry /usr/bin/awk h2 /usr/bin/gawk; entry /usr/bin/gawk h2
  entry /usr/local/bin/x h2; entry /etc/rkhunter.conf h2; } > "$ROOT/rkh-current.dat"
printf '/usr/bin/b\n' > "$ROOT/mismatch"; printf '/usr/local/bin/x\n' > "$ROOT/unowned"
out=$(printf 'usr/bin/syn-scan\n' | runh)
got=$(grep '^File:' "$rkdb/rkhunter.dat" 2>/dev/null | cut -d: -f3 | sort | tr '\n' ' ')
[ "$got" = "/etc/rkhunter.conf /usr/bin/a /usr/bin/awk /usr/bin/gawk " ] \
  && ok "the first baseline records only what matches its package" \
  || bad "first baseline" "got [$got] — $out"
grep -q '/usr/bin/b (SHA256 checksum mismatch)' <<<"$out" && grep -q '/usr/local/bin/x (no package owns it)' <<<"$out" \
  && ok "…and names what it left out, and why" || bad "first baseline report" "$out"
[ "$(stat -c %a "$rkdb/rkhunter.dat" 2>/dev/null)" = 600 ] \
  && ok "…root-only, as rkhunter keeps it" || bad "baseline mode"

# A transaction: a upgraded, gawk upgraded (awk records it through the link),
# who installed, kill removed. b changed too, and pacman did not touch it —
# that is the tampered binary, and it must keep its old entry.
{ printf 'Version:2026092200\nHost:box\nOS:SynapseOS 1\nFormatVersion:1\n'
  entry /usr/bin/a h1; entry /usr/bin/b h1; entry /usr/bin/awk h1 /usr/bin/gawk
  entry /usr/bin/gawk h1; entry /usr/bin/kill h1; } > "$rkdb/rkhunter.dat"
{ printf 'Version:2026092300\nHost:box\nOS:SynapseOS 2\nFormatVersion:1\n'
  entry /usr/bin/a h2; entry /usr/bin/b h2; entry /usr/bin/awk h2 /usr/bin/gawk
  entry /usr/bin/gawk h2; entry /usr/bin/who h2; } > "$ROOT/rkh-current.dat"
printf 'usr/bin/a\nusr/bin/gawk\nusr/bin/who\nusr/bin/kill\n' | runh >/dev/null
got=$(grep '^File:' "$rkdb/rkhunter.dat" | cut -d: -f3,4 | sort | tr '\n' ' ')
[ "$got" = "/usr/bin/a:h2 /usr/bin/awk:h2 /usr/bin/b:h1 /usr/bin/gawk:h2 /usr/bin/who:h2 " ] \
  && ok "a transaction re-records what it touched — and a binary it did not touch keeps its old entry" \
  || bad "merge" "got [$got]"
grep -qx 'OS:SynapseOS 2' "$rkdb/rkhunter.dat" \
  && ok "…and the header follows the system" || bad "merge header" "$(head -4 "$rkdb/rkhunter.dat")"

# rkhunter failing to record anything leaves the baseline alone.
cp "$rkdb/rkhunter.dat" "$ROOT/dat.before"; : > "$ROOT/rkh-fail"
out=$(printf 'usr/bin/a\n' | runh); rc=$?
cmp -s "$rkdb/rkhunter.dat" "$ROOT/dat.before" && [ "$rc" = 0 ] \
  && ok "a failed --propupd leaves the baseline as it was, and the transaction alone" \
  || bad "propupd failure" "rc=$rc $out"
rm -f "$ROOT/rkh-fail"

# rkhunter removed: nothing will keep the baseline current, so it goes.
printf 'usr/bin/rkhunter\n' | RKH="$ROOT/no-such-rkhunter" runh >/dev/null
[ ! -e "$rkdb/rkhunter.dat" ] \
  && ok "removing rkhunter drops a baseline nothing would keep current" \
  || bad "baseline left behind after rkhunter was removed"

# The hook hands the helper its targets, at the path meson installs it to.
hook="$SRC/hooks/76-syn-scan-rkhunter.hook"
grep -qx 'Exec = /usr/lib/syn-scan/rkhunter-baseline' "$hook" && grep -qx 'NeedsTargets' "$hook" \
  && grep -qx 'Target = usr/bin/\*' "$hook" && grep -qx 'When = PostTransaction' "$hook" \
  && grep -q "install_dir: get_option('prefix') / 'lib/syn-scan'" "$SRC/meson.build" \
  && ok "the hook runs the installed helper after the transaction, with its targets" \
  || bad "hook file"

printf '\n  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
