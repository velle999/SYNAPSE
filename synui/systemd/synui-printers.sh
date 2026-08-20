#!/bin/bash
# synui-printers — find network printers and set them up.
#
# SynapseOS could open CUPS's web interface (the control panel's Printers row,
# localhost:631) and nothing else. That page is a complete admin UI and it is
# also the wrong first answer to "I have a printer on my network": it asks you
# to pick a discovery protocol and a driver, and the whole point of a modern
# network printer is that neither question has an interesting answer any more.
#
# ── What it does ────────────────────────────────────────────────────────────
#
#   scan            what is on the network, and which of it is already set up
#   add [--auto]    add the ones that are not
#
# ── Why lpinfo and not avahi ────────────────────────────────────────────────
#
# CUPS's own dnssd backend already browses mDNS and, crucially, already knows
# how to turn what it finds into a DEVICE URI it will accept. `avahi-browse`
# would give the same announcements and leave this script assembling ipp:// URIs
# out of TXT records by hand — a second, worse implementation of a thing cupsd
# does correctly, and one that would drift the first time a printer announced
# something unusual. avahi is still consulted for one thing: telling "there are
# no printers" apart from "cupsd is not browsing", which look identical from
# here and have completely different fixes.
#
# ── Driverless only, on purpose ─────────────────────────────────────────────
#
# `-m everywhere` is IPP Everywhere: the printer describes its own capabilities
# and CUPS builds the queue from that. Every network printer sold in the last
# decade does it. This script deliberately does NOT go hunting for PPDs or
# vendor drivers — that path needs a human decision (velle's own M2020W is an
# SPL printer that needs the Samsung ULD driver and is packaged separately,
# see SYNAPSE/samsung-m2020), and a script that guessed would install the wrong
# filter and produce forty blank pages.
#
# ── Privilege ───────────────────────────────────────────────────────────────
#
# lpadmin is refused for anyone outside cupsd's SystemGroup, which on Arch is
# `sys` and contains nobody. So: TRY IT FIRST — a machine whose policy already
# allows it must not be made to authenticate — and fall back to pkexec, which
# is how syn-disks and syn-model escalate. Never sudo: there is no terminal to
# type a password into when this is launched from a menu.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

