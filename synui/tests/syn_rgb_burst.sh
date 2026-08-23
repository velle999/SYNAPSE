#!/bin/bash
# syn_rgb_burst.sh — a burst of wallpapers must not kill the watch.
#
# pkgrel 437. Clicking through a wallpaper folder froze the lights on a colour
# from the middle of the run, for the rest of the session: syn-rgb.service
# tripped systemd's default start limit (5 starts in 10s) and took syn-rgb.path
# down with it — `unit-start-limit-hit`, and nothing re-arms a failed path unit.
#
# ⛔ NO HARDWARE AND NOT SYN-RGB'S OWN UNITS. This drives a scratch path+service
# PAIR under $XDG_RUNTIME_DIR, whose service echoes instead of pushing. Touching
# syn-rgb.path here would leave the machine running the test with its real
# bridge stopped.
#
# ⚠ ONE ARM PROVES NOTHING. A unit that survives a burst looks identical
# whether the limit was lifted or merely never reached, so the two arms differ
# by exactly one line — StartLimitIntervalSec=0 — and BOTH are assertions: the
# limited arm must DIE, or this test is not reproducing the bug it guards.
set -u

UNIT=${1:-$(dirname "$0")/../systemd/syn-rgb.service}
[ -f "$UNIT" ] || { echo "no syn-rgb.service at $UNIT" >&2; exit 1; }

# A build chroot has no session bus and no user manager. That is not a failure
# of the fix, so say so and pass — the shipped-unit assertion at the end is
# static and still runs.
if ! systemctl --user show -p Version >/dev/null 2>&1 || [ -z "${XDG_RUNTIME_DIR:-}" ]; then
    echo "SKIP: no systemd user session (this needs a logged-in seat)"
    grep -q '^StartLimitIntervalSec=0' "$UNIT" || {
        echo "  FAIL  the shipped syn-rgb.service has no StartLimitIntervalSec=0" >&2
        exit 1
    }
    echo "1 passed, 0 failed (static check only)"
    exit 0
fi

TAG=synrgbburst$$
UDIR=$XDG_RUNTIME_DIR/systemd/user
TMP=$(mktemp -d /tmp/syn-rgb-burst-XXXXXX) || exit 1

cleanup() {
    for v in limited unlimited; do
        systemctl --user stop "$TAG-$v.path" >/dev/null 2>&1
        systemctl --user reset-failed "$TAG-$v.path" "$TAG-$v.service" >/dev/null 2>&1
        rm -f "$UDIR/$TAG-$v.path" "$UDIR/$TAG-$v.service"
    done
    systemctl --user daemon-reload >/dev/null 2>&1
    rm -rf "$TMP"
}
trap cleanup EXIT

pass=0; fail=0
ok()  { pass=$((pass + 1)); }
bad() { fail=$((fail + 1)); printf '  FAIL  %s\n' "$*"; }
check() { if [ "$2" = "$3" ]; then ok; else bad "$1: expected [$2] got [$3]"; fi; }

mkdir -p "$UDIR" || exit 1
for v in limited unlimited; do
    # The ONLY difference between the two arms.
    if [ "$v" = unlimited ]; then LIM='StartLimitIntervalSec=0'; else LIM='# default limit'; fi
    cat > "$UDIR/$TAG-$v.service" <<UNITEOF
[Unit]
Description=syn-rgb burst probe ($v)
$LIM
[Service]
Type=oneshot
ExecStart=/usr/bin/env sh -c 'echo push'
UNITEOF
    cat > "$UDIR/$TAG-$v.path" <<UNITEOF
[Unit]
Description=syn-rgb burst probe path ($v)
[Path]
PathChanged=$TMP/palette-$v.state
Unit=$TAG-$v.service
UNITEOF
    : > "$TMP/palette-$v.state"
done
systemctl --user daemon-reload

for v in limited unlimited; do
    systemctl --user start "$TAG-$v.path" || { echo "cannot start probe path" >&2; exit 1; }
done

# Eight wallpapers, half a second apart: comfortably inside the 10s window and
# slower than the service, so each write gets its own run rather than being
# swallowed by one that is still going.
for i in 1 2 3 4 5 6 7 8; do
    for v in limited unlimited; do printf 'use=yes\nok=yes\naccent=#00000%s\n' "$i" > "$TMP/palette-$v.state"; done
    sleep 0.5
done
sleep 2

# ⚠ THE LIMITED ARM DYING IS THE PROOF THE BURST WAS BIG ENOUGH. If systemd's
# default ever stops being 5-in-10s this flips first, and loudly, instead of
# the unlimited arm quietly passing for the wrong reason.
check "an un-lifted start limit still kills the path unit" \
      "failed" "$(systemctl --user is-active "$TAG-limited.path")"
check "and with StartLimitIntervalSec=0 the watch survives" \
      "active" "$(systemctl --user is-active "$TAG-unlimited.path")"

# The colour that matters is the LAST one — a frozen bridge is one that stopped
# before the end of the burst, so counting runs is what says it kept up.
runs=$(journalctl --user -u "$TAG-unlimited.service" --since "-2min" --no-pager 2>/dev/null | grep -c 'push')
if [ "$runs" -ge 8 ]; then ok; else bad "every wallpaper in the burst landed: expected 8+ got [$runs]"; fi

# And the shipped unit is the one that has to carry it.
grep -q '^StartLimitIntervalSec=0' "$UNIT" && ok || bad "syn-rgb.service ships StartLimitIntervalSec=0"

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
