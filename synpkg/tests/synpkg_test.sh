#!/usr/bin/env bash
#
# synpkg_test.sh — read-only checks against a real ALPM database.
#
# Everything here is a QUERY. The test suite runs under `meson test`, which may
# run on a live desktop or in a makepkg chroot, and a package manager test that
# can install or remove is one bad path away from mutating the machine that is
# building it. Nothing below takes the database lock or needs root.
#
# Environment-dependent facts (is BlackArch enabled, is anything upgradable) are
# checked for SHAPE, never for a specific answer: those differ between velle's
# box, the ISO build chroot, and a fresh install, and a test that only passes on
# one of them is a test that gets disabled.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

SYNPKG=${1:-./build/synpkg}
[ -x "$SYNPKG" ] || { echo "not executable: $SYNPKG" >&2; exit 1; }

# The catalogue lives beside the source during a build, not at SYNPKG_DATADIR.
export SYNPKG_CURATED="${SYNPKG_CURATED:-$(dirname "$0")/../data/curated.tsv}"

pass=0 fail=0

ok()   { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad()  { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail + 1)); }
check() { if [ "$2" = 0 ]; then ok "$1"; else bad "$1"; fi; }

# `((n++))` evaluates to the OLD value, so a bare post-increment returns 1 the
# first time and kills the script under `set -e`. Hence $((n + 1)) above.

echo "synpkg tests — $SYNPKG"

# ── the binary answers at all ───────────────────────────────────────────────
"$SYNPKG" --version | grep -q '^synpkg '
check "--version prints a version" $?

"$SYNPKG" --help | grep -q 'the SynapseOS package manager'
check "--help prints usage" $?

"$SYNPKG" definitely-not-a-command >/dev/null 2>&1
[ $? -eq 2 ] && ok "unknown command exits 2" || bad "unknown command exits 2"

# ── TSV shape ───────────────────────────────────────────────────────────────
# The GUI splits on tab and trusts the column count. A row with the wrong
# number of fields renders as blank cells with no error anywhere, so the field
# count is the single most important invariant in this program.

tsv_cols() { awk -F'\t' 'NR==1 {print NF}'; }

n=$("$SYNPKG" --tsv status | tsv_cols)
[ "$n" = 4 ] && ok "status --tsv has 4 columns" || bad "status --tsv has 4 columns (got $n)"

n=$("$SYNPKG" --tsv installed --explicit | tsv_cols)
[ "$n" = 6 ] && ok "installed --tsv has 6 columns" || bad "installed --tsv has 6 columns (got $n)"

n=$("$SYNPKG" --tsv suggest | tsv_cols)
[ "$n" = 6 ] && ok "suggest --tsv has 6 columns" || bad "suggest --tsv has 6 columns (got $n)"

n=$("$SYNPKG" --tsv updates | tsv_cols)
[ "$n" = 5 ] && ok "updates --tsv has 5 columns" || bad "updates --tsv has 5 columns (got $n)"

# Every data row must carry the same field count as its header. This is what
# catches a description containing a literal tab — the exact bug the field
# stripping in tsv_row() exists to prevent, and one that only appears when some
# package upstream adds one.
ragged=$("$SYNPKG" --tsv installed | awk -F'\t' 'NR==1 {want=NF; next} NF!=want {n++} END {print n+0}')
[ "$ragged" = 0 ] && ok "no ragged rows in installed --tsv" \
                  || bad "$ragged ragged rows in installed --tsv"

ragged=$("$SYNPKG" --tsv suggest | awk -F'\t' 'NR==1 {want=NF; next} NF!=want {n++} END {print n+0}')
[ "$ragged" = 0 ] && ok "no ragged rows in suggest --tsv" \
                  || bad "$ragged ragged rows in suggest --tsv"

# ── TSV mode never writes anything but records to stdout ────────────────────
# A single stray progress line on stdout becomes a garbage row in the GUI.
stray=$("$SYNPKG" --tsv status 2>/dev/null | grep -cv $'\t')
[ "$stray" = 0 ] && ok "status --tsv writes only tab-separated rows" \
                 || bad "status --tsv wrote $stray non-record lines to stdout"

# ── the curated catalogue ───────────────────────────────────────────────────
# A malformed line warns rather than rendering an empty row, so the absence of
# warnings is the check.
warns=$("$SYNPKG" --tsv suggest 2>&1 >/dev/null | grep -c 'catalogue line')
[ "$warns" = 0 ] && ok "curated.tsv parses with no malformed lines" \
                 || bad "$warns malformed lines in curated.tsv"

"$SYNPKG" suggest categories | grep -q 'Browsers'
check "suggest categories lists Browsers" $?

# Filtering by a category must not return the whole catalogue.
all=$("$SYNPKG" --tsv suggest | wc -l)
one=$("$SYNPKG" --tsv suggest Browsers | wc -l)
[ "$one" -lt "$all" ] && ok "suggest <category> filters" \
                      || bad "suggest <category> returned everything ($one/$all)"

# Every catalogue field must be non-empty: a blank label renders as an unnamed
# button, which is worse than the entry being absent.
blank=$(awk -F'\t' '/^[^#]/ && NF>0 { for (i=1;i<=5;i++) if ($i=="") { n++; break } } END {print n+0}' \
        "$SYNPKG_CURATED")
[ "$blank" = 0 ] && ok "no empty fields in curated.tsv" || bad "$blank entries have an empty field"

# ── exit codes a poller depends on ──────────────────────────────────────────
# 100 means "nothing to do" and MUST be distinguishable from 1, or a status bar
# reports a failure every time the system is current.
"$SYNPKG" --tsv updates >/dev/null 2>&1
rc=$?
[ "$rc" = 0 ] || [ "$rc" = 100 ] && ok "updates exits 0 or 100" \
                                 || bad "updates exited $rc"

# ── arsenal degrades without the repo ───────────────────────────────────────
# status must answer on a machine with no BlackArch rather than failing: the
# whole point of the app on a fresh install is telling you it is missing.
"$SYNPKG" --tsv arsenal status >/dev/null 2>&1
rc=$?
case $rc in
    0|2|3) ok "arsenal status answers (rc=$rc)" ;;
    *)     bad "arsenal status exited $rc" ;;
esac

n=$("$SYNPKG" --tsv arsenal status | tsv_cols)
[ "$n" = 3 ] && ok "arsenal status --tsv has 3 columns" \
             || bad "arsenal status --tsv has 3 columns (got $n)"

# A non-blackarch group must be refused rather than listed: `packages base` on
# an unvalidated path would render a core group as security tooling.
"$SYNPKG" arsenal packages base >/dev/null 2>&1
[ $? -ne 0 ] && ok "arsenal packages rejects a non-blackarch group" \
             || bad "arsenal packages accepted a non-blackarch group"

# ── cachyos ─────────────────────────────────────────────────────────────────
# Same rule as arsenal: status must ANSWER on a machine without the repo. The
# Kernel pane asks this to decide whether its Cachy rows can install, and a
# status that errored would make them look broken rather than unavailable.
"$SYNPKG" cachyos status >/dev/null 2>&1
check "cachyos status answers without the repo" $?

n=$("$SYNPKG" --tsv cachyos status | tsv_cols)
[ "$n" = 2 ] && ok "cachyos status --tsv has 2 columns" \
             || bad "cachyos status --tsv has 2 columns (got $n)"

