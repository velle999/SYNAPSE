#!/usr/bin/env bash
# downloads_test.sh — `syn downloads` counts a split ISO correctly, and says so
# when it cannot count at all.
#
# ⚠ IT NEVER TOUCHES THE NETWORK. Every phase runs against a fixture through
# SYN_DOWNLOADS_JSON. A test that reached GitHub would fail on a train, pass for
# the wrong reason behind a proxy, and assert against counters that change under
# it — and the thing worth testing here is not that GitHub answers, it is what
# this script decides about the answer.
#
# What it guards, in the order the bugs would land:
#
#   1. THE PART SET. The ISO is published in .part00/.part01/…, the parts do not
#      agree, and the answer is a floor (the least-downloaded part) and a ceiling
#      (the most). Summing them — the obvious implementation — reports a
#      three-part release three times over.
#   2. `.parts.sha256` IS NOT A PART. It matches a naive *part* glob and it is a
#      300-byte checksum file that verifiers fetch far more often than the media,
#      so counting it drags the floor down to something unrelated.
#   3. THE NAME IS CONSUMED BY THE COUNTER THAT USES IT, which is what stops the
#      release BODY — the one field a human writes — from lending a name to a
#      stray "download_count" further down the object.
#   4. A FAILURE IS WRITTEN, with a reason. The reason travelled out of a command
#      substitution once and arrived empty; the bar then says "could not count
#      downloads" with nothing after it, which is the one thing this whole
#      arrangement exists to avoid.
#   5. THE STATE FILE IS A CONTRACT with synui's bar module. Every key that file
#      reads has to be a key this writes — checked against the QML source in both
#      directions, because a rename on either side is silently a zero.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
tool="$here/../syn-downloads.sh"
qml="$here/../../synui/quickshell/modules/IsoDownloads.qml"

fails=0
check() {  # check <description> <expected> <actual>
    if [ "$2" = "$3" ]; then
        printf '  ok    %s\n' "$1"
    else
        printf '  FAIL  %s — expected [%s], got [%s]\n' "$1" "$2" "$3"
        fails=$((fails + 1))
    fi
}

[ -r "$tool" ] || { echo "FAIL: $tool not found"; exit 1; }

tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' INT TERM EXIT

echo "syn downloads"

