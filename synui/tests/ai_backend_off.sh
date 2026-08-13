#!/bin/sh
# ai_backend_off.sh — "off" has to mean off after a REBOOT, not just until one
#
# The AI backend row cycles GPU → CPU → off. Off used to be a `systemctl stop`
# and a line in /run/synapd/backend, and neither of those survives a boot:
#
#   * synapd.service is enabled (multi-user.target.wants), and synguard, synnet
#     and synui each Wants= it — so the next boot started the daemon again no
#     matter what had been stopped. `disable` would not have closed that either;
#     a Wants= from another unit does not consult enablement. Only a mask does.
#   * /run is a tmpfs, so the recorded choice was gone by the time the desktop
#     came up, and the row read "auto" on a machine its owner had set to off.
#
# Both halves are asserted here, and asserted by RUNNING the helper — with a
# fake systemctl on PATH, so a test of "off must hold" cannot stop the AI daemon
# on the machine running it, and with SYNUI_AI_ROOT pointing every file the
# helper writes at a scratch directory for the same reason.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

helper=${1:?usage: ai_backend_off.sh <systemd/synui-ai-backend.sh>}

fails=0
check() {  # check <description> <expected> <actual>
    if [ "$2" = "$3" ]; then
        printf '  ok    %s\n' "$1"
    else
        printf '  FAIL  %s — expected %s, got %s\n' "$1" "$2" "$3"
        fails=$((fails + 1))
    fi
}

tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM

# ── The fakes ────────────────────────────────────────────────
#
# systemctl records every verb it is given and answers the queries the helper
# asks. is-enabled has to say something plausible: "enabled" for synapd (which
# is what makes the reboot hole real) and a failure for the bridge socket, which
# most installs never turn on.
mkdir -p "$tmp/bin"
cat > "$tmp/bin/systemctl" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$SYSTEMCTL_LOG"
for a in "$@"; do
    case $a in
    is-enabled)
        for b in "$@"; do
            case $b in synapd.service) echo enabled; exit 0 ;; esac
        done
        exit 1 ;;
    is-active) exit 1 ;;   # nothing is running in here
    esac
done
exit 0
EOF
# The helper re-execs itself under `sudo -n /usr/bin/synui-ai-backend` when it
# is not root, which a test is not. Answering `id -u` with 0 is what keeps this
# test on the copy it was handed: a sudo stub that just ran the command instead
# would run the INSTALLED helper, which finds itself still not root and re-execs
# sudo again — a loop, and the first version of this test hung in it.
cat > "$tmp/bin/id" <<'EOF'
#!/bin/sh
[ "${1:-}" = "-u" ] && { echo 0; exit 0; }
exec /usr/bin/id "$@"
EOF
# So sudo must never be reached. If it is, fail loudly rather than hang.
cat > "$tmp/bin/sudo" <<'EOF'
#!/bin/sh
echo "test bug: the helper re-execed under sudo" >&2
exit 99
EOF
chmod +x "$tmp/bin/systemctl" "$tmp/bin/sudo" "$tmp/bin/id"

SYSTEMCTL_LOG="$tmp/systemctl.log"
export SYSTEMCTL_LOG
export SYNUI_AI_ROOT="$tmp/root"
state="$tmp/root/etc/synapd/backend"
export PATH="$tmp/bin:$PATH"

run() {  # run <verb> — fresh log each time
    : > "$SYSTEMCTL_LOG"
    sh "$helper" "$1" >/dev/null 2>&1
}
logged() { grep -q "$1" "$SYSTEMCTL_LOG" && echo yes || echo no; }

# ── 1. off masks, and records itself somewhere durable ───────
run off
check "off masks synapd.service"        yes "$(logged '^mask .*synapd\.service')"
check "off masks synapd.socket"         yes "$(logged '^mask .*synapd\.socket')"
check "off still stops the units"       yes "$(logged '^stop .*synapd\.service')"
check "off is recorded"                 off "$(cat "$state" 2>/dev/null)"

# Under the prefix it lands in etc/, not run/ — which is the whole point: the
# record has to outlive the boot that the mask outlives.
check "the record is not on a tmpfs" yes \
      "$([ -f "$state" ] && [ ! -e "$tmp/root/run/synapd/backend" ] && echo yes || echo no)"

# ── 2. status reads off back, mask or no mask ────────────────
check "status reports off"              off "$(sh "$helper" status 2>/dev/null)"

# ── 3. gpu/cpu is the inverse: it has to UNMASK ──────────────
#
# Without this, coming back from off reports success while systemd refuses every
# start — the row would say GPU with no daemon behind it.
run gpu
check "gpu unmasks synapd.service"      yes "$(logged '^unmask .*synapd\.service')"
check "gpu unmasks synapd.socket"       yes "$(logged '^unmask .*synapd\.socket')"
check "gpu starts the socket"           yes "$(logged '^start synapd\.socket')"
check "gpu is recorded"                 gpu "$(cat "$state" 2>/dev/null)"

# The unmask must come BEFORE the start, or the start it is meant to enable has
# already failed.
unmask_at=$(grep -n '^unmask' "$SYSTEMCTL_LOG" | head -1 | cut -d: -f1)
start_at=$(grep -n '^start synapd.socket' "$SYSTEMCTL_LOG" | head -1 | cut -d: -f1)
check "unmask precedes start" yes \
      "$([ -n "$unmask_at" ] && [ -n "$start_at" ] && [ "$unmask_at" -lt "$start_at" ] \
         && echo yes || echo no)"

if [ "$fails" -eq 0 ]; then
    printf 'ai_backend_off: all checks passed\n'
    exit 0
fi
printf 'ai_backend_off: %d check(s) failed\n' "$fails"
exit 1
