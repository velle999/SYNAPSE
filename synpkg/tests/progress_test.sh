#!/bin/sh
# progress_test.sh — the ILoveCandy bar is actually drawn, and it is pacman's.
#
# ⚠ EVERYTHING HERE RUNS UNDER A PTY, and that is the point rather than a
# detail. progress.c draws nothing when stderr is not a terminal — deliberately,
# because the bar redraws itself with a carriage return and a log file full of
# those is worse than no bar — so a rig that ran the driver on a pipe would
# assert an empty string and pass forever. `script` is what supplies the pty.
#
# ⚠ AND pacman-conf IS SHIMMED ON PATH, rather than pointed at a hermetic file.
# progress.c asks pconf(), which runs plain `pacman-conf ILoveCandy` through
# execvp — no --config, and pacman-conf has no environment variable for it, so
# PATH is the only seam that does not mean putting a test flag into production
# code. A test that used the real /etc/pacman.conf would pass or fail on whether
# the machine running it happens to like candy, which is the setting under test.
#
# Usage: progress_test.sh /path/to/progress_test
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

DRIVER=${1:?usage: progress_test.sh /path/to/progress_test}

command -v script >/dev/null 2>&1 \
    || { echo "SKIP: util-linux 'script' not installed (it supplies the pty)."; exit 77; }

TMP=$(mktemp -d /tmp/synpkgprog.XXXXXX)
trap 'rm -rf "$TMP"' INT TERM EXIT
mkdir -p "$TMP/bin"

fails=0
ok()  { printf '  ok    %s\n' "$1"; }
bad() { printf '  FAIL  %s\n' "$1"; fails=$((fails + 1)); }

# The shim. It answers exactly as the real pacman-conf does for a bare
# directive: prints the directive's own name when set, nothing when not, exit 0
# either way — which is why progress.c tests the OUTPUT and not the status.
# $TMP/set is the list of directives this case has switched on.
cat > "$TMP/bin/pacman-conf" <<'SHIM'
#!/bin/sh
for want in "$@"; do
    grep -qx "$want" "$SYNPKG_TEST_SET" 2>/dev/null && echo "$want"
done
exit 0
SHIM
chmod +x "$TMP/bin/pacman-conf"

# Run the driver on a pty with the given directives set, capturing what it drew.
#
# ⚠ SCRIPT'S OWN CHATTER IS STRIPPED, not tolerated: it writes "Script started"
# and "Script done" into the same stream, and both contain letters this file
# greps for. -q suppresses them on util-linux; the sed is the belt for the
# versions where it does not.
#
# ⚠ AND THE DRIVER IS RE-EXEC'D PER CASE, which the caching in progress.c makes
# mandatory: prog_flag() asks pacman-conf once per process and remembers, so two
# cases in one process would both get the first one's answer.
draw() {  # draw <directives-file> <percent>...
    _s=$1; shift
    SYNPKG_TEST_SET="$_s" PATH="$TMP/bin:$PATH" COLUMNS=80 script -q -c \
        "SYNPKG_TEST_SET='$_s' PATH='$TMP/bin:$PATH' COLUMNS=80 '$DRIVER' $*" \
        /dev/null 2>&1 | sed -e '/^Script started/d' -e '/^Script done/d'
}

printf 'ILoveCandy\n'    > "$TMP/candy"
: > "$TMP/plain"
printf 'NoProgressBar\n' > "$TMP/nobar"

# The shim itself, before anything leans on it. A shim that answered wrong would
# make every case below agree with each other and with nothing real.
if [ "$(SYNPKG_TEST_SET="$TMP/candy" PATH="$TMP/bin:$PATH" pacman-conf ILoveCandy)" = ILoveCandy ] &&
   [ -z "$(SYNPKG_TEST_SET="$TMP/plain" PATH="$TMP/bin:$PATH" pacman-conf ILoveCandy)" ]; then
    ok "the pacman-conf shim answers set/unset the way the real one does"
else
    bad "the shim is wrong; nothing below means anything"
    exit 1
fi

# ── 1. a pty gets a bar at all ────────────────────────────────
OUT=$(draw "$TMP/plain" 0 50 100)
case "$OUT" in
    *"["*"]"*"100%"*) ok "a bar is drawn on a terminal, and it reaches 100%" ;;
    *) bad "no bar on a pty — got: $(printf '%s' "$OUT" | tr -d '\r' | tail -1)" ;;
esac

# ── 2. …and NOT on a pipe ─────────────────────────────────────
#
# The guard that keeps carriage returns out of syn-update's captured output and
# out of the GUI's pipe. Asserted because it is invisible when it works and a
# mess when it does not.
PIPED=$(SYNPKG_TEST_SET="$TMP/plain" PATH="$TMP/bin:$PATH" "$DRIVER" 0 50 100 2>&1)
if [ -z "$PIPED" ]; then
    ok "…and nothing at all when stderr is a pipe"
else
    bad "the bar drew into a pipe: $PIPED"
fi

# ── 3. ILoveCandy is the difference between the two bars ──────
#
# ⚠ THE DISCRIMINATING CHECK. A plain bar is hashes and dashes; the chomp is a
# 'c'/'C' mouth with 'o' pellets ahead of it. Asserting only that candy draws
# SOMETHING would pass on a plain bar, which is precisely the bug — the option
# was set, and the output was a bar with no candy in it.
CANDY=$(draw "$TMP/candy" 0 20 40 60)
PLAIN=$(draw "$TMP/plain" 0 20 40 60)

case "$CANDY" in
    *[cC]*o*) ok "ILoveCandy draws the mouth and its pellets" ;;
    *) bad "ILoveCandy drew no candy: $(printf '%s' "$CANDY" | tr '\r' '\n' | tail -1)" ;;
esac

case "$PLAIN" in
    *o*) bad "the plain bar has pellets in it — the flag is not being read" ;;
    *"#"*) ok "…and without it the bar is plain hashes, so the flag decides" ;;
    *) bad "the plain bar drew neither hashes nor pellets" ;;
esac

# The mouth CHEWS: over a run of advancing percentages both faces appear. A
# mouth stuck open is what a `lasthash` that never updates looks like, and it
# passes every check above.
LONG=$(draw "$TMP/candy" 0 5 10 15 20 25 30 35 40 45 50)
if printf '%s' "$LONG" | grep -q 'C' && printf '%s' "$LONG" | grep -q 'c'; then
    ok "…and it chews: both mouth positions appear across a moving bar"
else
    bad "the mouth never changed shape — it is drawn but not animated"
fi

# ── 4. NoProgressBar means nobody's bar ───────────────────────
NOBAR=$(draw "$TMP/nobar" 0 50 100)
if [ -z "$(printf '%s' "$NOBAR" | tr -d '\r\n')" ]; then
    ok "NoProgressBar silences it, on a terminal, exactly as pacman does"
else
    bad "NoProgressBar drew anyway: $NOBAR"
fi

echo
if [ "$fails" -eq 0 ]; then
    echo "progress_test: PASS"
    exit 0
fi
echo "progress_test: $fails check(s) failed"
exit 1
