"""System control helpers for Vibe Code slash commands."""

import shutil
import subprocess


def _run(cmd: str, timeout: int = 10) -> tuple[str, int]:
    try:
        r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=timeout)
        out = (r.stdout + r.stderr).strip()
        return out, r.returncode
    except subprocess.TimeoutExpired:
        return f"[timed out after {timeout}s]", 1
    except Exception as e:
        return str(e), 1


def sys_info() -> str:
    parts = []

    out, _ = _run("uptime -p")
    if out:
        parts.append(f"Uptime:  {out}")

    model, _ = _run("grep 'model name' /proc/cpuinfo | head -1 | cut -d: -f2")
    if model:
        parts.append(f"CPU:     {model.strip()}")

    cpu_use, _ = _run(
        "top -bn1 | grep 'Cpu(s)' | "
        "awk '{for(i=1;i<=NF;i++) if($i~/us,/) {gsub(/,/,\"\",$i); print $i\"%\"; exit}}'"
    )
    if cpu_use:
        parts.append(f"CPU use: {cpu_use}")

    mem, _ = _run("free -h | awk 'NR==2{printf \"%s / %s used\", $3, $2}'")
    if mem:
        parts.append(f"Memory:  {mem}")

    disk, _ = _run("df -h / | awk 'NR==2{printf \"%s / %s used (%s)\", $3, $2, $5}'")
    if disk:
        parts.append(f"Disk /:  {disk}")

    return "\n".join(parts) if parts else "Could not retrieve system info."


def gpu_info() -> str:
    if not shutil.which("nvidia-smi"):
        return "nvidia-smi not found."
    out, rc = _run(
        "nvidia-smi --query-gpu=name,utilization.gpu,memory.used,memory.total,temperature.gpu"
        " --format=csv,noheader,nounits"
    )
    if rc != 0 or not out:
        return f"nvidia-smi error: {out}"
    lines = []
    for i, line in enumerate(out.strip().splitlines()):
        cols = [c.strip() for c in line.split(",")]
        if len(cols) >= 5:
            name, util, mem_used, mem_total, temp = cols[:5]
            lines.append(
                f"GPU {i}: {name}\n"
                f"  Util: {util}%  |  VRAM: {mem_used} / {mem_total} MiB  |  Temp: {temp}°C"
            )
    return "\n".join(lines) if lines else out


def net_info() -> str:
    parts = []
    out, _ = _run("ip -brief addr")
    if out:
        parts.append("Interfaces:\n" + "\n".join(f"  {l}" for l in out.splitlines()))
    out, _ = _run("ss -tulpn 2>/dev/null")
    if out:
        parts.append("Listening ports:\n" + "\n".join(f"  {l}" for l in out.splitlines()))
    return "\n\n".join(parts) if parts else "Could not retrieve network info."


def ps_list(filter_str: str | None = None) -> str:
    out, rc = _run("ps aux --sort=-%cpu")
    if rc != 0:
        return f"Error: {out}"
    lines = out.splitlines()
    header = lines[0] if lines else ""
    rows = lines[1:]
    if filter_str:
        rows = [r for r in rows if filter_str.lower() in r.lower()]
    rows = rows[:25]
    return "\n".join([header] + rows) if rows else "No matching processes."


def kill_process(target: str) -> str:
    import shlex
    target = target.strip()
    if not target:
        return "Error: no target specified."
    if target.isdigit():
        out, rc = _run(f"kill {target}")
        if rc == 0:
            return f"Sent SIGTERM to PID {target}."
        return f"kill {target}: {out or f'failed (exit {rc})'}"
    else:
        out, rc = _run(f"pkill -f {shlex.quote(target)}")
        if rc == 0:
            return f"Sent SIGTERM to processes matching '{target}'."
        return f"pkill '{target}': {out or 'no matching processes'}"


_VALID_SERVICE_ACTIONS = {"status", "start", "stop", "restart", "reload", "enable", "disable"}


def service_control(name: str, action: str = "status") -> str:
    if action not in _VALID_SERVICE_ACTIONS:
        return f"Unknown action '{action}'. Valid: {', '.join(sorted(_VALID_SERVICE_ACTIONS))}"
    if action == "status":
        cmd = f"systemctl status {name} --no-pager -l"
    else:
        cmd = f"sudo systemctl {action} {name}"
    out, rc = _run(cmd, timeout=15)
    return out or f"[exit code {rc}]"


def open_file_manager(path: str = ".") -> str:
    managers = ["thunar", "nautilus", "dolphin", "nemo", "pcmanfm"]
    for mgr in managers:
        if shutil.which(mgr):
            subprocess.Popen(
                [mgr, path],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                start_new_session=True,
            )
            return f"Opened {mgr} at {path}"
    # Fallback: xdg-open
    if shutil.which("xdg-open"):
        subprocess.Popen(
            ["xdg-open", path],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
        return f"Opened {path} with xdg-open"
    return "No file manager found (tried: " + ", ".join(managers) + ", xdg-open)"


def services_list(filter_str: str | None = None) -> str:
    out, rc = _run("systemctl list-units --type=service --state=running --no-pager --no-legend")
    if rc != 0:
        return f"Error: {out}"
    lines = out.strip().splitlines()
    if filter_str:
        lines = [l for l in lines if filter_str.lower() in l.lower()]
    return "\n".join(lines) if lines else "No matching running services."
