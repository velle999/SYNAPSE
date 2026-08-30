"""The command-line half of the companion: personas, todos, habits, goals and
the pomodoro.

⛔ EVERY ONE OF THESE HAS A WINDOW HALF TOO (the chat window's /commands and the
bar's timer). That is the house rule, and it is also what makes the CLI worth
having: the same store, the same words, so a habit ticked from a terminal is
the one the window shows.

⚠ ARGUMENT PARSING IS BY HAND rather than argparse, to match the rest of vibe's
verbs — and because these take free text ("vibe todo add buy milk on tuesday")
where argparse would demand quoting.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
"""

from __future__ import annotations

import time

from vibe import personas, pomodoro
from vibe import productivity as P


def _flag(argv: list[str], name: str, default=None):
    """Pull `--name value` out of argv, returning (value, remaining)."""
    if name in argv:
        i = argv.index(name)
        if i + 1 < len(argv):
            return argv[i + 1], argv[:i] + argv[i + 2:]
        return default, argv[:i]
    return default, argv


def _int(val, default):
    try:
        return int(val)
    except (TypeError, ValueError):
        return default


# ── persona ─────────────────────────────────────────────────────────────────

def verb_persona(argv: list[str]) -> int:
    if not argv or argv[0] in ("list", "-l", "--list"):
        for key in personas.names():
            print(personas.line(key))
        return 0
    if argv[0] == "show":
        p = personas.get()
        print(f"{personas.current()} — {p['name']}")
        print(f"temperature {p['temperature']}   accent {p['accent']}")
        if p["greeting"]:
            print(f"greeting: {p['greeting']}")
        return 0
    name = argv[0]
    if not personas.select(name):
        print(f"no such persona: {name}")
        print("try: " + ", ".join(personas.names()))
        return 1
    p = personas.get(name)
    print(f"persona is now {name} — {p['name']}")
    # ⚠ The greeting is how you can tell it took. A silent switch on a setting
    # whose whole effect is tone leaves you asking the next question just to
    # find out whether it worked.
    if p["greeting"]:
        print()
        print(p["greeting"])
    return 0


# ── todo ────────────────────────────────────────────────────────────────────

def _print_todos(items, title):
    if not items:
        print(f"{title}: nothing.")
        return
    print(title)
    for t in items:
        print("  " + P.todo_line(t))


def verb_todo(argv: list[str]) -> int:
    if not argv or argv[0] == "list":
        _print_todos(P.todo_all(), "Tasks")
        return 0
    cmd, rest = argv[0], argv[1:]

    if cmd == "today":
        _print_todos(P.todo_today(), "Today")
        return 0
    if cmd == "overdue":
        _print_todos(P.todo_overdue(), "Overdue")
        return 0
    if cmd == "add":
        project, rest = _flag(rest, "--project", "inbox")
        prio, rest = _flag(rest, "--prio")
        due, rest = _flag(rest, "--due")
        tags, rest = _flag(rest, "--tags")
        content = " ".join(rest).strip()
        if not content:
            print("usage: vibe todo add <text> [--project P] [--prio 1-4] "
                  "[--due YYYY-MM-DD] [--tags a,b]")
            return 1
        t = P.todo_add(content, project or "inbox", _int(prio, 2), due, tags)
        print(P.todo_line(t))
        return 0
    if cmd == "stats":
        s = P.todo_stats()
        print(f"{s['active']} active, {s['done']} done, {s['overdue']} overdue, "
              f"{s['today_done']} finished today ({s['completion_rate']}%)")
        return 0

    # The rest all take an id.
    if not rest:
        print(f"usage: vibe todo {cmd} <id>")
        return 1
    tid = _int(rest[0], 0)
    action = {"done": P.todo_complete, "start": P.todo_start,
              "reopen": P.todo_reopen, "cancel": P.todo_cancel}.get(cmd)
    if action:
        t = action(tid)
        if not t:
            print(f"no task #{tid}")
            return 1
        print(P.todo_line(t))
        return 0
    if cmd in ("rm", "delete"):
        if not P.todo_delete(tid):
            print(f"no task #{tid}")
            return 1
        print(f"deleted #{tid}")
        return 0
    print(f"vibe todo: unknown command '{cmd}'")
    return 1


# ── habit ───────────────────────────────────────────────────────────────────

def verb_habit(argv: list[str]) -> int:
    if not argv or argv[0] == "list":
        board = P.habit_dashboard()
        if not board:
            print("No habits yet. `vibe habit add Exercise`")
            return 0
        print("Habits            last 7 days   30d")
        for h in board:
            print("  " + P.habit_line(h))
        return 0
    cmd, rest = argv[0], argv[1:]
    if cmd == "add":
        icon, rest = _flag(rest, "--icon", "*")
        name = " ".join(rest).strip()
        if not name:
            print("usage: vibe habit add <name> [--icon X]")
            return 1
        h = P.habit_add(name, icon or "*")
        print(f"#{h['id']} {h['name']}")
        return 0
    if not rest:
        print(f"usage: vibe habit {cmd} <id>")
        return 1
    hid = _int(rest[0], 0)
    day = rest[1] if len(rest) > 1 else None
    if cmd == "check":
        P.habit_check(hid, day)
        print(f"#{hid} checked, streak {P.habit_streak(hid)}")
        return 0
    if cmd == "uncheck":
        P.habit_uncheck(hid, day)
        print(f"#{hid} unchecked")
        return 0
    if cmd in ("rm", "delete"):
        if not P.habit_delete(hid):
            print(f"no habit #{hid}")
            return 1
        print(f"deleted #{hid}")
        return 0
    print(f"vibe habit: unknown command '{cmd}'")
    return 1


