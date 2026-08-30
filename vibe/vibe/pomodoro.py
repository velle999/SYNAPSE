"""The pomodoro timer — one running session at a time, on disk, visible from
the bar.

⛔ THE RUNNING TIMER IS A ROW AND A STATE FILE, NOT A VARIABLE IN A SERVER.
velle.ai kept it in a Map keyed by the websocket that started it, which on this
desktop would fail three ways at once: `vibe serve` lives only as long as the
chat window, so closing the window would stop the clock; the CLI is a different
process and could not see it; and the BAR is a third process, which is where
velle asked for it to show up. So the row IS the timer (running == no
ended_at), and a small state file under ~/.config/synui carries the deadline to
the bar — the same shape the wake indicator already uses.

⚠ THE BAR DOES THE COUNTING. The file carries the END TIME, not a remaining
count, so the indicator ticks in QML arithmetic with nothing spawned per
second. A state file that had to be rewritten every second to stay true would
be a write per second forever, on a laptop.

⚠ `completed` MEANS THE WHOLE THING RAN. velle.ai set it on any stop, so a
pomodoro abandoned after four minutes counted as a full one and the day's total
was whatever had been started. Stopping early records the end and leaves
completed at 0; the stats below only count the ones that finished.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
"""

from __future__ import annotations

import os
import time
from pathlib import Path

from vibe.store import connect, one, rows

STATE = (Path(os.environ.get("XDG_CONFIG_HOME") or (Path.home() / ".config"))
         / "synui" / "pomodoro.state")


def _state_path() -> Path:
    """⚠ Resolved per call, not at import: the tests move XDG_CONFIG_HOME, and
    a module-level constant would have them writing the real desktop's."""
    return (Path(os.environ.get("XDG_CONFIG_HOME") or (Path.home() / ".config"))
            / "synui" / "pomodoro.state")


def _publish(session: dict | None) -> None:
    """Tell the bar. Absent file == nothing running, which is also what a
    desktop that has never used the timer looks like."""
    path = _state_path()
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        if session is None:
            path.unlink(missing_ok=True)
            return
        body = ("# Written by vibe. The bar reads it.\n"
                f"id = {session['id']}\n"
                f"task = {session.get('task') or ''}\n"
                f"kind = {session['type']}\n"
                f"duration = {session['duration']}\n"
                f"started = {int(session['started_epoch'])}\n"
                f"ends = {int(session['ends_epoch'])}\n")
        tmp = path.with_suffix(".tmp")
        tmp.write_text(body, encoding="utf-8")
        os.replace(tmp, path)
    except OSError:
        # No indicator on a desktop whose config dir is unwritable. The timer
        # itself is in the database and still works.
        pass


def _epoch(sqlite_ts: str) -> float:
    """`datetime('now','localtime')` text back to an epoch.

    ⛔ THE COLUMN IS LOCAL TIME, NOT UTC — that is velle.ai's schema default and
    changing it would misread every existing row. So it is parsed AS local
    time; reading it as UTC puts every deadline hours out, which on a 25 minute
    timer means one that is over before it starts."""
    return time.mktime(time.strptime(sqlite_ts.split(".")[0], "%Y-%m-%d %H:%M:%S"))


def _decorate(row: dict) -> dict:
    started = _epoch(row["started_at"])
    ends = started + row["duration"] * 60
    now = time.time()
    remaining = max(0, int(round(ends - now)))
    return {**row,
            "started_epoch": started,
            "ends_epoch": ends,
            "elapsed_seconds": int(now - started),
            "remaining_seconds": remaining,
            "remaining": f"{remaining // 60}:{remaining % 60:02d}",
            "expired": now >= ends}


def _running(c) -> dict | None:
    r = one(c.execute("SELECT * FROM pomodoro_sessions WHERE ended_at IS NULL"
                      " ORDER BY id DESC LIMIT 1"))
    return _decorate(r) if r else None


