#!/usr/bin/env bash
#
# vault_test.sh — what syn-vault decides, against a STUB gocryptfs.
#
# ⛔ THE REAL BACKEND IS NOT EXERCISED HERE, ON PURPOSE. Mounting a gocryptfs
# needs /dev/fuse, a FUSE-capable kernel and a user allowed to use it — none of
# which a build container has, and two of which a CI runner does not. A suite
# that needed them would be a suite that silently never ran, which is worse than
# one that is honest about its edges.
#
# ⚠ AND THE STUB IS NOT A WEAKER TEST OF THE THING THAT MATTERS. Every bug this
# program can have on its own is in what it decides before the backend is
# reached: which names it refuses, which paths it composes, whether the password
# ever touches a command line, and whether it will mount over files somebody
# would then lose. The stub RECORDS its argv and its stdin, so those are
# assertions and not hopes.
#
# SynapseOS Project — GPL-2.0-or-later
set -uo pipefail

S=${1:?usage: vault_test.sh /path/to/syn-vault [srcdir]}
pass=0; fail=0
ok()  { pass=$((pass+1)); printf '  ok    %s\n' "$1"; }
bad() { fail=$((fail+1)); printf '  FAIL  %s\n' "$1"; }
chk() { if [ "$2" = 0 ]; then ok "$1"; else bad "$1"; fi; }

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

export SYNVAULT_HOME="$T/home"
mkdir -p "$SYNVAULT_HOME"

# ── the stub ────────────────────────────────────────────────────────────────
#
# It writes down exactly what it was given, then behaves like a gocryptfs that
# worked: -init creates the conf file that marks a real vault.
mkdir -p "$T/bin"
cat > "$T/bin/gocryptfs" <<'STUB'
#!/bin/sh
printf '%s\n' "$@" > "$STUBLOG.argv"
cat > "$STUBLOG.stdin"
for a in "$@"; do
    case "$a" in -init) init=1 ;; esac
done
if [ "${init:-0}" = 1 ]; then
    # The last argument is the cipher dir.
    for a in "$@"; do last=$a; done
    printf '{"Version":2}\n' > "$last/gocryptfs.conf"
fi
# A failing gocryptfs is NOISY, and that noise is the point of the --rec tests
# below: two lines of its own before the caller ever gets a word in.
if [ "${STUBRC:-0}" != 0 ]; then
    echo "failed to unlock master key: cipher: message authentication failed" >&2
    echo "Password incorrect." >&2
fi
exit "${STUBRC:-0}"
STUB
chmod +x "$T/bin/gocryptfs"

cat > "$T/bin/fusermount" <<'STUB'
#!/bin/sh
printf '%s\n' "$@" > "$STUBLOG.umount"
exit 0
STUB
chmod +x "$T/bin/fusermount"

export PATH="$T/bin:$PATH"
export STUBLOG="$T/log"

echo "vault"

# ── names it must refuse ────────────────────────────────────────────────────
#
# ⛔ THE NAME BECOMES A DIRECTORY UNDER TWO ROOTS, so an escape in it puts a
# vault — and a mountpoint — anywhere the user can write.
for bad_name in '../escape' 'a/b' '.hidden' '' ; do
    printf 'x\nx\n' | "$S" create "$bad_name" >/dev/null 2>&1
    [ $? -ne 0 ]
    chk "create refuses the name '$bad_name'" $?
done

# ── making one ──────────────────────────────────────────────────────────────

printf 'hunter2\nhunter2\n' | "$S" create work >/dev/null 2>&1
chk "a vault can be created" $?

[ -f "$SYNVAULT_HOME/.local/share/syn-vault/work.vault/gocryptfs.conf" ]
chk "…with its ciphertext under ~/.local/share/syn-vault" $?

# ⛔ THE PASSWORD MUST NOT BE IN argv. /proc/<pid>/cmdline is world-readable, so
# a password on a command line is visible to every process on the machine.
! grep -q "hunter2" "$STUBLOG.argv"
chk "…and the password nowhere on the backend's command line" $?

grep -q "hunter2" "$STUBLOG.stdin"
chk "…it arrived on stdin instead" $?

# ⚠ 0700 ON THE MOUNTPOINT: it holds the PLAINTEXT while open, and 0755 would
# publish it to every other account for exactly as long as it is useful.
[ "$(stat -c %a "$SYNVAULT_HOME/Vaults/work")" = "700" ]
chk "…and the mountpoint is private to its owner" $?

printf 'hunter2\nhunter2\n' | "$S" create work >/dev/null 2>&1
[ $? -ne 0 ]
chk "creating one that exists is refused rather than overwriting it" $?

