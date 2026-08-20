#!/bin/sh
# printers.sh — network printer discovery and auto-setup (synui-printers)
#
# Driven against fake CUPS tools on PATH. It cannot be tested any other way and
# it must not be tested any other way: the real commands would add queues to the
# machine running the build, and `lpadmin` on a developer's box is not a thing a
# test suite gets to touch.
#
# ⚠ THE CASE THIS EXISTS FOR IS THE PARSER. lpinfo's long form names its fields
# `uri` and `info`; the first version of this script looked for `device-uri` and
# `device-info` — the names of the IPP ATTRIBUTES, which is a reasonable guess
# and completely wrong. Nothing would ever have matched, and the symptom on a
# network full of printers would have been "no network printers found". A
# discovery tool's failure mode and its normal result are the same sentence,
# which is exactly why the format is pinned here.
#
# Usage: printers.sh [path/to/synui-printers.sh]
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
tool=${1:-$here/../systemd/synui-printers.sh}
[ -x "$tool" ] || { echo "  ABORT no synui-printers.sh at $tool"; exit 1; }

fails=0
ok()  { printf '  ok    %s\n' "$1"; }
bad() { printf '  FAIL  %s\n' "$1"; fails=$((fails + 1)); }

tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM
mkdir -p "$tmp/bin"

# ── The fakes ────────────────────────────────────────────────
#
# Two printers announced. The first is already set up; the second is not, and
# its name is the normal awkward case — a space, an apostrophe and brackets,
# none of which CUPS accepts in a queue name.
cat > "$tmp/bin/lpinfo" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$LPINFO_LOG"
cat <<'OUT'
Device: uri = ipp://laser.local/ipp/print
        class = network
        info = Office Laser
        make-and-model = HP LaserJet
        device-id = MFG:HP;MDL:LaserJet;
Device: uri = ipp://spare.local/ipp/print
        class = network
        info = Kenzie's Printer (Spare)
        make-and-model = Brother HL-2270DW
        device-id = MFG:Brother;
Device: uri = socket://192.168.40.7:9100
        class = network
        info =
        make-and-model = Raw Socket Printer
OUT
EOF

cat > "$tmp/bin/lpstat" <<'EOF'
#!/bin/sh
case " $* " in
*" -v "*) echo "device for Office_Laser: ipp://laser.local/ipp/print" ;;
*" -d "*) [ -n "${HAVE_DEFAULT:-}" ] && echo "system default destination: Office_Laser" || echo "no system default destination" ;;
esac
exit 0
EOF

# Refuses unless ALLOW_LPADMIN is set — which is what cupsd does to anyone
# outside its SystemGroup, and the reason pkexec exists here.
cat > "$tmp/bin/lpadmin" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$LPADMIN_LOG"
[ -n "${ALLOW_LPADMIN:-}" ] || { echo "lpadmin: Forbidden" >&2; exit 1; }
exit 0
EOF

cat > "$tmp/bin/pkexec" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$PKEXEC_LOG"
ALLOW_LPADMIN=1 "$@"
EOF

cat > "$tmp/bin/notify-send" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$NOTIFY_LOG"
EOF

chmod +x "$tmp/bin"/*
LPINFO_LOG=$tmp/lpinfo.log LPADMIN_LOG=$tmp/lpadmin.log
PKEXEC_LOG=$tmp/pkexec.log NOTIFY_LOG=$tmp/notify.log
export LPINFO_LOG LPADMIN_LOG PKEXEC_LOG NOTIFY_LOG

reset_logs() { : > "$LPADMIN_LOG"; : > "$PKEXEC_LOG"; : > "$NOTIFY_LOG"; : > "$LPINFO_LOG"; }
run() { PATH="$tmp/bin:$PATH" sh "$tool" "$@"; }

# ── scan ─────────────────────────────────────────────────────
reset_logs
out=$(run scan 2>&1)

printf '%s' "$out" | grep -q 'Office Laser' \
    && ok "the lpinfo long form is parsed (uri/info, not device-uri/device-info)" \
    || bad "nothing was parsed out of lpinfo — the field names are wrong again"

printf '%s' "$out" | grep -q 'set up.*Office Laser' \
    && ok "a printer that already has a queue is marked as set up" \
    || bad "an already-configured printer was not recognised"

printf '%s' "$out" | grep -q 'found.*Spare' \
    && ok "a printer with no queue is offered" \
    || bad "the unconfigured printer was not offered"

printf '%s' "$out" | grep -q 'Raw Socket Printer' \
    && ok "a device with no info falls back to make-and-model" \
    || bad "a device reporting no info was dropped"

[ ! -s "$LPADMIN_LOG" ] \
    && ok "scan changes nothing" \
    || bad "scan called lpadmin"

# ── add --auto ───────────────────────────────────────────────
reset_logs
out=$(run add --auto --notify 2>&1)

grep -q -- "-m everywhere" "$LPADMIN_LOG" \
    && ok "queues are created driverless (IPP Everywhere)" \
    || bad "lpadmin was called without -m everywhere"

grep -q "ipp://laser.local/ipp/print" "$LPADMIN_LOG" \
    && bad "the already-configured printer was added a second time" \
    || ok "an already-configured printer is left alone"

grep -q "ipp://spare.local/ipp/print" "$LPADMIN_LOG" \
    && ok "the new printer is added" \
    || bad "the new printer was not added"

# CUPS refuses a queue name with a space, a slash or a '#'; the apostrophe and
# brackets are ordinary in a printer's own name for itself.
if grep -q -- "-p Kenzies_Printer_Spare" "$LPADMIN_LOG"; then
    ok "the queue name is sanitised for CUPS"
else
    bad "the queue name was not sanitised: $(grep -o -- '-p [^ ]*' "$LPADMIN_LOG" | head -3 | tr '\n' ' ')"
fi

grep -q "lpadmin" "$PKEXEC_LOG" \
    && ok "a refused lpadmin is retried through pkexec" \
    || bad "lpadmin was refused and pkexec was never tried"

grep -q -- "-d " "$LPADMIN_LOG" \
    && ok "the first printer becomes the default when there is none" \
    || bad "no default was set on a system with no default destination"

[ -s "$NOTIFY_LOG" ] \
    && ok "--notify reports through a desktop notification" \
    || bad "--notify sent no notification"

# ── a system that already HAS a default keeps it ─────────────
reset_logs
HAVE_DEFAULT=1 PATH="$tmp/bin:$PATH" sh "$tool" add --auto >/dev/null 2>&1
grep -q -- "-d " "$LPADMIN_LOG" \
    && bad "an existing default printer was overwritten" \
    || ok "an existing default printer is left alone"

# ── the policy that already allows it is not made to authenticate ──
reset_logs
ALLOW_LPADMIN=1 PATH="$tmp/bin:$PATH" sh "$tool" add --auto >/dev/null 2>&1
[ -s "$PKEXEC_LOG" ] \
    && bad "pkexec was used even though lpadmin was permitted" \
    || ok "pkexec is only used after lpadmin is actually refused"

# ── arguments ────────────────────────────────────────────────
run add >/dev/null 2>&1 \
    && bad "add with no URI and no --auto should be a usage error" \
    || ok "add needs a URI or --auto"

if [ "$fails" -gt 0 ]; then
    printf 'printers: %d failure(s)\n' "$fails" >&2
    exit 1
fi
printf 'printers: ok\n'
