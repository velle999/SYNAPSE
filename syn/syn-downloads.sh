#!/usr/bin/env bash
# syn-downloads — how many people have downloaded the SynapseOS ISO.
#
# Reads the download counters GitHub keeps on every release asset and leaves the
# answer in a small file the bar's ISO-downloads module draws. `syn downloads`
# prints the same numbers as a table.
#
# ── THERE IS NO SINGLE "DOWNLOAD COUNT", AND THAT IS THE WHOLE PROBLEM ───────
#
# The ISO is five gigabytes and GitHub refuses a release asset over two, so
# publish-release.sh splits it: SynapseOS-1.0.0-x86_64.iso.part00, .part01,
# .part02. Every part carries its own counter and they do not agree — 1.0.0's
# predecessor sits at 6, 6, 4. Somebody started three parts and kept two.
#
# So this reports a FLOOR and a CEILING rather than inventing one number:
#
#   complete = the LEAST-downloaded part of the set — nobody can have assembled
#              an ISO without every part, so this many people could have
#   started  = the MOST-downloaded part — this many people began
#
# ⚠ SUMMING THE ASSETS IS THE OBVIOUS WRONG ANSWER. A three-part release would
# read triple, a five-part one quintuple (0.2.7 shipped five), and the number
# would jump whenever the part count changed rather than when anybody downloaded
# anything. The part count has been 2, 3, 4 and 5 across the releases to date.
#
# ⛔ AND `.parts.sha256` IS NOT A PART. Every release also carries .sha256,
# .b2sum, .asc and .parts.sha256 — that last one matches a naive `*part*` and
# would drag a checksum file's counter into the floor, pinning "complete" to
# whatever a handful of verifiers fetched. The match here is anchored:
# `.iso` exactly, or `.iso.part<digits>` at the END of the name.
#
# ── IT DOES THE NETWORK SO THE BAR NEVER HAS TO ─────────────────────────────
#
# Same arrangement, and the same reasoning, as `syn-update ping` and weather.c:
# the bar module is instantiated once per MONITOR inside the compositor's shell
# process, so a fetch there would be one request per screen and a stalled
# connect would be a stalled bar. This runs from a systemd USER timer and writes
# ~/.cache/syn/downloads; the module watches that file and fetches nothing.
#
# ⚠ OFF BY DEFAULT. This is the only thing on a SynapseOS desktop besides the
# weather that talks to a server nobody asked it to talk to, and counting our own
# downloads is not worth a background request from every installed machine.
# `syn downloads --watch on` is the opt-in; without it the module never appears,
# because the file it reads is not there.
#
# ⚠ A REFUSAL IS WRITTEN TO THE STATE FILE, as status=error with a reason. A
# checker that fails silently is worse than one switched off: the bar would go
# on showing a stale count with nothing anywhere saying the counting had stopped.
#
# ⚠ NO PRIVILEGE ANYWHERE IN THIS FILE — no sudo, no polkit, nothing owned by
# root. It reads a public HTTP endpoint and writes one file in the user's cache.
# Under a timer there is no terminal to prompt in, and a bare sudo with nowhere
# to prompt does not fail, it opens a PAM conversation that pam_faillock counts
# as a wrong password (see syn-update 58). The way not to have that problem is
# to need nothing.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
# https://github.com/velle999/SYNAPSE

set -uo pipefail

VERSION="0.1.0"

# The project's own releases. An override because the ISO-splitting arrangement
# is not unique to this repository and the test suite points it at a fixture.
REPO="${SYN_DOWNLOADS_REPO:-velle999/SYNAPSE}"
API="${SYN_DOWNLOADS_API:-https://api.github.com}"

# ⚠ THE TEST SEAM, and it is read-only: a file of captured API JSON to parse
# instead of fetching. tests/downloads_test.sh runs every parse rule through it,
# because a test that reached GitHub is one that fails on a train and passes for
# the wrong reason behind a proxy — and the counters it asserts against would
# change under it anyway.
FIXTURE="${SYN_DOWNLOADS_JSON:-}"

STATE_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/syn"
STATE="$STATE_DIR/downloads"

UNIT="syn-downloads.timer"
DROPIN_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user/$UNIT.d"
DROPIN="$DROPIN_DIR/interval.conf"

if [ -t 1 ]; then
    C_B=$'\e[1m'; C_DIM=$'\e[2m'; C_OK=$'\e[38;5;82m'
    C_WARN=$'\e[38;5;214m'; C_ERR=$'\e[38;5;203m'; C_R=$'\e[0m'
