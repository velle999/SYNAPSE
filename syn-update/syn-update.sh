#!/usr/bin/env bash
# syn-update — bring an INSTALLED SynapseOS up to date from git.
#
# THE PROBLEM THIS SOLVES
#
# syn-install gives every installed system a pacman repo like this:
#
#     [synapseos]
#     Server = file:///var/cache/synapseos
#
# That is a DIRECTORY OF FILES COPIED OFF THE ISO AT INSTALL TIME and nothing
# ever writes to it again. So `pacman -Syu` faithfully upgrades all of Arch and
# can never see a newer synui, synapd or synguard: an installed system is
# frozen at whatever ISO installed it, forever, with no error to notice.
#
# This closes that: fetch the SynapseOS source repo, work out which components
# have moved ahead of what is installed, rebuild those from source and install
# them. The local [synapseos] repo is updated too, so pacman's own view of the
# world stays honest.
#
# WHY IT DRIVES build-all.sh INSTEAD OF BUILDING ANYTHING ITSELF
#
# Every SynapseOS PKGBUILD does `source=("<pkg>-0.1.0.tar.gz")`, so something
# has to roll that tarball from the tree first. There are already TWO
# implementations of that collector (build-all.sh and archiso/build.sh) and they
# have drifted FOUR times — data/, HARDENING.md, sysusers/+tmpfiles/, and
# quickshell/ — each time shipping a package that built fine one way and died in
# package() the other. A third copy in here would be a fifth drift waiting to
# happen, so this file contains no tar command at all.
#
# `build-all.sh <pkg>...` also filters the component list against its OWN fixed
# build order rather than the order of its arguments, so handing it several
# packages gets the dependency ordering (synapse-llama before synapd, scenefx
# before synui) for free. That ordering is a second thing not worth reimplement-
# ing here.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
# https://github.com/velle999/SYNAPSE

set -uo pipefail

VERSION="0.1.0"

REPO_URL="${SYN_UPDATE_REPO:-https://github.com/velle999/SYNAPSE.git}"
REPO_REF="${SYN_UPDATE_REF:-main}"

# One shared tree rather than a clone per user: the packages it produces are
# installed system-wide, so a per-$HOME copy would mean several multi-GB trees
# that all build the same thing. It is chowned to whoever first runs setup
# because makepkg refuses to run as root and has to own its build directory.
SRC="${SYN_UPDATE_SRC:-/var/lib/synapse-src}"

LOCAL_REPO=/var/cache/synapseos

# The git tree, when SRC is temporarily pointed somewhere that is not one.
# Set only by cmd_check, which scans a mktemp -d of remote PKGBUILDs.
SRC_GIT=""

# Components build-all.sh knows how to build from a source tarball. Kept as a
# list here rather than scraped out of build-all.sh's KNOWN= line so that a
# rename over there fails loudly in review instead of silently narrowing what
# this updates.
# syn-update is in this list on purpose: an updater that cannot update itself
# can never ship its own fix, and would have to be repaired by reinstalling the
# OS — the exact dead end this tool exists to remove.
COMPONENTS=(scenefx0.5 synapd synsh synnet synguard synui synapse_kmod
            syn syn-model syn-install syn-update syn-firstboot
            nexus-chat tepris vibe chibi samsung-m2020 syn-arsenal synpkg synfiles
            syn-settings syn-disks syn-cal syn-vault syn-clean syn-play syn-edit syntty syn-confine
            limine-mkinitcpio-hook fetch synapse-wallpapers syn-arcade cliamp
            synstudio syn-gfn syn-remote)

# On the ISO but NOT updatable this way, with the reason. Reported rather than
# skipped in silence: a component quietly frozen forever is exactly the bug
# this tool exists to fix, and reintroducing it for a subset would be worse
# than not covering them, because nothing would say so.
declare -A UNSUPPORTED=(
    [synapse-llama]="packaged from a tree that archiso/build.sh --llama-only stages; there is no staging tree on an installed system"
    [linux-wallpaperengine]="cmake fetches a ~1.3 GB CEF blob and needs ~6 GB of scratch to build; its synui-wpengine control script now lives in synui, which IS updatable"
    [limine-snapper-sync]="vendored upstream at a pinned tag, so there is no local source to ship; its gradle build fetches plugins and Java dependencies from the network and produces a GraalVM native-image, which is a long build for a package that can only ever come out identical"
)

# In COMPONENTS — so it is UPDATED when present — but never ADDED to a system
# that does not have it. Kept apart from UNSUPPORTED, which is about what cannot
# be built here; this is about what must not arrive uninvited.
#
# syn-install ships /usr/bin/syn-install and /usr/bin/syn-crypt: a disk
# partitioner. It belongs to the ISO, and it is the one component a normal
# installed system is EXPECTED not to have. When scan() learned to install new
# components it found exactly this and nothing else, so the first `apply` after
# that change would have put a disk installer on PATH on every machine — a
# footgun handed out as an update.
declare -A NEVER_ADD=(
    [syn-install]="the ISO's disk installer; an installed system is not meant to have it"
    # It writes limine boot entries, and it is boot-critical on a limine
    # machine — so it belongs in COMPONENTS, where it stays current.
    #
    # But it must never ARRIVE on its own. It depends on `limine`, so adding it
    # would install a second bootloader on a GRUB box; worse, it ships
    # /etc/pacman.d/hooks/90-mkinitcpio-install.hook, which SHADOWS Arch's own
    # hook of that name, and a wrapper at /usr/local/bin/mkinitcpio that comes
    # first on PATH and prompts interactively. On a machine that does not boot
    # limine that is a broken initramfs pipeline delivered as an update.
    #
    # It reaches limine machines two ways instead: syn-install installs it
    # during a limine install, and syn-settings' Kernel pane offers it when it
    # finds limine with no entry generator.
    [limine-mkinitcpio-hook]="writes limine boot entries; on any other bootloader it would override the mkinitcpio hook and pull in limine"
    # A driver for ONE printer, the Samsung Xpress M2020W, which is an SPL
    # device the driverless-IPP path cannot see. It is in COMPONENTS because
    # the machines that do print with it must keep getting fixes.
    #
    # It could never be a checkbox at install time: Samsung's EULA licenses the
    # ULD binaries "strictly for the personal use" of whoever downloaded them,
    # which is why the ISO stopped carrying it and `syn printer samsung`
    # fetches it onto the machine that will print with it — after showing the
    # licence. So syn-install never writes its name into components.conf, and
    # an absent name is the ONE thing this file reads as "never offered, still
    # new" — which made every fresh install take a printer driver on its first
    # apply, with the licence step skipped on the way past.
    #
    # Reported by velle 2026-09-03 as the updater installing a printer driver
    # on systems where nobody asked for one.
    [samsung-m2020]="a driver for one Samsung SPL printer; \`syn printer samsung\` installs it, licence and all"
)

# How the ones above are MEANT to arrive. Kept beside the reason rather than
# folded into it, because these two sentences are read in different places: the
# reason says why an update did not bring it, this says what to type instead,
# and a name given on the command line deserves the second one.
declare -A NEVER_ADD_HOW=(
    [syn-install]="it comes with the ISO"
    [limine-mkinitcpio-hook]="syn-install adds it during a limine install, and syn-settings' Kernel pane offers it"
    [samsung-m2020]="syn printer samsung"
)

# ── what this machine was OFFERED, and what it DECLINED ──────────────────────
#
# Every SynapseOS package is a CHECKBOX at install time (syn-install 79,
# 2026-08-16). "Not installed" is therefore an ANSWER, not a gap — and scan()
# could not tell the two apart. A component that existed in the tree and not on
# the disk went straight into NEW and `apply` installed it, so somebody who
# ticked eleven of the twenty-five components got the other fourteen back on
# their first update. The only way to avoid it was to name every component by
# hand, one at a time (`syn-update apply synui synapd …`), which is not
# something anybody should have to know.
#
# The record is a file this tool and syn-install both write:
#
#     synui = selected
#     vibe  = declined
#
# syn-install writes it from the picker at install time; `syn-update apply`
# rewrites it from what is actually on the disk when a build finishes.
#
# The rule scan() applies to a component that is NOT installed:
#
#   named in the file (either state)
#       This machine has been offered it and does not have it. That is the
#       user's answer, in one direction or the other — a box left unticked, or
#       a `pacman -R` afterwards. DECLINED: reported, never built.
#
#   NOT named in the file
#       The tree has gained a component since this machine last looked. It was
#       never on offer, so nobody has declined it. NEW, and installed, exactly
#       as before.
#
# That second arm is what keeps the 2026-08-08 fix alive: synui pkgrel 317
# pointed two start-menu rows at synpkg, a component that had only just landed
# in COMPONENTS, and an updater that refuses to add anything ships the menu
# without the thing it opens.
#
# NO FILE AT ALL is every system installed before this existed, and it is read
# as "everything missing was declined", not as "nothing has ever been offered".
# The other reading re-installs, once per machine, precisely the software this
# change exists to stop installing. The first `apply` writes the file, so the
# assumption is made once and never again; anything the tree gains AFTER that
# is genuinely new and still arrives on its own. A component wrongly assumed
# declined is never stuck — it is listed in its own section of every report,
# with the command that installs it, and naming it is the opt-in.
MANIFEST="${SYN_UPDATE_MANIFEST:-/etc/synapseos/components.conf}"

# The three files that say which release this machine is running. Seamed as one
# directory so stamp_os_identity() can be tested without root and without
# rewriting the identity of the box running the test.
ETC="${SYN_UPDATE_ETC:-/etc}"

# name -> selected|declined, and whether the file existed at all. The flag is
# separate because an EMPTY manifest and an ABSENT one mean opposite things:
# empty is "offered everything, took none of it", absent is "never asked".
declare -A COMP_KNOWN=()
MANIFEST_PRESENT=0