"$SYNPKG" cachyos frobnicate >/dev/null 2>&1
[ $? -ne 0 ] && ok "cachyos rejects an unknown subcommand" \
             || bad "cachyos accepted an unknown subcommand"

# The bootstrap helper. It cannot be RUN here — it needs root and the network —
# but the two ways it could be wrong before it ever gets that far are cheap to
# check, and a syntax error in it would only surface at the polkit prompt.
CACHY_SH="$(dirname "$0")/../data/synpkg-enable-cachyos.sh"
if [ -f "$CACHY_SH" ]; then
    bash -n "$CACHY_SH" >/dev/null 2>&1
    check "synpkg-enable-cachyos.sh parses" $?

    bash "$CACHY_SH" frobnicate >/dev/null 2>&1
    [ $? -ne 0 ] && ok "the cachyos helper rejects an unknown action" \
                 || bad "the cachyos helper accepted an unknown action"

    # ⚠ It must NEVER add the v3/v4 repositories or upstream's pacman fork.
    # That is the whole reason it exists instead of running cachyos-repo.sh,
    # and it is one careless copy-paste from upstream away from being undone.
    n=$(grep -cE '^[^#]*(cachyos-v3|cachyos-v4|cachyos-znver4|pacman-7)' "$CACHY_SH")
    [ "$n" = 0 ] && ok "the cachyos helper adds no v3/v4 repo or pacman fork" \
                 || bad "the cachyos helper has $n line(s) touching v3/v4 or pacman"

    # Appended, never inserted ahead of [core] — so core and extra keep
    # precedence and enabling the repo cannot re-resolve an installed package.
    grep -q '>> "\$PACMAN_CONF"' "$CACHY_SH" \
        && ok "the cachyos repo section is appended" \
        || bad "the cachyos repo section is not appended to pacman.conf"
fi

# ⚠ escalate() re-execs SYNPKG, so the word it is handed must be one of
# synpkg's OWN subcommands — never the helper script's action.
#
# Getting this wrong is invisible until a real install: the pkexec prompt
# appears, the user authenticates, and the root child dies on synpkg's usage
# message, which run_quiet() swallows. That is exactly what shipped —
# `synpkg cachyos enable` is not a subcommand, so every Cachy kernel install
# asked for a password and then did nothing.
CACHY_C="$(dirname "$0")/../src/cachyos.c"
if [ -f "$CACHY_C" ]; then
    if grep -A3 'escalate("cachyos"' "$CACHY_C" | grep -qE '"(enable|disable)"[^-]'; then
        bad "cachyos escalates a helper ACTION where a synpkg SUBCOMMAND is required"
    else
        ok "cachyos escalates a real synpkg subcommand"
    fi

    # Every subcommand handed to escalate() must be one the dispatcher accepts.
    for sub in $(grep -oE 'escalate\("cachyos", 1, [a-z]+\)' "$CACHY_C" >/dev/null 2>&1; \
                 grep -oE '"(enable|disable)-repo"' "$CACHY_C" | tr -d '"' | sort -u); do
        if grep -q "strcmp(sub, \"$sub\")" "$CACHY_C"; then
            ok "cachyos dispatcher accepts '$sub'"
        else
            bad "cachyos uses '$sub' but the dispatcher does not accept it"
        fi
    done
else
    bad "synpkg-enable-cachyos.sh not found beside the tests: $CACHY_SH"
fi

# Downloading is the longest part of installing anything large — a kernel is a
# couple of hundred megabytes — and it used to report only "downloaded <file>",
# after the fact, and only under --verbose. A caller reading this from outside a
# terminal (syn-settings' Kernel pane, which forwards it to its window) showed a
# status line frozen for minutes, which reads as a hung application.
#
# A source check, because the real one needs a download: the callback must
# handle the PROGRESS event, not just COMPLETED. Losing that is silent — nothing
# fails, there is simply nothing to see for several minutes.
ALPM_C="$(dirname "$0")/../src/alpmctx.c"
if [ -f "$ALPM_C" ]; then
    if grep -q 'ALPM_DOWNLOAD_PROGRESS' "$ALPM_C"; then
        ok "the download callback reports progress, not just completion"
    else
        bad "cb_download ignores ALPM_DOWNLOAD_PROGRESS — downloads are silent again"
    fi
    # Per-chunk, libcurl calls this thousands of times a second. Unthrottled it
    # is a hot loop on a flush, and a flood for anything keeping a history.
    if grep -q 'last_pct' "$ALPM_C"; then
        ok "download progress is throttled to whole percents"
    else
        bad "download progress is unthrottled"
    fi
else
    bad "alpmctx.c not found beside the tests: $ALPM_C"
fi

# ── mutations refuse rather than assume ─────────────────────────────────────
# In TSV mode confirm() returns false, so a transaction without --noconfirm
# declines. NOTE: the two checks below do NOT cover that — they only prove an
# empty target list errors. The decline path needs root and a real transaction,
# so it is untested here, and that gap is exactly how syn-settings' kernel
# installer shipped broken: it never passed --noconfirm, so every install
# authenticated through polkit and then silently declined itself. confirm() now
# says why it refused, which is the part that would have surfaced it.
"$SYNPKG" install >/dev/null 2>&1
[ $? -ne 0 ] && ok "install with no targets is an error" \
             || bad "install with no targets succeeded"

"$SYNPKG" remove >/dev/null 2>&1
[ $? -ne 0 ] && ok "remove with no targets is an error" \
             || bad "remove with no targets succeeded"

# ── about ───────────────────────────────────────────────────────────────────
# The About pane is the only place that reports a source being switched OFF.
# If it stops naming one, that source silently becomes undiscoverable.
n=$("$SYNPKG" --tsv about | tsv_cols)
[ "$n" = 4 ] && ok "about --tsv has 4 columns" || bad "about --tsv has 4 columns (got $n)"

ragged=$("$SYNPKG" --tsv about | awk -F'\t' 'NR==1 {want=NF; next} NF!=want {n++} END {print n+0}')
[ "$ragged" = 0 ] && ok "no ragged rows in about --tsv" \
                  || bad "$ragged ragged rows in about --tsv"

missing=""
for item in Repositories AUR Flathub BlackArch SynapseOS; do
    "$SYNPKG" --tsv about | cut -f1 | grep -qx "$item" || missing="$missing $item"
done
[ -z "$missing" ] && ok "about names every source" || bad "about omits:$missing"

# Every state must be one the front-ends know how to colour. A typo here
# renders as the "info" accent on a source that is actually broken.
strange=$("$SYNPKG" --tsv about | awk -F'\t' 'NR>1 && $2 !~ /^(ok|off|missing|info)$/ {n++} END {print n+0}')
[ "$strange" = 0 ] && ok "about states are all known" || bad "$strange unknown states in about"

# ── installed: --native / --foreign partition the whole set ─────────────────
# The Repositories tab shows --native and the AUR tab shows --foreign. If they
# overlap a package appears twice under two different sources; if they leave a
# gap it is unreachable from either tab.
all=$("$SYNPKG" --tsv installed | tail -n +2 | wc -l)
nat=$("$SYNPKG" --tsv installed --native | tail -n +2 | wc -l)
frn=$("$SYNPKG" --tsv installed --foreign | tail -n +2 | wc -l)
[ $((nat + frn)) = "$all" ] && ok "--native and --foreign partition installed" \
    || bad "--native ($nat) + --foreign ($frn) != installed ($all)"

