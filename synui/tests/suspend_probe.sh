#!/usr/bin/env bash
# suspend_probe.sh — instrument one suspend/resume cycle, end to end.
#
# Three separate questions get answered by one sleep, because a suspend is
# expensive to ask for and easy to misread:
#
#   1. WHERE DOES THE TIME GO? `PM: suspend entry` is not "asleep". The kernel
#      still has to run the PM_SUSPEND_PREPARE notifiers before it freezes
#      anything, and on this box that gap has been observed at 1m, 2m07s, 3m53s
#      and once EIGHT HOURS (2026-07-27 02:45 -> 10:51). The machine is fully
#      awake for all of it with its monitors dark, which reads to a person as
#      "it went to sleep" right up until the fans never stop. So this samples
#      once a second across the gap and records what the sleeping task is
#      blocked on, straight out of the kernel stack.
#
#   2. DID THE COMPOSITOR SURVIVE? A resume walks libseat -> wlroots -> synui,
#      and synui 205 fixed a deadlock on exactly that path. Alive proves
#      nothing: the failure mode is a process that is running and dispatching
#      NOTHING. `synctl` answering is the only real test, so it is timed.
#
#   3. DID THE SCREENS COME BACK BY THEMSELVES? Before 207 they did not — DPMS
#      stayed off until an input event, so the user had to type at a black
#      screen to discover the machine was up. Verifying that needs the outputs
#      read WITHOUT touching the mouse, which is why this is a script and not a
#      person watching.
#
# Usage:
#   tests/suspend_probe.sh                 # plain cycle
#   tests/suspend_probe.sh --freeze-x      # also SIGSTOP Xwayland first
#   tests/suspend_probe.sh --dry-run       # collect state, suspend nothing
#
# --freeze-x is the deadlock reproduction. It starts Xwayland, holds a client
# on it, then SIGSTOPs it so the X socket accepts connections and never answers
# — the "peer that never answers" that hung xcb_connect and wedged the event
# loop. On resume, apply_primary fires straight into it. Pre-205 that is a hard
# wedge; the probe reports whether any primary_worker piled up behind it.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
# https://github.com/velle999/SYNAPSE

set -uo pipefail

FREEZE_X=0
DRY_RUN=0
for a in "$@"; do
    case "$a" in
        --freeze-x) FREEZE_X=1 ;;
        --dry-run)  DRY_RUN=1 ;;
        -h|--help)  sed -n '2,40p' "$0"; exit 0 ;;
        *) echo "suspend_probe: unknown option $a" >&2; exit 2 ;;
    esac
done

OUT=${SUSPEND_PROBE_OUT:-$(mktemp -d -t suspend-probe-XXXXXX)}
mkdir -p "$OUT"
SAMPLES="$OUT/samples.tsv"
REPORT="$OUT/report.txt"

say() { printf '%s\n' "$*" | tee -a "$REPORT"; }
hr()  { say "────────────────────────────────────────────────────────"; }

SYNUI=$(pgrep -x synui | head -1)
[ -n "$SYNUI" ] || { echo "suspend_probe: synui is not running" >&2; exit 1; }

# ── thread census ────────────────────────────────────────────
# A primary_worker stuck in xcb_connect shows up here and nowhere else: it is
# an extra thread parked in a poll-flavoured wchan. Counting them is how we
# tell "apply_primary fired into a hung X and the loop shrugged" apart from
# "apply_primary never ran", which look identical from the outside and which
# is the trap that made an earlier PASS meaningless.
threads_dump() {
    local pid=$1 t comm wchan n=0
    for t in /proc/"$pid"/task/*; do
        [ -e "$t" ] || continue
        comm=$(cat "$t/comm" 2>/dev/null)
        wchan=$(cat "$t/wchan" 2>/dev/null)
        printf '  %-8s %-16s %s\n' "${t##*/}" "$comm" "${wchan:-?}"
        n=$((n+1))
    done
    printf '  total: %d\n' "$n"
}