load_manifest() {
    COMP_KNOWN=(); MANIFEST_PRESENT=0
    [ -r "$MANIFEST" ] || return 0
    MANIFEST_PRESENT=1

    local line name state
    while IFS= read -r line || [ -n "$line" ]; do
        line=${line%%#*}
        case $line in *=*) ;; *) continue ;; esac
        name=${line%%=*}; state=${line#*=}
        # Whitespace is stripped rather than assumed away: this file is meant
        # to be readable and editable by hand, and `vibe = declined` with the
        # spaces the example shows must parse.
        name=${name//[[:space:]]/}; state=${state//[[:space:]]/}
        [ -n "$name" ] || continue
        COMP_KNOWN[$name]="${state:-declined}"
    done < "$MANIFEST"
    return 0
}

# ⛔ ESCALATE WITH THIS, NEVER WITH A BARE `sudo`.
#
# A `sudo` with no controlling terminal does NOT "simply refuse". It opens a PAM
# conversation, that conversation fails for want of anywhere to prompt, and
# pam_faillock COUNTS THAT AS A WRONG PASSWORD against a user who never typed
# one. deny is effectively 3, so three of them lock the ACCOUNT — and because
# greetd and synui-lock reach the same system-auth stack, the symptom is a login
# screen rejecting a correct password with nothing on it saying why. It cost
# velle two lockouts from another tool doing exactly this.
#
# `sudo -n` never records a failure: it refuses before PAM opens a conversation.
# So where there is a terminal to prompt on, prompt; where there is not, use -n
# and let the caller report what it could not do. This is strictly safer than a
# bare sudo and identical wherever one would have worked.
sudo_safe() {
    if [ -t 0 ]; then sudo "$@"; else sudo -n "$@"; fi
}

# Put a staged manifest in place, escalating only when that is actually needed.
#
# ⚠ The test is the FILE, not the uid. The same mistake in synpkg's ignore.c
# escalated a run that had been pointed at a fixture, and pkexec — which strips
# the environment — wrote the system file instead. Here it also makes both
# writers untestable and makes SYN_UPDATE_MANIFEST a setting that only half
# works.
install_manifest() {   # install_manifest <staged-file>
    local dir; dir=$(dirname "$MANIFEST")
    if [ -w "$MANIFEST" ] || { [ ! -e "$MANIFEST" ] && [ -w "$dir" ]; }; then
        install -Dm644 "$1" "$MANIFEST"
    else
        # -D so /etc/synapseos is created on a system that has never had it.
        sudo_safe install -Dm644 "$1" "$MANIFEST"
    fi
}

# Rewrite the manifest from what is on the disk NOW.
#
# Called from a successful `apply` and nowhere else. `check` is read-only and
# stays that way, and a run that failed to build says nothing about what
# anybody wanted.
#
# Computed from `pacman -Q` rather than from what this run decided to build, so
# it also records the answer given with `pacman -R`: removing a component is a
# decline, and the next update must not put it back — the same complaint from
# the other direction.
#
# Only components that EXIST in the tree at this revision are written. Naming
# one that does not exist yet would pre-decline a component nobody has been
# shown, which is the 2026-08-08 bug written into a file.
save_manifest() {
    local tmp c inst
    tmp=$(mktemp) || { warn "cannot write $MANIFEST (mktemp failed)"; return 0; }

    {
        say "# SynapseOS components this machine has been offered."
        say "#"
        say "# selected = installed here.  declined = offered and not taken."
        say "# held     = installed here, and NOT to be updated."
        say "#"
        say "# A held component is reported by every check, with its pending"
        say "# version, so a hold is visible rather than silent. Release one"
        say "# with: syn-update unignore <component>"
        say "#"
        say "# syn-update will not ADD a component named here: an unticked box at"
        say "# install time, or a later \`pacman -R\`, is an answer and it is kept."
        say "# A component that is NOT named here has never been on offer on this"
        say "# machine, so a new one still installs itself on the next update."
        say "#"
        say "# To take something you declined:   syn-update apply <component>"
        say "# Written by syn-install, and rewritten by every \`syn-update apply\`."
        for c in "${COMPONENTS[@]}"; do
            [ -f "$SRC/$c/PKGBUILD" ] || continue
            inst=$(pacman -Q "$c" 2>/dev/null | awk '{print $2}')
            if [ -n "$inst" ]; then
                # held is a THIRD state and it has to survive this rewrite.
                # This function recomputes from `pacman -Q`, which can only see
                # installed-or-not — so a held component, being installed,
                # would be written back as plain `selected` and quietly resume
                # updating on the next run. The hold is the user's answer, the
                # same as a decline, and it is kept the same way.
                if [ "${COMP_KNOWN[$c]:-}" = held ]; then
                    printf '%s = held\n' "$c"
                else
                    printf '%s = selected\n' "$c"
                fi
            else
                printf '%s = declined\n' "$c"
            fi
        done
    } > "$tmp"

    # sudo, where it is needed, is already held open by sudo_keepalive_start()
    # at this point in an apply.
    if install_manifest "$tmp"; then
        [ "$MANIFEST_PRESENT" = 1 ] || ok "recorded your component selection in $MANIFEST"
    else
        warn "could not write $MANIFEST — the next update will assume the same
    thing again, which is harmless but means this cannot record a new answer."
    fi
    rm -f "$tmp"
    return 0
}

# Re-stamp the release version this machine reports about itself.
#
# THE BUG. os-release is written once — build.sh stamps airootfs from
# iso_version, syn-install copies it onto the disk — and NOTHING ever rewrote it
# again. A box installed from 0.2.6 media and updated ever since reported 0.2.6
# forever: fastfetch, the motd on the login screen, `lsb_release`, every bug
# report. The tree it was updated from said 0.2.9.5. This is the same fault the
# ISO had from v0.1.1 to v0.1.6 (see build.sh's OS identity block), one layer
# later — the write was fine, the machine was just lying about itself.
#
# ⚠ AND os-release IS NOT WHERE IT LOOKS. On an installed system /etc/os-release
# is a SYMLINK to ../usr/lib/os-release, and that file belongs to Arch's
# `filesystem` package. It got there by accident: systemd's tmpfiles etc.conf
# creates the symlink during pacstrap, and the installer's `cp /etc/os-release
# /mnt/etc/os-release` then FOLLOWS it and writes SynapseOS's identity through
# into filesystem's file. `pacman -Qkk filesystem` has been reporting that as a
# checksum mismatch on every installed machine ever since.
#
# filesystem does not list /usr/lib/os-release in a backup array, so the first
# `pacman -Syu` that upgrades filesystem overwrites it with Arch's — and the
# machine forgets it is SynapseOS entirely: NAME, ID, PRETTY_NAME, the lot. It
# has not fired yet only because filesystem does not upgrade often.
#
# So this writes a REAL FILE at /etc/os-release rather than through the symlink.
# os-release(5) gives /etc precedence over /usr/lib, `filesystem` does not ship
# /etc/os-release at all (only /usr/lib/os-release), and tmpfiles' rule for it is
# `L`, not `L+` — it creates the symlink only where nothing exists, so it will
# not undo this. After that a filesystem upgrade can restore /usr/lib/os-release
# to Arch's with no effect on what anything reads.
#
# Called from a successful apply only. `check` is read-only, and a run that
# failed to build has not moved the machine to a new release.
stamp_os_identity() {
    local ver osr tmp

    # The SAME source of truth build.sh uses, read the same way: iso_version in
    # profiledef.sh is the DISTRO version. Not SYNAPSEOS_VERSION and not any
    # pkgver — the packages stay 0.1.0-N on purpose.
    ver=$(sed -n 's/^iso_version="\([^"]*\)".*/\1/p' \
          "$SRC/archiso/profiledef.sh" 2>/dev/null)
    if [ -z "$ver" ]; then
        warn "could not read iso_version from $SRC/archiso/profiledef.sh —
    leaving the reported version alone rather than guessing at it."
        return 0
    fi

    osr="$ETC/os-release"
    [ -e "$osr" ] || return 0

    # Read THROUGH the symlink (cat, not readlink) — the current content is the
    # starting point, so fields this does not touch are carried across intact.
    tmp=$(mktemp) || return 0
    cat "$osr" > "$tmp" 2>/dev/null || { rm -f "$tmp"; return 0; }

    # Refuse to stamp a file that is not ours. On a machine where a filesystem
    # upgrade already restored Arch's, rewriting VERSION_ID would produce an
    # os-release reading NAME="Arch Linux" VERSION_ID="0.2.9.5" — a worse lie
    # than the stale one, and one nothing else could detect.
    if ! grep -q '^ID=synapseos$' "$tmp"; then
        rm -f "$tmp"
        warn "$osr does not identify as SynapseOS — not stamping a version into
    somebody else's os-release. Reinstall or restore it first."
        return 0
    fi

    # The loose version pattern is build.sh's, which makes this idempotent: it
    # rewrites whatever version is already there, whatever that version is.
    local _v='[0-9][0-9A-Za-z.+~-]*'
    sed -i \
        -e "s|^PRETTY_NAME=\"SynapseOS ${_v}\"|PRETTY_NAME=\"SynapseOS ${ver}\"|" \
        -e "s|^VERSION=\"${_v}\"|VERSION=\"${ver}\"|" \
        -e "s|^VERSION_ID=\"${_v}\"|VERSION_ID=\"${ver}\"|" \
        -e "s|^BUILD_ID=${_v}|BUILD_ID=${ver}|" \
        -e "s|^IMAGE_VERSION=${_v}|IMAGE_VERSION=${ver}|" \
        "$tmp"

    # Prove it landed rather than trusting sed's exit status, which is 0 whether
    # or not the pattern matched — a silent no-match is exactly how the version
    # persisted in the first place.
    if ! grep -q "^VERSION_ID=\"${ver}\"$" "$tmp"; then
        rm -f "$tmp"
        warn "could not stamp $ver into $osr — its VERSION_ID line does not look
    like one this knows how to rewrite. Left as it was."
        return 0
    fi

    if [ "$(cat "$osr" 2>/dev/null)" = "$(cat "$tmp")" ] && [ ! -L "$osr" ]; then
        rm -f "$tmp"          # already correct AND already a real file
    else
        # install, not `cp` and not `sed -i` on the path: this has to REPLACE
        # the symlink with a regular file, not write through it into the
        # filesystem package's copy, which is the whole point.
        #
        # ⚠ The privilege test is on the DIRECTORY, not the file. Replacing a
        # symlink is an unlink-and-create in its parent, so `-w` on the path
        # answers the wrong question twice over: it follows the symlink and
        # reports on filesystem's file, which is the one thing not being written.
        if [ -w "$(dirname "$osr")" ]; then
            install -Dm644 "$tmp" "$osr"
        else
            sudo_safe install -Dm644 "$tmp" "$osr"
        fi && ok "reported version is now $ver" \
           || warn "could not write $osr — this machine still reports its old version"
        rm -f "$tmp"
    fi

    stamp_banner_version "$ETC/issue" "$ver"
    stamp_banner_version "$ETC/motd"  "$ver"

    # os-release is where /etc/lsb-release is derived FROM, so a new version
    # here leaves lsb_release quoting the old one until the next transaction
    # that happens to move syn or lsb-release. See 75-syn-lsb-release.hook.
    #
    # ⛔ ONLY WHEN $ETC IS THE REAL /etc, and this was the one line in the
    # function that did not care. The helper writes /etc/lsb-release by absolute
    # path — so a run pointed at a staged tree, which is every run of the test
    # suite, reached past its fixture and rewrote the identity of the machine
    # doing the testing. SYN_UPDATE_ETC exists precisely so that cannot happen.
    #
    # ⛔ AND THROUGH sudo_safe, NEVER A BARE sudo. This was `sudo …` with no
    # gate at all: on any path without a terminal — the GUI's, the ping timer's
    # — that is a failed PAM conversation, which pam_faillock counts as a wrong
    # password. See sudo_safe above for what three of those do to the account.
    #
    # ⚠ AND IT SAYS WHEN IT COULD NOT, instead of `|| true`. lsb_release quoting
    # last month's version is a small wrong answer that is very hard to trace
    # back to an updater that reported success.
    if [ "$ETC" = /etc ] && [ -x /usr/lib/syn/syn-lsb-release ]; then
        sudo_safe /usr/lib/syn/syn-lsb-release >/dev/null 2>&1 ||
            warn "could not refresh /etc/lsb-release — lsb_release will quote
    the old version until the next upgrade that moves syn or lsb-release"
    fi
}

# The getty banner and the login motd carry the version as prose, and both go
# stale with os-release.
#
# ⚠ ONLY WHERE THE FILE ALREADY CARRIES A SynapseOS VERSION. A machine whose
# /etc/issue is still stock Arch (`\S{PRETTY_NAME} \r (\l)`, which interpolates
# from os-release and is therefore already correct) must be left alone —
# installing branding is syn-install's job, and an updater that started writing
# banners nobody asked for would be a surprise on every machine that predates
# the installer copying them.
stamp_banner_version() {   # stamp_banner_version <file> <version>
    local f=$1 ver=$2 tmp
    local _v='[0-9][0-9A-Za-z.+~-]*'

    [ -f "$f" ] || return 0
    grep -q "SynapseOS ${_v}\|Version ${_v}" "$f" 2>/dev/null || return 0

    tmp=$(mktemp) || return 0
    sed -e "s|SynapseOS ${_v} |SynapseOS ${ver} |" \
        -e "s|Version ${_v} |Version ${ver} |" "$f" > "$tmp" 2>/dev/null \
        || { rm -f "$tmp"; return 0; }

    if [ "$(cat "$f")" = "$(cat "$tmp")" ]; then
        rm -f "$tmp"; return 0
    fi
    if [ -w "$f" ]; then install -Dm644 "$tmp" "$f"
    else sudo_safe install -Dm644 "$tmp" "$f"; fi \
        || warn "could not update the version in $f"
    rm -f "$tmp"
}

# ── output ───────────────────────────────────────────────────

if [ -t 1 ]; then
    C_B=$'\e[1m'; C_DIM=$'\e[2m'; C_OK=$'\e[38;5;82m'
    C_WARN=$'\e[38;5;214m'; C_ERR=$'\e[38;5;203m'; C_R=$'\e[0m'
else
    C_B=""; C_DIM=""; C_OK=""; C_WARN=""; C_ERR=""; C_R=""
fi

say()  { printf '%s\n' "$*"; }
info() { printf '%s==>%s %s\n' "$C_B" "$C_R" "$*"; }
ok()   { printf '%s  ok%s  %s\n' "$C_OK" "$C_R" "$*"; }
warn() { printf '%swarn%s  %s\n' "$C_WARN" "$C_R" "$*" >&2; }
die()  { printf '%sfail%s  %s\n' "$C_ERR" "$C_R" "$*" >&2; exit 1; }

# ── the failure that looks like a broken keyring and is not ──────────────────
#
# A build dies at its first `makepkg -s`, because resolving dependencies means
# pacman reads the sync databases, and pacman refuses to load ANY of them if one
# fails its signature check:
#
#   error: blackarch: signature from "Levon 'noptrix' Kayan ..." is invalid
#   error: database 'blackarch' is not valid (invalid or corrupted database
#          (PGP signature))
#   error: failed to prepare transaction (invalid or corrupted database)
#
# ⚠ NOTHING IS WRONG WITH THE KEYS, AND `pacman-key` IS THE WRONG TOOL. Seen
# three times now (blackarch 2026-08-11, blackarch and cachyos together
# 2026-08-14): both keys held [full] trust and had not expired, and the .db
# matched the mirror's byte for byte. What was stale was the SIGNATURE beside
# it — a `.db.sig` written two days before the `.db` it signs, which is a
# perfectly good signature over different content, and gpg calls that BADSIG.
# It happens because pacman fetches the two files separately and the mirror can
# update between them, or keep serving a cached sig.
#
# So this checks the one thing that is actually true of that state — a
# signature file OLDER than the database it signs — rather than matching
# pacman's wording, which changes between versions. It costs two stats per repo
# and needs no privileges.
#
# The directory is a variable purely so the tests can point it at a fixture —
# these two functions are otherwise unreachable without a real machine to break.
SYNC_DIR="${SYN_UPDATE_SYNC_DIR:-/var/lib/pacman/sync}"

stale_db_sig_hint() {
    local db sig repos=()
    for db in "$SYNC_DIR"/*.db; do
        [ -f "$db" ] || continue
        sig="$db.sig"
        [ -f "$sig" ] || continue
        [ "$sig" -ot "$db" ] && repos+=("$(basename "${db%.db}")")
    done
    [ ${#repos[@]} -gt 0 ] || return 0

    say ""
    warn "the signature is OLDER than the database it signs: ${repos[*]}"
    say "  That is a good signature over different content, which pacman reports"
    say "  as \"invalid or corrupted database (PGP signature)\". The keys are"
    say "  fine and pacman-key is the wrong tool. Force a fresh download of both"
    say "  halves:"
    say ""
    say "      ${C_OK}sudo pacman -Syy${C_R}"
    say ""
    say "  Two y's: plain -Sy can keep the cached signature, which is the stale"
    say "  half. Then run this again."
}

# ── refresh the databases BEFORE the build, not after it fails ───────────────
#
# The hint above is a post-mortem, and a post-mortem arrives after a build that
# ran for minutes. Both database failures it covers are preventable by doing
# what the hint asks for FIRST:
#
#   * the stale .db.sig — the mirror updated the database between the two
#     fetches, and pacman then refuses to load ANY repository; and
#   * a 404 on a dependency, because a database older than the mirrors asks for
#     a version that has already been rotated out (see the pacman notes: the
#     error names the dependency, so it reads as a broken package).
#
# `makepkg -s` resolves each component's makedepends through pacman, so both
# land on the FIRST component build-all.sh touches.
#
# TWO y's. Plain -Sy is happy to keep a cached .db.sig, and that cached
# signature is exactly the stale half in the first case.
#
# ── AND IT COSTS NO EXTRA PASSWORD ──────────────────────────────────────────
#
# This runs where sudo is needed anyway: build-all.sh installs every component
# it produces with `sudo pacman -U`, so an apply always authenticates. Asking
# here just moves that prompt to the front, where somebody is still watching,
# instead of stopping for a password fifteen minutes into an unattended build.
DB_FRESH_SECS="${SYN_UPDATE_DB_FRESH_SECS:-600}"

# Whether a refresh is worth a download. `synpkg upgrade` refreshes as root and
# THEN calls `syn-update apply`, and two applies in a row are normal; re-pulling
# every database each time is ~12 MB of nothing. Freshness is the two facts that
# actually matter — how old the database is, and whether its signature predates
# it — not a flag the caller has to pass (an older syn-update dies on an
# argument it does not know, so a flag from synpkg would break on version skew).
# ── WHICH refresh, and it is not always the expensive one ────────────────────
#
# ⚠ -Sy AND -Syy ARE NOT THE SAME COST, and this used to run the expensive one
# unconditionally.
#
#   -Sy   asks each mirror whether its database has changed and downloads only
#         the ones that have. On a machine synced an hour ago that is a handful
#         of HTTP requests and no payload — under a second.
#   -Syy  downloads EVERY database whether it changed or not. ~12-25 MB and
#         tens of seconds on a slow mirror, every single apply.
#
# The doubled y exists for ONE failure and it is worth being precise about it: a
# mirror that updated its .db between pacman's two fetches leaves a CACHED .db.sig
# that no longer matches, and pacman then refuses to load ANY repository. Plain
# -Sy is happy to keep that cached signature, so -Sy cannot fix it — but that is
# the only thing -Sy cannot fix, and it is rare.
#
# So the expensive one is paid for when it is the answer, and not otherwise:
#
#   · signature older than its database on disk  → -Syy, that is the case above
#   · database older than DB_FRESH_SECS          → -Sy, which is what closes the
#                                                  404-on-a-rotated-out-version
#                                                  gap, and is nearly free
#   · -Sy came back complaining about a signature or a corrupt database
#                                                → escalate to -Syy and retry
#
# The escalation is the part that makes the cheap path safe to prefer: the
# expensive refresh still happens on the run that needs it, discovered from what
# pacman actually said rather than paid for in advance by every other run.
#
# Which db needs which. Prints "force" if any signature predates its database,
# "sync" if any database is simply older than the window, and nothing when
# everything is current.
pacman_dbs_state() {
    local db sig now mtime found=0 want=""
    now=$(date +%s)
    for db in "$SYNC_DIR"/*.db; do
        [ -f "$db" ] || continue
        found=1
        sig="$db.sig"
        # ⚠ A signature OLDER than the database it signs is the stale-sig case,
        # and it is the only one a plain -Sy will not clear.
        if [ -f "$sig" ] && [ "$sig" -ot "$db" ]; then
            printf 'force\n'
            return 0
        fi
        mtime=$(stat -c %Y "$db" 2>/dev/null) || { printf 'force\n'; return 0; }
        [ $((now - mtime)) -gt "$DB_FRESH_SECS" ] && want="sync"
    done
    # No databases at all is not "fresh" — it is a system that has never synced,
    # and there is nothing on disk for -Sy to compare against.
    [ "$found" = 1 ] || { printf 'force\n'; return 0; }
    [ -n "$want" ] && printf '%s\n' "$want"
    return 0
}

# Did pacman fail in the way that only a forced refresh fixes? Matched on what
# it prints, because the exit status is the same 1 for every database problem.
db_sig_failure() {
    printf '%s' "$1" | grep -qiE "signature|corrupt|invalid or corrupted|GPGME|keyring"
}

sync_pacman_dbs() {
    local state
    state=$(pacman_dbs_state)

    if [ -z "$state" ]; then
        ok "pacman's databases are current (synced within ${DB_FRESH_SECS}s)"
        return 0
    fi

    # Same guard as setup_src: the GUI has no terminal to prompt on. Name the
    # command instead of failing at an invisible prompt — the build below may
    # still succeed against what is on disk, and refusing to try would be worse.
    if ! can_sudo; then
        # Name the refresh this machine actually needs. Telling someone to run
        # the 25 MB one when a plain -Sy would do is how the expensive command
        # becomes the one everybody reaches for by habit.
        local fix="sudo pacman -Sy"
        [ "$state" = force ] && fix="sudo pacman -Syy"
        warn "cannot refresh pacman's databases from here (no cached credential, no terminal).
    Run:  $fix"
        return 0
    fi

    if [ "$state" = force ]; then
        info "refreshing pacman's databases (pacman -Syy — a signature is older than its database)"
        sudo_safe pacman -Syy --noconfirm ||
            warn "database refresh failed — building against the databases already on disk"
        return 0
    fi

    info "refreshing pacman's databases (pacman -Sy)"
    local out rc
    out=$(sudo_safe pacman -Sy --noconfirm 2>&1); rc=$?
    printf '%s\n' "$out"
    [ "$rc" = 0 ] && return 0

    # ⚠ THE ESCALATION, and it is why the cheap path is safe to take first.
    if db_sig_failure "$out"; then
        info "that looks like a stale signature — forcing a full refresh (pacman -Syy)"
        sudo_safe pacman -Syy --noconfirm ||
            warn "database refresh failed — building against the databases already on disk"
        return 0
    fi

    warn "database refresh failed — building against the databases already on disk"
}

# ── preconditions ────────────────────────────────────────────

need_not_root() {
    # makepkg refuses to run as root, and build-all.sh calls it directly. Say so
    # here rather than letting the build get eight packages in and abort.
    [ "$(id -u)" -ne 0 ] ||
        die "run syn-update as your normal user, not root — makepkg refuses to run as root (it calls sudo itself where it needs to)"
}

need_tools() {
    local t missing=()
    for t in git makepkg pacman vercmp sudo; do
        command -v "$t" >/dev/null 2>&1 || missing+=("$t")
    done
    [ ${#missing[@]} -eq 0 ] ||
        die "missing required tool(s): ${missing[*]} (base-devel and git are needed to build from source)"
}

# ── the source tree ──────────────────────────────────────────

# sudo needs either a cached credential or a terminal to prompt on, and the
# updater GUI has neither — Quickshell runs `check` with no controlling
# terminal. Without this guard sudo's own diagnosis is what reaches the user:
#
#   sudo: a terminal is required to read the password; either use the -S option
#   sudo: a password is required
#   fail  cannot create /var/lib/synapse-src
#
# — in a window with no way to answer it, on a fresh install, from a button
# whose whole contract is that it only READS. Fresh installs now get the
# directory from syn-install, so this fires only on an older install or after
# someone removed it: say what to run instead of failing at an invisible prompt.
can_sudo() { sudo -n true 2>/dev/null || [ -t 0 ]; }

# ── ONE password for the whole run ───────────────────────────────────────────
#
# ⚠ sudo's credential expires after `timestamp_timeout`, which is FIVE MINUTES
# by default and is not set to anything else on a stock SynapseOS box. An apply
# is one authentication followed by a build that runs for far longer than that:
# synui alone is minutes, and build-all.sh reaches for sudo again after EVERY
# component it finishes (`pacman -U`). So the timestamp expires mid-run and sudo
# stops to ask again — three or four times in a full rebuild, at unpredictable
# moments, long after the person who typed the first password walked away.
# The build then sits at an invisible prompt inside build-all.sh's output.
#
# So: authenticate ONCE, up front, and hold the credential open for as long as
# this script lives. `sudo -n true` refreshes an existing timestamp without
# prompting; it cannot CREATE one, which is what makes the loop safe — if the
# credential is ever gone, the loop quietly does nothing rather than popping a
# prompt in the background.
#
# ⚠ It must not outlive the run. Two things stop that: the loop tests that this
# script's pid is still alive on every iteration, and the EXIT trap kills it.
# A refresher left running would hold a passwordless root credential open for
# whoever is at the keyboard.
#
# ⚠ Refreshed every 60s against a 300s timeout, not every 250s. sleep is not a
# scheduler: a machine that is compiling on every core can be late, and being
# late once is the whole failure this exists to prevent.
SUDO_KEEPALIVE_PID=""

sudo_keepalive_stop() {
    [ -n "$SUDO_KEEPALIVE_PID" ] || return 0
    kill "$SUDO_KEEPALIVE_PID" 2>/dev/null
    wait "$SUDO_KEEPALIVE_PID" 2>/dev/null
    SUDO_KEEPALIVE_PID=""
}

# Ask now, once, and say why — a bare password prompt from a script that has so
# far only printed git output is the kind of thing people type into without
# knowing what asked.
sudo_keepalive_start() {
    [ -n "$SUDO_KEEPALIVE_PID" ] && return 0

    # Already passwordless (a cached credential, or NOPASSWD): nothing to hold
    # open, and nothing to explain.
    if ! sudo -n true 2>/dev/null; then
        [ -t 0 ] || return 1     # no terminal to prompt on — caller decides
        say ""
        info "asking for your password once, now — it is held for the whole build"
        say "${C_DIM}  (sudo forgets after 5 minutes; a full rebuild is longer than that,${C_R}"
        say "${C_DIM}   and every component finishes with a \`pacman -U\`)${C_R}"
        sudo -v || return 1
    fi

    local parent=$$
    ( while kill -0 "$parent" 2>/dev/null; do
          sudo -n true 2>/dev/null || exit 0
          sleep 60
      done ) &
    SUDO_KEEPALIVE_PID=$!
    trap sudo_keepalive_stop EXIT INT TERM
    return 0
}

setup_src() {
    if [ -d "$SRC/.git" ]; then
        # Someone else's tree, or one left root-owned by an older run: makepkg
        # writes into it, so it has to be ours.
        if [ ! -w "$SRC" ]; then
            can_sudo || die "$SRC is not writable by $(id -un), and this session cannot ask for a password.
  Run this once in a terminal:  sudo chown -R $(id -un):$(id -gn) $SRC"
            info "taking ownership of $SRC"
            sudo_safe chown -R "$(id -un):$(id -gn)" "$SRC" || die "cannot take ownership of $SRC"
        fi
        return 0
    fi

    # An EMPTY directory is not "something else is here".
    #
    # This used to refuse anything at $SRC that was not a git checkout, which was
    # right when the only way the path existed was somebody putting something
    # there. It is wrong now: syn-install creates /var/lib/synapse-src empty and
    # user-owned at install time, precisely so the updater GUI never has to ask
    # for a password it cannot prompt for. The guard then rejected the very
    # directory that fix creates, and every fresh install opened Updates to
    # "exists but is not a git checkout — move it aside".
    #
    # git clones into an existing empty directory quite happily, so the only
    # thing worth refusing is a path with something IN it.
    if [ -e "$SRC" ] && [ ! -d "$SRC" ]; then
        die "$SRC exists and is not a directory — move it aside"
    fi
    if [ -d "$SRC" ] && [ -n "$(ls -A "$SRC" 2>/dev/null)" ]; then
        die "$SRC exists but is not a git checkout — move it aside"
    fi

    info "first run: cloning $REPO_URL into $SRC"

    # Pre-created by syn-install in the common case, so nothing here needs root.
    # Only reach for sudo when the directory is genuinely absent, or is there but
    # owned by someone else.
    #
    # ⚠ can_sudo() ALREADY GUARDS THESE — it is `sudo -n true || [ -t 0 ]`, so a
    # session with neither a cached credential nor a terminal dies with advice
    # instead of escalating. sudo_safe closes the gap between the two anyway:
    # the credential can expire between the guard and the call, and on that path
    # a bare sudo would be a failed PAM conversation rather than a clean refusal.
    if [ ! -d "$SRC" ]; then
        can_sudo || die "$SRC does not exist yet, and this session cannot ask for a password.
  Run this once in a terminal:  sudo install -d -o $(id -un) -g $(id -gn) $SRC"
        sudo_safe install -d -o "$(id -un)" -g "$(id -gn)" "$SRC" || die "cannot create $SRC"
    elif [ ! -w "$SRC" ]; then
        can_sudo || die "$SRC is not writable by $(id -un), and this session cannot ask for a password.
  Run this once in a terminal:  sudo chown $(id -un):$(id -gn) $SRC"
        sudo_safe chown "$(id -un):$(id -gn)" "$SRC" || die "cannot take ownership of $SRC"
    fi
    git clone --branch "$REPO_REF" "$REPO_URL" "$SRC" || die "clone failed"
    ok "cloned"
}

# The revision currently checked out, and the one upstream is offering.
local_rev()  { git -C "$SRC" rev-parse HEAD 2>/dev/null; }
remote_rev() { git -C "$SRC" rev-parse "origin/$REPO_REF" 2>/dev/null; }

fetch_src() {
    info "fetching $REPO_REF from origin"
    git -C "$SRC" fetch --quiet origin "$REPO_REF" || die "git fetch failed (no network?)"

    # A dirty tree means someone edited the update cache by hand. Refuse rather
    # than reset --hard over their work without asking.
    if [ -n "$(git -C "$SRC" status --porcelain 2>/dev/null)" ]; then
        warn "$SRC has local modifications; they will be discarded by --force"
        [ "${FORCE:-0}" = 1 ] ||
            die "refusing to overwrite local changes in $SRC (re-run with --force to discard them)"
    fi
}

checkout_remote() {
    # ⛔ --force PROMISED TO DISCARD LOCAL CHANGES AND DID NOT DISCARD ALL OF
    # THEM. `reset --hard` restores tracked files and leaves UNTRACKED ones
    # exactly where they are — so a tree made dirty by a stray file (which is
    # what a build leaving something behind produces, and what chibi 22's
    # rename produced on every machine at once) passed the guard under --force,
    # got reset, and was still dirty on the next run. Refusing for ever, with
    # the one documented escape hatch not escaping.
    #
    # ⚠ `-fd` AND DELIBERATELY NOT `-fdx`. Without -x, git clean leaves IGNORED
    # files alone — which is the 461MB whisper model and every other cached
    # download this tree is supposed to keep. -x would re-download half a
    # gigabyte on every forced update to fix a stray text file.
    if [ "${FORCE:-0}" = 1 ]; then
        git -C "$SRC" clean --quiet -fd
    fi
    git -C "$SRC" checkout --quiet -B "$REPO_REF" "origin/$REPO_REF" ||
        die "could not check out origin/$REPO_REF"
    git -C "$SRC" reset --quiet --hard "origin/$REPO_REF"
}

# ── what is behind ───────────────────────────────────────────

# PKGBUILD field, without sourcing the file (a PKGBUILD is arbitrary code and
# this runs before anything has been reviewed).
# The pkgver/pkgrel a PKGBUILD actually PRODUCES, not the text on its line.
#
# This was a sed scrape, and the vendored limine-mkinitcpio-hook composes its
# version from another variable:
#
#     _pkgver=1.37.1
#     pkgver="${_pkgver}${_extver}"
#
# The scrape returned the literal string ${_pkgver}${_extver}, and `vercmp`
# ranks that ABOVE every real version — so the component was permanently "out
# of date". Every run added it to CHANGED and handed it to build-all.sh, which
# (having never been told the name) exited 1 on `unknown component`, taking the
# whole update with it: on 2026-08-12 syn-settings 17 could not ship because a
# package nobody had touched looked newer than itself, for ever.
#
# So it is SOURCED. That executes the file — which is exactly what makepkg is
# about to do with the same file from the same tree, so it adds no exposure the
# update does not already have — in a subshell, with `set +e +u` because a
# PKGBUILD is not written to be sourced by a script running under either.
#
# A value that cannot be resolved returns EMPTY, and the caller skips the
# component with a warning. A version that cannot be read must never become an
# update: that is the failure this whole function is here to stop.
pkgfield() {
    local v
    v=$( set +e +u; cd "$1" 2>/dev/null && . ./PKGBUILD >/dev/null 2>&1 &&
         printf '%s' "${!2}" ) || return 1
    case "$v" in *'$'*|"") return 1 ;; esac
    printf '%s' "$v"
}

# Fills CHANGED with "<component> <installed> <available>" for every component
# that is installed AND older than the tree, and NEW with "<component>
# <available>" for one that exists upstream but is not installed here.
#
# NEW used to be `continue` — "this updates a system, it does not add software
# to it". That principle broke the desktop on 2026-08-08. synui pkgrel 317
# pointed the start menu's "Software Manager" and "Update System" rows at
# `synpkg`, a component that had just landed in COMPONENTS and therefore could
# never arrive: apply shipped the synui that needed it and skipped the thing it
# needed, so every updated system got two dead menu rows. A component list that
# silently declines to complete itself is the same class of bug as a component
# frozen forever, which is what this tool exists to remove.
#
# NEW is kept SEPARATE from CHANGED rather than folded in, because installing
# software a system has never had is a louder act than updating what it runs,
# and report() says so in its own section. It is never silent.
CHANGED=()
NEW=()
SKIPPED=()
BLOCKED=()
# Installed, out of date, and deliberately not being updated — `held` in the
# manifest, put there by `syn-update ignore`.
#
# Kept OUT of CHANGED rather than filtered later, so nothing downstream has to
# remember to exclude it: apply builds CHANGED, and a held component is
# therefore never built by accident. Naming one on the command line is the
# documented override, because an explicit request is a newer answer than a
# stored one.
#
# It is REPORTED in full, with the version it is refusing, for the same reason
# DECLINED is: the reason for a hold is almost always temporary, and a list
# that omits what it is holding is how a component stays pinned for a year.
HELD=()
# Components in the tree that this machine has been OFFERED and does not have.
# Reported in full, built only when named on the command line. See MANIFEST.
DECLINED=()

# Component names named on the command line: `syn-update apply synui`. Empty
# means "everything that changed", which is what apply has always done and
# stays the default.
SELECT=()
# Was this component named on the command line?
in_select() {
    local c
    for c in "${SELECT[@]}"; do [ "$c" = "$1" ] && return 0; done
    return 1
}

# What this run actually handed to build-all.sh. The local-repo refresh below
# publishes from THIS, not from CHANGED+NEW: with a named subset those are no
# longer the same set, and copying a package that was built but never installed
# would have the [synapseos] repo advertising a version the machine is not
# running.
BUILT=()

# The names build-all.sh will ACCEPT, read from the revision that would run.
#
# COMPONENTS is deliberately not scraped out of build-all.sh (see its comment),
# and the price of two hand-kept lists is that they can disagree. They did:
# limine-mkinitcpio-hook was added here and never to KNOWN= over there, so the
# first run that wanted it died on `unknown component` — after fetching, with
# nothing built, and naming neither list as the thing that was wrong. Every
# later run died the same way, so no fix to anything could ship past it.
#
# build-all.sh already has the mirror-image guard (a KNOWN= name with no build
# rule); this is the other half. A disagreement now costs that one component,
# reported, instead of the entire update.
#
# Read with `git show` rather than off disk so `check` describes what `apply`
# would do — cmd_check never moves the tree.
#
# SRC_GIT, not SRC: cmd_check points SRC at a mktemp -d of PKGBUILDs read out
# of the remote revision, and that directory is not a git repository. Reading
# the tree through it would fail, silently, in exactly the mode whose whole job
# is to tell you what apply is going to do.
buildable_names() {
    git -C "${SRC_GIT:-$SRC}" show "origin/$REPO_REF:build-all.sh" 2>/dev/null |
        awk '/^KNOWN=\(/{f=1} f{print} f && /\)/{exit}' |
        sed 's/^KNOWN=(//; s/).*//' | tr -s ' \t\n' ' '
}

# Scan against what upstream is OFFERING, which means reading the PKGBUILDs at
# the REMOTE revision and not the checked-out one. Done without moving the tree,
# so the caller stays read-only.
#
# ⚠ scan() ON ITS OWN ANSWERS THE WRONG QUESTION, and silently. It reads
# $SRC/<component>/PKGBUILD, which in a checked-out tree is the revision this
# machine is ALREADY on — so every component compares equal and the report is a
# confident "everything is current" on a machine that is six releases behind.
# `ping` did exactly that on its first run. Nothing errors, because nothing is
# wrong: it is a correct answer to a question nobody asked.
#
# So this exists to be the only way to ask, and both callers use it.
scan_remote() {
    local tmp; tmp=$(mktemp -d)
    # ⛔ NO `trap ... RETURN` HERE, which is what this was and what it cost.
    # Bash does not scope a RETURN trap to the function that set it unless
    # `set -T` is on: it stays armed and fires again on the NEXT function
    # return, by which time `tmp` is out of scope and `set -u` kills the script
    # — after its work was done, so `ping` printed its answer and then died
    # with "tmp: unbound variable". It survived in cmd_check only because that
    # returns at the very end of the run. Explicit cleanup instead; there is
    # exactly one exit from this function.
    local c
    for c in "${COMPONENTS[@]}"; do
        mkdir -p "$tmp/$c"
        # `rm -rf`, not `rmdir`: the redirect creates the file BEFORE git runs,
        # so a component that does not exist at the remote revision leaves an
        # empty PKGBUILD behind. rmdir then fails loudly ("Directory not
        # empty") and the empty file goes on to trip scan()'s "cannot read
        # pkgver/pkgrel" warning — two pieces of noise for a component that is
        # simply not there yet, which is the normal case when an older system
        # updates to a tree that has since gained a component.
        git -C "$SRC" show "origin/$REPO_REF:$c/PKGBUILD" > "$tmp/$c/PKGBUILD" 2>/dev/null ||
            rm -rf "$tmp/$c"
    done
    local real="$SRC"; SRC="$tmp"; SRC_GIT="$real"; scan; SRC="$real"; SRC_GIT=""
    rm -rf "$tmp"
}

scan() {
    CHANGED=(); NEW=(); SKIPPED=(); BLOCKED=(); DECLINED=(); HELD=()

    # Read before the loop, not per component: the file is one stat and the
    # loop asks about it twenty-five times.
    load_manifest

    # Empty means the scrape failed, not that nothing is buildable — an update
    # must not be withheld because a guard could not read its own input.
    local known; known=$(buildable_names)

    local c ver rel avail inst
    for c in "${COMPONENTS[@]}"; do
        [ -f "$SRC/$c/PKGBUILD" ] || continue

        if [ -n "$known" ]; then
            case " $known " in
                *" $c "*) ;;
                *) BLOCKED+=("$c"); continue ;;
            esac
        fi

        ver=$(pkgfield "$SRC/$c" pkgver)
        rel=$(pkgfield "$SRC/$c" pkgrel)
        [ -n "$ver" ] && [ -n "$rel" ] || { warn "$c: cannot read pkgver/pkgrel, skipping"; continue; }
        avail="$ver-$rel"

        inst=$(pacman -Q "$c" 2>/dev/null | awk '{print $2}')
        # Not installed here. Reachable only for a component that DOES exist at
        # the revision being scanned — cmd_check drops the directory entirely
        # for one that does not, and the `-f PKGBUILD` guard above catches the
        # rest — so an older system updating to a tree it has never seen reports
        # the components it is genuinely missing, not every name in the list.
        if [ -z "$inst" ]; then
            # Must never arrive on its own, whatever any file says.
            [ -n "${NEVER_ADD[$c]:-}" ] && continue

            # The whole point of the manifest: an absence this machine has
            # already answered for is a CHOICE, and a choice is not an update.
            # No manifest at all is read the same way — see the comment there.
            if [ "$MANIFEST_PRESENT" = 1 ] && [ -z "${COMP_KNOWN[$c]+set}" ]; then
                NEW+=("$c $avail")
            else
                DECLINED+=("$c $avail")
            fi
            continue
        fi

        # vercmp, not string compare: 0.1.0-203 vs 0.1.0-99 sorts the wrong way
        # as text, and that mistake silently declines to ship an update.
        if [ "$(vercmp "$avail" "$inst")" -gt 0 ]; then
            # Named on the command line beats the stored hold: `syn-update
            # apply synui` on a held synui is an explicit, newer answer, and
            # refusing it would leave no way to take one update without first
            # releasing the hold permanently.
            if [ "${COMP_KNOWN[$c]:-}" = held ] && ! in_select "$c"; then
                HELD+=("$c $inst $avail")
            else
                CHANGED+=("$c $inst $avail")
            fi
        fi
    done

    # Anything installed and NOT buildable here. Sorted, because a bash
    # associative array iterates in hash order and a list that reshuffles
    # between runs looks like something changed when nothing did.
    local p
    while IFS= read -r p; do
        pacman -Q "$p" >/dev/null 2>&1 && SKIPPED+=("$p")
    done < <(printf '%s\n' "${!UNSUPPORTED[@]}" | sort)
}

