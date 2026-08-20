#!/usr/bin/env bash
#
# bpf-enforce-check.sh — does synguard's kernel enforcement do what it says?
#
# Item 1 of docs/SECURITY-ROADMAP.md. synguard ships three acting rules
# (50-default-deny.rules) but `--bpf-enforce` is NOT in its unit, so the kernel
# gate has never run outside a developer's head — and every safety property
# that makes arming it thinkable is currently a claim in a comment:
#
#   · a 30-second warmup, so a bad rule cannot stop you logging in
#   · fail-open when the daemon wedges, so a hung synguard is not a brick
#   · `synapse.bpf_enforce=0` on the kernel command line as the way back
#
# This turns four of those claims into observations. It arms the gate, watches,
# and disarms — on a machine you are sitting at.
#
# ─────────────────────────────────────────────────────────────────────────────
# ⚠ IT ARMS KERNEL ENFORCEMENT ON THIS MACHINE, BRIEFLY. Read this first.
#
# The three shipped rules touch /etc/ld.so.preload, a canary file, and exec out
# of /dev — none of them on a login path. The risk is not those rules; it is
# that the BPF hooks themselves run on every file_open and every exec while the
# gate is armed, and that has not been observed before, which is the point.
#
# THREE THINGS PROTECT YOU, in order of how much you should rely on them:
#
#   1. A DEAD-MAN TIMER. Before arming, this schedules a transient systemd
#      timer that removes the drop-in and restarts synguard after
#      $DISARM_AFTER seconds no matter what — if this script dies, if your SSH
#      drops, if the machine stops answering you. It is cancelled on a clean
#      finish. This is the protection that works when you are not in a position
#      to type.
#   2. An EXIT trap that disarms immediately on any exit path, including Ctrl-C.
#   3. `synapse.bpf_enforce=0` at the boot menu, which needs hands on the
#      keyboard and is therefore the LAST resort rather than the first — and is
#      itself one of the things this cannot verify (see the end).
#
# ⚠ RUN IT WHERE YOU CAN REACH THE KEYBOARD. Over SSH alone, a machine that
# stops answering is a machine you cannot rescue: the escape hatch is in the
# bootloader.
#
# Usage:  sudo ./tools/bpf-enforce-check.sh
#         sudo DISARM_AFTER=300 ./tools/bpf-enforce-check.sh
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

DISARM_AFTER=${DISARM_AFTER:-600}
DROPIN_DIR=/etc/systemd/system/synguard.service.d
DROPIN=$DROPIN_DIR/99-bpf-enforce-check.conf
DEADMAN=synguard-bpf-disarm
CANARY=/var/lib/synguard/bpf-canary
PRELOAD=/etc/ld.so.preload
MARKER=bpf-enforce-check-not-a-real-library

pass=0 fail=0 skip=0
ok()   { printf '  \033[32mok\033[0m    %s\n' "$1"; pass=$((pass+1)); }
bad()  { printf '  \033[31mFAIL\033[0m  %s\n' "$1"; fail=$((fail+1)); }
note() { printf '        %s\n' "$1"; }
skipm(){ printf '  \033[33mskip\033[0m  %s\n' "$1"; skip=$((skip+1)); }
head2(){ printf '\n\033[1m%s\033[0m\n' "$1"; }

[ "$(id -u)" = 0 ] || { echo "needs root: sudo $0" >&2; exit 2; }

# ── Reading the canary ───────────────────────────────────────────────────────
#
# ⚠ NEVER IN THIS SHELL. A deny tears down the opener's process TREE
# (sg_kill_tree), and this script would be in it — a userspace kill would take
# the test harness along with the test. systemd-run puts the reader in a scope
# of its own, so the worst case is a dead scope.
#
# --wait --collect so the exit status comes back and no unit is left behind.
# "Operation not permitted" on a root-owned file read BY ROOT is the
# observation this whole script exists for: only an LSM can produce it.
read_canary() {
    systemd-run -q --wait --collect --property=StandardError=journal \
        /usr/bin/cat "$CANARY" >/dev/null 2>&1
}