"$SYNPKG" installed --native --foreign >/dev/null 2>&1
[ $? -ne 0 ] && ok "--native with --foreign is refused" \
             || bad "--native with --foreign was accepted"

n=$("$SYNPKG" --tsv aur installed | tsv_cols)
[ "$n" = 6 ] && ok "aur installed --tsv has 6 columns" \
             || bad "aur installed --tsv has 6 columns (got $n)"

# Foreign packages are labelled `local`, never `aur`: nothing on disk records
# where a package came from, and on a SynapseOS box this list includes synpkg
# itself. A confident `aur` badge on the program's own row is a lie.
wrong=$("$SYNPKG" --tsv aur installed | awk -F'\t' 'NR>1 && $4 != "local" {n++} END {print n+0}')
[ "$wrong" = 0 ] && ok "aur installed labels rows local, not aur" \
                 || bad "$wrong aur-installed rows claim a source they cannot know"

# ── Flatpak, against a stub ─────────────────────────────────────────────────
# flatpak is an optdepend, so the machine running these tests may not have it —
# and the parsing is exactly the part worth testing. The stub emits the column
# shapes flatpak documents, including the two traps the real code guards:
#   * BoxesDevel, to catch a substring match reporting Boxes as installed;
#   * a remote-ls row with no version column, which flatpak genuinely lacks.
STUB=$(mktemp -d)
trap 'rm -rf "$STUB"' EXIT

cat > "$STUB/flatpak" <<'STUBEOF'
#!/usr/bin/env bash
cols=""
for a in "$@"; do case $a in --columns=*) cols=${a#--columns=} ;; esac; done
case "$1" in
remotes)
    case $cols in
        name) echo "flathub" ;;
        *) printf 'flathub\thttps://dl.flathub.org/repo/\t\n' ;;
    esac ;;
list)
    case $cols in
        application) printf 'org.mozilla.firefox\norg.gnome.BoxesDevel\n' ;;
        application,version) printf 'org.mozilla.firefox\t142.0\norg.gnome.BoxesDevel\t46.1\n' ;;
        *) printf 'org.mozilla.firefox\t142.0\tflathub\tFirefox\n' ;;
    esac ;;
search)
    printf 'org.mozilla.firefox\t142.0\tflathub\tFirefox\tWeb browser\n'
    printf 'org.gnome.Boxes\t46.1\tflathub\tBoxes\tVirtual machines\n' ;;
remote-ls)
    printf 'org.mozilla.firefox\tflathub\n' ;;
*) exit 0 ;;
esac
STUBEOF
chmod +x "$STUB/flatpak"

fp() { PATH="$STUB:$PATH" "$SYNPKG" "$@"; }

n=$(fp --tsv flatpak search firefox | tsv_cols)
[ "$n" = 7 ] && ok "flatpak search --tsv has 7 columns" \
             || bad "flatpak search --tsv has 7 columns (got $n)"

# The seventh column must be named, not just present: the GUI reads rows by
# header name, so a renamed column silently blanks the app title everywhere.
fp --tsv flatpak search firefox | head -1 | grep -q 'title$'
check "flatpak search names its title column" $?

ragged=$(fp --tsv flatpak search firefox | awk -F'\t' 'NR==1 {want=NF; next} NF!=want {n++} END {print n+0}')
[ "$ragged" = 0 ] && ok "no ragged rows in flatpak search --tsv" \
                  || bad "$ragged ragged rows in flatpak search --tsv"

# org.gnome.Boxes is NOT installed; org.gnome.BoxesDevel is. A substring test
# would mark the wrong one, and the button would offer to remove software the
# user never installed.
inst=$(fp --tsv flatpak search boxes | awk -F'\t' '$1=="org.gnome.Boxes" {print $2}')
[ "$inst" = "0" ] && ok "flatpak search does not confuse Boxes with BoxesDevel" \
                  || bad "org.gnome.Boxes reported installed=$inst (want 0)"

inst=$(fp --tsv flatpak search firefox | awk -F'\t' '$1=="org.mozilla.firefox" {print $2}')
[ "$inst" = "1" ] && ok "flatpak search marks an installed app installed" \
                  || bad "org.mozilla.firefox reported installed=$inst (want 1)"

n=$(fp --tsv flatpak list | tsv_cols)
[ "$n" = 7 ] && ok "flatpak list --tsv has 7 columns" \
             || bad "flatpak list --tsv has 7 columns (got $n)"

# Updates must share the 5-column shape pacman and the AUR emit, because the
# GUI concatenates all three into one Updates pane.
n=$(fp --tsv flatpak updates | tsv_cols)
[ "$n" = 5 ] && ok "flatpak updates --tsv has 5 columns" \
             || bad "flatpak updates --tsv has 5 columns (got $n)"

fp --tsv flatpak updates | grep -q '^org.mozilla.firefox	142.0	'
check "flatpak updates pairs the app with its installed version" $?

stray=$(fp --tsv flatpak search firefox 2>/dev/null | grep -cv $'\t')
[ "$stray" = 0 ] && ok "flatpak search --tsv writes only records to stdout" \
                 || bad "flatpak search --tsv wrote $stray non-record lines"

# ── Category panes ──────────────────────────────────────────────────────────
# Four sources feed ONE pane in the GUI, and the pane keys its fields by header
# name. If any of them drifts a column the pane does not error — it draws blank
# labels or, worse, sends the wrong string back as the category to open. This
# is the invariant that makes one pane possible, so it is checked as one.
for cmd in "suggest categories" "arsenal categories" "groups"; do
    hdr=$("$SYNPKG" --tsv $cmd | head -1)
    [ "$hdr" = $'category\ttotal\tinstalled\tlabel' ] \
        && ok "$cmd --tsv has the category header" \
        || bad "$cmd --tsv header is '$hdr'"
done

# The counts are numbers the pane does arithmetic on; a non-numeric cell renders
# as NaN rather than failing.
nonnum=$("$SYNPKG" --tsv suggest categories |
         awk -F'\t' 'NR>1 && ($2 !~ /^[0-9]+$/ || $3 !~ /^[0-9]+$/) {n++} END {print n+0}')
[ "$nonnum" = 0 ] && ok "suggest categories counts are numeric" \
                  || bad "$nonnum suggest categories rows have non-numeric counts"

# installed can never exceed total — if it does, the pane shows "17/6".
overcount=$("$SYNPKG" --tsv suggest categories |
            awk -F'\t' 'NR>1 && $3 > $2 {n++} END {print n+0}')
[ "$overcount" = 0 ] && ok "suggest categories never counts more installed than total" \
                     || bad "$overcount suggest categories rows have installed > total"

# Arsenal's label is the group with its "blackarch-" prefix off. Stripping it in
# the GUI would mean the shared pane knowing one source's naming scheme.
bad_label=$("$SYNPKG" --tsv arsenal categories |
            awk -F'\t' 'NR>1 && ($1 !~ /^blackarch-/ || $4 != substr($1, 11)) {n++} END {print n+0}')
[ "$bad_label" = 0 ] && ok "arsenal categories label drops the blackarch- prefix" \
                     || bad "$bad_label arsenal rows have a mislabelled category"

