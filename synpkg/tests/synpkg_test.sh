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
else
    bad "synpkg-enable-cachyos.sh not found beside the tests: $CACHY_SH"
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
else
    bad "synpkg.qml not found beside the tests: $QML"
fi

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
