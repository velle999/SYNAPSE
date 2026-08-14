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
            nexus-chat tepris vibe samsung-m2020 syn-arsenal synpkg synfiles
            syn-settings syn-disks syn-edit syn-confine limine-mkinitcpio-hook
            fetch synapse-wallpapers)

# On the ISO but NOT updatable this way, with the reason. Reported rather than
# skipped in silence: a component quietly frozen forever is exactly the bug
# this tool exists to fix, and reintroducing it for a subset would be worse
# than not covering them, because nothing would say so.
declare -A UNSUPPORTED=(
    [synapse-llama]="packaged from a tree that archiso/build.sh --llama-only stages; there is no staging tree on an installed system"
    [linux-wallpaperengine]="cmake fetches a ~1.3 GB CEF blob and needs ~6 GB of scratch to build; its synui-wpengine control script now lives in synui, which IS updatable"
    [chibi]="371 MB of bundled voice models; rebuild cost is all download, no source change"
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
)

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

setup_src() {
    if [ -d "$SRC/.git" ]; then
        # Someone else's tree, or one left root-owned by an older run: makepkg
        # writes into it, so it has to be ours.
        if [ ! -w "$SRC" ]; then
            can_sudo || die "$SRC is not writable by $(id -un), and this session cannot ask for a password.
  Run this once in a terminal:  sudo chown -R $(id -un):$(id -gn) $SRC"
            info "taking ownership of $SRC"
            sudo chown -R "$(id -un):$(id -gn)" "$SRC" || die "cannot take ownership of $SRC"
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
    if [ ! -d "$SRC" ]; then
        can_sudo || die "$SRC does not exist yet, and this session cannot ask for a password.
  Run this once in a terminal:  sudo install -d -o $(id -un) -g $(id -gn) $SRC"
        sudo install -d -o "$(id -un)" -g "$(id -gn)" "$SRC" || die "cannot create $SRC"
    elif [ ! -w "$SRC" ]; then
        can_sudo || die "$SRC is not writable by $(id -un), and this session cannot ask for a password.
  Run this once in a terminal:  sudo chown $(id -un):$(id -gn) $SRC"
        sudo chown "$(id -un):$(id -gn)" "$SRC" || die "cannot take ownership of $SRC"
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

# Component names named on the command line: `syn-update apply synui`. Empty
# means "everything that changed", which is what apply has always done and
# stays the default.
SELECT=()
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

scan() {
    CHANGED=(); NEW=(); SKIPPED=(); BLOCKED=()

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
            [ -n "${NEVER_ADD[$c]:-}" ] || NEW+=("$c $avail")
            continue
        fi

        # vercmp, not string compare: 0.1.0-203 vs 0.1.0-99 sorts the wrong way
        # as text, and that mistake silently declines to ship an update.
        if [ "$(vercmp "$avail" "$inst")" -gt 0 ]; then
            CHANGED+=("$c $inst $avail")
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
    if [ ${#CHANGED[@]} -eq 0 ] && [ ${#NEW[@]} -eq 0 ]; then
        ok "everything build-all.sh can update is already current"
    elif [ ${#CHANGED[@]} -eq 0 ]; then
        ok "every installed component is current"
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

    # Scan against what upstream is OFFERING, which means reading the PKGBUILDs
    # at the remote revision, not the checked-out one. Done without moving the
    # tree so `check` stays read-only.
    local tmp; tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' RETURN
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

    report
    say ""
    { [ ${#CHANGED[@]} -gt 0 ] || [ ${#NEW[@]} -gt 0 ]; } &&
        say "Run ${C_B}syn-update apply${C_R} to build and install them."
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

    if [ ${#CHANGED[@]} -eq 0 ] && [ ${#NEW[@]} -eq 0 ]; then
        say ""
        ok "nothing to build"
        return 0
    fi

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

    # ONE invocation, all components: build-all.sh walks its own fixed order and
    # skips what was not asked for, so dependencies come out right even though
    # the arguments are unordered. Calling it once per package would build them
    # in the order given, which is exactly the wrong thing.
    BUILT=("${names[@]}")
    ( cd "$SRC" && ./build-all.sh "${names[@]}" ) || die "build failed — the tree is at $(git -C "$SRC" rev-parse --short HEAD), nothing was rolled back"

    refresh_local_repo
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
        sudo cp -f "$pkg" "$LOCAL_REPO/" && copied=$((copied + 1))
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
            sudo cp -f "$pkg" "$LOCAL_REPO/" &&
                reconciled=$((reconciled + 1))
        done
    done

    prune_superseded
    rebuild_db
    sudo pacman -Sy --quiet >/dev/null 2>&1 || true
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
        sudo rm -f "$LOCAL_REPO"/synapseos.db* "$LOCAL_REPO"/synapseos.files*
        warn "$LOCAL_REPO now holds no packages; [synapseos] is empty"
        return 0
    fi

    sudo rm -f "$LOCAL_REPO"/synapseos.db* "$LOCAL_REPO"/synapseos.files*
    sudo repo-add --quiet "$LOCAL_REPO/synapseos.db.tar.gz" "${pkgs[@]}" >/dev/null 2>&1 ||
        { warn "repo-add failed; [synapseos] index may be incomplete"; return 0; }

    # syn-install creates these by hand at install time, so recreate them here
    # rather than assuming repo-add left them behind.
    sudo ln -sf synapseos.db.tar.gz    "$LOCAL_REPO/synapseos.db"
    sudo ln -sf synapseos.files.tar.gz "$LOCAL_REPO/synapseos.files"
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
            sudo mkdir -p "$LOCAL_REPO/superseded"
            sudo mv -f "$f" "$LOCAL_REPO/superseded/" && moved=$((moved + 1))
        fi
    done
    shopt -u nullglob

    [ "$moved" -gt 0 ] &&
        info "retired $moved package file(s) older than what is installed -> $LOCAL_REPO/superseded"
    return 0
}

cmd_status() {
    need_tools
    [ -d "$SRC/.git" ] || die "no source tree yet — run: syn-update check"

    say "source:    $SRC"
    say "remote:    $REPO_URL ($REPO_REF)"
    say "revision:  $(git -C "$SRC" rev-parse --short HEAD 2>/dev/null) $(git -C "$SRC" log -1 --format='%s' 2>/dev/null)"
    say ""
    printf '  %-16s %s\n' COMPONENT INSTALLED
    local c inst
    for c in "${COMPONENTS[@]}"; do
        inst=$(pacman -Q "$c" 2>/dev/null | awk '{print $2}')
        printf '  %-16s %s\n' "$c" "${inst:-${C_DIM}not installed${C_R}}"
    done
}

usage() {
    cat <<HELP
syn-update $VERSION — update an installed SynapseOS from git

Usage:
  syn-update check          Fetch and show what would change (default, read-only)
  syn-update apply          Fetch, rebuild the changed components, install them,
                            and install any component the tree has gained since
  syn-update apply <name>…  Only the components named. build-all.sh still walks
                            its own order, so the names are a filter and not a
                            sequence. Warns when a named component depends on
                            another that is also out of date and was not named.
  syn-update status         Show the source revision and installed versions
  syn-update help           This help

Options:
  --force                   With apply: discard local changes in the source tree
  --ref <branch>            Track a branch other than main

Environment:
  SYN_UPDATE_REPO           Source repository (default: the SynapseOS GitHub)
  SYN_UPDATE_REF            Branch to track (default: main)
  SYN_UPDATE_SRC            Where the source tree lives (default: /var/lib/synapse-src)

Components are rebuilt from source with makepkg, so this needs base-devel and
each component's makedepends. Components with a large prebuilt payload
(synapse-llama, linux-wallpaperengine, chibi) are not updated this way and move
with an ISO upgrade instead; syn-update lists them rather than skipping quietly.
HELP
}

# ── entry ────────────────────────────────────────────────────

FORCE=0
CMD=""
while [ $# -gt 0 ]; do
    case "$1" in
        --force)        FORCE=1 ;;
        --ref)          shift; REPO_REF="${1:-main}" ;;
        -h|--help|help) usage; exit 0 ;;
        check|apply|status) CMD="$1" ;;
        # Bare words after `apply` are component names. Validated below rather
        # than here, because COMPONENTS describes the revision that would RUN
        # and setup_src has not fetched yet at this point.
        *)  [ "$CMD" = apply ] ||
                die "unknown argument: $1 (try: syn-update help)"
            SELECT+=("$1") ;;
    esac
    shift
done

# A name that is not a component builds nothing and would exit 0 — the same
# silent no-op that froze syn-arsenal for two releases when it was in
# build-all.sh's KNOWN= list with no build rule. Refuse it by name instead, and
# say what the unbuildable ones are rather than pretending they are typos.
for _s in "${SELECT[@]}"; do
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
    check)  cmd_check ;;
    apply)  cmd_apply ;;
    status) cmd_status ;;
esac