# ── goal ────────────────────────────────────────────────────────────────────

def verb_goal(argv: list[str]) -> int:
    if not argv or argv[0] == "list":
        # `vibe goal list done` / `abandoned` — a finished goal has to stay
        # reachable, or the first thing anybody completes disappears and reads
        # as data loss.
        want = argv[1] if len(argv) > 1 else "active"
        want = {"done": "completed"}.get(want, want)
        goals = P.goal_all(want)
        if not goals:
            others = sum(len(P.goal_all(st)) for st in ("active", "completed",
                                                        "abandoned") if st != want)
            tail = (f" ({others} in another state — `vibe goal list done`)"
                    if others else " `vibe goal add Ship 1.0`")
            print(f"No {want} goals.{tail}")
            return 0
        for g in goals:
            print(P.goal_line(g))
            for m in g["milestones"]:
                print(f"      {'[x]' if m['completed'] else '[ ]'} "
                      f"#{m['id']} {m['title']}")
        return 0
    cmd, rest = argv[0], argv[1:]
    if cmd == "add":
        due, rest = _flag(rest, "--due")
        cat, rest = _flag(rest, "--cat", "general")
        title = " ".join(rest).strip()
        if not title:
            print("usage: vibe goal add <title> [--due YYYY-MM-DD] [--cat C]")
            return 1
        g = P.goal_add(title, None, due, cat or "general")
        print(P.goal_line(g))
        return 0
    if not rest:
        print(f"usage: vibe goal {cmd} <id> …")
        return 1
    gid = _int(rest[0], 0)
    if cmd == "progress":
        if len(rest) < 2:
            print("usage: vibe goal progress <id> <0-100>")
            return 1
        g = P.goal_progress(gid, _int(rest[1], 0))
        if not g:
            print(f"no goal #{gid}")
            return 1
        print(P.goal_line(g))
        return 0
    if cmd == "milestone":
        title = " ".join(rest[1:]).strip()
        if not title:
            print("usage: vibe goal milestone <goal-id> <title>")
            return 1
        g = P.goal_milestone(gid, title)
        if not g:
            print(f"no goal #{gid}")
            return 1
        print(P.goal_line(g))
        return 0
    if cmd == "done":
        # ⚠ THE ID HERE IS A MILESTONE'S, not a goal's, and the reply says
        # which goal moved so a mistyped id is visible immediately.
        g = P.goal_milestone_done(gid)
        if not g:
            print(f"no milestone #{gid}")
            return 1
        print(P.goal_line(g))
        return 0
    if cmd in ("rm", "delete"):
        if not P.goal_delete(gid):
            print(f"no goal #{gid}")
            return 1
        print(f"deleted #{gid}")
        return 0
    print(f"vibe goal: unknown command '{cmd}'")
    return 1


# ── pomodoro ────────────────────────────────────────────────────────────────

def verb_pom(argv: list[str]) -> int:
    if not argv or argv[0] == "status":
        print(pomodoro.line(pomodoro.status()))
        return 0
    cmd, rest = argv[0], argv[1:]
    if cmd == "start":
        mins, rest = _flag(rest, "--min")
        task = " ".join(rest).strip() or None
        s = pomodoro.start(task, _int(mins, 25))
        if s.get("error"):
            live = s["session"]
            print(f"already running: {live['remaining']} left"
                  + (f" — {live['task']}" if live.get("task") else ""))
            return 1
        print(f"{s['duration']} minutes"
              + (f" — {s['task']}" if s.get("task") else "")
              + f", until {time.strftime('%H:%M', time.localtime(s['ends_epoch']))}")
        return 0
    if cmd == "stop":
        s = pomodoro.stop()
        if not s:
            print("nothing running")
            return 1
        mins = s["elapsed_seconds"] // 60
        print(f"stopped after {mins} min"
              + ("" if s["completed"] else " — not counted, it did not finish"))
        return 0
    if cmd == "stats":
        t, w = pomodoro.today_stats(), pomodoro.week_stats()
        print(f"today: {t['sessions']} sessions, {t['total_display']}")
        print(f"week:  {w['sessions']} sessions, {w['total_minutes'] // 60}h "
              f"{w['total_minutes'] % 60}m")
        for day in sorted(w["by_day"]):
            print(f"  {day}  {w['by_day'][day]} min")
        return 0
    print(f"vibe pom: unknown command '{cmd}'")
    return 1


# ── quant ───────────────────────────────────────────────────────────────────

def verb_quant(argv: list[str]) -> int:
    """`vibe quant AAPL MSFT` — the one verb here that goes to the network.

    ⚠ It says so when it fails. "Could not reach Yahoo" and "no such symbol"
    are different problems with the same shape on screen, and somebody on a
    train needs to know which they have."""
    from vibe import quant
    if not argv:
        print("usage: vibe quant <ticker> [ticker …]")
        return 1
    bad = 0
    for sym in argv:
        try:
            print(quant.report(quant.quote(sym)))
        except quant.QuantError as e:
            print(f"{sym}: {e}")
            bad = 1
    return bad
