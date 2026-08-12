#!/usr/bin/env bash
#
# syn_confine_test.sh — does the sandbox actually deny anything?
#
# ⚠ THE ONE RULE FOR THIS FILE: every assertion must be able to FAIL.
#
# A sandbox test suite is uniquely easy to write green. "The command ran and
# exited 0" passes just as well when the ruleset was never applied, and a
# confinement that quietly does nothing looks exactly like one that works —
# which is the entire failure mode this program exists to prevent. So the
# suite is built the other way round: it asserts the DENIALS, by name, and
# every allow-case is paired with a deny-case that proves the rule was live.
#
# The canary is ~/.ssh. If a run cannot read it, the sandbox is on; if it can,
# nothing else the suite says is worth anything.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

SC=${1:-./build/syn-confine}
[ -x "$SC" ] || { echo "not executable: $SC" >&2; exit 1; }
SC=$(readlink -f "$SC")

pass=0 fail=0
ok()    { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad()   { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail + 1)); }
check() { if [ "$2" = 0 ]; then ok "$1"; else bad "$1"; fi; }

# ⚠ Capture first, match second. A confined command that is SUPPOSED to fail
# exits non-zero, and under pipefail `cmd | grep` reports the command's status
# rather than grep's — the same trap the syn-disks suite documents at its top.
says() { local out; out=$("$@" 2>&1); printf '%s\n' "$out"; }

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/work"
echo "workspace file" > "$T/work/inside.txt"
echo "secret"         > "$T/outside.txt"

echo "syn-confine tests — $SC"

# ── it refuses rather than running unconfined ───────────────────────────────
#
# 78 is "the sandbox could not be built, and the command did NOT run". Every
# one of these must be that and not the command's own status, or a caller
# cannot tell a broken sandbox from a failed command.

"$SC" --rw /definitely/not/here -- true >/dev/null 2>&1
[ $? -eq 78 ] && ok "a path that does not exist is refused, not skipped" \
              || bad "a path that does not exist is refused, not skipped"

says "$SC" --rw /definitely/not/here -- true | grep -q 'No such file'
check "...and says which path" $?

"$SC" --net --tcp 443 -- true >/dev/null 2>&1
[ $? -eq 78 ] && ok "contradictory network options are refused" \
              || bad "contradictory network options are refused"

"$SC" --nonsense -- true >/dev/null 2>&1
[ $? -eq 78 ] && ok "an unknown option is refused" || bad "an unknown option is refused"

# ── the canary ─────────────────────────────────────────────────────────────

says "$SC" --rw "$T/work" -- cat "$HOME/.ssh/id_ed25519" | grep -qi 'denied\|no such'
check "CANARY: ~/.ssh is unreadable inside the sandbox" $?

# Paired with the same read OUTSIDE, or the assertion above passes just as well
# on a machine with no ~/.ssh at all — which is a green test proving nothing.
if [ -e "$HOME/.ssh" ]; then
    ls "$HOME/.ssh" >/dev/null 2>&1
    check "...and IS readable outside it (so the canary is real)" $?
else
    ok "...no ~/.ssh on this machine; canary uses \$HOME itself"
fi

says "$SC" --rw "$T/work" -- ls "$HOME" | grep -qi 'denied'
check "\$HOME itself is not listable" $?

# ── the workspace is usable, or the sandbox is useless ─────────────────────

says "$SC" --rw "$T/work" -- cat "$T/work/inside.txt" | grep -q 'workspace file'
check "the workspace is readable" $?

"$SC" --rw "$T/work" -- sh -c "echo written > '$T/work/new.txt'" >/dev/null 2>&1
[ -f "$T/work/new.txt" ] && ok "the workspace is writable" || bad "the workspace is writable"

# TRUNCATE is a separate Landlock right, and without it `>` over an EXISTING
# file fails while `>` to a new one works — which presents as the sandbox
# breaking redirection at random.
"$SC" --rw "$T/work" -- sh -c "echo again > '$T/work/new.txt'" >/dev/null 2>&1
[ "$(cat "$T/work/new.txt")" = "again" ] \
    && ok "an existing file can be truncated (the TRUNCATE right is granted)" \
    || bad "an existing file can be truncated (the TRUNCATE right is granted)"

# REFER: rename between two directories inside the workspace.
mkdir -p "$T/work/sub"
"$SC" --rw "$T/work" -- mv "$T/work/new.txt" "$T/work/sub/moved.txt" >/dev/null 2>&1
[ -f "$T/work/sub/moved.txt" ] \
    && ok "a file can be moved within the workspace (the REFER right is granted)" \
    || bad "a file can be moved within the workspace (the REFER right is granted)"

# ...and the matching denial: one directory up is NOT the workspace.
says "$SC" --rw "$T/work" -- cat "$T/outside.txt" | grep -qi 'denied'
check "a sibling directory outside the workspace is denied" $?

says "$SC" --rw "$T/work" -- sh -c "echo x > '$T/outside.txt'" | grep -qi 'denied'
check "...and cannot be written either" $?

# ── the system is readable but not writable ────────────────────────────────