notify=0
auto=0
me=${0##*/}

say() { printf '%s\n' "$*"; }
note() {
    # A toast only when asked. Launched from a menu there is no terminal to
    # print to, and a setup that reports nothing is indistinguishable from one
    # that did not run.
    [ "$notify" -eq 1 ] && command -v notify-send >/dev/null 2>&1 &&
        notify-send -a synui "Printers" "$1"
    say "$1"
}

usage() {
    cat <<EOF
usage: $me scan
       $me add [--auto] [URI…]

  scan              list network printers, marking the ones already set up
  add URI…          add those printers (driverless, IPP Everywhere)
  add --auto        add every discovered printer that is not set up yet

  --notify          also report through a desktop notification
EOF
}

# ── Discovery ────────────────────────────────────────────────
#
# lpinfo prints "network ipp://host/ipp/print" or, for a dnssd discovery,
# 'network dnssd://Name._ipp._tcp.local/…' followed by the make and model in
# quotes. Two fields matter: the URI, and whatever human name can be salvaged.
discover() {
    command -v lpinfo >/dev/null 2>&1 || return 0
    # -l is the long form: one field per line, including `info`, which is the
    # printer's own name for itself. The short form gives only the URI and a
    # bare model string, and a queue called "HP_LaserJet" where the printer
    # calls itself "Office Laser" is a queue nobody recognises.
    #
    # ⚠ THE FIELD NAMES ARE `uri` AND `info`, indented under a "Device:" line —
    # NOT device-uri/device-info, which is what the first version of this looked
    # for and is what the IPP ATTRIBUTES are called. Nothing would ever have
    # matched, and the symptom would have been "no network printers found" on a
    # network with printers on it: the same silent shape as the mDNS flag bug in
    # synfiles' netscan. Verified against the binary's own format strings:
    #
    #     Device: uri = %s
    #             class = %s
    #             info = %s
    #             make-and-model = %s
    #
    # make-and-model is the fallback for a device that reports no info, which is
    # common for a raw socket:// queue.
    timeout 25 lpinfo --include-schemes dnssd,ipp,ipps,socket -l -v 2>/dev/null |
    awk '
        function flush(  n) {
            if (uri == "") return
            n = info; if (n == "") n = model
            print uri "\t" n
            uri = ""; info = ""; model = ""
        }
        /^Device: uri = /            { flush(); uri = substr($0, 15) }
        /^[ \t]*info = /             { info = substr($0, index($0, "= ") + 2) }
        /^[ \t]*make-and-model = /   { model = substr($0, index($0, "= ") + 2) }
        END { flush() }
    '
}

# Device URIs CUPS already has a queue for.
configured_uris() {
    command -v lpstat >/dev/null 2>&1 || return 0
    lpstat -v 2>/dev/null | sed -n 's/^device for [^:]*: //p'
}

# A CUPS queue name: letters, digits, underscore and dash only. CUPS refuses a
# name with a space, a slash or a '#' in it, and a printer that announces itself
# as "Kenzie's Printer (Office)" is the normal case, not the odd one.
#
# Quotes are DROPPED and everything else is collapsed to one underscore, which
# is the difference between "Kenzies_Printer_Spare" and "Kenzie_s_Printer_Spare".
# This name is what the print dialogue of every application shows for the rest
# of the printer's life, so the ugly-but-correct answer is not good enough.
queue_name() {
    printf '%s' "$1" |
        sed -e "s/['\`\"]//g" -e 's/[^A-Za-z0-9_-]\+/_/g' -e 's/^_//' -e 's/_$//' |
        cut -c1-40
}

# ── Adding ───────────────────────────────────────────────────
#
# Returns 0 if the queue exists afterwards. Tries unprivileged first: a box
# whose cupsd policy already permits this must not be made to authenticate for
# no reason.
add_printer() {
    local name=$1 uri=$2

    if lpadmin -p "$name" -E -v "$uri" -m everywhere 2>/dev/null; then
        return 0
    fi

    if command -v pkexec >/dev/null 2>&1; then
        # pkexec needs an absolute path — it does not search PATH.
        local lpadmin_bin
        lpadmin_bin=$(command -v lpadmin) || return 1
        if pkexec "$lpadmin_bin" -p "$name" -E -v "$uri" -m everywhere 2>/dev/null; then
            return 0
        fi
    fi
    return 1
}

cmd_scan() {
    local found=0 conf uri info name
    conf=$(configured_uris)

    while IFS=$'\t' read -r uri info; do
        [ -n "$uri" ] || continue
        found=$((found + 1))
        name=$(queue_name "${info:-$uri}")
        if printf '%s\n' "$conf" | grep -qxF "$uri"; then
            say "set up    ${info:-$uri}"
            say "          $uri"
        else
            say "found     ${info:-$uri}  ->  $name"
            say "          $uri"
        fi
    done <<< "$(discover)"

    if [ "$found" -eq 0 ]; then
        say "no network printers found."
        # THE TWO CASES THAT LOOK IDENTICAL. If avahi can see printers and cupsd
        # offered none, the problem is cupsd's browsing (or its dnssd backend),
        # not the network — and that is a completely different thing to go and
        # fix.
        if command -v avahi-browse >/dev/null 2>&1; then
            local seen
            seen=$(timeout 8 avahi-browse -ptrk _ipp._tcp 2>/dev/null | grep -c '^=')
            if [ "${seen:-0}" -gt 0 ]; then
                say ""
                say "⚠ but avahi sees $seen IPP announcement(s) — cupsd is not browsing them."
                say "  Check that cups.service is running and that avahi-daemon is up."
            fi
        fi
        return 100
    fi
    return 0
}

cmd_add() {
    local wanted=("$@")
    local conf added=0 skipped=0 failed=0 uri info name first=""
    conf=$(configured_uris)

    while IFS=$'\t' read -r uri info; do
        [ -n "$uri" ] || continue

        if [ "$auto" -eq 0 ]; then
            local match=0 w
            for w in "${wanted[@]}"; do
                [ "$w" = "$uri" ] && match=1
            done
            [ "$match" -eq 1 ] || continue
        fi

        if printf '%s\n' "$conf" | grep -qxF "$uri"; then
            skipped=$((skipped + 1))
            continue
        fi

        name=$(queue_name "${info:-printer}")
        [ -n "$name" ] || name="printer"

        if add_printer "$name" "$uri"; then
            say "added $name ($uri)"
            added=$((added + 1))
            [ -z "$first" ] && first=$name
        else
            say "could not add $name ($uri) — lpadmin refused" >&2
            failed=$((failed + 1))
        fi
    done <<< "$(discover)"

    # A default, but only if there is none. Overwriting somebody's default
    # printer because a new one appeared on the network is exactly the kind of
    # "helpful" that gets a document printed in another room.
    if [ -n "$first" ] && ! lpstat -d 2>/dev/null | grep -q 'system default destination: '; then
        lpadmin -d "$first" 2>/dev/null ||
            { command -v pkexec >/dev/null 2>&1 &&
              pkexec "$(command -v lpadmin)" -d "$first" 2>/dev/null; } || true
        say "default printer: $first"
    fi

    if [ "$added" -gt 0 ]; then
        note "$added printer(s) added$([ "$skipped" -gt 0 ] && echo ", $skipped already set up")"
        return 0
    fi
    if [ "$failed" -gt 0 ]; then
        note "could not add $failed printer(s) — authorisation refused"
        return 1
    fi
    if [ "$skipped" -gt 0 ]; then
        note "every printer on this network is already set up"
        return 0
    fi
    note "no new network printers found"
    return 100
}

cmd=""
args=()
for a in "$@"; do
    case $a in
        --notify) notify=1 ;;
        --auto)   auto=1 ;;
        -h|--help) usage; exit 0 ;;
        scan|add) [ -z "$cmd" ] && cmd=$a || args+=("$a") ;;
        *)        args+=("$a") ;;
    esac
done

case ${cmd:-} in
    scan) cmd_scan ;;
    add)
        if [ "$auto" -eq 0 ] && [ ${#args[@]} -eq 0 ]; then
            echo "$me: add needs a URI, or --auto" >&2
            usage >&2
            exit 2
        fi
        cmd_add "${args[@]+"${args[@]}"}"
        ;;
    *) usage >&2; exit 2 ;;
esac