# ── the header is owed even when there is nothing to list ───────────────────
#
# ⚠ The header check above passed on the development machine and FAILED on the
# VM, because it was reading whatever state that machine's pacman.conf happened
# to be in. With BlackArch configured it listed categories; without, it emitted
# `disabled 0` — a three-column status record standing in for the pane's
# four-column header.
#
# So the state is a FIXTURE now, not an inheritance. pconf_repo_list shells out
# to pacman-conf, so a stub on PATH is enough to take the repo away from a
# machine that has it — and to give this assertion the same verdict everywhere.
cat > "$STUB/pacman-conf" <<'STUBEOF'
#!/bin/sh
# Every repo this machine has, except blackarch.
# ⚠ `exec a | b` execs only the PIPELINE's subshell and the script carries on
# to the next line, which silently emitted the list TWICE — with blackarch in
# the second copy — and looked exactly like the stub not being used at all.
if [ "$1" = "--repo-list" ]; then
    /usr/bin/pacman-conf --repo-list | grep -vx blackarch
    exit 0
fi
exec /usr/bin/pacman-conf "$@"
STUBEOF
chmod +x "$STUB/pacman-conf"

noba() { PATH="$STUB:$PATH" "$SYNPKG" "$@"; }

hdr=$(noba --tsv arsenal categories | head -1)
[ "$hdr" = $'category\ttotal\tinstalled\tlabel' ] \
    && ok "arsenal categories keeps its header with the repo absent" \
    || bad "arsenal categories header with no repo is '$hdr'"

n=$(noba --tsv arsenal categories | wc -l)
[ "$n" = 1 ] && ok "...and lists nothing under it" \
             || bad "arsenal categories emitted $n lines with no repo"

# The listing delegated to arsenal_status for the message and the exit code,
# which is how the status record ended up in the data. Same defect, and it was
# only ever caught in `categories`.
hdr=$(noba --tsv arsenal packages blackarch-fuzzer | head -1)
[ "$hdr" = $'name\tinstalled\tversion\trepo\tsize\tdescription' ] \
    && ok "arsenal packages keeps its header with the repo absent" \
    || bad "arsenal packages header with no repo is '$hdr'"

# The reason is not lost — it is the exit code, and status is what reports it.
noba --tsv arsenal categories >/dev/null 2>&1
[ $? = 2 ] && ok "...and the exit code still says the repo is disabled" \
           || bad "arsenal categories with no repo did not exit 2"

st=$(noba --tsv arsenal status | head -1 | cut -f1)
[ "$st" = disabled ] && ok "arsenal status still reports the state as a record" \
                     || bad "arsenal status with no repo said '$st'"

n=$("$SYNPKG" --tsv groups | wc -l)
[ "$n" -gt 1 ] && ok "groups lists at least one browsable group" \
               || bad "groups listed nothing (got $n lines)"

# Groups outside the curated set are refused rather than rendered. `kf6` is 300
# libraries with an Install button on each, and none of them is installed by hand.
"$SYNPKG" groups kf6 >/dev/null 2>&1
[ $? -eq 1 ] && ok "groups refuses a non-browsable group" \
             || bad "groups accepted kf6"

# A browsable group must emit the SIX-column package shape, not the category
# shape: its rows go into the same list as a search result.
grp=$("$SYNPKG" --tsv groups | awk -F'\t' 'NR==2 {print $1}')
if [ -n "$grp" ]; then
    n=$("$SYNPKG" --tsv groups "$grp" | tsv_cols)
    [ "$n" = 6 ] && ok "groups <group> --tsv has 6 columns" \
                 || bad "groups <group> --tsv has 6 columns (got $n)"
else
    ok "groups <group> --tsv has 6 columns (skipped: no groups on this machine)"
fi

# The Games category is data, so it is deterministic and worth pinning: it is
# what the Gaming category is NOT, and folding the two together was considered
# and rejected.
"$SYNPKG" --tsv suggest categories | grep -q $'^Games\t'
check "the catalogue has a Games category" $?

"$SYNPKG" --tsv suggest Games | grep -q $'^Games\t0ad\t'
check "suggest Games lists a game" $?

# Gaming is launchers and overlays, Games are games. A game turning up under
# Gaming means the two have been merged by accident.
"$SYNPKG" --tsv suggest Gaming | grep -q $'\t0ad\t' && \
    bad "0ad is in Gaming — Gaming is tooling, Games are games" || \
    ok "Gaming holds no games"

# ── Flathub categories, against an AppStream fixture ────────────────────────
# The scanner reads a 48MB catalogue for five fields, and every trap below is
# one the real Flathub index contains. A fixture rather than the live index:
# these must give the same answer on the ISO build chroot, which has neither
# flatpak nor a remote.
AS="$STUB/home/.local/share/flatpak/appstream/flathub/x86_64/active"
mkdir -p "$AS"
cat > "$AS/appstream.xml" <<'ASEOF'
<?xml version="1.0" encoding="UTF-8"?>
<components version="0.8">
  <component type="desktop-application">
    <id>org.mozilla.firefox.desktop</id>
    <name>Firefox</name>
    <name xml:lang="de">Feuerfuchs</name>
    <summary>Browse the web</summary>
    <summary xml:lang="de">Das Web durchsuchen</summary>
    <bundle type="flatpak">app/org.mozilla.firefox/x86_64/stable</bundle>
    <categories><category>Network</category><category>WebBrowser</category></categories>
    <releases><release timestamp="1" version="142.0"/></releases>
  </component>
  <component type="desktop-application">
    <id>org.audacityteam.Audacity</id>
    <name>Audacity</name>
    <summary>Record &amp; edit audio</summary>
    <bundle type="flatpak">app/org.audacityteam.Audacity/x86_64/stable</bundle>
    <categories><category>AudioVideo</category><category>Audio</category></categories>
  </component>
  <component type="desktop-application">
    <id>org.example.AudioOnly</id>
    <name>Audio Only</name>
    <summary>Carries the Audio subcategory and nothing else</summary>
    <bundle type="flatpak">app/org.example.AudioOnly/x86_64/stable</bundle>
    <categories><category>Audio</category></categories>
  </component>
  <component type="runtime">
    <id>org.freedesktop.Platform</id>
    <name>Freedesktop Platform</name>
    <summary>A runtime, not an application</summary>
    <bundle type="flatpak">runtime/org.freedesktop.Platform/x86_64/24.08</bundle>
    <categories><category>Network</category></categories>
  </component>
  <component type="desktop-application">
    <id>org.example.Uncategorised</id>
    <name>Uncategorised</name>
    <summary>No categories element at all</summary>
    <bundle type="flatpak">app/org.example.Uncategorised/x86_64/stable</bundle>
  </component>
</components>
ASEOF

# HOME is what picks the fixture up: flatpak searches the user installation
# before the system one, and so does this.
fa() { PATH="$STUB:$PATH" HOME="$STUB/home" "$SYNPKG" "$@"; }

hdr=$(fa --tsv flatpak categories | head -1)
[ "$hdr" = $'category\ttotal\tinstalled\tlabel' ] \
    && ok "flatpak categories --tsv has the category header" \
    || bad "flatpak categories --tsv header is '$hdr'"

# Internet holds Firefox and NOT the runtime that also claims Network.
row=$(fa --tsv flatpak categories | awk -F'\t' '$1=="Network" {print $2"/"$3}')
[ "$row" = "1/1" ] && ok "flatpak categories skips runtimes and counts installed" \
                   || bad "Network category is $row (want 1/1 — a runtime leaked in?)"

# Audio is a SUBcategory of nothing here: a substring match would file the
# Audio-only app under AudioVideo, which is how Audacity and a soundboard end
# up in the same bucket as a video editor.
row=$(fa --tsv flatpak categories | awk -F'\t' '$1=="AudioVideo" {print $2}')
[ "$row" = "1" ] && ok "flatpak categories matches whole category names" \
                 || bad "AudioVideo holds $row apps (want 1 — did Audio substring-match?)"