# ── The fixture ─────────────────────────────────────────────────────────────
#
# Written in the API's own field order, because that order is what the parser
# leans on: inside a release `tag_name` comes before the assets, and inside an
# asset `name` comes before `download_count` with an `uploader` object in
# between — an object which has a `login` and deliberately no `name`.
#
# Four releases, each carrying one of the cases:
#   v2.0.0  three parts at 9/9/5, a .parts.sha256 at 400 and a .sha256 at 12,
#           plus a BODY naming a part and a count
#   v1.5.0  one whole .iso, not split
#   v1.0.0  two parts, both zero — a published release nobody has fetched
#   v0.9.0  no ISO at all, the way the earliest releases were published
cat > "$tmp/fixture.json" <<'JSON'
[{"url":"u","id":1,"tag_name":"v2.0.0","target_commitish":"main","name":"SynapseOS 2.0.0","draft":false,"author":{"login":"velle999","id":9},"prerelease":false,"created_at":"2026-09-01T00:00:00Z","published_at":"2026-09-01T00:00:00Z","assets":[
{"url":"a","id":11,"name":"SynapseOS-2.0.0-x86_64.iso.part00","label":null,"uploader":{"login":"velle999","id":9},"content_type":"application/octet-stream","state":"uploaded","size":1992294400,"download_count":9,"created_at":"x","updated_at":"y","browser_download_url":"z"},
{"url":"a","id":12,"name":"SynapseOS-2.0.0-x86_64.iso.part01","label":null,"uploader":{"login":"velle999","id":9},"content_type":"application/octet-stream","state":"uploaded","size":1992294400,"download_count":9,"created_at":"x","updated_at":"y","browser_download_url":"z"},
{"url":"a","id":13,"name":"SynapseOS-2.0.0-x86_64.iso.part02","label":null,"uploader":{"login":"velle999","id":9},"content_type":"application/octet-stream","state":"uploaded","size":980647936,"download_count":5,"created_at":"x","updated_at":"y","browser_download_url":"z"},
{"url":"a","id":14,"name":"SynapseOS-2.0.0-x86_64.iso.parts.sha256","label":null,"uploader":{"login":"velle999","id":9},"content_type":"text/plain","state":"uploaded","size":300,"download_count":400,"created_at":"x","updated_at":"y","browser_download_url":"z"},
{"url":"a","id":15,"name":"SynapseOS-2.0.0-x86_64.iso.sha256","label":null,"uploader":{"login":"velle999","id":9},"content_type":"text/plain","state":"uploaded","size":93,"download_count":12,"created_at":"x","updated_at":"y","browser_download_url":"z"},
{"url":"a","id":16,"name":"SynapseOS-2.0.0-x86_64.iso.asc","label":null,"uploader":{"login":"velle999","id":9},"content_type":"text/plain","state":"uploaded","size":228,"download_count":7,"created_at":"x","updated_at":"y","browser_download_url":"z"}],
"tarball_url":"t","zipball_url":"z","body":"Reassemble with cat. \"name\":\"SynapseOS-2.0.0-x86_64.iso.part00\" and \"download_count\": 9999 are written here on purpose."},
{"url":"u","id":2,"tag_name":"v1.5.0","target_commitish":"main","name":"SynapseOS 1.5.0","draft":false,"author":{"login":"velle999","id":9},"prerelease":false,"created_at":"2026-08-01T00:00:00Z","published_at":"2026-08-01T00:00:00Z","assets":[
{"url":"a","id":21,"name":"SynapseOS-1.5.0-x86_64.iso","label":null,"uploader":{"login":"velle999","id":9},"content_type":"application/octet-stream","state":"uploaded","size":1500000000,"download_count":6,"created_at":"x","updated_at":"y","browser_download_url":"z"},
{"url":"a","id":22,"name":"SynapseOS-1.5.0-x86_64.iso.b2sum","label":null,"uploader":{"login":"velle999","id":9},"content_type":"text/plain","state":"uploaded","size":157,"download_count":3,"created_at":"x","updated_at":"y","browser_download_url":"z"}],
"tarball_url":"t","zipball_url":"z","body":"notes"},
{"url":"u","id":3,"tag_name":"v1.0.0","target_commitish":"main","name":"SynapseOS 1.0.0","draft":false,"author":{"login":"velle999","id":9},"prerelease":false,"created_at":"2026-07-01T00:00:00Z","published_at":"2026-07-01T00:00:00Z","assets":[
{"url":"a","id":31,"name":"SynapseOS-1.0.0-x86_64.iso.part00","label":null,"uploader":{"login":"velle999","id":9},"content_type":"application/octet-stream","state":"uploaded","size":1992294400,"download_count":0,"created_at":"x","updated_at":"y","browser_download_url":"z"},
{"url":"a","id":32,"name":"SynapseOS-1.0.0-x86_64.iso.part01","label":null,"uploader":{"login":"velle999","id":9},"content_type":"application/octet-stream","state":"uploaded","size":992294400,"download_count":0,"created_at":"x","updated_at":"y","browser_download_url":"z"}],
"tarball_url":"t","zipball_url":"z","body":"notes"},
{"url":"u","id":4,"tag_name":"v0.9.0","target_commitish":"main","name":"SynapseOS 0.9.0","draft":false,"author":{"login":"velle999","id":9},"prerelease":false,"created_at":"2026-06-01T00:00:00Z","published_at":"2026-06-01T00:00:00Z","assets":[],"tarball_url":"t","zipball_url":"z","body":"no media on this one"}]
JSON

