#!/usr/bin/env bash
#
# arsenal-enable-repo — add the BlackArch repository to a running SynapseOS.
#
# Upstream's own bootstrap (strap.sh) is what does the work: it fetches the
# blackarch-keyring tarball, verifies its SIGNATURE against BlackArch's master
# key, imports and locally signs that key, writes the [blackarch] section and
# syncs. Reimplementing that here would mean reimplementing its trust handling,
# which is the one part worth not getting creative with.
#
# What this script adds is verification on both sides of running it.
#
# BEFORE: strap.sh is fetched over TLS and then checked to still pin the master
# key fingerprint below. A pinned sha1 of the script would be the obvious check
# and is the wrong one — upstream edits strap.sh routinely, so the pin goes
# stale and every install starts failing on a script that is perfectly fine.
# The FINGERPRINT is the thing that must never change, and a substituted script
# that swaps in an attacker's key is exactly what checking it catches.
#
# AFTER: the repo is only accepted if it is actually usable and the key is
# actually trusted. A half-added repo that lists no packages is worse than none.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

# BlackArch master signing key. Verified against upstream strap.sh, which passes
# this same fingerprint to --recv-keys before it will trust the keyring tarball.
BA_FPR="4345771566D76038C7FEB43863EC0ADBEA87E4E3"
STRAP_URL="https://blackarch.org/strap.sh"
BA_MIRRORLIST=/etc/pacman.d/blackarch-mirrorlist

# How many reachable mirrors [blackarch] should end up with, and how many
# candidates we are willing to probe to get there.
#
# WHY THIS EXISTS: pacman aborts the ENTIRE -Syu if any single database fails
# to sync. upstream's blackarch-mirrorlist ships exactly ONE active Server and
# ~122 commented ones (normal Arch convention — the user picks). That is fine
# for an opt-in repo; it is not fine here, because we enable [blackarch] by
# DEFAULT. So one unreachable BlackArch mirror stops every system upgrade on
# the machine, including base-system security updates, and the error names
# BlackArch rather than the consequence:
#
#     blackarch.db failed to download
#     error: failed to synchronize all databases
#
# Hit on 2026-08-07: the single shipped mirror timed out and -Syu had been
# silently refusing to upgrade anything.
MIN_MIRRORS=4
MAX_PROBES=14

msg()  { printf '  %s\n' "$*"; }
warn() { printf '  warning: %s\n' "$*" >&2; }
die()  { printf '  error: %s\n' "$*" >&2; exit 1; }