else
    C_B=""; C_DIM=""; C_OK=""; C_WARN=""; C_ERR=""; C_R=""
fi
say()  { printf '%s\n' "$*"; }
bad()  { printf '  %s✗%s %s\n' "$C_ERR" "$C_R" "$*" >&2; }
warn() { printf '  %s!%s %s\n' "$C_WARN" "$C_R" "$*" >&2; }

usage() {
    cat << HELP
syn downloads $VERSION — how many people have downloaded the SynapseOS ISO

Usage:
  syn downloads              Ask GitHub now, print the table, update the bar
  syn downloads --cached     Print the last answer, ask nothing
  syn downloads --refresh    Ask and update the bar, print nothing (the timer)
  syn downloads --watch <on|off|status|<interval>>
                             Check periodically in the background. OFF by
                             default. <interval> is a systemd time span —
                             12h, 1d, 90min — and setting one switches it on.
  syn downloads --help       This help

The ISO ships in parts, so one "download" has a floor and a ceiling:
"complete" counts the least-downloaded part of a release's set, "started" the
most. Summing the parts would report a three-part release three times over.

The bar's ISO-downloads row reads ~/.cache/syn/downloads and never fetches
anything itself. It is invisible until this has run once.
HELP
}

# ── Asking GitHub ───────────────────────────────────────────────────────────

# The reason the last fetch failed, for the state file and the terminal.
FAIL_REASON=""

# All of the repository's releases, as raw JSON on stdout. Non-zero and
# FAIL_REASON set on any failure.
#
# ⛔ per_page=100 AND A PAGING LOOP, because the default is 30 and this
# repository already has 28 releases. On the default, release 31 would push the
# oldest off the end of page one and the lifetime total would DROP on the day a
# release was published — a counter that goes backwards when the project ships,
# with nothing saying why. The loop stops on the first short page, and 10 pages
# of 100 is a thousand releases, at which point something else is wrong.
fetch_json() {
    if [ -n "$FIXTURE" ]; then
        if [ ! -r "$FIXTURE" ]; then
            FAIL_REASON="cannot read $FIXTURE"
            return 1
        fi
        cat -- "$FIXTURE"
        return 0
    fi

    command -v curl >/dev/null 2>&1 || { FAIL_REASON="curl is not installed"; return 1; }

    local tmp page=1 code body got
    tmp=$(mktemp -d) || { FAIL_REASON="no temporary directory"; return 1; }

    while [ "$page" -le 10 ]; do
        body="$tmp/page"
        # ⚠ THE HTTP STATUS IS ASKED FOR SEPARATELY, not inferred from curl's
        # exit code. `curl -f` turns 403 into exit 22 with an empty body and no
        # way to tell a rate limit from a deleted repository, and the rate limit
        # is the failure this will actually hit: unauthenticated GitHub allows
        # 60 requests an hour per address, which is generous for a 12-hourly
        # timer and not generous for a person in a loop.
        code=$(curl -sS -m 60 -o "$body" -w '%{http_code}' \
                    -H 'Accept: application/vnd.github+json' \
                    -H 'X-GitHub-Api-Version: 2022-11-28' \
                    "$API/repos/$REPO/releases?per_page=100&page=$page" \
                    2>"$tmp/err")
        case "$code" in
            200) : ;;
            000|"") FAIL_REASON="cannot reach ${API#https://}"; rm -rf "$tmp"; return 1 ;;
            403|429) FAIL_REASON="GitHub API rate limit reached"; rm -rf "$tmp"; return 1 ;;
            404) FAIL_REASON="no releases for $REPO"; rm -rf "$tmp"; return 1 ;;
            *) FAIL_REASON="GitHub answered HTTP $code"; rm -rf "$tmp"; return 1 ;;
        esac
        cat "$body"
        # How many releases this page carried. `|| true` because grep exits 1 on
        # no match and this script runs under pipefail.
        got=$(grep -o '"tag_name"' "$body" 2>/dev/null | wc -l || true)
        [ "${got:-0}" -lt 100 ] && break
        page=$((page + 1))
    done
    rm -rf "$tmp"
    return 0
}