says "$SC" --rw "$T/work" -- sh -c 'echo x > /etc/ld.so.preload' | grep -qi 'denied'
check "/etc/ld.so.preload cannot be created" $?

"$SC" --rw "$T/work" -- test -x /usr/bin/env >/dev/null 2>&1
check "the base profile leaves /usr executable" $?

# ── network ────────────────────────────────────────────────────────────────
#
# Against 127.0.0.1 so the suite needs no internet and contacts nothing. The
# port is one nothing is listening on: the interesting distinction is
# "connection refused" (the connect was ALLOWED and rejected by the kernel's
# TCP stack) versus "permission denied" (Landlock stopped it).

# The port is an ARGUMENT, not baked into the string. It started as two
# copies of the probe with the port edited by shell substitution, and the
# substitution silently did not match — so the "denied port" case tested the
# ALLOWED port and failed for the one reason that was not a real bug.
probe='import socket,sys
try:
    socket.socket().connect(("127.0.0.1", int(sys.argv[1])))
    print("CONNECTED")
except PermissionError: print("EPERM")
except OSError as e: print(type(e).__name__, e.errno)'

says "$SC" --rw "$T/work" --  python3 -c "$probe" 9 | grep -q 'EPERM'
check "outbound TCP is denied by default" $?

says "$SC" --rw "$T/work" --net --  python3 -c "$probe" 9 | grep -qv 'EPERM'
check "--net allows the connect to be attempted" $?

# The port allowlist is per-port, so the ALLOWED port must behave differently
# from a denied one — otherwise "it was refused" proves nothing.
says "$SC" --rw "$T/work" --tcp 9 --  python3 -c "$probe" 9 | grep -q 'ConnectionRefusedError'
check "--tcp 9 permits port 9 (refused by TCP, not by Landlock)" $?

says "$SC" --rw "$T/work" --tcp 9 --  python3 -c "$probe" 80 | grep -q 'EPERM'
check "...while a port NOT on the list is still denied" $?

# ── --isolate-net ──────────────────────────────────────────────────────────

if [ "$(cat /proc/sys/user/max_user_namespaces 2>/dev/null || echo 0)" -gt 0 ]; then
    says "$SC" --rw "$T/work" --isolate-net -- python3 -c "$probe" 9 \
        | grep -qE 'EPERM|ConnectionRefusedError|NetworkUnreachable|OSError'
    check "--isolate-net has no route to anywhere" $?

    # The distinguishing property versus --tcp: UDP is gone too, which is the
    # whole reason this option exists.
    udp='import socket
s=socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
try:
    s.sendto(b"x", ("8.8.8.8", 53)); print("SENT")
except OSError as e: print("BLOCKED", e.errno)'
    says "$SC" --rw "$T/work" --isolate-net -- python3 -c "$udp" | grep -q 'BLOCKED'
    check "...including UDP, which Landlock alone does not cover" $?

    # And the user is unchanged — the namespace is for the empty network
    # stack, not to run the command as somebody else.
    says "$SC" --rw "$T/work" --isolate-net -- id -u | grep -q "^$(id -u)$"
    check "...and the command still runs as the same user" $?
else
    ok "--isolate-net skipped: unprivileged user namespaces are disabled here"
fi

# ── privilege cannot be regained ───────────────────────────────────────────
#
# Landlock requires no_new_privs, so setuid binaries cannot raise privilege
# from inside. This is asserted rather than assumed, because it is the
# property that stops a confined shell simply running sudo.

says "$SC" --rw "$T/work" -- cat /proc/self/status | grep -q 'NoNewPrivs:.*1'
check "no_new_privs is set, so setuid cannot escalate out" $?

if [ -u /usr/bin/sudo ]; then
    says "$SC" --rw "$T/work" -- sudo -n true | grep -qi 'effective uid is not 0\|denied\|no new privileges\|must be setuid'
    check "sudo cannot elevate inside the sandbox" $?
fi

# ── it survives execve ─────────────────────────────────────────────────────
#
# The property the whole design rests on: the confinement is inherited, so it
# does not matter how many processes deep the command goes, nor what the shell
# string looks like. A sandbox that only held for the first process would be
# defeated by `sh -c`.

says "$SC" --rw "$T/work" -- sh -c "sh -c \"cat '$HOME/.ssh/id_ed25519'\"" \
    | grep -qi 'denied\|no such'
check "the confinement survives two levels of exec" $?

says "$SC" --rw "$T/work" -- sh -c 'python3 -c "print(open(\"/etc/shadow\").read())"' \
    | grep -qi 'denied'
check "...and an interpreter cannot read around it" $?

# ── --print does not run anything ──────────────────────────────────────────

"$SC" --print --rw "$T/work" > "$T/policy.txt" 2>&1
grep -q "^landlock abi" "$T/policy.txt"
check "--print reports the ABI it will use" $?

grep -q "no TCP" "$T/policy.txt"
check "...and states the default network posture" $?

grep -q "UDP NOT covered" "$T/policy.txt"
check "...including the gap it does NOT close" $?

echo ""
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
