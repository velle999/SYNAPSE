#!/bin/sh
# netscan_test.sh — network discovery, driven against fake announcers
#
# The real thing cannot be tested on a build machine: a network either has a
# NAS on it or it does not, and neither answer proves the parser. So every
# discovery tool is faked on PATH and the binary is run for real.
#
# ⚠ THE CASE THIS EXISTS FOR IS THE SILENT ONE. The first version of netscan.c
# passed `-pterk` to avahi-browse — one letter that is not an option. avahi
# answered "invalid option -- 'e'" on a stderr that was being discarded, exited
# 1, and printed nothing, so the scan reported "nothing announced itself on
# this network". That is a plausible answer and it was wrong, and no test that
# checks "does a real scan find things" would ever have caught it, because on
# most networks the right answer IS empty. A discovery tool has to distinguish
# "nothing is there" from "I could not look".
#
# Usage: netscan_test.sh <path/to/synfiles>
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

bin=${1:?usage: netscan_test.sh <synfiles binary>}
case $bin in /*) ;; *) bin=$PWD/$bin ;; esac

fails=0
ok()  { printf '  ok    %s\n' "$1"; }
bad() { printf '  FAIL  %s\n' "$1"; fails=$((fails + 1)); }

tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM
mkdir -p "$tmp/bin"

# ── The fakes ────────────────────────────────────────────────
#
# avahi announces one NAS over SMB, with the two shapes that broke earlier
# versions: a service name with avahi's \032 escape for a space, and a
# hostname with the trailing dot that is correct DNS and wrong in a URI.
cat > "$tmp/bin/avahi-browse" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$AVAHI_LOG"
[ -n "${AVAHI_FAIL:-}" ] && { echo "avahi-browse: invalid option" >&2; exit 1; }
case " $* " in
*" _smb._tcp "*)
  echo '=;wlan0;IPv4;Front\032Room\032NAS;_smb._tcp;local;nas.local.;192.168.40.10;445;"txt"'
  ;;
*" _sftp-ssh._tcp "*)
  echo '=;wlan0;IPv4;buildbox;_sftp-ssh._tcp;local;buildbox.local;192.168.40.11;22;"txt"'
  ;;
esac
exit 0
EOF

# NetBIOS answers with the SAME machine (by its NetBIOS name) plus a Windows
# box that announces nothing over mDNS — the case the second source exists for.
cat > "$tmp/bin/nmblookup" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$NMB_LOG"
case " $* " in
*" -A 192.168.40.10 "*) printf '\tNAS             <00> -         B <ACTIVE>\n' ;;
*" -A 192.168.40.99 "*) printf '\t__MSBROWSE__    <01> - <GROUP> B <ACTIVE>\n'
                        printf '\tKENZIE-PC       <00> -         B <ACTIVE>\n' ;;
*" -A "*)               echo "No reply" ;;
*)                      echo "192.168.40.10 *<00>"
                        echo "192.168.40.99 *<00>" ;;
esac
exit 0
EOF

cat > "$tmp/bin/smbclient" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$SMB_LOG"
case " $* " in
*"//KENZIE-PC"*) echo "Disk|Users|"; echo "Disk|C\$|Default share" ;;
*)               echo "Disk|media|Films and telly"
                 echo "Disk|backups|"
                 echo "IPC|IPC\$|IPC Service"
                 echo "Disk|print\$|Printer Drivers" ;;
esac
exit 0
EOF

cat > "$tmp/bin/gio" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$GIO_LOG"
exit 0
EOF

chmod +x "$tmp/bin"/*
AVAHI_LOG=$tmp/avahi.log NMB_LOG=$tmp/nmb.log SMB_LOG=$tmp/smb.log GIO_LOG=$tmp/gio.log
export AVAHI_LOG NMB_LOG SMB_LOG GIO_LOG
: > "$AVAHI_LOG"; : > "$NMB_LOG"; : > "$SMB_LOG"; : > "$GIO_LOG"

run() { PATH="$tmp/bin:$PATH" "$bin" "$@"; }

out=$(run --rec netscan 2>/dev/null)

# ── Hosts ────────────────────────────────────────────────────
# ⚠ The uri field is PERCENT-ENCODED, like every other field in these records —
# "smb%3A//nas.local". That is the program's one rule for record fields (the GUI
# decodes every field once, at the parse), and a test that greps for the plain
# form is testing its own assumption rather than the output.
printf '%s' "$out" | grep -q 'smb%3A//nas.local	host' \
    && ok "the mDNS NAS is offered as a host" \
    || bad "no host row for the mDNS NAS"

printf '%s' "$out" | grep -q 'nas\.local\.' \
    && bad "the trailing dot on the mDNS hostname reached the URI" \
    || ok "the mDNS trailing dot is stripped from the URI"

printf '%s' "$out" | grep -q 'Front Room NAS' \
    && ok "avahi's \\032 escape is decoded to a space" \
    || bad "the service name still carries avahi's \\032 escape"

printf '%s' "$out" | grep -q 'sftp%3A//buildbox.local' \
    && ok "SFTP is discovered as well as SMB" \
    || bad "the _sftp-ssh._tcp host is missing"

printf '%s' "$out" | grep -q 'smb%3A//KENZIE-PC' \
    && ok "the NetBIOS-only Windows machine is found" \
    || bad "NetBIOS discovery found nothing — the Windows half is dead"

printf '%s' "$out" | grep -q '__MSBROWSE__' \
    && bad "the browser election was listed as a host" \
    || ok "__MSBROWSE__ is not offered as a host"

# ── Shares ───────────────────────────────────────────────────
printf '%s' "$out" | grep -q 'smb%3A//nas.local/media	share' \
    && ok "a share is a row of its own, with a real URI" \
    || bad "shares were not enumerated"

printf '%s' "$out" | grep -q 'media on Front Room NAS' \
    && ok "a share is titled by share and server" \
    || bad "the share title is wrong"

for admin in 'IPC%24' 'IPC\$' 'print%24' 'print\$' 'C%24' 'C\$'; do
    printf '%s' "$out" | grep -q "$admin" \
        && bad "administrative share $admin was offered as a place"
done
ok "administrative shares (IPC\$, print\$, C\$) are not offered"

# The same machine announced by BOTH sources must appear once.
# awk on the FIELD, not grep on the line: the header row is literally
# "uri<TAB>kind<TAB>title<TAB>icon<TAB>host<TAB>…", so a grep for a tab-wrapped
# "host" counts it and every count is one too many.
n=$(printf '%s\n' "$out" | awk -F'\t' '$2=="host"' | wc -l)
[ "$n" -eq 3 ] \
    && ok "the machine announced by mDNS and NetBIOS is listed once (3 hosts)" \
    || { bad "expected 3 host rows, got $n — dedupe across the two sources"
         printf '%s\n' "$out" | awk -F'\t' '$2=="host"{print "        host row: " $1}'; }

# ── The samba tools' missing config ──────────────────────────
if [ -r /etc/samba/smb.conf ]; then
    ok "(this box has /etc/samba/smb.conf — the -s fallback is not exercised)"
else
    grep -q -- '-s /dev/null' "$SMB_LOG" \
        && ok "smbclient runs on defaults when there is no /etc/samba/smb.conf" \
        || bad "smbclient was run without -s on a box with no smb.conf — it refuses"
fi

# ── A probe that FAILS is not an empty network ───────────────
fout=$(AVAHI_FAIL=1 PATH="$tmp/bin:$PATH" "$bin" netscan 2>&1)
printf '%s' "$fout" | grep -qi 'avahi-browse failed' \
    && ok "a failing mDNS probe is reported, not reported as an empty network" \
    || bad "a broken avahi-browse was silently indistinguishable from an empty LAN"

# ── netmount ─────────────────────────────────────────────────
run netmount 'not-a-uri' >/dev/null 2>&1 \
    && bad "netmount accepted something that is not a URI" \
    || ok "netmount refuses an argument that is not a URI"

run netmount 'smb://nas.local/media' >/dev/null 2>&1
grep -q 'mount smb://nas.local/media' "$GIO_LOG" \
    && ok "netmount hands the URI to gio, and mounts nothing itself" \
    || bad "netmount did not call gio mount"

if [ "$fails" -gt 0 ]; then
    printf 'netscan_test: %d failure(s)\n' "$fails" >&2
    exit 1
fi
printf 'netscan_test: ok\n'