# JSON on stdin, one `<tag>TAB<complete>TAB<started>TAB<parts>` line per release
# that carries an ISO, newest first. Releases with no ISO attached — the first
# nine, before the media was published here at all — print nothing.
#
# ⛔ NO jq AND NO PYTHON. This package depends on bash and systemd; synui's own
# tree deliberately has no jq either. The extraction is therefore a token stream
# and an awk state machine, which is exactly the shape that has to be justified:
# a `grep "download_count"` reader that trusted position would be the trap
# documented in reference_json_field_key_matches_a_value.
#
# ⚠ WHAT MAKES IT SAFE IS THE ORDER GitHub WRITES AND THE NAME FILTER. Inside a
# release object `tag_name` precedes the assets array; inside an asset, `name`
# precedes `download_count`, and the `uploader` object between them has a
# `login` but no `name`. So the counter's asset is the nearest name above it.
# A name is CONSUMED when a counter uses it, so the release notes at the bottom
# of the object — the one field a human writes, the one that could contain
# anything — cannot lend a name to a stray counter: it would have none, and an
# unnamed counter is dropped. And nothing is counted whose name does not end in
# .iso or .iso.partNN, so a misparse reports zero ISO assets, which this refuses
# to write, rather than a plausible wrong number.
parse_releases() {
    grep -oE '"tag_name": *"[^"]*"|"name": *"[^"]*"|"download_count": *[0-9]+' \
    | awk -F'"' '
        function emit() {
            if (tag != "" && parts > 0)
                printf "%s\t%d\t%d\t%d\n", tag, lo, hi, parts
        }
        $2 == "tag_name" { emit(); tag = $4; parts = 0; lo = 0; hi = 0; name = ""; next }
        $2 == "name"     { name = $4; next }
        $2 == "download_count" {
            n = $3; gsub(/[^0-9]/, "", n); n = n + 0
            if (name ~ /\.iso$/ || name ~ /\.iso\.part[0-9]+$/) {
                if (parts == 0 || n < lo) lo = n
                if (parts == 0 || n > hi) hi = n
                parts++
            }
            name = ""
            next
        }
        END { emit() }
    '
}

# Everything the state file and the table are built from, set by collect():
#   ROWS        the per-release lines, newest first
#   N_REL       releases carrying an ISO
#   TOTAL_FULL  complete downloads over every release
#   TOTAL_START started downloads over every release
#   L_TAG L_VER L_FULL L_START L_PARTS   the newest release with an ISO
ROWS=""; N_REL=0; TOTAL_FULL=0; TOTAL_START=0
L_TAG=""; L_VER=""; L_FULL=0; L_START=0; L_PARTS=0

collect() {
    local raw
    raw=$(mktemp) || { FAIL_REASON="no temporary file"; return 1; }
    # ⛔ REDIRECTED INTO A FILE, NEVER `json=$(fetch_json)`. A command
    # substitution runs in a SUBSHELL, so the FAIL_REASON that fetch_json sets
    # on the way out dies with it — and the caller then writes `reason=` empty
    # into the state file and the bar's tooltip says "could not count downloads"
    # with nothing after it. Which is the one thing the header above promises
    # this never does. Found by pulling the network out from under it: the
    # reason was there, in a shell that had already exited. A redirection is not
    # a subshell, so this keeps it.
    if ! fetch_json > "$raw"; then rm -f "$raw"; return 1; fi
    ROWS=$(parse_releases < "$raw")
    rm -f "$raw"
    if [ -z "$ROWS" ]; then
        # No ISO assets anywhere in the answer. Either the JSON was not what we
        # think it is or the naming changed; both are errors, and neither is a
        # zero to show on a bar.
        FAIL_REASON="no ISO assets found in $REPO releases"
        return 1
    fi

    N_REL=0; TOTAL_FULL=0; TOTAL_START=0
    local tag full start parts
    while IFS=$'\t' read -r tag full start parts; do
        [ -n "$tag" ] || continue
        N_REL=$((N_REL + 1))
        TOTAL_FULL=$((TOTAL_FULL + full))
        TOTAL_START=$((TOTAL_START + start))
        if [ "$N_REL" = 1 ]; then
            L_TAG="$tag"; L_FULL="$full"; L_START="$start"; L_PARTS="$parts"
            # The version the way the ISO and the About page spell it: 1.0.0,
            # not v1.0.0. The tag is kept beside it because the release URL uses
            # the tag and nothing else does.
            L_VER="${tag#v}"
        fi
    done <<< "$ROWS"
    return 0
}

