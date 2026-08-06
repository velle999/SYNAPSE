#!/usr/bin/env bash
#
# arsenal-tui — the terminal half of SYNAPSE Arsenal (`syn arsenal`).
#
# Pure bash by design. fzf is not installed on SynapseOS and neither dialog nor
# whiptail is guaranteed on a fresh install, so depending on any of them would
# make the "works over SSH on a box you just built" case the one that breaks.
# Everything here is `read` and ANSI.
#
# NOTE: mouse reporting is never enabled. A TUI killed with the mouse still on
# leaves the terminal emitting escape garbage for every movement, and the user
# has to blind-type `reset` to recover.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

QUERY="${SYN_ARSENAL_QUERY:-/usr/lib/syn-arsenal/arsenal-query}"
[ -x "$QUERY" ] || QUERY="$(dirname "$0")/arsenal-query.sh"

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
    B=$'\e[1m'; DIM=$'\e[2m'; R=$'\e[0m'
    CY=$'\e[36m'; GN=$'\e[32m'; YL=$'\e[33m'; RD=$'\e[31m'
else
    B='' DIM='' R='' CY='' GN='' YL='' RD=''
fi

PAGE=${SYN_ARSENAL_PAGE:-20}

banner() {
    printf '%s\n' "${CY}${B}  SYNAPSE Arsenal${R} ${DIM}— BlackArch tooling${R}"
}

# ── Preflight ───────────────────────────────────────────────────────────────
# A disabled repo is the overwhelmingly likely first-run state, and "no
# categories" on its own reads as a broken app. Name the actual cause and the
# actual fix instead.
preflight() {
    local st state count keyring
    st=$("$QUERY" status) || { echo "${RD}arsenal: backend failed${R}" >&2; exit 1; }
    IFS=$'\t' read -r state count keyring <<<"$st"
    case "$state" in
        disabled)
            banner
            echo "  ${YL}BlackArch is not enabled on this system.${R}"
            echo "  Enable it with:  ${B}sudo syn arsenal --enable-repo${R}"
            exit 1 ;;
        unsynced)
            banner
            echo "  ${YL}BlackArch is configured but never synced.${R}"
            echo "  Run:  ${B}sudo pacman -Sy${R}"
            exit 1 ;;
    esac
    if [ "$keyring" = missing ]; then
        echo "  ${YL}warning:${R} blackarch-keyring is not installed — signing key" >&2
        echo "  rotations will not reach this machine. ${B}sudo pacman -S blackarch-keyring${R}" >&2
        echo >&2
    fi
    ARSENAL_COUNT="$count"
}

# ── Category list ───────────────────────────────────────────────────────────
pick_category() {
    local -a names=() counts=()
    local line n c
    while IFS=$'\t' read -r n c; do names+=("$n"); counts+=("$c"); done < <("$QUERY" categories)
    [ "${#names[@]}" -eq 0 ] && { echo "no categories found" >&2; return 1; }

    local off=0 total=${#names[@]}
    while :; do
        clear 2>/dev/null
        banner
        echo "  ${DIM}${total} categories · ${ARSENAL_COUNT} packages${R}"
        echo
        local i end=$(( off + PAGE ))
        [ "$end" -gt "$total" ] && end=$total
        for (( i = off; i < end; i++ )); do
            printf '  %s%3d%s  %-34s %s%5d%s\n' \
                "$CY" "$((i+1))" "$R" "${names[i]#blackarch-}" "$DIM" "${counts[i]}" "$R"
        done
        echo
        echo "  ${DIM}[number] open · [n]ext · [p]rev · [/]search · [q]uit${R}"
        printf '  > '
        read -r ans || return 0
        case "$ans" in
            q|Q) return 0 ;;
            n|N) [ "$end" -lt "$total" ] && off=$end ;;
            p|P) off=$(( off - PAGE )); [ "$off" -lt 0 ] && off=0 ;;
            /*)  local term="${ans#/}" j
                 clear 2>/dev/null; banner; echo "  ${DIM}matching '${term}'${R}"; echo
                 for (( j = 0; j < total; j++ )); do
                     case "${names[j]}" in *"$term"*)
                         printf '  %s%3d%s  %-34s %s%5d%s\n' \
                             "$CY" "$((j+1))" "$R" "${names[j]#blackarch-}" "$DIM" "${counts[j]}" "$R" ;;
                     esac
                 done
                 echo; printf '  ${DIM}enter number, or blank to go back${R}\n  > '
                 read -r ans || return 0
                 [[ "$ans" =~ ^[0-9]+$ ]] && show_packages "${names[$((ans-1))]}"
                 ;;
            '')  ;;
            *)   if [[ "$ans" =~ ^[0-9]+$ ]] && [ "$ans" -ge 1 ] && [ "$ans" -le "$total" ]; then
                     show_packages "${names[$((ans-1))]}"
                 fi ;;
        esac
    done
}

# ── Packages in a category ──────────────────────────────────────────────────
show_packages() {
    local group="$1"
    local -a pk=() inst=() ver=() desc=()
    local a b c d
    while IFS=$'\t' read -r a b c d; do
        pk+=("$a"); inst+=("$b"); ver+=("$c"); desc+=("$d")
    done < <("$QUERY" packages "$group")
    local total=${#pk[@]}
    [ "$total" -eq 0 ] && { echo "  (empty category)"; read -r -p "  enter to go back "; return 0; }

    local off=0
    while :; do
        clear 2>/dev/null
        banner
        echo "  ${B}${group#blackarch-}${R} ${DIM}· ${total} packages${R}"
        echo
        local i end=$(( off + PAGE ))
        [ "$end" -gt "$total" ] && end=$total
        for (( i = off; i < end; i++ )); do
            local mark="   "
            [ "${inst[i]}" = 1 ] && mark="${GN} ● ${R}"
            printf '  %s%3d%s%s %-24s %s%.46s%s\n' \
                "$CY" "$((i+1))" "$R" "$mark" "${pk[i]}" "$DIM" "${desc[i]}" "$R"
        done
        echo
        echo "  ${DIM}[n]ext · [p]rev · i <num> install · r <num> remove · [q]back${R}"
        printf '  > '
        read -r ans || return 0
        case "$ans" in
            q|Q|'') return 0 ;;
            n|N) [ "$end" -lt "$total" ] && off=$end ;;
            p|P) off=$(( off - PAGE )); [ "$off" -lt 0 ] && off=0 ;;
            i\ *|r\ *)
                local verb="${ans%% *}" num="${ans#* }"
                [[ "$num" =~ ^[0-9]+$ ]] || continue
                [ "$num" -ge 1 ] && [ "$num" -le "$total" ] || continue
                local target="${pk[$((num-1))]}"
                echo
                if [ "$verb" = i ]; then
                    echo "  ${B}installing ${target}${R}"
                    "$QUERY" install "$target" && inst[$((num-1))]=1
                else
                    echo "  ${B}removing ${target}${R}"
                    "$QUERY" remove "$target" && inst[$((num-1))]=0
                fi
                read -r -p "  enter to continue " ;;
        esac
    done
}

case "${1:-}" in
    -h|--help)
        cat <<EOF
Usage: syn arsenal

Browse and install BlackArch security tooling by category.
Options:
  --enable-repo   add the BlackArch repository (needs root)
EOF
        exit 0 ;;
    --enable-repo) exec "${QUERY%arsenal-query*}arsenal-enable-repo" ;;
esac

preflight
pick_category
clear 2>/dev/null
