#!/usr/bin/env bash
# os_identity_test.sh — an updated machine reports the release it is running
#
# os-release was written once, at ISO build and again by the installer copying
# it, and nothing ever rewrote it. A box installed from 0.2.6 media and updated
# for a month still said 0.2.6 while its source tree said 0.2.9.5 — in
# fastfetch, in lsb_release, in the motd on the login screen, and in every bug
# report anybody sent. stamp_os_identity() closes that on a successful apply.
#
# ⚠ THE HARD PART IS NOT THE VERSION, IT IS WHICH FILE. On an installed system
# /etc/os-release is a SYMLINK to ../usr/lib/os-release, which belongs to Arch's
# `filesystem` package — the installer's `cp` followed it and wrote SynapseOS's
# identity through into filesystem's copy. filesystem has no backup array for
# it, so the next upgrade of that package restores Arch's file and the machine
# forgets its own name. So the checks below care as much about WHERE the bytes
# land as what they say: after a stamp, /etc/os-release must be a regular file
# and /usr/lib/os-release must be untouched.
#
# The functions are lifted out of the script and run in a stubbed world, the
# same way apply_select_test.sh does it, so nothing here needs root and nothing
# rewrites the identity of the machine running the suite.
set -u

here=$(cd "$(dirname "$0")" && pwd)
E=${1:-$here/../syn-update.sh}
[ -f "$E" ] || { echo "  ABORT no syn-update.sh at $E"; exit 1; }

pass=0; fail=0
ok()  { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail + 1)); }
check() { if [ "$2" = "$3" ]; then ok "$1"; else
    bad "$1 — wanted [$2], got [$3]"; fi; }

T=$(mktemp -d); trap 'rm -rf "$T"' EXIT

# ── the stubbed world ───────────────────────────────────────────────────────
harness() {
    cat <<'STUB'
say()  { echo "$*"; }
info() { echo "info  $*"; }
ok()   { echo "ok    $*"; }
warn() { echo "warn  $*"; }
die()  { echo "fail  $*"; exit 1; }
# ⛔ NOT optional. Both writers fall back to `sudo install` where the target is
# not writable, and an unstubbed run of that is a password prompt from a test
# suite — or, on a box with a cached credential, a real write to the identity of
# the machine running it. Everything below writes inside a temp dir it owns, so
# the unprivileged branch is the one that should be taken; this stub is here to
# make it LOUD if it ever is not.
sudo() { echo "UNEXPECTED-SUDO $*"; return 1; }
STUB
    sed -n '/^stamp_os_identity() {/,/^}/p' "$1"
    sed -n '/^stamp_banner_version() {/,/^}/p' "$1"
}

# stamp <src-dir> <etc-dir> — run the real function against a fake world
stamp() {
    { harness "$E"; echo "SRC=\"$1\""; echo "ETC=\"$2\""; echo 'stamp_os_identity'; } \
        > "$T/run.sh"
    bash "$T/run.sh" 2>&1
}

# A fake world: a source tree carrying iso_version, and an /etc laid out exactly
# as an installed machine's is — os-release a SYMLINK into a filesystem-owned
# /usr/lib copy.
world() {   # world <name> <iso_version>
    local w="$T/$1"
    rm -rf "$w"; mkdir -p "$w/src/archiso" "$w/etc" "$w/usr/lib"
    printf 'iso_version="%s"\n' "$2" > "$w/src/archiso/profiledef.sh"
    cat > "$w/usr/lib/os-release" <<'OSREL'
NAME="SynapseOS"
PRETTY_NAME="SynapseOS 0.2.6"
ID=synapseos
ID_LIKE=arch
VERSION="0.2.6"
VERSION_ID="0.2.6"
BUILD_ID=0.2.6
HOME_URL="https://github.com/velle999/SYNAPSE"
IMAGE_ID=SynapseOS
IMAGE_VERSION=0.2.6
OSREL
    ln -s ../usr/lib/os-release "$w/etc/os-release"
    echo "$w"
}