# An app with no <categories> belongs nowhere and must not inflate a count.
fa --tsv flatpak categories | grep -q 'Uncategorised' && \
    bad "an uncategorised app reached the category pane" || \
    ok "an uncategorised app is left out"

n=$(fa --tsv flatpak category Network | tsv_cols)
[ "$n" = 7 ] && ok "flatpak category --tsv has 7 columns" \
             || bad "flatpak category --tsv has 7 columns (got $n)"

# THE trap. 474 of Flathub's components carry a legacy <id> ending in
# ".desktop", and `flatpak install org.mozilla.firefox.desktop` installs
# nothing. The installable id comes from the bundle ref.
id=$(fa --tsv flatpak category Network | awk -F'\t' 'NR==2 {print $1}')
[ "$id" = "org.mozilla.firefox" ] && ok "flatpak category takes the id from the bundle ref" \
                                  || bad "row id is '$id' (want org.mozilla.firefox)"

# Every human string is repeated once per translation. Matching "<name" rather
# than "<name>" hands back whichever language sorted first.
title=$(fa --tsv flatpak category Network | awk -F'\t' 'NR==2 {print $7}')
[ "$title" = "Firefox" ] && ok "flatpak category takes the untranslated name" \
                         || bad "row title is '$title' (want Firefox, not a translation)"

ver=$(fa --tsv flatpak category Network | awk -F'\t' 'NR==2 {print $3}')
[ "$ver" = "142.0" ] && ok "flatpak category reads the release version" \
                     || bad "row version is '$ver' (want 142.0)"

desc=$(fa --tsv flatpak category AudioVideo | awk -F'\t' 'NR==2 {print $6}')
[ "$desc" = "Record & edit audio" ] && ok "flatpak category decodes XML entities" \
                                    || bad "row description is '$desc' (want an unescaped &)"

# The display label is accepted as well as the catalogue key, so
# `synpkg flatpak category Internet` does what it looks like it does — the GUI
# shows the label and a person retyping it should not be told it is not a
# category.
by_key=$(fa --tsv flatpak category Network)
by_label=$(fa --tsv flatpak category Internet)
[ "$by_key" = "$by_label" ] && ok "flatpak category accepts a display label" \
                            || bad "'Internet' and 'Network' returned different rows"

fa flatpak category Nonsense >/dev/null 2>&1
[ $? -eq 1 ] && ok "flatpak category refuses an unknown category" \
             || bad "flatpak category accepted 'Nonsense'"

# A machine with a remote but no index is a real state, and it must not look
# like "Flathub is empty". 100 is this program's "nothing to do".
#
# SYNPKG_APPSTREAM rather than an empty HOME: the discovery path falls back to
# /var/lib/flatpak, so on a machine that HAS an index this would otherwise
# quietly test the real catalogue and pass for the wrong reason.
PATH="$STUB:$PATH" SYNPKG_APPSTREAM="$STUB/nothing-here.xml" \
    "$SYNPKG" --tsv flatpak categories >/dev/null 2>&1
[ $? -eq 100 ] && ok "flatpak categories reports 100 when there is no index" \
               || bad "flatpak categories did not report 100 without an index"

# The document's root element is <components>, and "<component" is a prefix of
# it. A scan that does not check the delimiter treats the root tag as the first
# component and drops the first real application — silently, forever, and only
# the first one, which is why it survives a spot-check of the output.
first=$(fa --tsv flatpak category Network | awk -F'\t' 'NR==2 {print $1}')
[ "$first" = "org.mozilla.firefox" ] \
    && ok "the <components> root does not swallow the first application" \
    || bad "first application is '$first' — root element mis-scanned?"

# ── install: the AUR fallback ───────────────────────────────────────────────
#
# `install` used to stop at "not found in any repository", so a package the AUR
# carries and no repository does could not be installed by name at all — which
# is what broke `synpkg install limine-mkinitcpio-hook` on limine machines
# installed before the local repo carried it.
#
# None of these may build anything, so every case here is one that must REFUSE.
# The fallback's success path is deliberately not exercised: it clones and runs
# makepkg, which is not a thing a test suite should do to the machine it is on.

# A name in neither place must say so, and must say it about BOTH places — the
# old message named only repositories and was then telling the truth about half
# the search.
out=$("$SYNPKG" install definitely-not-a-real-package-xyzzy 2>&1 </dev/null || true)
case "$out" in
    *"not found in any repository or in the AUR"*)
        ok "a name in neither place says both were searched" ;;
    *) bad "unexpected refusal: $out" ;;
esac

# --no-aur must not merely skip the build, it must not CLAIM to have searched
# the AUR. A message naming a source that was never consulted is worse than a
# terse one.
out=$("$SYNPKG" install definitely-not-a-real-package-xyzzy --no-aur 2>&1 </dev/null || true)
case "$out" in
    *"or in the AUR"*) bad "--no-aur still claimed to have searched the AUR" ;;
    *"not found in any repository"*)
        ok "--no-aur refuses without claiming an AUR search" ;;
    *) bad "unexpected --no-aur refusal: $out" ;;
esac

# `upgrade` is three passes now — repositories, the AUR, then the SynapseOS
# components — and each of the two optional ones has an off switch. A component
# rebuild is minutes of compiling, so someone who only wants their repository
# packages current has to be able to say so.
out=$("$SYNPKG" upgrade --no-system --definitely-not-a-flag 2>&1 </dev/null || true)
case "$out" in
    *"unknown argument '--definitely-not-a-flag'"*)
        ok "upgrade accepts --no-system and still rejects nonsense" ;;
    *"unknown argument '--no-system'"*)
        bad "upgrade does not accept --no-system" ;;
    *) bad "unexpected upgrade argument handling: $out" ;;
esac

# THE POINT OF THE FLAG: a package that really is in the AUR must be refused
# outright under --no-aur rather than built.
#
# The name is checked against BOTH sources before the case runs, and the case
# is skipped unless it is genuinely AUR-only. That guard is not defensive
# padding — the first version of this test used `yay`, which is in [extra], so
# `install` correctly took the REPOSITORY path and asked pkexec to authenticate
# a real transaction on the machine running the suite. A test must not be able
# to do that, and "the name I picked is in no repo" is not a fact that stays
# true: limine-mkinitcpio-hook is AUR-only today and is vendored in-tree, so on
# a box whose local repo carries it this must skip, not escalate.
probe=limine-mkinitcpio-hook
if pacman -Si "$probe" >/dev/null 2>&1; then
    ok "skipped: $probe is in a repository here, so it is not an AUR-only case"
elif curl -fsS --max-time 15 \
        "https://aur.archlinux.org/rpc/v5/info?arg[]=$probe" 2>/dev/null \
        | grep -q '"resultcount":1'; then
    out=$("$SYNPKG" install "$probe" --no-aur 2>&1 </dev/null || true)
    case "$out" in
        *"not found in any repository"*)
            ok "--no-aur refuses a package that IS in the AUR" ;;
        *) bad "--no-aur did not refuse an AUR-only package: $out" ;;
    esac
else
    ok "offline or $probe is gone from the AUR; that case was not exercised"
fi

