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

# ── mutations refuse rather than assume ─────────────────────────────────────
# In TSV mode confirm() returns false, so a transaction without --noconfirm
# must decline. This is what stops a GUI click from installing silently if the
# --noconfirm flag is ever dropped from the QML.
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

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
