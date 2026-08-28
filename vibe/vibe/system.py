"""system.py — what this machine actually is, read off this machine.

⛔ THIS FILE EXISTS BECAUSE THE ASSISTANT MADE THE ANSWER UP. Asked "pc stats?"
it replied, in the confident register of a spec sheet: Intel Core i7-9700K,
16 GB DDR4, GTX 1650, 512 GB NVMe. The machine is an AMD Ryzen 5 5600X with
32 GB and an RTX 3060. Not one field was right, and nothing in the answer said
it was a guess — because to the model it was not a guess, it was the only kind
of answer available. The turn had routed to ASK, which has no tools, so the
question "what is this computer" was put to something that has never seen it.

**A fact about this machine is read, or it is not stated.** Every line below
comes from /proc, from a tool that queries the hardware, or from nowhere — and
a field that cannot be read is left out rather than filled in. There is no
formatting cleverness here on purpose: the value of this module is entirely
that its output is not composed by anything.

⚠ NOTHING HERE SHELLS OUT TO A PIPELINE ANY MORE. The facts above used to be
`_run("grep ... | head -1 | cut -d: -f2")` one-liners, which is a lot of shell
to get a string that is four lines into a file this process can just read.
The REPL's own commands below still run programs, because `ps` and `systemctl`
ARE the answer to what they ask.

⛔ AND THERE IS ONE FILE-MANAGER LIST. This file carried a second one, in a
different order and without synfiles in it, beside the real one in desktop.py —
so `/files` in the REPL and "open downloads" in the window opened different
programs. `open_file_manager`
is a thin call into desktop.py now. Two owners of one question is how the wrong
one gets edited, and this pair had already drifted.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
"""

import os
import platform
import re
import shutil
import subprocess
from pathlib import Path


def _run(argv: list, timeout: int = 5) -> str:
    """One program, no shell, and silence rather than an exception."""
    try:
        r = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
    except Exception:
        return ""
    return r.stdout.strip() if r.returncode == 0 else ""


def _read(path: str) -> str:
    try:
        return Path(path).read_text()
    except OSError:
        return ""


def _gib(kb: float) -> str:
    return f"{kb / 1048576:.1f} GiB"


# ── the fields ──────────────────────────────────────────────────────────────

def cpu() -> str:
    """Model name and how many threads the kernel sees.

    ⚠ `model name` IS ABSENT ON SOME ARCHITECTURES (arm64 has no such line), so
    a missing one is a missing field, not a crash."""
    m = re.search(r"^model name\s*:\s*(.+)$", _read("/proc/cpuinfo"), re.M)
    if not m:
        m = re.search(r"^Model\s*:\s*(.+)$", _read("/proc/cpuinfo"), re.M)
    if not m:
        return ""
    n = os.cpu_count()
    return f"{m.group(1).strip()}" + (f" ({n} threads)" if n else "")


def memory() -> str:
    """Total, and how much of it is actually in use.

    ⚠ AVAILABLE, NOT FREE. `MemFree` counts page cache as used and reports a
    32 GiB machine as nearly full; `MemAvailable` is the kernel's own estimate
    of what a new process could have, which is the number a person means."""
    info = _read("/proc/meminfo")
    tot = re.search(r"^MemTotal:\s+(\d+)", info, re.M)
    avail = re.search(r"^MemAvailable:\s+(\d+)", info, re.M)
    if not tot:
        return ""
    t = float(tot.group(1))
    if not avail:
        return _gib(t)
    used = t - float(avail.group(1))
    return f"{_gib(used)} used of {_gib(t)}"


def gpu() -> list:
    """Every graphics device, named by the hardware rather than by the driver.

    nvidia-smi first when it is there, because it also knows the VRAM and what
    the card is doing; lspci is the answer that works on every machine."""
    out = []
    if shutil.which("nvidia-smi"):
        smi = _run(["nvidia-smi",
                    "--query-gpu=name,memory.total,temperature.gpu",
                    "--format=csv,noheader,nounits"])
        for line in smi.splitlines():
            cols = [c.strip() for c in line.split(",")]
            if len(cols) >= 3 and cols[0]:
                out.append(f"{cols[0]} ({cols[1]} MiB VRAM, {cols[2]}°C)")
            elif cols and cols[0]:
                out.append(cols[0])
    if out:
        return out
    if shutil.which("lspci"):
        # -mm quotes each field, so a device whose name contains a colon (and
        # they do) does not split into the wrong number of pieces.
        for line in _run(["lspci", "-mm"]).splitlines():
            if not re.search(r'"(VGA compatible controller|3D controller|'
                             r'Display controller)"', line):
                continue
            fields = re.findall(r'"([^"]*)"', line)
            if len(fields) >= 3:
                out.append(f"{fields[1]} {fields[2]}".strip())
    return out