# ⚠ ASK, DO NOT SCRAPE THE BOOT LINE. `bpf-lsm: denied=…` used to be logged
# once, from the startup banner, and never again — so reading it out of the
# journal after causing a refusal read the value from BEFORE the refusal, and
# this script reported "the counter did not move" against a gate that was
# working perfectly. That was this rig's first real finding, and it was a
# finding about synguard rather than about the kernel: a refusal left no trace
# in the journal at all.
#
# synguard dumps a fresh line on SIGUSR1 now. Sample it that way, and give the
# main loop a moment to service the flag — it is set in the handler and acted
# on by a loop that ticks once a second.
bpf_sample() {
    local pid
    pid=$(systemctl show synguard -p MainPID --value)
    [ -n "$pid" ] && [ "$pid" != 0 ] && kill -USR1 "$pid" 2>/dev/null
    sleep 2
}
gate_line()  { journalctl -u synguard -n 200 --no-pager 2>/dev/null |
               grep -o 'bpf-lsm: .*' | tail -1; }
denied_now() { bpf_sample
               journalctl -u synguard -n 400 --no-pager 2>/dev/null |
               grep -o 'denied=[0-9]*' | tail -1 | cut -d= -f2; }

disarm() {
    rm -f "$DROPIN"
    rmdir "$DROPIN_DIR" 2>/dev/null
    systemctl daemon-reload 2>/dev/null
    systemctl restart synguard 2>/dev/null
    systemctl stop "$DEADMAN.timer" "$DEADMAN.service" 2>/dev/null
}

# Put the drop-in back and restart. Phase 4 needs the gate down to plant a file
# the armed rule refuses to let it create, and up again to test it.
rearm() {
    mkdir -p "$DROPIN_DIR"
    {
        echo "[Service]"
        echo "ExecStart="
        echo "ExecStart=/usr/bin/synguard --foreground --mode enforce --rules /etc/synguard/rules.d/ --bpf-enforce"
    } > "$DROPIN"
    systemctl daemon-reload
    systemctl restart synguard
    # ⚠ Past the warmup, or every check after this reads an unarmed gate and
    # scores it as the rule not working.
    note "re-arming and waiting out the 30s warmup…"
    sleep 34
}

cleanup() {
    local rc=$?
    printf '\n\033[1mdisarming\033[0m\n'
    # Anything the preload phase may have left behind. Checked by MARKER rather
    # than assumed: a stray /etc/ld.so.preload is a file every process on this
    # machine reads at exec, and leaving one would be a worse bug than anything
    # being tested here. The marker means we can never remove somebody else's.
    if [ -e "$PRELOAD" ] && grep -q "$MARKER" "$PRELOAD" 2>/dev/null; then
        rm -f "$PRELOAD"
        note "removed the test $PRELOAD"
    fi
    disarm
    sleep 2
    if systemctl is-active --quiet synguard; then
        printf '  \033[32mok\033[0m    synguard is running, gate not armed\n'
    else
        printf '  \033[31mFAIL\033[0m  synguard is NOT running — start it: systemctl start synguard\n'
    fi
    exit $rc
}
trap cleanup EXIT INT TERM

# ── Preflight ────────────────────────────────────────────────────────────────
head2 "preflight"

command -v synguard >/dev/null || { bad "synguard is not installed"; exit 1; }
ok "synguard $(pacman -Q synguard 2>/dev/null | awk '{print $2}')"

if grep -q bpf /sys/kernel/security/lsm 2>/dev/null; then
    ok "the kernel has the BPF LSM  [$(cat /sys/kernel/security/lsm)]"
else
    bad "bpf is not in /sys/kernel/security/lsm — nothing below can work."
    note "Needs lsm=...,bpf on the kernel command line. Stopping."
    exit 1
fi

# ⚠ The rule cannot fire on a file that is not there: lsm/file_open only fires
# for an open that reaches a file, so a missing canary is a rule that looks
# armed and can never act. synguard.service recreates it in ExecStartPre.
[ -e "$CANARY" ] && ok "the canary file exists" \
                 || { bad "$CANARY is missing — the positive control cannot work"; exit 1; }

