"""Todos, habits, goals and the pomodoro — the productivity half of velle.ai,
ported onto the companion store.

⛔ THE SCHEMAS ARE VELLE.AI'S, UNCHANGED. Column for column, check constraint
for check constraint, so an existing companion.db opens here and reads back the
same. What changed is where the RUNNING pomodoro lives, and that change is the
whole reason this file is not a transliteration — see store.py's note: the
timer was a Map entry keyed by websocket, which on this desktop would mean a
timer that dies when the chat window is closed and that the bar could never
see.

⚠ EVERY WRITE IS THE PERSON'S OWN. Nothing here is a model-proposed action, so
none of it goes through the confirmation gate that bash and write_file do; the
tools that expose these to the model are declared separately in tools.py and
the writing ones are gated there.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
"""

from __future__ import annotations

import os
from datetime import date, datetime, timedelta
from pathlib import Path

from vibe.store import connect, one, rows

# ── Todos ───────────────────────────────────────────────────────────────────

_EDITABLE = ("content", "project", "priority", "due_date", "tags", "status")


def todo_add(content: str, project: str = "inbox", priority: int = 2,
             due_date: str | None = None, tags: str | None = None) -> dict:
    with connect() as c:
        cur = c.execute(
            "INSERT INTO todos (content, project, priority, due_date, tags)"
            " VALUES (?,?,?,?,?)", (content, project, priority, due_date, tags))
        return _todo(c, cur.lastrowid)


def _todo(c, tid: int) -> dict | None:
    return one(c.execute("SELECT * FROM todos WHERE id = ?", (tid,)))


def todo_get(tid: int) -> dict | None:
    with connect() as c:
        return _todo(c, tid)


def todo_all(status: str | None = None) -> list[dict]:
    with connect() as c:
        if status:
            return rows(c.execute(
                "SELECT * FROM todos WHERE status = ?"
                " ORDER BY priority ASC, created_at DESC", (status,)))
        return rows(c.execute(
            "SELECT * FROM todos WHERE status != 'cancelled'"
            " ORDER BY status ASC, priority ASC, created_at DESC"))


def todo_today() -> list[dict]:
    with connect() as c:
        return rows(c.execute(
            "SELECT * FROM todos WHERE status IN ('todo','doing')"
            " AND (due_date = date('now','localtime') OR due_date IS NULL)"
            " ORDER BY priority ASC"))


def todo_overdue() -> list[dict]:
    with connect() as c:
        return rows(c.execute(
            "SELECT * FROM todos WHERE status = 'todo'"
            " AND due_date < date('now','localtime') ORDER BY due_date ASC"))


def _todo_set(tid: int, sql: str, args: tuple = ()) -> dict | None:
    with connect() as c:
        c.execute(sql, args + (tid,))
        return _todo(c, tid)


def todo_complete(tid: int) -> dict | None:
    return _todo_set(tid, "UPDATE todos SET status = 'done',"
                          " completed_at = datetime('now','localtime') WHERE id = ?")


def todo_start(tid: int) -> dict | None:
    return _todo_set(tid, "UPDATE todos SET status = 'doing' WHERE id = ?")


def todo_reopen(tid: int) -> dict | None:
    """Put a finished task back.

    ⚠ `completed_at` IS CLEARED, not left behind. It is the column
    `todo_stats` counts as finished today, so a task reopened with its
    timestamp still on it goes on being counted — the day's total would claim
    work that has been put back on the list.
    """
    return _todo_set(tid, "UPDATE todos SET status = 'todo',"
                          " completed_at = NULL WHERE id = ?")


def todo_cancel(tid: int) -> dict | None:
    return _todo_set(tid, "UPDATE todos SET status = 'cancelled' WHERE id = ?")


def todo_edit(tid: int, **updates) -> dict | None:
    """⚠ The column names are whitelisted rather than interpolated from the
    caller's keys — this is the one place here that builds SQL by hand."""
    fields, vals = [], []
    for k, v in updates.items():
        if k in _EDITABLE:
            fields.append(f"{k} = ?")
            vals.append(v)
    if not fields:
        return todo_get(tid)
    with connect() as c:
        c.execute(f"UPDATE todos SET {', '.join(fields)} WHERE id = ?",
                  (*vals, tid))
        return _todo(c, tid)


def todo_delete(tid: int) -> bool:
    with connect() as c:
        return c.execute("DELETE FROM todos WHERE id = ?", (tid,)).rowcount > 0