blocked_in_poll() {
    local pid=$1 t n=0 w
    for t in /proc/"$pid"/task/*; do
        [ -e "$t" ] || continue
        w=$(cat "$t/wchan" 2>/dev/null)
        case "$w" in *poll*|*epoll*) n=$((n+1)) ;; esac
    done
    printf '%d' "$n"
}

outputs_state() { timeout 15 synctl outputs 2>/dev/null; }

# ── optional: wedge X ────────────────────────────────────────
XW=""; HOLDER=""
restore_pm() { :; }   # replaced once pm_print_times has been touched
cleanup_all() {
    [ -n "$XW" ] && kill -CONT "$XW" 2>/dev/null
    [ -n "$HOLDER" ] && kill "$HOLDER" 2>/dev/null   # by pid: `pkill -f` matches itself
    [ -n "${SAMPLER:-}" ] && kill "$SAMPLER" 2>/dev/null
    restore_pm
    return 0
}
trap cleanup_all EXIT INT TERM

if [ "$FREEZE_X" = 1 ]; then
    say "== arming the frozen-X reproduction =="
    DISPLAY=:0 timeout 15 xrandr --query >/dev/null 2>&1
    # Xwayland runs with -terminate, so it exits once idle. Hold a client on it
    # or it is gone before the suspend and xwayland_up is back to 0.
    DISPLAY=:0 xprop -root -spy >"$OUT/xspy.log" 2>&1 &
    HOLDER=$!
    sleep 3
    XW=$(pgrep -x Xwayland | head -1)
    if [ -z "$XW" ]; then
        say "  !! Xwayland did not start — cannot run the frozen-X case"
        FREEZE_X=0
    else
        kill -STOP "$XW"
        sleep 1
        say "  Xwayland pid $XW state=$(ps -o stat= -p "$XW" 2>/dev/null) (T = frozen)"
        # Prove the peer really is deaf, or the whole case is theatre.
        DISPLAY=:0 timeout 5 xrandr --query >/dev/null 2>&1
        say "  fresh X connect exit=$? (124 = hung = the condition we want)"
    fi
fi

# ── preflight ────────────────────────────────────────────────
hr; say "PREFLIGHT  $(date '+%Y-%m-%d %H:%M:%S')"
say "synui pid   : $SYNUI  (installed: $(pacman -Q synui 2>/dev/null))"
EXE=$(readlink /proc/"$SYNUI"/exe)
say "exe         : $EXE"
# `pacman -Q` describes the DISK. A compositor cannot be restarted in place, so
# after an upgrade the installed version and the running one routinely differ,
# and the running one is what the test measures. "(deleted)" is that mismatch:
# the file this process was exec'd from has been replaced underneath it. Saying
# so here costs one line and saves a whole suspend spent testing old code.
case "$EXE" in
    *"(deleted)"*)
        say ""
        say "  !! the RUNNING binary is NOT the installed one — it was replaced"
        say "  !! by an upgrade and this process still holds the old image."
        say "  !! Anything added since is NOT under test. Restart the session"
        say "  !! first if that is what you meant to check."
        say "" ;;
esac
say "VRAM in use : $(nvidia-smi --query-gpu=memory.used --format=csv,noheader 2>/dev/null || echo n/a)"
nvidia-smi --query-compute-apps=pid,process_name,used_memory --format=csv,noheader 2>/dev/null \
    | sed 's/^/  gpu client: /' | tee -a "$REPORT"
say "nvidia sleep units: $(systemctl is-enabled nvidia-suspend.service nvidia-resume.service 2>&1 | tr '\n' ' ')"
say "/var/tmp free: $(df -h /var/tmp | awk 'NR==2{print $4}')  (VRAM is dumped here)"
say "threads before:"; threads_dump "$SYNUI" | tee -a "$REPORT"
BASE_POLL=$(blocked_in_poll "$SYNUI")
say "poll-blocked baseline: $BASE_POLL  (compare against the count on wake)"

MARK=$(date '+%Y-%m-%d %H:%M:%S')

if [ "$DRY_RUN" = 1 ]; then
    hr; say "dry run — not suspending. State in $OUT"; exit 0
fi

# ── sampler ──────────────────────────────────────────────────
# Runs THROUGH the stall. Userspace is not frozen yet, which is the whole
# point: if this keeps producing rows after `PM: suspend entry`, the machine
# was never asleep. It gets frozen with everything else at the real freeze and
# thaws on resume, so the gap in its own timestamps is the true sleep.
# NO nvidia-smi in this loop, and nothing else that talks to the GPU.
#
# v1 sampled nvidia-smi every second and produced exactly one row: querying the
# driver while it is preparing to suspend BLOCKS, so the sampler wedged on the
# very thing it was there to measure and the whole stall went unobserved. Only
# cheap /proc reads here.
#
# This can only ever see the PRE-FREEZE stall. Once the kernel freezes
# userspace this loop is frozen with it, so the freeze -> S3 window is
# invisible from here by construction — that half needs pm_print_times below.
# Only /proc files that are plain reads. NOTHING that inspects another task.
#
# v2 sampled `pgrep systemd-sleep` and that task's /proc/PID/wchan, and managed
# 38 rows in 613 seconds — about 16s an iteration. Reading wchan unwinds the
# target's kernel stack and blocks while it sits in an uninterruptible PM
# callback, so the sampler stalled on the same thing it was watching. Twice
# now (nvidia-smi in v1) the instrument has been blocked BY the phenomenon.
#
# What actually separates the two hypotheses is bytes written. If the stall is
# NVIDIA dumping VRAM to /var/tmp under PreserveVideoMemoryAllocations, the
# disk counters climb by gigabytes. If it is a notifier hanging, they are flat.
# /proc/diskstats and /proc/meminfo are cheap, lock-free reads.
printf 'time\tsectors_written\tdirty_kb\twriteback_kb\n' >"$SAMPLES"
(
    while :; do
        # field 10 of each diskstats row is sectors written; sum whole disks
        # only (skip partitions) so nothing is counted twice.
        w=$(awk '$3 ~ /^(nvme[0-9]+n[0-9]+|sd[a-z]+)$/ {s+=$10} END{print s+0}' /proc/diskstats 2>/dev/null)
        d=$(awk '/^Dirty:/{print $2}' /proc/meminfo 2>/dev/null)
        b=$(awk '/^Writeback:/{print $2}' /proc/meminfo 2>/dev/null)
        printf '%s\t%s\t%s\t%s\n' "$(date '+%H:%M:%S')" "${w:-0}" "${d:-0}" "${b:-0}"
        sleep 1
    done
) >>"$SAMPLES" 2>/dev/null &
SAMPLER=$!

# Kernel-side per-device timing. The stall moves between phases run to run
# (233s/127s all pre-freeze, then 42s pre-freeze + 141s in device suspend), and
# device suspend happens with every process frozen — no userspace sampler can
# see it. pm_print_times makes the kernel time each device's callback and print
# it, which is the only way to name the device that is actually slow.
PM_TIMES_WAS=$(cat /sys/power/pm_print_times 2>/dev/null || echo 0)
PM_DEBUG_WAS=$(cat /sys/power/pm_debug_messages 2>/dev/null || echo 0)
sudo -n sh -c 'echo 1 > /sys/power/pm_print_times; echo 1 > /sys/power/pm_debug_messages' 2>/dev/null \
    && say "pm_print_times/pm_debug_messages: ON (restored on exit)" \
    || say "pm_print_times: could not enable (need sudo) — device timings unavailable"
restore_pm() {
    sudo -n sh -c "echo ${PM_TIMES_WAS:-0} > /sys/power/pm_print_times;
                   echo ${PM_DEBUG_WAS:-0} > /sys/power/pm_debug_messages" 2>/dev/null
}

hr; say "SUSPENDING at $(date '+%H:%M:%S') — wake it when you are ready"
systemctl suspend

# ── wait for resume ──────────────────────────────────────────
# grep -c, NOT grep -q, and the count compared separately.
#
# `journalctl | grep -q` under `set -o pipefail` is a trap: grep -q exits the
# instant it matches, journalctl then dies of SIGPIPE (141), and pipefail
# reports the PIPELINE as failed even though the match succeeded. The loop
# therefore spins forever precisely BECAUSE the machine resumed. It did — this
# probe sat in this loop for six minutes after a successful wake on its first
# real run. grep -c consumes all of its input, so nothing gets SIGPIPE.
while :; do
    n=$(journalctl --since "$MARK" 2>/dev/null \
        | grep -cE 'PM: suspend exit|System returned from sleep')
    [ "${n:-0}" -gt 0 ] && break
    sleep 5
done

# Capture BEFORE unfreezing anything — a stuck worker that has already drained
# proves nothing, which is exactly how the first run of this test fooled us.
RESUME_T=$(date '+%H:%M:%S')
POLL_BEFORE=$(blocked_in_poll "$SYNUI")
THREADS_BEFORE=$(ls /proc/"$SYNUI"/task 2>/dev/null | wc -l)

t0=$(date +%s%N)
if OUTS=$(outputs_state); then RESP="YES"; else RESP="NO"; OUTS=""; fi
t1=$(date +%s%N)
SYNCTL_MS=$(( (t1 - t0) / 1000000 ))

kill "$SAMPLER" 2>/dev/null

# ── report ───────────────────────────────────────────────────
hr; say "RESUMED $RESUME_T"

say ""
say "PHASE TIMING (from the kernel, not from us)"
journalctl -k --since "$MARK" --no-pager 2>/dev/null \
    | grep -E 'PM: suspend entry|Freezing user space processes$|Preparing to enter|Waking up from|PM: suspend exit' \
    | sed 's/^/  /' | tee -a "$REPORT"

ENTRY=$(journalctl -k --since "$MARK" --no-pager 2>/dev/null | grep -m1 'PM: suspend entry' | awk '{print $3}')
FREEZE=$(journalctl -k --since "$MARK" --no-pager 2>/dev/null | grep -m1 'Freezing user space processes$' | awk '{print $3}')
S3=$(journalctl -k --since "$MARK" --no-pager 2>/dev/null | grep -m1 'Preparing to enter' | awk '{print $3}')

# Two separate stalls, and they trade places between runs, so report both or a
# short pre-freeze gap reads as "fixed" while the time simply moved.
if [ -n "$ENTRY" ] && [ -n "$FREEZE" ]; then
    gap=$(( $(date -d "$FREEZE" +%s) - $(date -d "$ENTRY" +%s) ))
    say ""
    say "  >> PRE-FREEZE STALL : ${gap}s   (entry -> freeze; userspace LIVE, monitors dark)"
    [ "$gap" -gt 20 ] && say "     the machine was NOT asleep for those ${gap}s"
fi
if [ -n "$FREEZE" ] && [ -n "$S3" ]; then
    gap2=$(( $(date -d "$S3" +%s) - $(date -d "$FREEZE" +%s) ))
    say "  >> DEVICE-SUSPEND STALL: ${gap2}s   (freeze -> S3; everything frozen, drivers only)"
fi
if [ -n "$ENTRY" ] && [ -n "$S3" ]; then
    say "  >> TOTAL AWAKE AFTER ASKING TO SLEEP: $(( $(date -d "$S3" +%s) - $(date -d "$ENTRY" +%s) ))s"
fi

say ""
say "SLOWEST DEVICE CALLBACKS (pm_print_times — names the driver, if enabled):"
journalctl -k --since "$MARK" --no-pager 2>/dev/null \
    | grep -oE 'call [^ ]+ returned [0-9-]+ after [0-9]+ usecs' \
    | awk '{u=$(NF-1); printf "%12d us  %s\n", u, $2}' \
    | sort -rn | head -10 | sed 's/^/  /' | tee -a "$REPORT"
say "  (empty => pm_print_times was off, or no callback was individually slow,"
say "   which would put the time in the notifier chain rather than a driver)"

say ""
say "DISK WRITTEN DURING THE STALL — this is the discriminator:"
say "  a VRAM dump moves GIGABYTES here; a hung notifier moves nothing."
awk -F'\t' -v e="$ENTRY" '
    NR>1 && $1!="" {
        if (first=="") { first=$2; ft=$1 }
        last=$2; lt=$1; n++
    }
    END {
        if (n<2) { print "  (too few samples — the sampler was blocked again)"; exit }
        mb=(last-first)*512/1048576
        printf "  %s -> %s over %d samples\n", ft, lt, n
        printf "  written: %.1f MiB\n", mb
        if (mb > 1024) print "  >> GIGABYTES written: consistent with the VRAM dump"
        else           print "  >> essentially NOTHING written: NOT a VRAM dump — a stuck notifier"
    }' "$SAMPLES" | tee -a "$REPORT"
say "  (full sample set: $SAMPLES)"

say ""
say "COMPOSITOR"
say "  synctl answered : $RESP  (${SYNCTL_MS}ms)"
say "  threads on wake : $THREADS_BEFORE   (poll-blocked: $POLL_BEFORE, baseline $BASE_POLL)"
if [ "$FREEZE_X" = 1 ]; then
    say "  Xwayland state  : $(ps -o stat= -p "$XW" 2>/dev/null || echo GONE)"
    say "  NOTE: poll-blocked threads above baseline == apply_primary fired into"
    say "        the frozen X. Zero means the path was never exercised and this"
    say "        run proves nothing about the deadlock."
fi

say ""
say "SCREENS (read without touching input — this is the 207 fix)"
if [ -n "$OUTS" ]; then
    echo "$OUTS" | python3 -c '
import json,sys
try: d=json.load(sys.stdin)
except Exception: sys.exit(0)
for o in d: print("  %-10s %sx%s  at %s" % (o["name"], o["size"][0], o["size"][1], o["at"]))
' | tee -a "$REPORT"
    say "  outputs enumerate => compositor is drawing without input"
else
    say "  !! synctl did not answer — cannot read output state"
fi

if [ "$RESP" = "NO" ]; then
    say ""
    say "  !! VERDICT: FAIL — event loop is not dispatching. Capturing a backtrace"
    sudo -n gdb -p "$SYNUI" -batch -ex 'thread apply all bt' >"$OUT/wedge-bt.txt" 2>&1
    say "     $OUT/wedge-bt.txt"
    grep -iE 'primary|xcb|pthread_mutex_lock|wl_event_loop|libseat' "$OUT/wedge-bt.txt" \
        | head -12 | sed 's/^/     /' | tee -a "$REPORT"
else
    say ""
    say "  VERDICT: compositor PASS"
fi

hr; say "artifacts: $OUT"