run() {  # run <cache-dir> [args…] — the tool against the fixture, never a network
    local cache="$1"; shift
    XDG_CACHE_HOME="$tmp/$cache" \
    SYN_DOWNLOADS_JSON="$tmp/fixture.json" \
    SYN_DOWNLOADS_REPO="velle999/SYNAPSE" \
        bash "$tool" "$@" 2>&1
}
state() { sed -n "s/^$2=\(.*\)$/\1/p" "$tmp/$1/syn/downloads" | head -n1; }

# ── 1. the part set ─────────────────────────────────────────────────────────
out=$(run ok --refresh); rc=$?
check "a fixture run succeeds"            "0"  "$rc"
check "--refresh prints nothing"          ""   "$out"

# 9/9/5 → the floor is 5, the ceiling 9. Not 23, which is what summing gives,
# and not 400, which is what counting the checksum file gives.
check "complete is the LEAST-downloaded part"  "5"  "$(state ok latest_full)"
check "started is the MOST-downloaded part"    "9"  "$(state ok latest_starts)"
check "the part count excludes .parts.sha256"  "3"  "$(state ok latest_parts)"

# 5 + 6 + 0 = 11. A sum over every ISO asset would be 9+9+5+6+0+0 = 29; a sum
# that also took the checksums and the signature would be 448.
check "the lifetime total sums the floors"     "11" "$(state ok total_full)"
check "the lifetime ceiling sums the ceilings" "15" "$(state ok total_starts)"

# v0.9.0 has no ISO, so it is not a release this counts — not a zero in the
# average, not a row in the table.
check "releases with no ISO are not counted"   "3"  "$(state ok counted)"

# ── 2. the newest release, and its spelling ─────────────────────────────────
check "the latest release is the first answered" "v2.0.0" "$(state ok latest_tag)"
# ⚠ THE VERSION IS THE TAG WITHOUT THE v. The ISO filename, the About page and
# os-release all spell it 2.0.0; only the release URL uses the tag.
check "the version drops the tag's v"            "2.0.0"  "$(state ok latest)"

# ── 3. an unsplit ISO is a set of one ──────────────────────────────────────
# The whole-.iso release must count as 6 complete and 6 started, which is only
# true if a plain `.iso` matches as well as `.iso.partNN`.
check "a single .iso release is counted" "v1.5.0 6 6 1" \
      "$(sed -n 's/^rel=\(v1\.5\.0 .*\)$/\1/p' "$tmp/ok/syn/downloads")"

# ── 4. the body cannot lend a name to a stray counter ──────────────────────
# v2.0.0's body contains a part name and a "download_count": 9999. If the
# parser paired them, that release's ceiling would be 9999 and the total would
# run away with it. Both are already asserted above — this states the intent
# so a future change that breaks it is read as breaking THIS.
check "a counter in the release body is ignored" "9" "$(state ok latest_starts)"

# ── 5. the failure paths, which must never be silent ───────────────────────
out=$(XDG_CACHE_HOME="$tmp/noent" SYN_DOWNLOADS_JSON="$tmp/does-not-exist.json" \
      bash "$tool" --refresh 2>&1); rc=$?
check "an unreadable source fails"        "1" "$rc"
check "…and writes status=error"          "error" "$(state noent status)"
# ⛔ THE REGRESSION. `json=$(fetch_json)` put the reason in a subshell that had
# already exited by the time the caller wrote the file, so this was empty and the
# bar's tooltip said "could not count downloads" and stopped.
reason=$(state noent reason)
check "…with a reason that is not empty"  "yes" "$([ -n "$reason" ] && echo yes || echo no)"

echo '[]' > "$tmp/empty.json"
XDG_CACHE_HOME="$tmp/empty" SYN_DOWNLOADS_JSON="$tmp/empty.json" \
    bash "$tool" --refresh >/dev/null 2>&1