# ── The state file ──────────────────────────────────────────────────────────
#
# ⚠ RENAMED INTO PLACE, and that is not tidiness — it is what makes the bar
# module's FileView safe to watch. A file written in place is empty for a frame,
# and the badge would blink to nothing every time the timer fired. Same reason
# `syn-update ping` does it.
write_state() {
    mkdir -p "$STATE_DIR" 2>/dev/null || return 1
    local tmp="$STATE.$$"
    {
        printf 'status=%s\n' "$1"
        printf 'checked=%s\n' "$(date +%s)"
        printf 'repo=%s\n' "$REPO"
        if [ "$1" = "error" ]; then
            printf 'reason=%s\n' "$2"
        else
            printf 'latest=%s\n'        "$L_VER"
            printf 'latest_tag=%s\n'    "$L_TAG"
            printf 'latest_full=%s\n'   "$L_FULL"
            printf 'latest_starts=%s\n' "$L_START"
            printf 'latest_parts=%s\n'  "$L_PARTS"
            printf 'total_full=%s\n'    "$TOTAL_FULL"
            printf 'total_starts=%s\n'  "$TOTAL_START"
            printf 'counted=%s\n'       "$N_REL"
            # One line per release, so `--cached` can print the whole table
            # without going back to the network. The bar reads none of these.
            printf '%s\n' "$ROWS" | while IFS=$'\t' read -r t f s p; do
                [ -n "$t" ] || continue
                printf 'rel=%s %s %s %s\n' "$t" "$f" "$s" "$p"
            done
        fi
    } > "$tmp" 2>/dev/null || { rm -f "$tmp"; return 1; }
    mv -f "$tmp" "$STATE" 2>/dev/null || { rm -f "$tmp"; return 1; }
    return 0
}

# An error state keeps the PREVIOUS numbers out of the file: a reader that found
# both would have to decide which it trusted, and the honest answer is that the
# last fetch failed. The reason is what the bar's tooltip shows.
write_error() {
    L_VER=""; L_TAG=""; L_FULL=0; L_START=0; L_PARTS=0
    ROWS=""; N_REL=0; TOTAL_FULL=0; TOTAL_START=0
    write_state error "$1"
}

state_get() {  # state_get <key>
    [ -r "$STATE" ] || return 1
    sed -n "s/^$1=\(.*\)$/\1/p" "$STATE" | head -n1
}

# ── The table ───────────────────────────────────────────────────────────────

ago() {  # ago <unix seconds>
    local when="${1:-0}" now mins
    [ "$when" -gt 0 ] 2>/dev/null || { printf 'never'; return; }
    now=$(date +%s)
    mins=$(( (now - when) / 60 ))
    if   [ "$mins" -lt 1 ]    ; then printf 'just now'
    elif [ "$mins" -lt 60 ]   ; then printf '%d min ago' "$mins"
    elif [ "$mins" -lt 2880 ] ; then printf '%dh ago' "$((mins / 60))"
    else printf '%d days ago' "$((mins / 1440))"
    fi
}

report() {  # report <rows> <checked-unix>
    local rows="$1" checked="$2" tag full start parts
    say ""
    say "  ${C_B}SynapseOS ISO downloads${C_R}  ${C_DIM}github.com/$REPO${C_R}"
    say ""
    printf '  %-12s %10s %10s %7s\n' "release" "complete" "started" "parts"
    while IFS=$'\t' read -r tag full start parts; do
        [ -n "$tag" ] || continue
        printf '  %-12s %10s %10s %7s\n' "${tag#v}" "$full" "$start" "$parts"
    done <<< "$rows"
    printf '  %-12s %10s %10s\n' "" "--------" "-------"
    printf '  %-12s %s%10s%s %10s\n' "all $N_REL" "$C_OK" "$TOTAL_FULL" "$C_R" "$TOTAL_START"
    say ""
    say "  ${C_DIM}complete = the least-downloaded part of a release's set, so the most"
    say "  people who could have assembled an ISO; started = the most-downloaded"
    say "  part. Checked $(ago "$checked").${C_R}"
    say ""
    if ! watch_is_on; then
        say "  ${C_DIM}Background checking is off — the bar's ISO-downloads row appears"
        say "  once this has run and updates with it. Turn it on with:"
        say "      syn downloads --watch 12h${C_R}"
        say ""
    fi
}

# ── The timer ───────────────────────────────────────────────────────────────

# ⚠ systemctl --user NEEDS A SESSION MANAGER, which a plain `ssh box syn
# downloads --watch on` does not have: it fails with "Failed to connect to bus".
# Said plainly here rather than left as a systemd diagnostic, because the fix is
# to run it in the desktop session and nothing in that message says so.
user_systemctl() {
    if ! command -v systemctl >/dev/null 2>&1; then
        bad "systemctl is not installed — cannot manage the background check"
        return 1
    fi
    systemctl --user "$@"
}

watch_is_on() {
    local s
    s=$(systemctl --user is-enabled "$UNIT" 2>/dev/null) || true
    # ⛔ `is-enabled` PRINTS SOMETHING FOR A UNIT IT CANNOT FIND — `not-found`,
    # not an empty line — and exits non-zero either way. So the test is against
    # the word, never against emptiness.
    [ "$s" = "enabled" ]
}