echo "=== the stamp lands, and lands in the right file ==="
w=$(world stamp 0.2.9.5)
before=$(md5sum < "$w/usr/lib/os-release")
out=$(stamp "$w/src" "$w/etc")
case "$out" in *UNEXPECTED-SUDO*) bad "it reached for sudo: $out" ;;
               *) ok "no sudo on a writable tree" ;; esac
case "$out" in *"reported version is now 0.2.9.5"*) ok "…and it says so" ;;
               *) bad "no confirmation line: $out" ;; esac

# ⚠ THE CENTRAL CHECK. A stamp written through the symlink would leave
# /etc/os-release a symlink and change the filesystem package's file — correct
# looking output, and wiped by the next `pacman -Syu` that moves filesystem.
[ -L "$w/etc/os-release" ] \
    && bad "/etc/os-release is still a symlink — the stamp went through it into
        filesystem's copy, which the next filesystem upgrade will overwrite" \
    || ok "/etc/os-release is a regular file now, not a symlink"
check "…and filesystem's copy was not touched" "$before" "$(md5sum < "$w/usr/lib/os-release")"

check "VERSION_ID"     'VERSION_ID="0.2.9.5"'            "$(grep '^VERSION_ID='  "$w/etc/os-release")"
check "VERSION"        'VERSION="0.2.9.5"'               "$(grep '^VERSION='     "$w/etc/os-release")"
check "PRETTY_NAME"    'PRETTY_NAME="SynapseOS 0.2.9.5"' "$(grep '^PRETTY_NAME=' "$w/etc/os-release")"
check "BUILD_ID"       'BUILD_ID=0.2.9.5'                "$(grep '^BUILD_ID='    "$w/etc/os-release")"
check "IMAGE_VERSION"  'IMAGE_VERSION=0.2.9.5'           "$(grep '^IMAGE_VERSION=' "$w/etc/os-release")"
# Everything not about the version has to survive: this rewrites a file, it does
# not generate one, so a field it has no opinion about must come out unchanged.
check "NAME is carried across"     'NAME="SynapseOS"' "$(grep '^NAME=' "$w/etc/os-release")"
check "HOME_URL is carried across" 'HOME_URL="https://github.com/velle999/SYNAPSE"' \
      "$(grep '^HOME_URL=' "$w/etc/os-release")"
check "no version left behind"     "0" "$(grep -c '0\.2\.6' "$w/etc/os-release")"

echo ""
echo "=== it is idempotent, and quiet when there is nothing to do ==="
first=$(md5sum < "$w/etc/os-release")
out=$(stamp "$w/src" "$w/etc")
check "a second stamp writes the same bytes" "$first" "$(md5sum < "$w/etc/os-release")"
case "$out" in *"reported version is now"*)
        bad "it announced a change it did not make: $out" ;;
    *) ok "…and says nothing, having changed nothing" ;; esac

echo ""
echo "=== it refuses to stamp somebody else's os-release ==="
# The state after a filesystem upgrade has already restored Arch's file. Blindly
# rewriting VERSION_ID there produces NAME="Arch Linux" with VERSION_ID="0.2.9.5"
# — a worse lie than the stale one, and one nothing downstream could detect.
w=$(world foreign 0.2.9.5)
rm -f "$w/etc/os-release"
cat > "$w/etc/os-release" <<'ARCH'
NAME="Arch Linux"
PRETTY_NAME="Arch Linux"
ID=arch
VERSION_ID="rolling"
ARCH
out=$(stamp "$w/src" "$w/etc")
case "$out" in *"does not identify as SynapseOS"*) ok "an Arch os-release is refused" ;;
               *) bad "it did not refuse: $out" ;; esac
check "…and left untouched" 'NAME="Arch Linux"' "$(grep '^NAME=' "$w/etc/os-release")"
check "…with no version stamped into it" "0" "$(grep -c '0\.2\.9\.5' "$w/etc/os-release")"