# An unknown option must still be rejected. --no-aur was added to this parser,
# and a parser that starts accepting anything beginning with two dashes is how
# a typo becomes a package name.
if "$SYNPKG" install --no-such-flag foo >/dev/null 2>&1 </dev/null; then
    bad "install accepted an unknown option"
else
    ok "install still rejects an unknown option"
fi

# ── the window follows the desktop font ─────────────────────────────────────
# The font is ~/.config/synui/font.state, not theme.json — it outlives a theme
# switch — and it carries a SCALE as well as a family. This window read neither
# until 2026-08-11: the control panel's font picker moved Settings and Files
# while Software and Arsenal stayed on the face they started with, which reads
# as "the theming missed those apps".
#
# Qt resolves an application's default font ONCE at startup, so both the family
# and the size have to be BINDINGS on every Text. A bare `font.pixelSize: 13`
# or a literal family is the regression, and neither shows up as an error
# anywhere — the window simply stops moving with the desktop.
QML="$(dirname "$0")/../data/synpkg.qml"
if [ -f "$QML" ]; then
    grep -q 'config/synui/font.state' "$QML" \
        && ok "the desktop font file is watched" \
        || bad "synpkg.qml does not read font.state"
    grep -q 'root.textScale = s' "$QML" \
        && ok "the scale is read from the same file" \
        || bad "synpkg.qml reads the family but not the scale"

    # awk rather than `grep -c ... | grep -vc ...`: grep exits 1 on no matches,
    # and under pipefail a correct zero would be read as a failed check.
    n=$(awk '/pixelSize: *[0-9]/ { n++ } END { print n + 0 }' "$QML")
    [ "$n" = 0 ] && ok "no pixel size bypasses ui()" \
                 || bad "$n pixel size(s) bypass ui()"

    # The monospace exception is exempt from the FAMILY rule — a command to
    # type is not prose — but not from the size rule above.
    n=$(awk '/family: *"/ && !/family: *"monospace"/ { n++ } END { print n + 0 }' "$QML")
    [ "$n" = 0 ] && ok "every literal family is the deliberate monospace" \
                 || bad "$n literal font family/families are not monospace"

    # ── Nothing in a row may run under the action button ────────────────────
    #
    # A Row lays children out left to right and does NOT clip, so a child wider
    # than the space left draws over whatever is there — which is the Install
    # button. The package id and the category did exactly that: at 520 px the
    # rows overflowed by 101-124 px, the button's own footprint, and the row
    # read "widelands Games" with "Install" printed through it.
    #
    # Each item is capped against its own x, which Row has already assigned
    # from the items before it. Three of them need it; the name has its own cap.
    n=$(awk '/nameRow\.width - x/ { n++ } END { print n + 0 }' "$QML")
    [ "$n" -ge 3 ] && ok "row items are capped against the space left" \
                   || bad "only $n row item(s) cap against nameRow.width - x (need 3)"

    # Math.max(0, …) is not decoration: a NEGATIVE width does not clamp, it
    # defeats clip and paints the item mirrored across its own origin.
    n=$(awk '/Math\.min\(implicitWidth, nameRow\.width - x\)/ && !/Math\.max\(0,/ { n++ } END { print n + 0 }' "$QML")
    [ "$n" = 0 ] && ok "every cap is floored at zero" \
                 || bad "$n cap(s) can go negative"

    # ── the wait has to look like a wait ────────────────────────────────────
    #
    # An install or a list refresh is a wait the user cannot do anything
    # during, and the only sign of either used to be the word "loading…" in the
    # far corner — which a person looking at the row they just clicked never
    # sees change.
    grep -q 'component ProgressTrack' "$QML" \
        && ok "the window has a progress track" \
        || bad "ProgressTrack is gone — a busy window looks idle again"

    # It must cover BOTH waits: a row action (busy) and a list read (loading).
    grep -q 'active: root.loading || root.busy !== ""' "$QML" \
        && ok "the track runs for both an action and a load" \
        || bad "the track no longer follows busy and loading"

    # NO FAKE PERCENTAGE. synpkg collects an install's output at the end, so
    # there is no progress to report; a bar that creeps to 90% and waits is a
    # lie the user finds out about. The shuttle is the honest state, and it is
    # what pct < 0 selects.
    awk '/component ProgressTrack/,/^    }/' "$QML" | grep -q 'property int pct: -1' \
        && ok "the track defaults to indeterminate" \
        || bad "the track no longer defaults to no-percentage"

    # from/to are read at restart, not bound, so a resize has to restart it or
    # the shuttle sweeps a width the window no longer has.
    awk '/component ProgressTrack/,/^    }/' "$QML" | grep -q 'onWidthChanged: if (shuttle.visible) shuttleAnim.restart()' \
        && ok "the shuttle restarts when the window is resized" \
        || bad "the shuttle will sweep a stale width after a resize"

    # ── SynapseOS components on the Updates page ────────────────────────────
    #
    # They were only ever on their own tab, so the one page a person opens to
    # ask "is anything out of date?" answered it wrongly — and the components
    # are the half of this system no other updater can see.
    grep -q '{ kind: "system",  tab: "system",  args: \["system", "check"\]' "$QML" \
        && ok "the Updates page checks SynapseOS components" \
        || bad "the Updates chain has no system step"

    # THE GUARD THAT MATTERS. A component row is tagged installed, so without
    # the source check in act() the button reads "Remove" and the click runs
    # `synpkg remove synui` — uninstalling the compositor from the Updates
    # page. The row must reach syn-update and nothing else.
    grep -q 'if (row.source === "system")' "$QML" \
        && ok "a component row is routed to syn-update, not into a transaction" \
        || bad "act() no longer routes system rows away from ALPM"

    # A row with no action button is what the SynapseOS tab used to be: a list
    # of available updates and no way to take any of them.
    #
    # Anchored on the `visible:` BINDING, not on the string anywhere in the
    # file — the first cut of this check matched the comment that explains why
    # the condition was removed, and failed against the fix it was written for.
    grep -qE '^[[:space:]]*visible:.*extra !== "component"' "$QML" \
        && bad "the action button is still hidden for SynapseOS components" \
        || ok "SynapseOS components get an action button"
else
    bad "synpkg.qml not found beside the tests: $QML"
fi

# ── the Arch news gate ──────────────────────────────────────────────────────
#
# Every check here reads a FIXTURE, never the network. archlinux.org rate-limits
# — it answered 429 after six requests while this was being written — and a test
# that needs the internet is a test that fails in the makepkg chroot and then
# gets deleted. SYNPKG_NEWS_FILE and SYNPKG_PACMAN_LOG exist for exactly this.

FEED="$(dirname "$0")/news-feed.xml"
LOGDIR=$(mktemp -d)
KDIR=$(mktemp -d)

# ⚠ ONE trap, naming EVERY temporary directory. `trap ... EXIT` REPLACES the
# handler; it does not add to it. A second `trap 'rm -rf "$LOGDIR"' EXIT` here
# silently disarmed the $STUB cleanup installed above, so every run of the suite
# would have leaked a directory — with nothing failing to say so.
trap 'rm -rf "$STUB" "$LOGDIR" "$KDIR"' EXIT