cmd_watch() {
    local arg="${1:-status}"
    case "$arg" in
        status)
            if watch_is_on; then
                local every
                every=$(sed -n 's/^OnUnitActiveSec=\(.*\)$/\1/p' "$DROPIN" 2>/dev/null | tail -n1)
                say "  background check: ${C_OK}on${C_R}, every ${every:-12h}"
            else
                say "  background check: ${C_DIM}off${C_R}"
            fi
            local checked
            checked=$(state_get checked 2>/dev/null) || checked=0
            say "  last answer: $(ago "${checked:-0}")"
            ;;
        off)
            user_systemctl disable --now "$UNIT" >/dev/null 2>&1
            say "  background check off. The numbers in the bar stop moving; the row"
            say "  keeps showing the last answer until ~/.cache/syn/downloads is removed."
            ;;
        on)
            cmd_watch_on ""
            ;;
        *)
            # Anything else is an interval. systemd validates it, not us: a
            # pattern here would be a second, worse copy of systemd-analyze's
            # parser and would disagree with it on the day somebody writes
            # "1d 12h".
            cmd_watch_on "$arg"
            ;;
    esac
}

cmd_watch_on() {  # cmd_watch_on <interval|"">
    local every="$1"
    if [ -n "$every" ]; then
        mkdir -p "$DROPIN_DIR" || { bad "cannot write $DROPIN_DIR"; return 1; }
        # ⚠ A DROP-IN UNDER ~/.config, NEVER THE SHIPPED UNIT. /usr/lib belongs
        # to the package and every upgrade of `syn` replaces it, so an interval
        # written there reverts silently.
        #
        # ⚠ AND `OnUnitActiveSec=` IS RESET BEFORE IT IS SET. systemd's
        # list-valued settings ACCUMULATE across drop-ins: without the empty
        # assignment the shipped 12h and this one would both be live and the
        # timer would fire on whichever came first. Exactly the trap
        # syn-update's ping drop-in documents.
        cat > "$DROPIN" << EOF
# Written by \`syn downloads --watch $every\`. Edit that, not this.
[Timer]
OnUnitActiveSec=
OnUnitActiveSec=$every
EOF
        user_systemctl daemon-reload >/dev/null 2>&1
    fi
    if ! user_systemctl enable --now "$UNIT" 2>&1 | sed 's/^/    /'; then
        bad "could not enable $UNIT"
        warn "systemctl --user needs a desktop session; run this in one, not over ssh"
        return 1
    fi
    say "  background check on, every ${every:-12h}."
    say "  The bar's ISO-downloads row appears with the first answer."
}

# ── Entry ───────────────────────────────────────────────────────────────────

cmd_cached() {
    local rows checked
    checked=$(state_get checked) || {
        say ""
        say "  Nothing has been counted yet. Run:  syn downloads"
        say ""
        return 1
    }
    if [ "$(state_get status)" = "error" ]; then
        bad "the last check failed: $(state_get reason)"
        return 1
    fi
    # Rebuild what report() needs out of the file rather than refetching.
    rows=$(sed -n 's/^rel=\([^ ]*\) \([0-9]*\) \([0-9]*\) \([0-9]*\)$/\1\t\2\t\3\t\4/p' "$STATE")
    N_REL=$(state_get counted);      TOTAL_FULL=$(state_get total_full)
    TOTAL_START=$(state_get total_starts)
    report "$rows" "$checked"
}

main() {
    case "${1:-}" in
        ""|--now|now)
            if ! collect; then
                write_error "$FAIL_REASON"
                bad "could not count downloads: $FAIL_REASON"
                return 1
            fi
            write_state ok || warn "could not write $STATE — the bar keeps its last answer"
            report "$ROWS" "$(date +%s)"
            ;;
        --refresh|refresh)
            # The timer's path, and the bar's Check-now. Quiet on success;
            # anything it has to say goes to the state file, because there is
            # nobody reading a terminal.
            if ! collect; then
                write_error "$FAIL_REASON"
                bad "could not count downloads: $FAIL_REASON"
                return 1
            fi
            write_state ok || { bad "could not write $STATE"; return 1; }
            ;;
        --cached|cached)  cmd_cached ;;
        --watch|watch)    shift; cmd_watch "${1:-status}" ;;
        --help|-h|help)   usage ;;
        --version)        say "syn downloads $VERSION" ;;
        *)
            bad "unknown option: $1"
            usage >&2
            return 1
            ;;
    esac
}

main "$@"