def disks() -> list:
    """Real filesystems and their free space, as `df` measures them.

    ⚠ tmpfs, devtmpfs AND THE REST ARE NOT DISKS. A listing that includes them
    tells the user their machine has fourteen drives.

    ⛔ AND ONE DEVICE IS ONE DISK. SynapseOS installs on btrfs, so `/`,
    `/home`, `/.snapshots`, `/var/log` and `/var/cache/pacman/pkg` are five
    subvolumes of the SAME filesystem — df reports the same 235 GB five times,
    and a reader counts five drives and adds them up. Keyed by SOURCE, first
    mountpoint seen (df lists them shortest-path first) wins."""
    if not shutil.which("df"):
        return []
    out = _run(["df", "-h", "--output=source,target,size,used,avail,pcent",
                "-x", "tmpfs", "-x", "devtmpfs", "-x", "efivarfs",
                "-x", "squashfs", "-x", "overlay"])
    got, seen = [], set()
    for r in out.splitlines()[1:]:
        f = r.split()
        if len(f) < 6 or f[0] in seen:
            continue
        seen.add(f[0])
        got.append(f"{f[1]}: {f[3]} used of {f[2]} ({f[5]} full, {f[4]} free)")
    return got


def os_release() -> str:
    m = re.search(r'^PRETTY_NAME="?([^"\n]+)"?', _read("/etc/os-release"), re.M)
    return m.group(1) if m else platform.system()


def kernel() -> str:
    return f"{platform.release()} ({platform.machine()})"


def uptime() -> str:
    raw = _read("/proc/uptime").split()
    if not raw:
        return ""
    secs = int(float(raw[0]))
    d, rem = divmod(secs, 86400)
    h, rem = divmod(rem, 3600)
    m = rem // 60
    bits = ([f"{d}d"] if d else []) + ([f"{h}h"] if d or h else []) + [f"{m}m"]
    return " ".join(bits)


def host() -> str:
    """The board, when the firmware bothered to write it down.

    ⚠ THE PLACEHOLDERS ARE THE COMMON CASE. Vendors ship "To Be Filled By
    O.E.M." and "Default string" in these files, and printing one is worse
    than printing nothing."""
    vendor = _read("/sys/devices/virtual/dmi/id/board_vendor").strip()
    name = _read("/sys/devices/virtual/dmi/id/board_name").strip()
    junk = {"", "to be filled by o.e.m.", "default string", "system manufacturer",
            "system product name", "unknown", "none", "not specified"}
    parts = [p for p in (vendor, name) if p.lower() not in junk]
    return " ".join(parts)


# ── the answer ──────────────────────────────────────────────────────────────

def machine_facts() -> str:
    """What this computer is, in the order someone asking would want it.

    ⚠ EVERY ROW IS OPTIONAL. A machine with no DMI, no lspci and no nvidia-smi
    still gets a correct short answer; what it does not get is an invented
    long one."""
    rows = [
        ("OS", os_release()),
        ("Kernel", kernel()),
        ("Board", host()),
        ("CPU", cpu()),
        ("Memory", memory()),
    ]
    lines = [f"{k}: {v}" for k, v in rows if v]
    for g in gpu():
        lines.append(f"GPU: {g}")
    for d in disks():
        lines.append(f"Disk {d}")
    up = uptime()
    if up:
        lines.append(f"Uptime: {up}")
    if not lines:
        return ("Error: this machine did not answer any of the questions asked "
                "of it — no /proc/cpuinfo, no /proc/meminfo, no os-release")
    return "\n".join(lines)


# ── the REPL's own commands ─────────────────────────────────────────────────
#
# ⚠ THESE ARE main.py's SLASH COMMANDS, not the model's tools. The model gets
# `system_info` above and `bash` for anything else; these exist so `/gpu`,
# `/ps` and `/services` stay one keystroke in the terminal REPL.

def sys_info() -> str:
    """`/sys` — the machine, plus what it is doing right now."""
    facts = machine_facts()
    load = _read("/proc/loadavg").split()
    if load[:3]:
        facts += f"\nLoad:   {' '.join(load[:3])}"
    return facts