def start(task: str | None = None, duration: int = 25,
          kind: str = "focus") -> dict:
    """Begin one. Refuses while another is running rather than replacing it —
    two timers started by accident is how somebody loses the one they meant."""
    duration = max(1, int(duration))
    with connect() as c:
        live = _running(c)
        if live and not live["expired"]:
            return {"error": "already running", "session": live}
        if live:
            # Expired and never collected: close it out honestly first.
            _finish(c, live)
        cur = c.execute("INSERT INTO pomodoro_sessions (task, duration, type)"
                        " VALUES (?,?,?)", (task, duration, kind))
        fresh = _decorate(one(c.execute(
            "SELECT * FROM pomodoro_sessions WHERE id = ?", (cur.lastrowid,))))
    _publish(fresh)
    return fresh


def _finish(c, live: dict) -> dict:
    """Close a session out. `completed` only when the full duration ran."""
    done = 1 if live["remaining_seconds"] == 0 else 0
    c.execute("UPDATE pomodoro_sessions SET completed = ?,"
              " ended_at = datetime('now','localtime') WHERE id = ?",
              (done, live["id"]))
    return {**live, "completed": done, "ended": True}


def stop() -> dict | None:
    with connect() as c:
        live = _running(c)
        if not live:
            _publish(None)
            return None
        out = _finish(c, live)
    _publish(None)
    return out


def status() -> dict | None:
    """What is running, or None.

    ⛔ IT RECONCILES. A timer that ran out while nobody was looking is not
    still running, and this is the only code that ever notices — the bar can
    count but cannot write. So the first reader after the deadline closes the
    session and clears the indicator."""
    with connect() as c:
        live = _running(c)
        if not live:
            return None
        if live["expired"]:
            out = _finish(c, live)
            _publish(None)
            _announce(out)
            return {**out, "just_finished": True}
    return live


def _announce(session: dict) -> None:
    """Say it on the desktop when a timer runs out.

    ⛔ EXACTLY ONE READER EVER SEES THE EXPIRY. The row is closed and the state
    file removed in the same breath, so whichever of the bar, the chat window
    or the CLI notices first is the only one that can notify — which is why the
    notification lives here rather than in any of the three. A copy in the bar
    would fire again in every other window that later read the same file.

    ⚠ Best effort. A desktop with no notification daemon still gets a finished
    timer; it just does not get told, and that is not worth an error path.
    """
    import shutil
    import subprocess
    if not shutil.which("notify-send"):
        return
    task = session.get("task") or ""
    body = f"{session['duration']} minutes done" + (f" — {task}" if task else "")
    try:
        subprocess.run(["notify-send", "-a", "vibe", "Focus timer", body],
                       timeout=5, check=False)
    except (OSError, subprocess.SubprocessError):
        pass


def today_stats() -> dict:
    with connect() as c:
        got = rows(c.execute(
            "SELECT duration FROM pomodoro_sessions WHERE completed = 1"
            " AND date(started_at) = date('now','localtime')"))
    total = sum(r["duration"] for r in got)
    return {"sessions": len(got), "total_minutes": total,
            "total_display": f"{total // 60}h {total % 60}m"}


def week_stats() -> dict:
    with connect() as c:
        got = rows(c.execute(
            "SELECT duration, started_at FROM pomodoro_sessions"
            " WHERE completed = 1"
            " AND started_at >= datetime('now','localtime','-7 days')"))
    by_day: dict[str, int] = {}
    for r in got:
        by_day[r["started_at"][:10]] = by_day.get(r["started_at"][:10], 0) + r["duration"]
    return {"sessions": len(got),
            "total_minutes": sum(r["duration"] for r in got),
            "by_day": by_day}


def line(s: dict | None) -> str:
    if not s:
        t = today_stats()
        return f"No timer running. Today: {t['sessions']} done, {t['total_display']}."
    task = f" — {s['task']}" if s.get("task") else ""
    if s.get("just_finished"):
        return f"Finished{task}. {s['duration']} minutes."
    return f"{s['remaining']} left{task}"