printf '[2020-01-01T00:00:00-0500] [PACMAN] starting full system upgrade\n' > "$LOGDIR/ancient.log"
printf '[2026-07-01T10:00:00-0500] [SYNPKG] completed full system upgrade\n' > "$LOGDIR/jul01.log"
printf '[2026-08-01T10:00:00-0500] [SYNPKG] completed full system upgrade\n' > "$LOGDIR/aug01.log"
# The format pacman used before it logged ISO 8601 with a zone.
printf '[2026-07-01 10:00] [PACMAN] starting full system upgrade\n' > "$LOGDIR/oldfmt.log"
# A log with no upgrade in it at all — the "cannot tell" case.
printf '[2026-08-01T10:00:00-0500] [ALPM] upgraded foo (1-1 -> 1-2)\n' > "$LOGDIR/nomarker.log"

export SYNPKG_NEWS_FILE="$FEED"

# Count the rendered date lines: one per item shown.
news_items() { grep -cE '^  [0-9]{4}-[0-9]{2}-[0-9]{2}$' || true; }

if [ ! -f "$FEED" ]; then
    bad "the news fixture is missing: $FEED"
else
    n=$("$SYNPKG" --no-color news --all 2>/dev/null | news_items)
    [ "$n" = 3 ] && ok "news --all renders every dated item" \
                 || bad "news --all renders every dated item (got $n, want 3)"

    # The undated item must NOT be shown as unread. A feed is allowed to omit
    # pubDate, and treating an undated post as brand new would show it at every
    # upgrade forever.
    n=$(SYNPKG_PACMAN_LOG="$LOGDIR/ancient.log" "$SYNPKG" --no-color news 2>/dev/null | news_items)
    [ "$n" = 3 ] && ok "an undated item is not counted as unread" \
                 || bad "an undated item is not counted as unread (got $n, want 3)"

    n=$(SYNPKG_PACMAN_LOG="$LOGDIR/jul01.log" "$SYNPKG" --no-color news 2>/dev/null | news_items)
    [ "$n" = 1 ] && ok "only news newer than the last upgrade is unread" \
                 || bad "only news newer than the last upgrade is unread (got $n, want 1)"

    SYNPKG_PACMAN_LOG="$LOGDIR/aug01.log" "$SYNPKG" --no-color news >/dev/null 2>&1
    [ $? -eq 100 ] && ok "nothing unread exits 100" || bad "nothing unread exits 100"

    # pacman's OLD log format. Silently failing to parse it would push the
    # cutoff to "unknown" and quietly change what the gate does.
    n=$(SYNPKG_PACMAN_LOG="$LOGDIR/oldfmt.log" "$SYNPKG" --no-color news 2>/dev/null | news_items)
    [ "$n" = 1 ] && ok "the pre-ISO8601 pacman.log timestamp still parses" \
                 || bad "the pre-ISO8601 pacman.log timestamp still parses (got $n, want 1)"

    # `[ALPM] transaction started` is logged for every install and every
    # removal. Matching it would mark the news read when a single package was
    # installed — wrong in the direction that hides a warning.
    n=$(SYNPKG_PACMAN_LOG="$LOGDIR/nomarker.log" "$SYNPKG" --no-color news 2>/dev/null | news_items)
    [ "$n" = 3 ] && ok "an ordinary package upgrade does not mark the news read" \
                 || bad "an ordinary package upgrade does not mark the news read (got $n, want 3)"

    # Entities, in both spellings, and CDATA. A title rendered as "Widgets &amp;
    # sprockets" is the visible half of a decoder that is also feeding the body.
    out=$("$SYNPKG" --no-color news --all 2>/dev/null)
    printf '%s' "$out" | grep -q 'Widgets & sprockets' \
        && ok "CDATA and named entities decode in a title" \
        || bad "CDATA and named entities decode in a title"
    printf '%s' "$out" | grep -q 'gizmo >= 2.0' \
        && ok "&gt; decodes in a title" || bad "&gt; decodes in a title"
    printf '%s' "$out" | grep -q '🎉' \
        && ok "a numeric entity decodes to UTF-8" || bad "a numeric entity decodes to UTF-8"
    # Matched on a fragment, not the whole command: the body is WRAPPED, so
    # "Run pacman -Syu --overwrite …" legitimately spans two lines. The first
    # version of this check wanted the whole string on one line and failed
    # against correct output.
    printf '%s' "$out" | grep -q -- "--overwrite '/usr/lib/gizmo/\*'" \
        && ok "escaped markup in a description survives tag stripping" \
        || bad "escaped markup in a description survives tag stripping"
    printf '%s' "$out" | grep -q '<p>' \
        && bad "HTML tags are leaking into the rendered body" \
        || ok "HTML tags are stripped from the body"

    # A token longer than a line has no space to break on. Hard-splitting it is
    # the difference between a wrapped paragraph and one 100-character line.
    longest=$(printf '%s' "$out" | awk '{ if (length($0) > m) m = length($0) } END { print m + 0 }')
    [ "$longest" -le 100 ] && ok "an unbreakable token is hard-split, not overflowed" \
                           || bad "a line ran to $longest characters"

    n=$("$SYNPKG" --tsv news --all 2>/dev/null | tsv_cols)
    [ "$n" = 4 ] && ok "news --tsv has 4 columns" || bad "news --tsv has 4 columns (got $n)"

    # An unparseable feed must be reported, not treated as "no news".
    printf 'not xml at all\n' > "$LOGDIR/junk.xml"
    SYNPKG_NEWS_FILE="$LOGDIR/junk.xml" "$SYNPKG" news >/dev/null 2>&1
    [ $? -eq 1 ] && ok "an unparseable feed fails rather than reporting no news" \
                 || bad "an unparseable feed fails rather than reporting no news"
fi

"$SYNPKG" --tsv config 2>/dev/null | grep -q '^upgrade_news' \
    && ok "upgrade_news is a listed setting" || bad "upgrade_news is a listed setting"

# ── installed vs running kernel ─────────────────────────────────────────────
#
# Against a SYNTHETIC /usr/lib/modules, so the assertions hold on velle's box, in
# a build chroot and on the ISO alike. The real tree holds whatever that machine
# happens to have booted.

mkdir -p "$KDIR/9.9.9-fictional" "$KDIR/8.8.8-no-pkgbase" "$KDIR/extramodules-fake"
echo linux > "$KDIR/9.9.9-fictional/pkgbase"

krows() { SYNPKG_MODULES_DIR="$KDIR" "$SYNPKG" --tsv status 2>/dev/null | grep '^kernel'; }

krows | grep -q '^kernel	linux	' \
    && ok "a kernel is found through its pkgbase file" \
    || bad "a kernel is found through its pkgbase file"

# A directory with no pkgbase is not a packaged kernel: dkms leftovers and
# extramodules directories live here too, and counting them as kernels would
# report a reboot that is not owed.
[ "$(krows | wc -l)" = 2 ] \
    && ok "a modules directory with no pkgbase is not a kernel" \
    || bad "a modules directory with no pkgbase is not a kernel"

# The running release is absent from the synthetic tree, which is exactly the
# state a kernel upgrade leaves behind.
krows | grep -q 'reboot-pending' \
    && ok "a running kernel that is no longer installed is reported" \
    || bad "a running kernel that is no longer installed is reported"

mkdir -p "$KDIR/$(uname -r)"
echo linux > "$KDIR/$(uname -r)/pkgbase"
krows | grep -q '	running$' \
    && ok "the running kernel is marked when it is still installed" \
    || bad "the running kernel is marked when it is still installed"
krows | grep -q 'reboot-pending' \
    && bad "a reboot is still reported when the running kernel is installed" \
    || ok "no reboot is reported when the running kernel is installed"