def todo_stats() -> dict:
    with connect() as c:
        def n(sql, args=()):
            return c.execute(sql, args).fetchone()[0]
        total = n("SELECT COUNT(*) FROM todos")
        done = n("SELECT COUNT(*) FROM todos WHERE status='done'")
        active = n("SELECT COUNT(*) FROM todos WHERE status IN ('todo','doing')")
        overdue = n("SELECT COUNT(*) FROM todos WHERE status='todo'"
                    " AND due_date < date('now','localtime')")
        today_done = n("SELECT COUNT(*) FROM todos WHERE status='done'"
                       " AND date(completed_at) = date('now','localtime')")
    return {"total": total, "done": done, "active": active, "overdue": overdue,
            "today_done": today_done,
            "completion_rate": round(done / total * 100, 1) if total else 0.0}


_PRIO = {1: "!!!", 2: "!!", 3: "!", 4: " "}
_STATUS = {"todo": "[ ]", "doing": "[~]", "done": "[x]", "cancelled": "[-]"}


def todo_line(t: dict) -> str:
    bits = [_STATUS.get(t["status"], "[ ]"), f"#{t['id']}", t["content"]]
    prio = _PRIO.get(t["priority"], " ").strip()
    if prio:
        bits.insert(1, prio)
    if t.get("due_date"):
        bits.append(f"due {t['due_date']}")
    if t.get("project") and t["project"] != "inbox":
        bits.append(f"[{t['project']}]")
    if t.get("tags"):
        bits.append(" ".join("#" + x.strip() for x in t["tags"].split(",")))
    return " ".join(bits)


# ── Habits ──────────────────────────────────────────────────────────────────

def habit_add(name: str, icon: str = "*", frequency: str = "daily",
              target: int = 1) -> dict:
    with connect() as c:
        cur = c.execute("INSERT INTO habits (name, icon, frequency, target)"
                        " VALUES (?,?,?,?)", (name, icon, frequency, target))
        return one(c.execute("SELECT * FROM habits WHERE id = ?", (cur.lastrowid,)))


def habit_all() -> list[dict]:
    with connect() as c:
        return rows(c.execute("SELECT * FROM habits ORDER BY created_at ASC"))


def habit_delete(hid: int) -> bool:
    with connect() as c:
        c.execute("DELETE FROM habit_logs WHERE habit_id = ?", (hid,))
        return c.execute("DELETE FROM habits WHERE id = ?", (hid,)).rowcount > 0


def habit_check(hid: int, day: str | None = None) -> dict:
    d = day or date.today().isoformat()
    with connect() as c:
        c.execute("INSERT OR REPLACE INTO habit_logs (habit_id, value, date)"
                  " VALUES (?,1,?)", (hid, d))
    return {"habit_id": hid, "date": d, "checked": True}


def habit_uncheck(hid: int, day: str | None = None) -> dict:
    d = day or date.today().isoformat()
    with connect() as c:
        c.execute("DELETE FROM habit_logs WHERE habit_id = ? AND date = ?", (hid, d))
    return {"habit_id": hid, "date": d, "checked": False}


def habit_streak(hid: int) -> int:
    """Consecutive days up to today.

    ⛔ A STREAK IS NOT BROKEN BY TODAY NOT BEING DONE YET. Yesterday counts as
    the anchor, or every streak would read zero every morning until the box was
    ticked — which is the one time of day somebody looks at it for motivation."""
    with connect() as c:
        days = [r["date"] for r in c.execute(
            "SELECT date FROM habit_logs WHERE habit_id = ? ORDER BY date DESC",
            (hid,))]
    if not days:
        return 0
    today = date.today()
    first = date.fromisoformat(days[0])
    if (today - first).days > 1:
        return 0
    streak = 1
    for prev, cur in zip(days, days[1:]):
        if (date.fromisoformat(prev) - date.fromisoformat(cur)).days == 1:
            streak += 1
        else:
            break
    return streak


def habit_week(hid: int) -> list[dict]:
    with connect() as c:
        done = {r["date"] for r in c.execute(
            "SELECT date FROM habit_logs WHERE habit_id = ?", (hid,))}
    out = []
    for i in range(6, -1, -1):
        d = (date.today() - timedelta(days=i)).isoformat()
        out.append({"date": d, "done": d in done})
    return out


def habit_rate(hid: int, days: int = 30) -> float:
    with connect() as c:
        n = c.execute(
            "SELECT COUNT(DISTINCT date) FROM habit_logs WHERE habit_id = ?"
            " AND date >= date('now','localtime',?)",
            (hid, f"-{int(days)} days")).fetchone()[0]
    return round(n / days * 100, 1) if days else 0.0


def habit_dashboard() -> list[dict]:
    today = date.today().isoformat()
    out = []
    for h in habit_all():
        week = habit_week(h["id"])
        out.append({**h, "streak": habit_streak(h["id"]), "week": week,
                    "rate_30d": habit_rate(h["id"], 30),
                    "done_today": any(d["date"] == today and d["done"] for d in week)})
    return out