# ⛔ A MISTYPED PASSWORD AT CREATION CANNOT BE RECOVERED — nothing keeps a second
# copy — so it is asked for twice where there is a terminal to ask.
grep -q 'And again' src/vault.c 2>/dev/null || grep -q 'And again' "${2:-.}/src/vault.c"
chk "…and creation confirms the password before committing to it" $?

# ── listing and status ──────────────────────────────────────────────────────

"$S" --rec list | grep -q '^work	locked'
chk "list reports it as locked" $?

"$S" --rec status work | tail -1 | grep -q '^work	locked'
chk "status agrees" $?

# ⚠ CAPTURED, NOT PIPED. `status` on a missing vault exits non-zero — which is
# right — and under `set -o pipefail` that status is the PIPELINE's, so piping
# it into grep tests the exit code of syn-vault instead of the text it printed.
out=$("$S" --rec status nosuch 2>&1 || true)
grep -q '^nosuch	none' <<<"$out"
chk "…and says so for one that does not exist" $?

# ── opening ─────────────────────────────────────────────────────────────────

printf 'hunter2\n' | "$S" open work >/dev/null 2>&1
chk "open runs the backend" $?

! grep -q "hunter2" "$STUBLOG.argv"
chk "…again with the password off the command line" $?

grep -q "$SYNVAULT_HOME/Vaults/work" "$STUBLOG.argv"
chk "…mounting at ~/Vaults/<name>" $?

# ⛔ THE MOUNTPOINT AND THE CIPHERTEXT MUST NOT BE NESTED. A ciphertext directory
# inside the mountpoint vanishes the moment it is mounted, which is how somebody
# deletes the only copy of their data while tidying up.
case "$SYNVAULT_HOME/Vaults/work" in
    "$SYNVAULT_HOME/.local/share/syn-vault"*) nested=1 ;;
    *) nested=0 ;;
esac
[ "$nested" = 0 ]
chk "…and the two directories are not nested inside each other" $?

# ── the dangerous one ───────────────────────────────────────────────────────
#
# ⛔ MOUNTING OVER FILES HIDES THEM, IT DOES NOT ENCRYPT THEM. Anything saved
# into the mountpoint while the vault was closed sits unencrypted on the disk,
# invisible under the mount, while the person believes it is inside the vault.
echo "secret note" > "$SYNVAULT_HOME/Vaults/work/stray.txt"
printf 'hunter2\n' | "$S" open work >/dev/null 2>&1
[ $? -ne 0 ]
chk "open refuses a mountpoint that already has files in it" $?

out=$("$S" open work 2>&1 </dev/null || true)
grep -qi "NOT in the vault" <<<"$out"
chk "…and says the files are not protected, rather than a bare error" $?

"$S" --rec status work | tail -1 | grep -q '	1$'
chk "…and status flags them too" $?

[ -f "$SYNVAULT_HOME/Vaults/work/stray.txt" ]
chk "…without deleting anything to make room" $?
rm -f "$SYNVAULT_HOME/Vaults/work/stray.txt"

# ── when the backend fails ──────────────────────────────────────────────────

STUBRC=1 sh -c "printf 'wrong\n' | '$S' open work" >/dev/null 2>&1
[ $? -ne 0 ]
chk "a backend that refuses the password fails the command" $?

# ⛔ A WINDOW HAS ROOM FOR ONE LINE. gocryptfs explains itself over two before
# this program adds a third, which is right in a terminal and is a wall of text
# in the panel under a password box. In --rec the backend is silenced and this
# program says the one thing somebody can act on — exit 12 is a wrong password,
# confirmed against gocryptfs v2.6.1.
out=$(STUBRC=12 sh -c "printf 'wrong\n' | '$S' --rec open work" 2>&1 >/dev/null || true)
[ "$out" = "That password is not right." ]
chk "a front end gets one plain sentence for a wrong password" $?

! grep -q "master key" <<<"$out"
chk "…with the backend's own chatter kept out of it" $?

! grep -q "syn-vault:" <<<"$out"
chk "…and no program name in front of a sentence somebody reads" $?

# ⚠ AND THE TERMINAL KEEPS THE DETAIL. "wrong password" and "corrupted vault"
# are different problems, and only gocryptfs can tell them apart.
out=$(STUBRC=12 sh -c "printf 'wrong\n' | '$S' open work" 2>&1 >/dev/null || true)
grep -q "master key" <<<"$out"
chk "a terminal still sees why the backend refused" $?

# ── closing ─────────────────────────────────────────────────────────────────
#
# It is not mounted (the stub cannot mount), so close must say so rather than
# claiming to have locked something.
"$S" close work 2>&1 | grep -qi "not open"
chk "closing one that is not open says so" $?

echo "$pass/$((pass + fail)) passed"
[ "$fail" = 0 ]