def gpu_info() -> str:
    """`/gpu` — utilisation and temperature, which the fact sheet leaves out."""
    if not shutil.which("nvidia-smi"):
        cards = gpu()
        return "\n".join(cards) if cards else "No graphics device found."
    out = _run(["nvidia-smi",
                "--query-gpu=name,utilization.gpu,memory.used,memory.total,"
                "temperature.gpu", "--format=csv,noheader,nounits"])
    if not out:
        return "nvidia-smi answered nothing."
    lines = []
    for i, line in enumerate(out.splitlines()):
        cols = [c.strip() for c in line.split(",")]
        if len(cols) >= 5:
            lines.append(f"GPU {i}: {cols[0]}\n"
                         f"  Util: {cols[1]}%  |  VRAM: {cols[2]} / {cols[3]} MiB"
                         f"  |  Temp: {cols[4]}°C")
    return "\n".join(lines) if lines else out


def net_info() -> str:
    """`/net` — the interfaces, and what is listening."""
    parts = []
    out = _run(["ip", "-brief", "addr"])
    if out:
        parts.append("Interfaces:\n" + "\n".join(f"  {l}" for l in out.splitlines()))
    out = _run(["ss", "-tulpn"])
    if out:
        parts.append("Listening ports:\n" + "\n".join(f"  {l}" for l in out.splitlines()))
    return "\n\n".join(parts) if parts else "Could not retrieve network info."


def ps_list(filter_str: str | None = None) -> str:
    """`/ps` — the busiest processes, optionally filtered by name."""
    out = _run(["ps", "aux", "--sort=-%cpu"])
    if not out:
        return "Error: ps answered nothing."
    rows = out.splitlines()
    head, body = rows[0], rows[1:]
    if filter_str:
        body = [l for l in body if filter_str.lower() in l.lower()]
    return "\n".join([head] + body[:25]) if body else "No matching processes."


def kill_process(target: str) -> str:
    """`/kill` — by pid when it is a number, by name otherwise.

    ⚠ NAMED PROCESSES ARE MATCHED EXACTLY. `pkill -f` on a substring is how a
    request to stop `code` ends a session's whole process tree."""
    t = (target or "").strip()
    if not t:
        return "Error: nothing to kill"
    if t.isdigit():
        try:
            os.kill(int(t), 15)
        except OSError as e:
            return f"Error: could not signal {t}: {e}"
        return f"Sent SIGTERM to {t}."
    if not shutil.which("pkill"):
        return "Error: pkill is not installed"
    r = subprocess.run(["pkill", "-x", t], capture_output=True, text=True)
    if r.returncode == 1:
        return f"No process is called exactly '{t}'."
    if r.returncode != 0:
        return f"Error: pkill {t} failed: {r.stderr.strip()}"
    return f"Sent SIGTERM to every process called '{t}'."


def service_control(name: str, action: str = "status") -> str:
    """`/service` — one systemd unit.

    ⛔ START/STOP/RESTART NEED ROOT AND ARE NOT SILENTLY ESCALATED HERE. The
    REPL prints what systemctl said, including its refusal; a helper that
    reached for sudo would be a privilege decision made by a chat prompt."""
    n = (name or "").strip()
    if not n:
        return "Error: no service named"
    a = (action or "status").strip()
    if a not in ("status", "start", "stop", "restart", "enable", "disable",
                 "is-active", "is-enabled"):
        return f"Error: '{a}' is not one of status/start/stop/restart/enable/disable"
    if not shutil.which("systemctl"):
        return "Error: systemctl is not installed"
    r = subprocess.run(["systemctl", "--no-pager", a, n],
                       capture_output=True, text=True)
    return (r.stdout + r.stderr).strip() or f"systemctl {a} {n}: exit {r.returncode}"


def services_list(filter_str: str | None = None) -> str:
    """`/services` — what is running."""
    if not shutil.which("systemctl"):
        return "Error: systemctl is not installed"
    r = subprocess.run(["systemctl", "list-units", "--type=service",
                        "--state=running", "--no-pager", "--no-legend"],
                       capture_output=True, text=True)
    if r.returncode != 0:
        return f"Error: {(r.stderr or r.stdout).strip()}"
    lines = r.stdout.strip().splitlines()
    if filter_str:
        lines = [l for l in lines if filter_str.lower() in l.lower()]
    return "\n".join(lines) if lines else "No matching running services."


def open_file_manager(path: str = ".") -> str:
    """`/files` — the SAME file manager the assistant opens.

    ⛔ NOT ITS OWN LIST. This used to be a second one, in its own order and
    without synfiles in it, so the REPL and the chat window opened different
    programs for the same request — and only one of the two got fixed when
    synfiles turned out to need `gui` in its argv."""
    from vibe import desktop
    return desktop._open_dir(Path(path).expanduser().resolve())