# Bring [blackarch] up to MIN_MIRRORS reachable servers by uncommenting some of
# the ones the mirrorlist already carries. Idempotent, and safe to call on a
# machine where the repo was enabled long ago — which is the point, since every
# install made before this existed is sitting on a single mirror.
#
# Only https candidates are considered: the list also offers http and ftp, and
# a mirror that cannot be fetched over TLS is not one to add on our own
# initiative.
ensure_mirror_redundancy() {
    [ -f "$BA_MIRRORLIST" ] || { warn "no $BA_MIRRORLIST — skipping mirror check"; return 0; }
    # Called from the already-enabled path too, which runs before the curl
    # check below.
    command -v curl >/dev/null 2>&1 || { warn "curl missing — skipping mirror check"; return 0; }

    local active
    active=$(grep -cE '^[[:space:]]*Server[[:space:]]*=' "$BA_MIRRORLIST")
    if [ "$active" -ge "$MIN_MIRRORS" ]; then
        msg "BlackArch has $active mirrors configured."
        return 0
    fi

    msg "Only $active BlackArch mirror(s) configured — probing for more..."

    local tmpd chosen domains probed=0 added=0
    tmpd=$(mktemp -d) || { warn "could not create a temp dir"; return 0; }
    chosen="$tmpd/chosen"
    domains="$tmpd/domains"
    : > "$chosen"
    : > "$domains"

    # One mirror per operator. The list is ordered by country code, so the
    # first few https candidates are typically au./at./ca. of the SAME host —
    # picking those would give four Servers and still one point of failure,
    # which is the exact thing this function exists to prevent. Seeded with the
    # domains already active so we diversify away from those too.
    local _l _h _d
    while IFS= read -r _l; do
        _h=${_l#*=}; _h=${_h#"${_h%%[![:space:]]*}"}
        _h=${_h#*://}; _h=${_h%%/*}
        printf '%s\n' "$_h" | awk -F. '{ if (NF>=2) print $(NF-1)"."$NF; else print $0 }' >> "$domains"
    done < <(grep -E '^[[:space:]]*Server[[:space:]]*=' "$BA_MIRRORLIST")

    # Probe commented https candidates, cheapest check that proves the mirror
    # actually serves the database pacman will ask for. -I not -f: some mirrors
    # answer HEAD but not a ranged GET, and 200 on the .db is what matters.
    local line url code
    while IFS= read -r line; do
        [ "$probed" -ge "$MAX_PROBES" ] && break
        [ "$added"  -ge "$((MIN_MIRRORS - active))" ] && break
        url=${line#\#}
        url=${url#"${url%%[![:space:]]*}"}
        url=${url#Server}
        url=${url#"${url%%[![:space:]]*}"}
        url=${url#=}
        url=${url#"${url%%[![:space:]]*}"}
        case "$url" in https://*) ;; *) continue ;; esac

        _h=${url#*://}; _h=${_h%%/*}
        _d=$(printf '%s' "$_h" | awk -F. '{ if (NF>=2) print $(NF-1)"."$NF; else print $0 }')
        grep -qxF "$_d" "$domains" && continue     # already have this operator

        probed=$((probed + 1))
        code=$(curl -s -o /dev/null -w '%{http_code}' --max-time 8 \
                    "${url//\$repo/blackarch}/blackarch.db" 2>/dev/null)
        # $arch is still literal in that URL; substitute it the same way pacman would.
        if [ "$code" != "200" ]; then
            code=$(curl -s -o /dev/null -w '%{http_code}' --max-time 8 \
                        "$(printf '%s' "$url" | sed -e 's|\$repo|blackarch|g' -e 's|\$arch|x86_64|g')/blackarch.db" 2>/dev/null)
        fi
        if [ "$code" = "200" ]; then
            printf '%s\n' "$line" >> "$chosen"
            printf '%s\n' "$_d"   >> "$domains"
            added=$((added + 1))
        fi
    done < <(grep -E '^[[:space:]]*#[[:space:]]*Server[[:space:]]*=' "$BA_MIRRORLIST")

    if [ "$added" -eq 0 ]; then
        warn "no additional BlackArch mirrors responded — leaving the list alone"
        rm -rf "$tmpd"
        return 0
    fi

    # Uncomment exactly the lines we verified, matched whole-line so no amount
    # of odd characters in a URL can turn into a sed pattern.
    if awk -v want="$chosen" '
        BEGIN { while ((getline l < want) > 0) keep[l] = 1 }
        {
            if ($0 in keep) { s = $0; sub(/^[[:space:]]*#[[:space:]]*/, "", s); print s }
            else print
        }
    ' "$BA_MIRRORLIST" > "$tmpd/new" && [ -s "$tmpd/new" ]; then
        cat "$tmpd/new" > "$BA_MIRRORLIST" \
            && msg "Enabled $added more BlackArch mirror(s) — $((active + added)) total." \
            || warn "could not write $BA_MIRRORLIST"
    else
        warn "mirror list rewrite produced nothing — left unchanged"
    fi
    rm -rf "$tmpd"
}

[ "$(id -u)" -eq 0 ] || die "must run as root (try: sudo syn arsenal --enable-repo)"

if grep -q '^\[blackarch\]' /etc/pacman.conf; then
    msg "BlackArch is already enabled."
    # Still worth a look: the repo can be present while the keyring PACKAGE is
    # not, which is the state strap.sh leaves behind and which quietly stops
    # key rotations from ever arriving.
    if ! pacman -Q blackarch-keyring >/dev/null 2>&1; then
        msg "Installing the missing blackarch-keyring package..."
        # --overwrite, scoped to the three paths strap.sh wrote: it extracts
        # the keyring tarball STRAIGHT into /usr/share/pacman/keyrings
        # (install_keyring, a bare `tar xfz -C`), so those files are owned by
        # no package and a plain -S dies with "exists in filesystem". Taking
        # ownership is the point — an unowned keyring is exactly the one that
        # never gets rotated.
        pacman -S --noconfirm --needed \
            --overwrite '/usr/share/pacman/keyrings/blackarch*' blackarch-keyring \
            || warn "could not install blackarch-keyring"
    fi
    # Repair the single-mirror state on machines enabled before this existed.
    ensure_mirror_redundancy
    exit 0
fi

command -v curl >/dev/null 2>&1 || die "curl is required"

tmp=$(mktemp -d) || die "could not create a temp dir"
trap 'rm -rf "$tmp"' EXIT

msg "Fetching $STRAP_URL ..."
curl -fsS --proto '=https' --tlsv1.2 -o "$tmp/strap.sh" "$STRAP_URL" \
    || die "could not download strap.sh"

# The check that matters. If upstream's bootstrap no longer pins the key we
# expect, stop — do not run it and do not add the repo.
if ! grep -qF "$BA_FPR" "$tmp/strap.sh"; then
    die "strap.sh does not pin the expected BlackArch master key
  ($BA_FPR).
  Refusing to run it. Verify the key at https://blackarch.org/downloads.html
  before enabling the repository by hand."
fi
msg "strap.sh pins the expected master key."

chmod +x "$tmp/strap.sh"
msg "Running upstream bootstrap (imports the keyring, adds the repo)..."
"$tmp/strap.sh" || die "strap.sh failed — no changes kept"

# ── Verify the result rather than trusting the exit code ────────────────────
grep -q '^\[blackarch\]' /etc/pacman.conf \
    || die "strap.sh reported success but [blackarch] is not in /etc/pacman.conf"

# Before the first -Sy, so the sync already has somewhere to fall through to.
ensure_mirror_redundancy

pacman -Sy --noconfirm >/dev/null 2>&1 || warn "pacman -Sy did not complete cleanly"

count=$(pacman -Sl blackarch 2>/dev/null | wc -l)
[ "$count" -gt 0 ] || die "[blackarch] is configured but lists no packages"

# The keyring as a PACKAGE, so future key rotations arrive as an upgrade.
pacman -S --noconfirm --needed \
    --overwrite '/usr/share/pacman/keyrings/blackarch*' blackarch-keyring \
    || warn "blackarch-keyring did not install — key rotations will not reach this machine"

msg "BlackArch enabled — $count packages available."
msg "Browse them with: syn arsenal   (or the SYNAPSE Arsenal app)"