def habit_line(h: dict) -> str:
    grid = "".join("#" if d["done"] else "." for d in h["week"])
    mark = "[x]" if h["done_today"] else "[ ]"
    streak = f" {h['streak']}d" if h["streak"] else ""
    return f"{mark} #{h['id']} {h['name']}  {grid}{streak}  {h['rate_30d']:.0f}%"


# ── Goals ───────────────────────────────────────────────────────────────────

def goal_add(title: str, description: str | None = None,
             target_date: str | None = None, category: str = "general") -> dict:
    with connect() as c:
        cur = c.execute("INSERT INTO goals (title, description, target_date,"
                        " category) VALUES (?,?,?,?)",
                        (title, description, target_date, category))
        return _goal(c, cur.lastrowid)


def _goal(c, gid: int) -> dict | None:
    g = one(c.execute("SELECT * FROM goals WHERE id = ?", (gid,)))
    if g:
        g["milestones"] = rows(c.execute(
            "SELECT * FROM milestones WHERE goal_id = ? ORDER BY id ASC", (gid,)))
    return g


def goal_get(gid: int) -> dict | None:
    with connect() as c:
        return _goal(c, gid)


def goal_all(status: str = "active") -> list[dict]:
    with connect() as c:
        out = rows(c.execute("SELECT * FROM goals WHERE status = ?"
                             " ORDER BY created_at DESC", (status,)))
        for g in out:
            g["milestones"] = rows(c.execute(
                "SELECT * FROM milestones WHERE goal_id = ? ORDER BY id ASC",
                (g["id"],)))
        return out


def goal_progress(gid: int, progress: int) -> dict | None:
    progress = max(0, min(100, int(progress)))
    with connect() as c:
        c.execute("UPDATE goals SET progress = ?, status = CASE WHEN ? >= 100"
                  " THEN 'completed' ELSE status END WHERE id = ?",
                  (progress, progress, gid))
        return _goal(c, gid)


def goal_milestone(gid: int, title: str) -> dict | None:
    with connect() as c:
        c.execute("INSERT INTO milestones (goal_id, title) VALUES (?,?)",
                  (gid, title))
        _recalc(c, gid)
        return _goal(c, gid)


def goal_milestone_done(mid: int) -> dict | None:
    with connect() as c:
        ms = one(c.execute("SELECT * FROM milestones WHERE id = ?", (mid,)))
        if not ms:
            return None
        c.execute("UPDATE milestones SET completed = 1,"
                  " completed_at = datetime('now','localtime') WHERE id = ?", (mid,))
        _recalc(c, ms["goal_id"])
        return _goal(c, ms["goal_id"])


def _recalc(c, gid: int) -> None:
    """Progress FOLLOWS the milestones once one has been completed.

    ⛔ NOT FROM THE MOMENT THE FIRST MILESTONE EXISTS. A goal sitting at a
    hand-set 40% has 0 of 1 milestones done the instant one is added, so a
    recalculation there rewrites 40% to 0% as a side effect of breaking the
    work down — the user typed a number and watched adding a subtask erase it.
    Milestones take over when one of them is finished, which is the first
    moment they have anything to say."""
    total = c.execute("SELECT COUNT(*) FROM milestones WHERE goal_id = ?",
                      (gid,)).fetchone()[0]
    done = c.execute("SELECT COUNT(*) FROM milestones WHERE goal_id = ?"
                     " AND completed = 1", (gid,)).fetchone()[0]
    if not total or not done:
        return
    pct = round(done / total * 100)
    c.execute("UPDATE goals SET progress = ?, status = CASE WHEN ? >= 100"
              " THEN 'completed' ELSE status END WHERE id = ?", (pct, pct, gid))


def goal_delete(gid: int) -> bool:
    with connect() as c:
        c.execute("DELETE FROM milestones WHERE goal_id = ?", (gid,))
        return c.execute("DELETE FROM goals WHERE id = ?", (gid,)).rowcount > 0


def progress_bar(pct: int, width: int = 20) -> str:
    filled = int(round(pct / 100 * width))
    return "#" * filled + "-" * (width - filled)


def goal_line(g: dict) -> str:
    due = f" due {g['target_date']}" if g.get("target_date") else ""
    ms = g.get("milestones") or []
    done = sum(1 for m in ms if m["completed"])
    tail = f"  {done}/{len(ms)} milestones" if ms else ""
    return (f"#{g['id']} {g['title']}{due}\n"
            f"    [{progress_bar(g['progress'])}] {g['progress']}%{tail}")
