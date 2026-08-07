#!/usr/bin/env python3
"""synui-record-status — is a screen recording running, and for how long?

One line of JSON for the bar's recording indicator (quickshell's
modules/Recording.qml parses it), shaped like synui-game-status:

    {"recording": true, "pid": 4711, "elapsed": 83,
     "file": "/home/velle/Videos/synui-20260807-031602.mp4",
     "output": "DP-3", "audio": "alsa_output.pci-0000_01_00.1.hdmi-stereo.monitor"}

**The state IS the wf-recorder process** — synui-record deliberately keeps no
state file, so there is nothing to go stale when a recorder dies (disk full,
screencopy refused, OOM). That property is worth preserving, so this reads
/proc directly rather than introducing the very file it avoids: a pill that
says "recording" over a dead recorder would be worse than no pill at all.

/proc rather than `pgrep`/`ps`, for three reasons: one pass gets liveness,
start time AND the command line (which is where the output name, the file and
the audio device already are, put there by synui-record); it spawns nothing,
and this is polled every couple of seconds for as long as the session lasts;
and it cannot be fooled by the poller matching its own `pgrep` argument.

Python, not shell, for the same reason synui-game-status is: the file path and
the PulseAudio device name end up inside a JSON string, and json.dumps escapes
them. A shell here-doc would let a quote in a path close the string.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
https://github.com/velle999/SYNAPSE
"""

import json
import os
import sys

PROC = "/proc"
COMM = "wf-recorder"


def uptime():
    with open(f"{PROC}/uptime", encoding="ascii") as f:
        return float(f.read().split()[0])


def start_time(pid):
    """Seconds since boot at which PID started, from /proc/PID/stat field 22.

    comm is field 2 and is wrapped in parentheses *and may itself contain
    spaces and parentheses*, so the line is split from the RIGHT of the last
    ')' — splitting on whitespace from the left mis-numbers every later field
    for any process whose name has a space in it.
    """
    with open(f"{PROC}/{pid}/stat", encoding="utf-8", errors="replace") as f:
        after = f.read().rsplit(")", 1)[1].split()
    # after[0] is field 3 (state), so field 22 is index 19.
    return int(after[19]) / os.sysconf("SC_CLK_TCK")


def cmdline(pid):
    with open(f"{PROC}/{pid}/cmdline", "rb") as f:
        return [a.decode("utf-8", "replace") for a in f.read().split(b"\0") if a]


def find_recorder():
    """The longest-running wf-recorder, or None.

    "Longest-running" only matters in the case that should not happen (two
    recorders): the older one is the take already in progress, and it is the
    one a stop is meant to end.
    """
    best = None
    for name in os.listdir(PROC):
        if not name.isdigit():
            continue
        pid = int(name)
        try:
            with open(f"{PROC}/{pid}/comm", encoding="ascii", errors="replace") as f:
                if f.read().strip() != COMM:
                    continue
            started = start_time(pid)
            args = cmdline(pid)
        except (OSError, ValueError, IndexError):
            # The process exited between listdir and the read, or /proc is
            # hidden (hidepid). Either way it is not something to report on.
            continue
        if best is None or started < best[1]:
            best = (pid, started, args)
    return best


def parse_args(args):
    """Recover what synui-record told wf-recorder to do.

    Only the spellings synui-record actually emits are handled — `-f FILE`,
    `-o OUTPUT`, `-aDEVICE` — plus the separated `-a DEVICE` form for a
    recorder someone started by hand. Anything unrecognised is simply not
    reported; guessing here would put a wrong filename in a tooltip.
    """
    info = {"file": "", "output": "", "audio": ""}
    i = 0
    while i < len(args):
        a = args[i]
        if a == "-f" and i + 1 < len(args):
            info["file"] = args[i + 1]
            i += 1
        elif a == "-o" and i + 1 < len(args):
            info["output"] = args[i + 1]
            i += 1
        elif a == "-a" and i + 1 < len(args):
            info["audio"] = args[i + 1]
            i += 1
        elif a.startswith("-a") and len(a) > 2:
            info["audio"] = a[2:]
        i += 1
    return info


def main():
    found = find_recorder()
    if found is None:
        print(json.dumps({"recording": False, "elapsed": 0}))
        return 0

    pid, started, args = found
    out = {"recording": True, "pid": pid}
    try:
        # Clamped at 0: uptime and starttime are sampled a moment apart, and a
        # recorder started this instant can otherwise come out at -1, which the
        # bar would format as a negative clock.
        out["elapsed"] = max(0, int(uptime() - started))
    except (OSError, ValueError, IndexError):
        out["elapsed"] = 0
    out.update(parse_args(args))
    print(json.dumps(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