n=$(SYNPKG_MODULES_DIR="$KDIR" "$SYNPKG" --tsv status | tsv_cols)
[ "$n" = 4 ] && ok "status --tsv still has 4 columns with kernel rows" \
             || bad "status --tsv still has 4 columns with kernel rows (got $n)"

# ── /etc/pacman.d/hooks must not be dropped ─────────────────────────────────
#
# alpm_initialize() seeds ONLY /usr/share/libalpm/hooks/. The /etc drop-in dir
# is added by pacman's CLI, not by the library, so a libalpm frontend that does
# not add it runs with fewer hooks than every package on the system assumes.
#
# The damage is not "some hooks are skipped". pacman resolves same-named hooks
# by letting the LAST directory win, so an /etc hook written to OVERRIDE an Arch
# one is replaced by the very hook it was meant to suppress. That shipped:
# limine's 90-mkinitcpio-install.hook never ran, Arch's ran in its place, and
# kernel upgrades stopped copying the new kernel into the bootloader's entry
# directory — leaving limine booting a kernel whose module tree the same
# upgrade had just deleted. Nothing reported an error at any layer.
#
# Asserted against the BINARY, not the source: the source is not what ships, and
# an undefined symbol is honest evidence the call survived compilation.
# Captured rather than piped into `grep -q`: this file runs under `pipefail`,
# and `grep -q` exits the moment it matches, so the producer takes SIGPIPE and
# the pipeline reports 141 — a PASSING condition read as a failure. It is a race
# on how much the producer had left to write, which is why the shorter pipelines
# above get away with it.
if command -v nm >/dev/null 2>&1; then
	syms=$(nm -D --undefined-only "$SYNPKG" 2>/dev/null)
	case "$syms" in
		*alpm_option_add_hookdir*)
			check "the binary calls alpm_option_add_hookdir (/etc/pacman.d/hooks)" 0 ;;
		*)
			check "the binary calls alpm_option_add_hookdir (/etc/pacman.d/hooks)" 1 ;;
	esac
else
	printf '  skip  nm unavailable — hookdir symbol not checked\n'
fi

# The fix reads HookDir through pacman-conf, which resolves the DEFAULT even
# though /etc/pacman.conf ships the line commented out. If that ever stops being
# true the code falls back to the same path by hand, but silently — so pin the
# assumption here rather than discover it from another unbootable machine.
if command -v pacman-conf >/dev/null 2>&1; then
	hookdir=$(pacman-conf HookDir 2>/dev/null)
	case "$hookdir" in
		*/etc/pacman.d/hooks*)
			check "pacman-conf resolves HookDir even when pacman.conf comments it" 0 ;;
		*)
			check "pacman-conf resolves HookDir even when pacman.conf comments it" 1 ;;
	esac
fi

# ── the kernel staged for boot ──────────────────────────────────────────────
#
# Synthetic images, never the machine's own /boot: this suite runs on velle's
# desktop, in a makepkg chroot and on the ISO builder, and a check that reads
# whichever kernels happen to be installed passes on one and fails on the next.
#
# A bzImage carries "HdrS" at 0x202 and, at 0x20e, a u16 offset (from 0x200) of
# its version string. 0x301 puts the string at 0x501 and, unlike a rounder
# value, contains no NUL byte — one less thing for printf and dd to disagree
# about.
mk_bzimage() {   # mk_bzimage PATH RELEASE
	mkdir -p "$(dirname "$1")"
	dd if=/dev/zero of="$1" bs=4096 count=1 status=none
	printf 'HdrS'     | dd of="$1" bs=1 seek=514  conv=notrunc status=none
	printf '\001\003' | dd of="$1" bs=1 seek=526  conv=notrunc status=none
	printf '%s (tester@synapse) #1 SMP' "$2" \
	                  | dd of="$1" bs=1 seek=1281 conv=notrunc status=none
}

BDIR=$(mktemp -d)
KDIR2=$(mktemp -d)
MID=0123456789abcdef0123456789abcdef
# ⚠ Still ONE trap, and it must name EVERY temporary directory — see the note
# at the earlier trap. This replaces that handler rather than adding to it, so
# dropping a name here leaks that directory on every run.
trap 'rm -rf "$STUB" "$LOGDIR" "$KDIR" "$BDIR" "$KDIR2"' EXIT

mkdir -p "$KDIR2/9.9.9-1-fake" && echo linux > "$KDIR2/9.9.9-1-fake/pkgbase"
mkdir -p "$KDIR2/9.9.9-1-other" && echo linux-other > "$KDIR2/9.9.9-1-other/pkgbase"

brows() {
	SYNPKG_MODULES_DIR="$KDIR2" SYNPKG_BOOT_DIR="$BDIR" SYNPKG_MACHINE_ID="$MID" \
		"$SYNPKG" --tsv status 2>/dev/null | grep '^boot	' || true
}

# The healthy case first, so a later "orphaned" is known to mean something.
mk_bzimage "$BDIR/vmlinuz-linux" 9.9.9-1-fake
[ "$(brows | grep -c '	ok$')" = 1 ] \
	&& ok "a boot image matching its installed kernel reads ok" \
	|| bad "a boot image matching its installed kernel reads ok"

# THE BUG. The bootloader's private copy is a release nothing installs any more,
# because the upgrade that replaced it deleted its module tree. Every filename
# and every hash still looks right; only the image itself gives it away.
mk_bzimage "$BDIR/$MID/linux/vmlinuz" 7.1.6-1-fake
brows | grep -q '	orphaned$' \
	&& ok "a staged kernel with no module tree is reported orphaned" \
	|| bad "a staged kernel with no module tree is reported orphaned"

# The path must be on the ORPHANED row specifically. Matching it anywhere in the
# output passes even when orphan detection is gone, because the same path shows
# up on an ok or stale row — checked by disabling the detection and watching
# this assertion stay green.
orphan_row=$(brows | grep '	orphaned$' || true)
case "$orphan_row" in
	*"$MID/linux/vmlinuz"*)
		ok "the orphaned row names the file, not just the package" ;;
	*)
		bad "the orphaned row names the file, not just the package" ;;
esac

# Installed, so not orphaned — but not a release this pkgbase owns. Behind, not
# fatal, and the two must not be reported as the same thing.
mk_bzimage "$BDIR/$MID/linux/vmlinuz" 9.9.9-1-other
brows | grep -q '	stale$' \
	&& ok "a staged kernel belonging to another package reads stale" \
	|| bad "a staged kernel belonging to another package reads stale"
brows | grep -q '	orphaned$' \
	&& bad "stale is not also reported as orphaned" \
	|| ok "stale is not also reported as orphaned"

# Not a bzImage. NULL means "no opinion" — a UKI or an arm64 Image must not
# collect a warning just because this parser cannot read it.
rm -f "$BDIR/$MID/linux/vmlinuz"
echo 'not a kernel' > "$BDIR/vmlinuz-linux-other"
[ "$(brows | grep -c 'vmlinuz-linux-other')" = 0 ] \
	&& ok "a file that is not a bzImage is passed over, not flagged" \
	|| bad "a file that is not a bzImage is passed over, not flagged"

n=$(SYNPKG_MODULES_DIR="$KDIR2" SYNPKG_BOOT_DIR="$BDIR" SYNPKG_MACHINE_ID="$MID" \
	"$SYNPKG" --tsv status | tsv_cols)
[ "$n" = 4 ] && ok "status --tsv still has 4 columns with boot rows" \
             || bad "status --tsv still has 4 columns with boot rows (got $n)"

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