# ── commands ─────────────────────────────────────────────────

show_commits() {
    local from="$1" to="$2"
    [ "$from" = "$to" ] && return 0
    say ""
    info "commits since your installed source revision"
    git -C "$SRC" log --no-merges --format="  ${C_DIM}%h${C_R} %s" "$from..$to" 2>/dev/null | head -40
    local n
    n=$(git -C "$SRC" rev-list --count "$from..$to" 2>/dev/null)
    [ "${n:-0}" -gt 40 ] && say "  ${C_DIM}... and $((n - 40)) more${C_R}"
}

report() {
    say ""
    # ⚠ "current" must not be said over a held component that is out of date.
    # HELD is deliberately not in CHANGED, so without this the headline for a
    # machine holding one back reads "everything is already current" — which is
    # false, and false in the direction that hides the hold.
    if [ ${#CHANGED[@]} -eq 0 ] && [ ${#NEW[@]} -eq 0 ] && [ ${#HELD[@]} -eq 0 ]; then
        ok "everything build-all.sh can update is already current"
    elif [ ${#CHANGED[@]} -eq 0 ] && [ ${#HELD[@]} -eq 0 ]; then
        ok "every installed component is current"
    elif [ ${#CHANGED[@]} -eq 0 ]; then
        ok "every component this machine is updating is current"
    fi

    if [ ${#CHANGED[@]} -gt 0 ]; then
        info "${#CHANGED[@]} component(s) to rebuild"
        printf '  %-16s %-14s -> %s\n' COMPONENT INSTALLED AVAILABLE
        local e
        for e in "${CHANGED[@]}"; do
            # shellcheck disable=SC2086
            set -- $e
            printf '  %-16s %-14s -> %s\n' "$1" "$2" "$3"
        done
    fi

    # Its own section, above SKIPPED: this is the one part of the report that
    # adds software rather than moving it forward, and it should read that way.
    if [ ${#NEW[@]} -gt 0 ]; then
        say ""
        info "${#NEW[@]} NEW component(s) to install"
        printf '  %-16s %s\n' COMPONENT AVAILABLE
        local n
        for n in "${NEW[@]}"; do
            # shellcheck disable=SC2086
            set -- $n
            printf '  %-16s %-14s %s\n' "$1" "$2" "(not installed here)"
        done
    fi

    # NOT an update, and NOT a problem — the user's own answer, read back.
    #
    # It gets a section rather than silence for the same reason UNSUPPORTED
    # does: a component quietly left out forever is the bug this tool exists to
    # remove, and "syn-update decided not to install vibe" has to be something
    # you can read rather than something you deduce. It also carries the one
    # command that changes the answer, because the point of the record is that
    # it is a decision and not a verdict.
    if [ ${#DECLINED[@]} -gt 0 ]; then
        say ""
        info "${#DECLINED[@]} component(s) available and NOT installed here"
        printf '  %-16s %s\n' COMPONENT AVAILABLE
        # The wording tracks how sure we are. With a manifest this IS the
        # user's answer read back. Without one it is an assumption made on
        # their behalf, and claiming they picked something they may never have
        # been shown would be the report lying to cover the guess.
        local d note="(you did not pick this)"
        [ "$MANIFEST_PRESENT" = 1 ] || note="(not installed here)"
        for d in "${DECLINED[@]}"; do
            # shellcheck disable=SC2086
            set -- $d
            printf '  %-16s %-14s %s\n' "$1" "$2" "$note"
        done
        # shellcheck disable=SC2086
        set -- ${DECLINED[0]}
        say "  ${C_DIM}Left alone. To take one: ${C_R}syn-update apply $1${C_DIM} — and it stays.${C_R}"
        if [ "$MANIFEST_PRESENT" != 1 ]; then
            say "  ${C_DIM}(no $MANIFEST yet, so anything missing is read as not wanted;${C_R}"
            say "  ${C_DIM} the next apply writes it and new components resume arriving.)${C_R}"
        fi
    fi

    # Held back on purpose. Above BLOCKED because this one is an answer, not a
    # fault — and it carries the pending version, which is the whole point: a
    # hold you cannot see the cost of is a hold you will never revisit.
    if [ ${#HELD[@]} -gt 0 ]; then
        say ""
        info "${#HELD[@]} component(s) HELD BACK — an update is waiting and will not be taken"
        # ⚠ NO " -> " ON THESE ROWS, and that is load-bearing rather than a
        # layout choice. synpkg's `system check --tsv` turns this report into a
        # table by scraping every indented line that CONTAINS " -> " — that is
        # how it finds the CHANGED rows, and CHANGED was the only section that
        # had an arrow. A held row in the same shape is read as a pending
        # update, so the Updates tab would offer an Update button on the one
        # component the user deliberately pinned.
        printf '  %-16s %-14s %s\n' COMPONENT INSTALLED STATE
        local hh
        for hh in "${HELD[@]}"; do
            # shellcheck disable=SC2086
            set -- $hh
            printf '  %-16s %-14s held, %s available\n' "$1" "$2" "$3"
        done
        # shellcheck disable=SC2086
        set -- ${HELD[0]}
        say "  ${C_DIM}Release it with: ${C_R}syn-update unignore $1${C_R}"
        say "  ${C_DIM}Or take this one update without releasing the hold: ${C_R}syn-update apply $1${C_R}"
    fi

    # A bug in OUR tree, not in this machine. Loud, and named as ours: the
    # person running an update cannot fix it and should not be left guessing.
    if [ ${#BLOCKED[@]} -gt 0 ]; then
        say ""
        warn "${#BLOCKED[@]} component(s) SKIPPED — build-all.sh does not accept the name:"
        local b
        for b in "${BLOCKED[@]}"; do printf '    %s\n' "$b" >&2; done
        printf '    %s\n' "This is a packaging bug in SynapseOS: the name is in syn-update's" >&2
        printf '    %s\n' "COMPONENTS but not in build-all.sh's KNOWN=. Everything else still" >&2
        printf '    %s\n' "updates; these will not until the two lists agree." >&2
    fi

    if [ ${#SKIPPED[@]} -gt 0 ]; then
        say ""
        warn "not updatable from source on an installed system:"
        local p
        for p in "${SKIPPED[@]}"; do
            printf '    %-24s %s\n' "$p" "${UNSUPPORTED[$p]}" >&2
        done
        printf '    %s\n' "These move with an ISO upgrade. syn-update will not touch them." >&2
    fi
}

cmd_check() {
    need_tools; setup_src; fetch_src

    local from to
    from=$(local_rev); to=$(remote_rev)

    if [ "$from" = "$to" ]; then
        ok "source is already at $(git -C "$SRC" rev-parse --short HEAD) ($REPO_REF)"
    else
        info "$(git -C "$SRC" rev-parse --short HEAD) -> $(git -C "$SRC" rev-parse --short origin/"$REPO_REF")"
    fi
    show_commits "$from" "$to"

    scan_remote

    report
    say ""
    if [ ${#CHANGED[@]} -gt 0 ] || [ ${#NEW[@]} -gt 0 ]; then
        say "Run ${C_B}syn-update apply${C_R} to build and install them."
    elif [ "$from" != "$to" ]; then
        # ⚠ COMMITS AND PACKAGES ARE NOT THE SAME THING, and the report used to
        # let you think they were. It listed everything that had landed
        # upstream, then said "already current", and stopped — so a run that
        # showed two commits and installed nothing read as an updater refusing
        # to do its job.
        #
        # Most commits here change nothing this machine installs: documentation,
        # CI, the archiso profile, a repo tool, or a component that is not on
        # this box. Saying which is the difference between "there is an update
        # it will not take" and "there was nothing in it for you".
        say "Those commits changed nothing this machine installs — documentation,"
        say "the ISO profile, repository tooling, or a component you do not have."
        say ""
        say "${C_DIM}\`syn-update apply\` still moves $SRC to that revision,${C_R}"
        say "${C_DIM}which is where the repository's own tools live.${C_R}"
    fi
    return 0
}

# Which OTHER components a component depends on, read out of its PKGBUILD
# rather than kept as a list here.
#
# A hand-maintained edge list is wrong the first time a depends= line changes,
# and the failure is silent in the worst way: a component linked against a
# library version that is about to be replaced. The real edges today are
# synui -> scenefx0.5, synnet -> synapd and vibe -> synapd; none of them is
# written down anywhere in this file, which is the point.
#
# build-all.sh does NOT pull these in for you. want() is a plain filter, so a
# selective run builds exactly what it was asked for and nothing else.
component_deps() {
    # TWO statements, and it has to stay that way. `local c=$1 pk="$SRC/$c/…"`
    # expands every word BEFORE local assigns any of them, so $c is empty there
    # and pk points at a PKGBUILD that does not exist — the function then
    # returns nothing, for every component, and the dependency warning below
    # can never fire. It read correctly under test only because the caller's
    # loop variable was also named c, so the expansion picked up the global.
    local c=$1
    local pk="$SRC/$c/PKGBUILD" dep out=""
    [ -f "$pk" ] || return 0
    # depends=( may span lines, so take the whole array and flatten it.
    for dep in $(awk '/^depends=\(/,/\)/' "$pk" 2>/dev/null | tr -d "'\"" | tr '(),=' '    '); do
        [ "$dep" = "depends" ] && continue
        [ "$dep" = "$c" ] && continue
        case " ${COMPONENTS[*]} " in *" $dep "*) out="$out $dep" ;; esac
    done
    printf '%s' "${out# }"
}

cmd_apply() {
    need_not_root; need_tools; setup_src; fetch_src

    local from to
    from=$(local_rev); to=$(remote_rev)
    show_commits "$from" "$to"

    checkout_remote
    scan
    report

    # CHANGED and NEW go into ONE build-all.sh invocation. They are reported
    # apart because they mean different things to the user, but they build
    # identically, and splitting the invocation would defeat build-all.sh's
    # fixed dependency order — the exact mistake the comment below warns about.
    local names=() e
    for e in "${CHANGED[@]}" "${NEW[@]}"; do
        # shellcheck disable=SC2086
        set -- $e
        names+=("$1")
    done

    # A DECLINED component becomes a candidate ONLY by being named.
    #
    # `syn-update apply vibe` on a machine that never installed vibe is not an
    # accident — typing the name IS the opt-in, and it is the same gesture the
    # per-row Install button in SYNAPSE Software makes. save_manifest() at the
    # end of this run records the new answer from the disk, so it never has to
    # be given a second time.
    #
    # A bare `apply` adds none of them. That is the whole change.
    if [ ${#SELECT[@]} -gt 0 ]; then
        local dsel ssel
        for dsel in "${DECLINED[@]}"; do
            # shellcheck disable=SC2086
            set -- $dsel
            for ssel in "${SELECT[@]}"; do
                [ "$ssel" = "$1" ] && { names+=("$1"); break; }
            done
        done
    fi

    # AFTER the two lists above are folded together, not before: an apply that
    # names nothing but a declined component has an empty CHANGED and an empty
    # NEW and still has work to do.
    if [ ${#names[@]} -eq 0 ]; then
        say ""
        ok "nothing to build"
        # Nothing was built, but the badge may still be showing a count that
        # this run has just resolved — the tree moved, and what it was counting
        # was the tree being behind.
        ping_refresh_quietly
        # ⚠ THE TREE MOVED ANYWAY. checkout_remote() ran above, so this machine
        # now has the new source even though no package changed — and saying
        # nothing about that is how "the updater did nothing" gets reported
        # against a run that did the one thing it had to do. The repository's
        # own scripts (tools/) are not packaged and live only here.
        if [ "$from" != "$to" ]; then
            say ""
            say "$SRC is now at $(git -C "$SRC" rev-parse --short HEAD)."
            say "${C_DIM}Nothing to build from it — the commits changed documentation,${C_R}"
            say "${C_DIM}the ISO profile, repository tooling, or a component this${C_R}"
            say "${C_DIM}machine does not have.${C_R}"
        fi
        return 0
    fi

    # ── A named subset ────────────────────────────────────────────────────
    #
    # `syn-update apply synui` builds synui and leaves the rest alone. This is
    # what makes a per-row Update button in SYNAPSE Software able to tell the
    # truth: without it the button says "update synui" and rebuilds everything
    # that changed.
    #
    # It stays ONE invocation with a filtered argument list — the arguments are
    # a filter, never a sequence, so build-all.sh still walks its own fixed
    # order. That property is what makes a subset safe at all.
    if [ ${#SELECT[@]} -gt 0 ]; then
        local want=() skip=() s found
        for s in "${SELECT[@]}"; do
            found=0
            for e in "${names[@]}"; do
                [ "$e" = "$s" ] && { want+=("$s"); found=1; break; }
            done
            [ "$found" = 1 ] || skip+=("$s")
        done

        # Asked for something that has nothing to build. Not an error: a GUI
        # that re-lists after a build will ask for a component that is now
        # current, and failing there would turn a no-op into a red banner.
        if [ ${#skip[@]} -gt 0 ]; then
            say ""
            info "already current, nothing to build: ${skip[*]}"
        fi
        if [ ${#want[@]} -eq 0 ]; then
            say ""
            ok "nothing to build"
            return 0
        fi

        # THE ONE THING A SUBSET CAN GET WRONG. Building synui while scenefx0.5
        # is also out of date links it against the scenefx that is about to be
        # replaced — and it will look like it worked. Say so, name the command
        # that does it properly, and carry on: this is the user's call, and
        # refusing would make the button useless in exactly the case where the
        # whole build is what they wanted to avoid.
        local d dd s2
        for s in "${want[@]}"; do
            for d in $(component_deps "$s"); do
                for e in "${names[@]}"; do
                    [ "$e" = "$d" ] || continue
                    dd=0
                    for s2 in "${want[@]}"; do [ "$s2" = "$d" ] && dd=1; done
                    [ "$dd" = 1 ] && continue
                    warn "$s depends on $d, which is ALSO out of date and is not being built.
    $s will be built against the $d already installed. To do both:
        syn-update apply $s $d"
                done
            done
        done

        names=("${want[@]}")
    fi

    say ""
    info "building: ${names[*]}"
    say "${C_DIM}  (build-all.sh orders these itself; arguments are a filter, not a sequence)${C_R}"
    say ""

    # ⚠ ONE password, asked HERE — before the database refresh and before
    # build-all.sh, both of which need root, and while somebody is still
    # watching. Held open for the rest of the run, so sudo's five-minute
    # timestamp cannot expire in the middle of a compositor build and stop at a
    # prompt nobody is there to answer. See sudo_keepalive_start().
    #
    # It is deliberately AFTER the "nothing to build" return above: an apply
    # with no work must not ask for a password at all.
    sudo_keepalive_start || true
    say ""

    # Databases first, while somebody is still watching — see sync_pacman_dbs().
    sync_pacman_dbs
    say ""

    # ONE invocation, all components: build-all.sh walks its own fixed order and
    # skips what was not asked for, so dependencies come out right even though
    # the arguments are unordered. Calling it once per package would build them
    # in the order given, which is exactly the wrong thing.
    BUILT=("${names[@]}")
    if ! ( cd "$SRC" && ./build-all.sh "${names[@]}" ); then
        stale_db_sig_hint
        die "build failed — the tree is at $(git -C "$SRC" rev-parse --short HEAD), nothing was rolled back"
    fi

    refresh_local_repo

    # Last, and only on success: the disk is now what the record describes.
    #
    # It is here rather than in `check` because writing it needs root, and this
    # is the one command that already holds a password. A machine with nothing
    # to build therefore never gains the file — which costs nothing, because
    # the no-file reading and the file it would have written say the same thing
    # until something actually changes.
    save_manifest

    # The tree just moved, and the release version is part of what moved. Here
    # rather than beside the git pull because a pull that fails to BUILD has not
    # put a new release on this disk — the same reasoning as save_manifest, and
    # it needs the root this command already holds.
    stamp_os_identity

    # The bar's badge is a file, and this is the command that just made it
    # wrong. See ping_refresh_quietly.
    ping_refresh_quietly

    say ""
    ok "updated to $(git -C "$SRC" rev-parse --short HEAD)"
    say ""
    say "A new synui does not take effect until the compositor restarts —"
    say "log out and back in to pick it up."
}

# Keep /var/cache/synapseos honest. build-all.sh installs with `pacman -U`
# directly, so without this the [synapseos] repo would still advertise the ISO's
# versions and a later `pacman -Syu` could offer to DOWNGRADE what we just
# installed.
refresh_local_repo() {
    [ -d "$LOCAL_REPO" ] || return 0
    info "refreshing the local [synapseos] repo"

    # BUILT, not CHANGED+NEW. Those were the same set until `apply <component>`
    # existed; now a subset run leaves the rest of CHANGED unbuilt, and the tree
    # still holds their PREVIOUS .pkg.tar.zst from an earlier run. Publishing
    # those would advertise versions this machine is not running — the exact
    # drift this function exists to prevent. NEW is covered because it is folded
    # into BUILT before build-all.sh is called.
    local c pkg copied=0
    for c in "${BUILT[@]}"; do
        pkg=$(ls -1t "$SRC/$c/$c"-*.pkg.tar.zst 2>/dev/null | grep -v -- "-debug-" | head -1)
        [ -n "$pkg" ] || continue
        sudo_safe cp -f "$pkg" "$LOCAL_REPO/" && copied=$((copied + 1))
    done

    # Then RECONCILE: publish anything that is installed at a version the repo
    # does not carry, whoever built it.
    #
    # The loop above only knows what THIS syn-update decided to build, and that
    # is not the whole set. build-all.sh installs every package it produces
    # with `pacman -U`, and it now pulls an in-repo dependency into the build
    # when a selective run needs one — so a component can be built, installed
    # and working while appearing in neither CHANGED nor NEW.
    #
    # That happened the moment vibe gained a hard dependency on syn-confine: a
    # machine on an older syn-update asks for vibe, build-all.sh builds and
    # installs syn-confine to satisfy it, and syn-confine is then a package on
    # the system that no repo advertises — permanently, because every later
    # scan finds it installed and unchanged and so never lists it either.
    #
    # Matched against the INSTALLED version rather than "newest built", so this
    # publishes what the machine is actually running and never advertises some
    # older build left in the tree.
    local ver inst reconciled=0
    for c in "${COMPONENTS[@]}"; do
        inst=$(pacman -Q "$c" 2>/dev/null | awk '{print $2}')
        [ -n "$inst" ] || continue
        for pkg in "$SRC/$c/$c"-*.pkg.tar.zst; do
            [ -e "$pkg" ] || continue
            case $pkg in *-debug-*) continue ;; esac
            read -r _ ver < <(pacman -Qp "$pkg" 2>/dev/null)
            [ "$ver" = "$inst" ] || continue
            [ -e "$LOCAL_REPO/$(basename "$pkg")" ] && continue
            sudo_safe cp -f "$pkg" "$LOCAL_REPO/" &&
                reconciled=$((reconciled + 1))
        done
    done

    prune_superseded
    rebuild_db
    sudo_safe pacman -Sy --quiet --noconfirm >/dev/null 2>&1 || true
    [ "$copied" -gt 0 ] && ok "published $copied package(s) to $LOCAL_REPO"
    [ "$reconciled" -gt 0 ] &&
        ok "published $reconciled installed package(s) the repo was missing"
    return 0
}

# Rebuild the repo index from the files actually present, from scratch.
#
# NOT a plain `repo-add db *.pkg.tar.zst`: repo-add only ever ADDS, so entries
# for packages whose files have gone stay in the database forever. Combined with
# prune_superseded that is worse than the downgrade it was fixing — the index
# would keep advertising synui 0.1.0-8 and pacman would then fail to find the
# file it was promised. Deleting the database first is what makes the index
# describe the directory instead of accumulating everything it has ever seen.
rebuild_db() {
    shopt -s nullglob
    local pkgs=("$LOCAL_REPO"/*.pkg.tar.zst)
    shopt -u nullglob

    if [ ${#pkgs[@]} -eq 0 ]; then
        # repo-add with no packages errors out; an empty repo is legitimate
        # here (everything in it was superseded), so just clear the index.
        sudo_safe rm -f "$LOCAL_REPO"/synapseos.db* "$LOCAL_REPO"/synapseos.files*
        warn "$LOCAL_REPO now holds no packages; [synapseos] is empty"
        return 0
    fi

    sudo_safe rm -f "$LOCAL_REPO"/synapseos.db* "$LOCAL_REPO"/synapseos.files*
    sudo_safe repo-add --quiet "$LOCAL_REPO/synapseos.db.tar.gz" "${pkgs[@]}" >/dev/null 2>&1 ||
        { warn "repo-add failed; [synapseos] index may be incomplete"; return 0; }

    # syn-install creates these by hand at install time, so recreate them here
    # rather than assuming repo-add left them behind.
    sudo_safe ln -sf synapseos.db.tar.gz    "$LOCAL_REPO/synapseos.db"
    sudo_safe ln -sf synapseos.files.tar.gz "$LOCAL_REPO/synapseos.files"
    return 0
}

# Retire package FILES in the local repo that are older than what is installed.
#
# /var/cache/synapseos is seeded from the ISO at install time and then never
# touched, so after a few updates it advertises versions far behind the running
# system — measured on the dev box, it offered synui 0.1.0-8 against 0.1.0-203
# installed, plus eight others. pacman will not downgrade of its own accord, but
# `pacman -S synui` to repair a broken install would fetch that 195-release-old
# package without hesitating, and `-Syuu` would take the whole system back.
#
# A repo entry that can only ever offer a downgrade has no upside, so move those
# files out of the indexed directory. Moved rather than deleted: they are the
# only copy of what the installing ISO shipped, which is occasionally worth
# having, and they are small.
prune_superseded() {
    local f name ver inst moved=0
    shopt -s nullglob
    for f in "$LOCAL_REPO"/*.pkg.tar.zst; do
        # Ask pacman rather than parsing the filename: a package name can
        # contain dashes (syn-install, synui-debug) and so can the version, so
        # splitting on "-" gets it wrong in both directions.
        read -r name ver < <(pacman -Qp "$f" 2>/dev/null)
        [ -n "$name" ] && [ -n "$ver" ] || continue

        inst=$(pacman -Q "$name" 2>/dev/null | awk '{print $2}')
        [ -n "$inst" ] || continue          # not installed: not a downgrade risk

        if [ "$(vercmp "$ver" "$inst")" -lt 0 ]; then
            sudo_safe mkdir -p "$LOCAL_REPO/superseded"
            sudo_safe mv -f "$f" "$LOCAL_REPO/superseded/" && moved=$((moved + 1))
        fi
    done
    shopt -u nullglob

    [ "$moved" -gt 0 ] &&
        info "retired $moved package file(s) older than what is installed -> $LOCAL_REPO/superseded"
    return 0
}

# ── holding a component back ─────────────────────────────────────────────
#
# The state lives in the manifest, beside `selected` and `declined`, because it
# is the same KIND of fact: an answer this machine has given about a component,
# which every later run has to read back rather than re-derive. A separate file
# would be a second place to look and a second place to forget.
#
# ⚠ This is NOT pacman's IgnorePkg, and it deliberately does not touch it.
# synpkg holds back REPOSITORY packages that way, because pacman honours it and
# an ignore only one tool obeyed would be a lie. A SynapseOS component is not
# upgraded by pacman at all — it is BUILT here and installed with `pacman -U` —
# so IgnorePkg would not stop it, and writing one would produce a hold that
# looks set and does nothing.

# Rewrite one component's line in the manifest, leaving every other line as it
# was. Not save_manifest(): that recomputes the whole file from `pacman -Q` and
# is only correct after a successful build.
manifest_set_state() {
    local name=$1 state=$2 tmp found=0 line n
    load_manifest

    tmp=$(mktemp) || die "cannot write $MANIFEST (mktemp failed)"

    if [ -r "$MANIFEST" ]; then
        while IFS= read -r line || [ -n "$line" ]; do
            n=${line%%#*}
            case $n in *=*) ;; *) printf '%s\n' "$line" >> "$tmp"; continue ;; esac
            n=${n%%=*}; n=${n//[[:space:]]/}
            if [ "$n" = "$name" ]; then
                printf '%s = %s\n' "$name" "$state" >> "$tmp"
                found=1
            else
                printf '%s\n' "$line" >> "$tmp"
            fi
        done < "$MANIFEST"
    fi
    # A component the manifest has never named — a machine installed before the
    # manifest existed, or one that predates this component. Appending is
    # right: the hold is an answer, and an answer about something not yet
    # recorded still has to be recorded.
    [ "$found" = 1 ] || printf '%s = %s\n' "$name" "$state" >> "$tmp"

    if install_manifest "$tmp"; then
        rm -f "$tmp"
        return 0
    fi
    rm -f "$tmp"
    die "could not write $MANIFEST"
}

cmd_ignore() {
    [ ${#SELECT[@]} -gt 0 ] ||
        die "ignore: name a component. \`syn-update ignored\` lists what is held."
    need_not_root
    local c
    for c in "${SELECT[@]}"; do
        load_manifest
        if [ "${COMP_KNOWN[$c]:-}" = held ]; then
            info "$c is already held back"
            continue
        fi
        # Holding something not installed is meaningless: it is not being
        # updated either way, and `declined` already says so. Refusing beats
        # writing a state that reads as a decision nobody made.
        if ! pacman -Q "$c" >/dev/null 2>&1; then
            warn "$c is not installed here, so there is no update to hold back"
            continue
        fi
        manifest_set_state "$c" held
        ok "$c is held back — syn-update will report its updates and not take them"
    done
    return 0
}

cmd_unignore() {
    [ ${#SELECT[@]} -gt 0 ] ||
        die "unignore: name a component. \`syn-update ignored\` lists what is held."
    need_not_root
    local c
    for c in "${SELECT[@]}"; do
        load_manifest
        if [ "${COMP_KNOWN[$c]:-}" != held ]; then
            info "$c was not being held back"
            continue
        fi
        manifest_set_state "$c" selected
        ok "$c released — the next apply will update it"
    done
    return 0
}

cmd_ignored() {
    load_manifest
    local c held=() inst
    for c in "${COMPONENTS[@]}"; do
        [ "${COMP_KNOWN[$c]:-}" = held ] && held+=("$c")
    done

    # Machine-readable, for synpkg's Held back pane.
    #
    # A FLAG, not a scrape. synpkg already turns `syn-update check` into a
    # table by pattern-matching its human output, and that coupling is exactly
    # why adding a section to the report above could break the Updates tab —
    # a new command should not inherit the same trap.
    if [ "$TSV" = 1 ]; then
        printf 'component\tinstalled\n'
        for c in "${held[@]}"; do
            inst=$(pacman -Q "$c" 2>/dev/null | awk '{print $2}')
            printf '%s\t%s\n' "$c" "$inst"
        done
        [ ${#held[@]} -gt 0 ]
        return $?
    fi

    if [ ${#held[@]} -eq 0 ]; then
        ok "no component is being held back"
        say "  ${C_DIM}hold one with: ${C_R}syn-update ignore <component>${C_R}"
        return 0
    fi

    # The installed version, and nothing about what is available: this command
    # is read-only and must not fetch. `syn-update check` is where a pending
    # version comes from, and it reports every one of these with it.
    info "${#held[@]} component(s) held back"
    printf '  %-16s %s\n' COMPONENT INSTALLED
    for c in "${held[@]}"; do
        inst=$(pacman -Q "$c" 2>/dev/null | awk '{print $2}')
        printf '  %-16s %s\n' "$c" "${inst:-not installed}"
    done
    say "  ${C_DIM}Release one with: ${C_R}syn-update unignore <component>${C_R}"
    say "  ${C_DIM}\`syn-update check\` shows which of them have an update waiting.${C_R}"
    return 0
}

cmd_status() {
    need_tools
    [ -d "$SRC/.git" ] || die "no source tree yet — run: syn-update check"

    say "source:    $SRC"
    say "remote:    $REPO_URL ($REPO_REF)"
    say "revision:  $(git -C "$SRC" rev-parse --short HEAD 2>/dev/null) $(git -C "$SRC" log -1 --format='%s' 2>/dev/null)"
    say ""
    # Three columns, because "not installed" alone never said WHY, and the
    # answer decides whether the next `apply` will do anything about it.
    load_manifest
    printf '  %-16s %s\n' COMPONENT INSTALLED
    local c inst note
    for c in "${COMPONENTS[@]}"; do
        inst=$(pacman -Q "$c" 2>/dev/null | awk '{print $2}')
        if [ -n "$inst" ]; then
            printf '  %-16s %s\n' "$c" "$inst"
            continue
        fi
        if [ -n "${NEVER_ADD[$c]:-}" ]; then
            note="never added automatically — ${NEVER_ADD_HOW[$c]:-installed by hand}"
        elif [ "$MANIFEST_PRESENT" = 1 ] && [ -z "${COMP_KNOWN[$c]+set}" ]; then
            note="new here — the next apply installs it"
        else
            note="not picked — syn-update apply $c"
        fi
        printf '  %-16s %-16s %s\n' "$c" "${C_DIM}not installed${C_R}" "${C_DIM}$note${C_R}"
    done

    if [ "$MANIFEST_PRESENT" = 1 ]; then
        say ""
        say "${C_DIM}Your component selection: $MANIFEST${C_R}"
    fi
}

# ── ping: the background check the tray icon reads ───────────────────────────
#
# A check nobody is watching, on a timer, whose whole output is a small file the
# bar's Updates indicator reads. `check` is the same work with a report a person
# reads; this is the same work with an answer a program reads.
#
# ⛔ IT MUST NEVER ASK FOR ANYTHING. It runs from a systemd user timer with no
# terminal: a sudo prompt there does not fail, it HANGS, holding the timer's
# service active until something kills it — and the next trigger is skipped
# because the unit is still running, so the indicator silently stops updating
# and nothing anywhere says why. setup_src() is the one path that can reach for
# sudo (taking back a tree an older root-run left behind), so ping refuses up
# front when the tree is not already ours rather than letting it try.
PING_STATE_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/syn-update"
PING_STATE="$PING_STATE_DIR/pending"

# The systemd user timer's name in both places it is spelled.
PING_UNIT="syn-update-ping.timer"

ping_write() {
    # status=ok|error, then the counts, then a row per pending component.
    # Written to a temporary and renamed: the bar polls this file and a
    # half-written one is a half-read one — the indicator would blink to
    # "0 updates" for a frame every time the timer fired.
    mkdir -p "$PING_STATE_DIR" || return 1
    local tmp="$PING_STATE.tmp"
    {
        printf 'checked=%s\n' "$(date +%s)"
        printf 'status=%s\n' "$1"
        shift
        printf '%s\n' "$@"
    } > "$tmp" && mv -f "$tmp" "$PING_STATE"
}

cmd_ping() {
    # ⚠ THE REFUSAL IS THE FEATURE. Writing status=error with a reason means the
    # indicator can say "could not check" instead of showing a stale count for
    # ever, which is the failure this file exists to make visible.
    if [ ! -d "$SRC/.git" ] || [ ! -w "$SRC" ]; then
        ping_write error "reason=$SRC is not a source tree owned by $(id -un)"
        die "$SRC is not usable without asking for a password, which a timer cannot do.
  Run this once in a terminal:  syn-update check"
    fi

    # Everything the report would have said, computed and thrown away except
    # for the counts. Quiet: a timer's stdout is the journal.
    need_tools >/dev/null 2>&1 || { ping_write error "reason=missing tools"; die "missing tools"; }
    if ! fetch_src >/dev/null 2>&1; then
        ping_write error "reason=could not reach $REPO_URL"
        die "could not fetch"
    fi
    ping_scan_and_write

    # A person running this by hand gets the one line they asked for.
    say "${#CHANGED[@]} update(s), ${#NEW[@]} new, ${#HELD[@]} held — $PING_STATE"
}

# The half of `ping` that looks and writes, without the guard and the die()s —
# so `apply` can leave the indicator telling the truth when it finishes.
#
# ⚠ IT IS A SEPARATE FUNCTION RATHER THAN A SECOND COPY IN cmd_apply, and the
# reason is the scan_remote note below: the difference between scan and
# scan_remote is invisible in the output and wrong only on a machine that is
# behind, which is precisely the machine nobody tests on.
ping_scan_and_write() {
    # ⚠ scan_remote, NOT scan. See its header: a bare scan() reads the
    # checked-out tree and reports "everything is current" on a machine that is
    # months behind, without erroring. That is what ping did first.
    scan_remote >/dev/null 2>&1 || true

    local rows=() e
    for e in "${CHANGED[@]}"; do
        # shellcheck disable=SC2086
        set -- $e
        rows+=("update	$1	$2	$3")
    done
    for e in "${NEW[@]}"; do
        # shellcheck disable=SC2086
        set -- $e
        rows+=("new	$1	-	$2")
    done
    for e in "${HELD[@]}"; do
        # shellcheck disable=SC2086
        set -- $e
        rows+=("held	$1	$2	$3")
    done

    ping_write ok \
        "rev=$(git -C "$SRC" rev-parse --short "origin/$REPO_REF" 2>/dev/null)" \
        "updates=${#CHANGED[@]}" \
        "new=${#NEW[@]}" \
        "held=${#HELD[@]}" \
        "${rows[@]}"
}

# ⚠ THE BADGE DOES NOT CLEAR ITSELF. The bar reads the ping state file and
# nothing else — deliberately, so no bar ever does a git fetch — and until this
# existed `apply` did not write it. So a machine that had just been updated by
# hand went on showing "3 updates" until the six-hourly timer next fired, which
# reads exactly like an updater that did not update anything.
#
# Quiet and never fatal: this runs after a successful build, and a failure to
# rewrite an indicator's cache must not turn a completed update into an error.
# The next timer tick corrects whatever this could not.
ping_refresh_quietly() {
    [ -d "$SRC/.git" ] && [ -w "$SRC" ] || return 0
    fetch_src >/dev/null 2>&1 || return 0
    ping_scan_and_write >/dev/null 2>&1 || return 0
}

# ── ping --every / --on / --off: the timer itself ────────────────────────────
#
# ⚠ THE INTERVAL LIVES IN A DROP-IN, NOT IN THE SHIPPED UNIT. A unit file in
# /usr/lib belongs to the package and is replaced by every upgrade, so an
# interval written there is one that silently reverts the next time syn-update
# updates itself — which, for an updater, is often. The drop-in is in the
# user's own config and survives.
ping_dropin_dir() { printf '%s/systemd/user/%s.d' "${XDG_CONFIG_HOME:-$HOME/.config}" "$PING_UNIT"; }

cmd_ping_every() {
    local spec="$1"
    # systemd's own time syntax, so "30m", "6h", "2d" and "1week" all work and
    # none of them is a vocabulary invented here. Validated by systemd rather
    # than by a regex of ours: it owns the grammar, and a regex would drift.
    if ! systemd-analyze timespan "$spec" >/dev/null 2>&1; then
        die "not a time systemd understands: $spec
  Try something like 30m, 6h, 12h, 1d, 1week."
    fi
    local d; d=$(ping_dropin_dir)
    mkdir -p "$d" || die "cannot write $d"
    cat > "$d/interval.conf" <<CONF
# Written by \`syn-update ping --every $spec\`. Delete this file to go back to
# the shipped default.
[Timer]
# ⚠ BOTH, and the empty assignment first. A drop-in ADDS to a list-valued
# setting rather than replacing it, so without the reset the shipped interval
# and this one are both live and the timer fires on whichever comes first.
OnUnitActiveSec=
OnUnitActiveSec=$spec
CONF
    systemctl --user daemon-reload 2>/dev/null || true
    systemctl --user restart "$PING_UNIT" 2>/dev/null || true
    ok "checking for updates every $spec"
}

cmd_ping_on() {
    systemctl --user enable --now "$PING_UNIT" 2>/dev/null \
        || die "could not enable $PING_UNIT"
    ok "update checks on ($(ping_interval_now))"
}

cmd_ping_off() {
    systemctl --user disable --now "$PING_UNIT" 2>/dev/null \
        || die "could not disable $PING_UNIT"
    ok "update checks off — the indicator keeps whatever it last saw"
}

ping_interval_now() {
    systemctl --user show "$PING_UNIT" -p TimersMonotonic --value 2>/dev/null \
        | grep -oE 'OnUnitActiveUSec=[^ }]+' | head -1 | cut -d= -f2 \
        || printf 'unknown'
}

cmd_ping_status() {
    local en; en=$(systemctl --user is-enabled "$PING_UNIT" 2>/dev/null || printf 'disabled')
    say "timer:    $en"
    say "interval: $(ping_interval_now)"
    if [ -r "$PING_STATE" ]; then
        say "state:    $PING_STATE"
        sed 's/^/  /' "$PING_STATE"
    else
        say "state:    nothing checked yet"
    fi
}

usage() {
    cat <<HELP
syn-update $VERSION — update an installed SynapseOS from git

Usage:
  syn-update check          Fetch and show what would change (default, read-only)
  syn-update apply          Fetch, refresh pacman's databases, rebuild the
                            changed components, install them, and install any
                            component the tree has gained since.
                            It does NOT install software you left unticked at
                            install time, or removed later — see COMPONENTS YOU
                            DID NOT PICK below.
                            Asks for your password ONCE, up front, and holds it
                            for the whole build — sudo forgets after five
                            minutes and a full rebuild is much longer.
  syn-update apply <name>…  Only the components named. build-all.sh still walks
                            its own order, so the names are a filter and not a
                            sequence. Warns when a named component depends on
                            another that is also out of date and was not named.
                            Naming a component you do not have INSTALLS it, and
                            that answer is remembered.
  syn-update ignore <name>… Hold a component back: every check keeps reporting
                            its update, and no apply takes it. Naming it on an
                            apply still builds that one update, without
                            releasing the hold.
  syn-update unignore <name>…
                            Let one go again.
  syn-update ignored        What is being held back
  syn-update status         Show the source revision and installed versions
  syn-update ping           Check quietly and write the answer where the bar's
                            update indicator reads it. This is what the timer
                            runs; by hand it prints one line.
  syn-update ping --every <time>
                            How often that timer runs — systemd's own syntax,
                            so 30m, 6h, 12h, 1d and 1week all work. The setting
                            is a drop-in under ~/.config/systemd/user, so a
                            syn-update upgrade cannot revert it.
  syn-update ping --on|--off
                            Turn the background checking on or off. This is the
                            CHECKING; hiding the bar indicator is a separate
                            switch, in the bar's own menu (right-click the bar
                            ▸ Updates), because one is about network
                            traffic and the other is about furniture.
  syn-update help           This help

Options:
  --force                   With apply: discard local changes in the source tree
  --ref <branch>            Track a branch other than main

Environment:
  SYN_UPDATE_REPO           Source repository (default: the SynapseOS GitHub)
  SYN_UPDATE_REF            Branch to track (default: main)
  SYN_UPDATE_SRC            Where the source tree lives (default: /var/lib/synapse-src)
  SYN_UPDATE_MANIFEST       The component selection record
                            (default: /etc/synapseos/components.conf)
  SYN_UPDATE_DB_FRESH_SECS  How recently pacman's databases must have been synced
                            for apply to skip refreshing them (default: 600).
                            Past that it runs `pacman -Sy`, which downloads only
                            what changed; the expensive `-Syy` is kept for the
                            one case -Sy cannot fix — a cached signature older
                            than the database it signs — and for a -Sy that
                            comes back complaining about one.

COMPONENTS YOU DID NOT PICK

Every SynapseOS package is a checkbox in the installer, so "not installed" is
an answer and not a gap. syn-update keeps it: a component this machine has been
offered and does not have is listed in its own section of the report and left
alone, however far ahead of you the tree gets. Naming it installs it, once:

    syn-update apply vibe

and from then on it updates with everything else. Removing a component with
`pacman -R` is the same answer in the other direction and is kept the same way.

A component the tree has gained SINCE this machine last looked is different —
nobody has declined it, so it installs on its own. That is how a new component
the desktop depends on reaches a system that was installed before it existed.

The record is /etc/synapseos/components.conf, written by the installer and
rewritten by every apply. It is plain text and safe to edit.

Components are rebuilt from source with makepkg, so this needs base-devel and
each component's makedepends. Components with a large prebuilt payload
(synapse-llama, linux-wallpaperengine) are not updated this way and move with an
ISO upgrade instead; syn-update lists them rather than skipping quietly.

chibi is updated here despite its payload. Its first build fetches about half a
gigabyte of voice and speech models, kept in the component directory afterwards,
so the cost falls once per checkout rather than once per update.
HELP
}

# ── entry ────────────────────────────────────────────────────

FORCE=0
CMD=""
# `ping` with no flag runs a check; with one it configures the timer instead.
PING_EVERY=""
PING_TOGGLE=""
# Machine-readable output. Only `ignored` honours it today; every other command
# here is a report a person reads, and claiming a flag applies where it does
# not is worse than not having it.
TSV=0
while [ $# -gt 0 ]; do
    case "$1" in
        --force)        FORCE=1 ;;
        --tsv)          TSV=1 ;;
        --ref)          shift; REPO_REF="${1:-main}" ;;
        -h|--help|help) usage; exit 0 ;;
        check|apply|status|ignore|unignore|ignored|ping) CMD="$1" ;;
        --every)        shift; PING_EVERY="${1:-}" ;;
        --on)           PING_TOGGLE=on ;;
        --off)          PING_TOGGLE=off ;;
        # Bare words after `apply`, `ignore` or `unignore` are component names.
        # Validated below rather than here, because COMPONENTS describes the
        # revision that would RUN and setup_src has not fetched yet.
        *)  case "$CMD" in
                apply|ignore|unignore) ;;
                ping) die "ping takes no component names (try: syn-update ping --every 6h)" ;;
                *) die "unknown argument: $1 (try: syn-update help)" ;;
            esac
            SELECT+=("$1") ;;
    esac
    shift
done

# A name that is not a component builds nothing and would exit 0 — the same
# silent no-op that froze syn-arsenal for two releases when it was in
# build-all.sh's KNOWN= list with no build rule. Refuse it by name instead, and
# say what the unbuildable ones are rather than pretending they are typos.
for _s in "${SELECT[@]}"; do
    # Naming one of these builds nothing: scan() drops a NEVER_ADD component
    # that is not installed before it can reach either list, so `apply` would
    # print "nothing to build" and exit 0 — the silent no-op the paragraph
    # above exists to refuse. Say where it actually comes from instead. Once it
    # IS installed the name is ordinary and updates it like any other.
    if [ -n "${NEVER_ADD[$_s]:-}" ] && ! pacman -Qq "$_s" >/dev/null 2>&1; then
        die "$_s is not added by an update:
    ${NEVER_ADD[$_s]}
  Install it with: ${NEVER_ADD_HOW[$_s]:-its own installer}"
    fi
    if [ -n "${UNSUPPORTED[$_s]:-}" ]; then
        die "$_s cannot be built on an installed system:
    ${UNSUPPORTED[$_s]}
  It moves with an ISO upgrade."
    fi
    case " ${COMPONENTS[*]} " in
        *" $_s "*) ;;
        *) die "$_s is not a SynapseOS component.
  Components: ${COMPONENTS[*]}" ;;
    esac
done

case "${CMD:-check}" in
    check)    cmd_check ;;
    apply)    cmd_apply ;;
    status)   cmd_status ;;
    ignore)   cmd_ignore ;;
    unignore) cmd_unignore ;;
    ignored)  cmd_ignored ;;
    ping)
        # The flags are the whole command when they are present: `--every`
        # configures and does not also check, because a `--every 1week` that
        # fetched immediately would be a surprising thing for a setting to do.
        if [ -n "$PING_EVERY" ]; then cmd_ping_every "$PING_EVERY"
        elif [ "$PING_TOGGLE" = on ];  then cmd_ping_on
        elif [ "$PING_TOGGLE" = off ]; then cmd_ping_off
        else cmd_ping
        fi ;;
esac