grep -q 'deny-bpf-canary' /etc/synguard/rules.d/50-default-deny.rules 2>/dev/null \
    && ok "50-default-deny.rules is installed and names the canary" \
    || { bad "the armed policy is not installed (needs synguard 0.1.0-35+)"; exit 1; }

# ── 1. The control ───────────────────────────────────────────────────────────
#
# Without this, "refused" proves nothing: a read failing for some unrelated
# reason would look exactly like enforcement working.
head2 "1 · before arming, the canary is readable (the control)"
if read_canary; then
    ok "root can read the canary with the gate unarmed"
else
    bad "the canary is ALREADY unreadable before arming — something else is"
    note "refusing it, and every result below would be meaningless. Stopping."
    exit 1
fi

# ── Arm, with the dead man first ─────────────────────────────────────────────
head2 "arming (dead-man disarm in ${DISARM_AFTER}s)"

# ⚠ THE TIMER GOES IN BEFORE THE GATE DOES. If that order is ever swapped there
# is a window where the machine is armed with nothing scheduled to disarm it.
systemctl stop "$DEADMAN.timer" "$DEADMAN.service" 2>/dev/null
if systemd-run -q --unit="$DEADMAN" --on-active="$DISARM_AFTER" \
       /bin/sh -c "rm -f $DROPIN; systemctl daemon-reload; systemctl restart synguard" 2>/dev/null; then
    ok "dead-man timer armed — this machine disarms itself in ${DISARM_AFTER}s"
    note "even if this script, or your session, goes away"
else
    bad "could not schedule the dead-man timer — refusing to arm without it"
    exit 1
fi

mkdir -p "$DROPIN_DIR"
{
    echo "# Written by tools/bpf-enforce-check.sh. Removed when it finishes, and"
    echo "# by the $DEADMAN timer if it does not."
    echo "[Service]"
    echo "ExecStart="
    echo "ExecStart=/usr/bin/synguard --foreground --mode enforce --rules /etc/synguard/rules.d/ --bpf-enforce"
} > "$DROPIN"
systemctl daemon-reload
systemctl restart synguard || { bad "synguard would not restart armed"; exit 1; }
sleep 3

if systemctl is-active --quiet synguard; then
    ok "synguard came up with --bpf-enforce"
else
    bad "synguard is not running after arming — stopping"
    exit 1
fi
note "$(gate_line)"

# ── 2. The warmup is real ────────────────────────────────────────────────────
head2 "2 · inside the 30s warmup, the rule does NOT act"
note "this is what stops a bad rule keeping you out of your own machine"
if read_canary; then
    ok "the canary is still readable seconds after arming"
else
    bad "the gate acted during the warmup — a bad rule WOULD lock you out"
fi

# ── 3. Past the warmup, it does ──────────────────────────────────────────────
head2 "3 · past the warmup, the canary is refused IN THE KERNEL"
note "waiting out the 30s warmup…"
sleep 32
before=$(denied_now)
if read_canary; then
    bad "root could still read the canary — the gate is NOT enforcing"
    note "$(gate_line)"
else
    ok "root was refused on a root-owned file — only an LSM can do that"
fi
after=$(denied_now)
if [ -n "$after" ] && [ "${after:-0}" != "${before:-0}" ]; then
    ok "the denied counter moved (${before:-0} -> ${after})"
else
    bad "the denied counter did not move (${before:-0} -> ${after:-?})"
    note "the refusal may have come from somewhere other than the gate"
fi
note "$(gate_line)"

# ── 4. /etc/ld.so.preload: the rule that matters ─────────────────────────────
#
# ⚠ THE PLANT HAPPENS BEFORE ARMING, and the first version of this got it
# wrong in a way worth writing down. It planted the file with the gate already
# armed — and the rule is `event open`, which refuses the O_CREAT too, so the
# shell's redirect failed with "Operation not permitted" and the file was never
# created. The next check then read a file that did not exist, failed, and was
# scored as "the planted preload is refused". A PASS FOR THE WRONG REASON: it
# would have passed identically with the rule doing nothing.
#
# So: plant while unarmed, re-arm, then test. That is also the honest scenario —
# a rootkit that got in before synguard did.
#
# The write being refused while armed is a real and separate result, and it is
# now asserted as itself rather than mistaken for this one.
head2 "4 · a preload planted BEFORE synguard is refused, and nothing breaks"
note "the claim: refusing the READ neuters an already-planted rootkit, and"
note "glibc treats a failed open as 'no preload' — so the machine still works"

