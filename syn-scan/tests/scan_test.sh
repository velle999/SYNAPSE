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
mkdir -p "$STUBS" "$SCANME" "$SYNSCAN_HOME"

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
# ⚠ The stub dir goes FIRST. engine_path() takes the first match in PATH order,
# so this shadows a real clamscan if the machine has one — which the developer
# box will, and CI will not.
export PATH="$STUBS:$PATH"

printf 'harmless\n' > "$SCANME/clean.txt"
printf 'X5O!P%%@AP[4\\PZX54(P^)7CC)7}$EICAR\n' > "$SCANME/eicar.com"

# ── 1. engines: an absent engine is reported, not hidden ────────────────────
out=$("$BIN" --rec engines)
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
out=$("$BIN" --rec engines)
grep -q '^engine	chkrootkit	.*	1	0	' <<<"$out" \
  && ok "an installed but unexecutable engine reads present=1 runnable=0" \
  || bad "present/runnable split" "$(grep chkrootkit <<<"$out")"

# ⚠ CAPTURE, THEN GREP. `set -o pipefail` is on and syn-scan exits 1 when it
# has findings, so `"$BIN" ... | grep -q` reports failure even when grep
# matched — the assertion would fail on a working program.
sysout=$("$BIN" scan --system 2>&1)
grep -qi 'needs root' <<<"$sysout" \
  && ok "a scan says an engine needs root rather than 'not installed'" \
  || bad "needs-root message" "$sysout"
chmod 0755 "$STUBS/chkrootkit"

# ── 2. clamav parsing ───────────────────────────────────────────────────────
out=$("$BIN" --rec scan "$SCANME")
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

out=$(SYNSCAN_CLAMD_SOCKET="$ROOT/fake-clamd.sock" "$BIN" --rec scan "$SCANME")
grep -q -- '--fdpass' "$STUBS/clamdscan.args" \
  && ok "the clamd path passes --fdpass" \
  || bad "--fdpass missing" "argv was: $(cat "$STUBS/clamdscan.args" 2>/dev/null)"

grep -q '^finding	clamav	infected	.*Eicar-Test-Signature' <<<"$out" \
  && ok "the clamd path parses the same record shape" || bad "clamdscan parse"

# ⚠ And the fallback is what runs when there is no socket — the ordinary case.
rm -f "$STUBS/clamscan.args"
"$BIN" --rec scan "$SCANME" >/dev/null
grep -q -- '--scan-archive=yes' "$STUBS/clamscan.args" 2>/dev/null \
  && ok "with no clamd socket it falls back to clamscan" \
  || bad "clamscan fallback not taken"

# ── 3. system engines only run for --system ─────────────────────────────────
out=$("$BIN" --rec scan "$SCANME")
grep -q 'rkhunter' <<<"$out" \
  && bad "rkhunter ran on a file scan" "a path list means nothing to it" \
  || ok "a file scan does not run the system engines"

out=$("$BIN" --rec scan --system)
grep -q '^finding	rkhunter	suspect	' <<<"$out" \
  && ok "--system runs rkhunter and drops its 'Warning:' prefix" \
  || bad "rkhunter --system"

grep -q '^finding	chkrootkit	suspect	' <<<"$out" \
  && ok "--system runs chkrootkit" || bad "chkrootkit --system"

# ── 4. --only ───────────────────────────────────────────────────────────────
out=$("$BIN" --rec --only clamav scan "$SCANME")
grep -q 'rkhunter' <<<"$out" && bad "--only clamav ran another engine" \
                             || ok "--only runs exactly the engine named"

"$BIN" --only nosuchengine scan "$SCANME" >/dev/null 2>&1 \
  && bad "--only accepted an engine that does not exist" \
  || ok "--only refuses an unknown engine"

# ── 5. quarantine round-trip ────────────────────────────────────────────────
victim="$SCANME/victim.bin"
printf 'original contents\n' > "$victim"
chmod 0644 "$victim"
before=$(sha256sum < "$victim")

"$BIN" --yes quarantine take "$victim" >/dev/null 2>&1
[ -e "$victim" ] && bad "quarantine take left the original in place" \
                 || ok "quarantine take removes the original"

id=$("$BIN" --rec quarantine list | awk -F'\t' '/^quarantine/{print $2; exit}')
[ -n "$id" ] && ok "quarantine list names what it holds" || bad "quarantine list empty"

# ⛔ 0600 AND NOT EXECUTABLE. A quarantine full of files that are still +x has
# only moved the problem.
blob="$SYNSCAN_HOME/quarantine/$id"
perm=$(stat -c %a "$blob" 2>/dev/null)
[ "$perm" = 600 ] && ok "quarantined file is 0600, whatever it was before" \
                  || bad "quarantine permissions" "got $perm"

"$BIN" --yes quarantine restore "$id" >/dev/null 2>&1
after=$(sha256sum < "$victim" 2>/dev/null)
[ "$before" = "$after" ] && ok "restore returns the exact bytes" \
                         || bad "restore content mismatch"
perm=$(stat -c %a "$victim" 2>/dev/null)
[ "$perm" = 644 ] && ok "restore returns the original mode" \
                  || bad "restore mode" "expected 644, got $perm"

# ── 6. restore never clobbers ───────────────────────────────────────────────
printf 'replacement\n' > "$victim"
"$BIN" --yes quarantine take "$victim" >/dev/null 2>&1
id=$("$BIN" --rec quarantine list | awk -F'\t' '/^quarantine/{print $2; exit}')
printf 'somebody put this back\n' > "$victim"
"$BIN" --yes quarantine restore "$id" >/dev/null 2>&1
grep -q 'somebody put this back' "$victim" \
  && ok "restore refuses to overwrite a file that reappeared" \
  || bad "restore clobbered a replacement"

# ── 7. the program has no delete ────────────────────────────────────────────
#
# ⛔ NOT A STYLE CHECK. The design promise is that a false positive can always
# be undone. A `delete` verb added later would break that silently.
"$BIN" scan --delete "$SCANME" >/dev/null 2>&1 \
  && bad "a --delete option exists" \
  || ok "there is no --delete"

"$BIN" --help 2>&1 | grep -qiE '^\s*syn-scan (delete|remove)' \
  && bad "help advertises a delete command" \
  || ok "help advertises no delete command"

# ── 8. --dry-run changes nothing ────────────────────────────────────────────
printf 'untouched\n' > "$SCANME/dry.bin"
"$BIN" --dry-run --yes quarantine take "$SCANME/dry.bin" >/dev/null 2>&1
[ -e "$SCANME/dry.bin" ] && ok "--dry-run leaves the file alone" \
                         || bad "--dry-run moved a file"

# ── 9. diagnostics stay off stdout in --rec ─────────────────────────────────
#
# ⚠ A warning printed on stdout is a parse error in the window, not a message.
out=$("$BIN" --rec --only clamav scan "$ROOT/does-not-exist" 2>/dev/null)
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
out=$(PATH="/nonexistent-for-tests:/usr/bin" SYNSCAN_HOME="$noeng_home" \
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

printf '\n  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