check "no ISO assets anywhere is an ERROR, not a zero" \
      "error" "$(state empty status)"
# A zero written here would be the worst outcome available: a bar reading "0"
# on a project whose media is being downloaded, with nothing to say it had lost
# the ability to count.
check "…and no total is left behind to show"  "" "$(state empty total_full)"

# ── 6. --cached needs no source at all ────────────────────────────────────
out=$(XDG_CACHE_HOME="$tmp/ok" SYN_DOWNLOADS_JSON="$tmp/does-not-exist.json" \
      bash "$tool" --cached 2>&1); rc=$?
check "--cached prints without a source"  "0" "$rc"
check "…and prints every counted release" "3" \
      "$(printf '%s\n' "$out" | grep -cE '^  [0-9]+\.[0-9]+\.[0-9]+ ')"
check "…including the lifetime line"      "1" \
      "$(printf '%s\n' "$out" | grep -c 'all 3')"

# ── 7. the state file is a contract with the bar module ───────────────────
if [ -r "$qml" ]; then
    missing=""
    # ⚠ AGAINST BOTH STATE FILES, not just the good one. `reason` is written on
    # the error path ALONE — deliberately, so a reader cannot find a reason and a
    # count together and have to decide which it believes — and a check that only
    # read the ok file would report the module's own error handling as a broken
    # contract. The vocabulary is the UNION of what this can write.
    cat "$tmp/ok/syn/downloads" "$tmp/noent/syn/downloads" > "$tmp/vocab"
    # Every num("x") / str("x") the module reads must be a key the writer emits.
    for key in $(grep -oE '(num|str)\("[a-z_]+"\)' "$qml" \
                 | sed 's/.*("//; s/")//' | sort -u); do
        grep -qE "^$key=" "$tmp/vocab" || missing="$missing $key"
    done
    check "every key the bar reads is a key this writes" "" "$missing"
else
    printf '  skip  the bar module is not in this tree (%s)\n' "$qml"
fi

# ── 8. invariants about the script itself ────────────────────────────────
# ⛔ NO PRIVILEGE, EVER. This runs from a systemd timer, where there is no
# terminal: a bare sudo does not fail there, it opens a PAM conversation that
# pam_faillock counts as a wrong password against a user who never typed one —
# and greetd shares that stack, so the symptom is a login screen refusing a
# correct password. Comments and strings stripped, so prose may still say sudo.
code=$(sed -e 's/#.*//' -e "s/'[^']*'//g" -e 's/"[^"]*"//g' "$tool")
# Comments gone, strings KEPT — the two checks below are about what the script
# says to the network and to the filesystem, and both of those live in strings.
lines=$(sed -e 's/#.*//' "$tool")
check "the script never reaches for sudo or pkexec" "0" \
      "$(printf '%s\n' "$code" | grep -cE '\b(sudo|pkexec|doas)\b')"

# ⛔ per_page=100. The default is 30 and this project already has 28 releases:
# on the default, release 31 pushes the oldest off page one and the LIFETIME
# TOTAL DROPS on the day a release is published.
# ⚠ COUNTED IN THE CODE, not in the file: the paragraph above this check's
# subject is written in the script too, and a grep over the whole file passes on
# the comment alone.
check "the request asks for 100 per page" "1" \
      "$(printf '%s\n' "$lines" | grep -c 'per_page=100')"

# The bar watches this file. A writer that did not rename into place would show
# it empty for a frame and blink the badge to nothing on every timer tick.
check "the state file is renamed into place" "1" \
      "$(printf '%s\n' "$lines" | grep -cE '^\s*mv -f "\$tmp" "\$STATE"')"
check "…and leaves no temporary file behind" "0" \
      "$(find "$tmp/ok/syn" -name 'downloads.*' | wc -l)"

echo
if [ "$fails" -eq 0 ]; then
    echo "all syn downloads checks passed"
    exit 0
fi
echo "$fails check(s) failed"
exit 1