if [ -e "$PRELOAD" ]; then
    skipm "$PRELOAD already exists — not touching it"
    note "this machine has one already, which is itself worth looking at"
else
    # First: with the gate ARMED, creating it must be refused. This is the
    # protection working at the moment of the attack rather than after it.
    if echo "/nonexistent/$MARKER.so" > "$PRELOAD" 2>/dev/null; then
        bad "the armed gate allowed $PRELOAD to be CREATED"
        rm -f "$PRELOAD"
    else
        ok "while armed, $PRELOAD cannot even be created"
    fi

    # Now the harder case: it is already there when synguard starts.
    note "disarming to plant one, as a rootkit that arrived first would…"
    disarm
    sleep 3
    if echo "/nonexistent/$MARKER.so" > "$PRELOAD" 2>/dev/null; then
        # ⚠ Prove the plant took. Without this the re-arm below would be
        # testing an empty directory again, which is how the first version of
        # this phase fooled itself.
        if grep -q "$MARKER" "$PRELOAD" 2>/dev/null; then
            ok "planted $PRELOAD while unarmed (the file really is there)"
        else
            bad "the plant did not take — the rest of this phase is meaningless"
        fi

        rearm
        if systemd-run -q --wait --collect /bin/true 2>/dev/null; then
            ok "a normal program still runs with the preload in place"
            note "glibc's failed open reads as 'no preload' — the machine works"
        else
            bad "a program failed to run — the refusal broke exec, which it must not"
        fi
        if systemd-run -q --wait --collect /usr/bin/cat "$PRELOAD" >/dev/null 2>&1; then
            bad "the planted $PRELOAD was READABLE — the rule did not act"
        else
            ok "the planted $PRELOAD is refused, so its libraries never load"
        fi

        # Removing it needs the gate down again — the rule refuses the open
        # either way round.
        disarm; sleep 2
        rm -f "$PRELOAD"
        note "removed the test file"
        rearm
    else
        bad "could not plant $PRELOAD even while disarmed"
    fi
fi

# ── 5. Fail-open when the daemon wedges ──────────────────────────────────────
head2 "5 · a wedged synguard fails OPEN"
note "SIGSTOP, not kill: 'alive but not answering' is the case the heartbeat"
note "exists for, and the one a kill cannot simulate"
gpid=$(systemctl show synguard -p MainPID --value)
if [ -n "$gpid" ] && [ "$gpid" != 0 ]; then
    kill -STOP "$gpid"
    sleep 7                     # the heartbeat goes stale after 5s
    if read_canary; then
        ok "with synguard wedged, the canary is readable again"
    else
        bad "still refused with synguard wedged — the gate does NOT fail open"
        note "this is the property that keeps a hung daemon from bricking a box"
    fi
    kill -CONT "$gpid"
    sleep 6
    if read_canary; then
        bad "still readable after synguard resumed — the gate did not re-arm"
    else
        ok "…and it re-arms when synguard answers again"
    fi
else
    skipm "could not find synguard's PID"
fi

# ── What this cannot check ───────────────────────────────────────────────────
head2 "not checked here"
skipm "synapse.bpf_enforce=0 — needs a reboot and the boot menu"
note "To verify by hand: reboot, add synapse.bpf_enforce=0 at the boot menu,"
note "and with the drop-in in place synguard should come up detect-only —"
note "'bpf-lsm: not loaded' in the journal, canary readable. That is the way"
note "back from a bad rule, and it is worth knowing it works BEFORE you need it."
skipm "the false-positive rate — needs an ordinary desktop session, not a script"
note "Arm it for a day of real use and watch:"
note "journalctl -u synguard | grep -E 'DENY|QUARANTINE'"

printf '\n\033[1m%d passed - %d failed - %d not checked\033[0m\n' "$pass" "$fail" "$skip"
[ "$fail" -eq 0 ]