echo ""
echo "=== a version it cannot read is not a version it invents ==="
w=$(world nover 0.2.9.5)
rm -f "$w/src/archiso/profiledef.sh"
out=$(stamp "$w/src" "$w/etc")
case "$out" in *"could not read iso_version"*) ok "a missing profiledef.sh says so" ;;
               *) bad "no warning: $out" ;; esac
check "…and os-release is left as it was" 'VERSION_ID="0.2.6"' \
      "$(grep '^VERSION_ID=' "$w/etc/os-release")"
[ -L "$w/etc/os-release" ] && ok "…including still being the symlink it was" \
                           || bad "it replaced the file it declined to stamp"

echo ""
echo "=== the banners ==="
w=$(world banners 0.2.9.5)
# Stock Arch issue. It interpolates PRETTY_NAME out of os-release, so it is
# ALREADY correct — and installing branding is syn-install's job, not an
# updater's. Touching it would surprise every machine that predates the
# installer copying the branded one.
printf '\\S{PRETTY_NAME} \\r (\\l)\n' > "$w/etc/issue"
stock=$(md5sum < "$w/etc/issue")
# A branded issue and the installed motd both carry the version as prose.
printf '  SynapseOS — Where the kernel thinks.\n  Version 0.2.6 — \\l\n' > "$w/etc/motd"
stamp "$w/src" "$w/etc" >/dev/null
check "a stock Arch issue is left alone" "$stock" "$(md5sum < "$w/etc/issue")"
check "a versioned banner is updated" "  Version 0.2.9.5 — \\l" \
      "$(grep 'Version' "$w/etc/motd")"
check "…and its prose is otherwise intact" "1" \
      "$(grep -c 'Where the kernel thinks' "$w/etc/motd")"

w=$(world nomotd 0.2.9.5)
out=$(stamp "$w/src" "$w/etc")          # no issue, no motd in this world
case "$out" in *UNEXPECTED-SUDO*|*"could not"*) bad "absent banners were a problem: $out" ;;
               *) ok "absent banners are not an error" ;; esac
[ -e "$w/etc/motd" ] && bad "it created a motd that was not there" \
                     || ok "…and it does not create one"

# ── no bare `sudo` anywhere in the script ───────────────────────────────────
#
# ⛔ A SCRIPT-WIDE INVARIANT, CHECKED HERE BECAUSE THIS IS WHERE IT BROKE. The
# lsb-release refresh was `sudo /usr/lib/syn/syn-lsb-release` with no gate at
# all, and the stub at the top caught it only because this suite happens to run
# the function containing it.
#
# A `sudo` with no controlling terminal does not refuse — it opens a PAM
# conversation, that conversation fails for want of anywhere to prompt, and
# pam_faillock counts it as a wrong password against a user who never typed one.
# Three of those lock the ACCOUNT, and because greetd and synui-lock share the
# system-auth stack the symptom is a login screen rejecting a correct password
# with nothing on it saying why. syn-update runs from a GUI button with no
# terminal and from a systemd timer, so every escalation has to go through
# sudo_safe: a terminal gets a prompt, no terminal gets `sudo -n`, which never
# records a failure because it refuses before PAM opens a conversation.
#
# ⚠ Comments and double-quoted strings are stripped first. Several matches in
# this script are `Run this once in a terminal:  sudo chown …` — advice printed
# to a human, which is exactly what should be there.
bare=$(sed 's/#.*//; s/"[^"]*"//g' "$E" |
       grep -nE '(^|[;&|]|\bthen\b|\belse\b|\bdo\b|\bif\b)[[:space:]]*sudo[[:space:]]' |
       grep -vE 'sudo_safe|sudo -n|sudo -v')
if [ -z "$bare" ]; then
    ok "every escalation goes through sudo_safe"
else
    bad "bare sudo — a tty-less run counts a faillock failure: $(echo "$bare" | tr '\n' ' ')"
fi

echo ""
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
