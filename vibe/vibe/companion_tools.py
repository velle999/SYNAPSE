"""The companion's tools — what the model may do with todos, habits, goals and
the timer.

⛔ READ FREELY, WRITE ON CONFIRMATION. Same line as everywhere else in vibe:
DOES IT WRITE. Listing tasks changes nothing and asking about it would train
the hand to press Enter without reading; adding a task, ticking a habit or
starting a timer are the person's own records being changed by something that
inferred them from a sentence, and those ask. The gate itself is
VibeModel._CONFIRM_TOOLS — this file only supplies the names.

⚠ THE DESCRIPTIONS ARE FOR A 7B. Each one says when to reach for it and what
it is NOT, because the model this desktop ships picks the wrong tool when two
sound alike — the measured failure that put Mistral Nemo in place of v0.2.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
"""

from __future__ import annotations

from vibe import pomodoro
from vibe import productivity as P

# ⛔ market_quote IS IN HERE AND IT WRITES NOTHING. The line for a network
# call is not "does it write" but "does it LEAVE": asking for a quote tells a
# company in California which ticker somebody was curious about, from a desktop
# whose whole pitch is that the model runs on your own machine. It is the only
# tool here that leaves, and it is the only one whose confirmation is about
# something other than a change.
WRITE_TOOLS = {"todo_add", "todo_complete", "habit_check",
               "pomodoro_start", "pomodoro_stop", "market_quote"}


def _fn(name, desc, props=None, required=()):
    return {"type": "function",
            "function": {"name": name, "description": desc,
                         "parameters": {"type": "object",
                                        "properties": props or {},
                                        "required": list(required)}}}


SCHEMAS = [
    _fn("todo_list",
        "List the user's tasks. Use for 'what am I doing today', 'my tasks', "
        "'anything overdue'. Read-only: it never adds or completes anything.",
        {"scope": {"type": "string",
                   "description": "all (default), today, or overdue"}}),
    _fn("todo_add",
        "Add ONE task to the user's list. Only when they asked for something "
        "to be remembered or added — not to record what you are about to do.",
        {"content": {"type": "string", "description": "The task, in the user's words"},
         "project": {"type": "string", "description": "Optional project; defaults to inbox"},
         "priority": {"type": "integer", "description": "1 highest .. 4 lowest, default 2"},
         "due_date": {"type": "string", "description": "Optional YYYY-MM-DD"}},
        ["content"]),
    _fn("todo_complete",
        "Mark one task done, by its number. The number comes from todo_list — "
        "never guess one.",
        {"id": {"type": "integer", "description": "The task's #number"}}, ["id"]),
    _fn("habit_list",
        "The habit tracker: every habit with its streak and the last seven "
        "days. Read-only.", {}),
    _fn("habit_check",
        "Tick one habit for today, by its number from habit_list.",
        {"id": {"type": "integer", "description": "The habit's #number"}}, ["id"]),
    _fn("goal_list",
        "The user's goals with their progress and milestones. Read-only.", {}),
    _fn("pomodoro_status",
        "How much time is left on the running focus timer, or that none is "
        "running. Read-only.", {}),
    _fn("pomodoro_start",
        "Start a focus timer. Only when the user asked to start one.",
        {"task": {"type": "string", "description": "What they are focusing on"},
         "minutes": {"type": "integer", "description": "Default 25"}}),
    _fn("pomodoro_stop", "Stop the running focus timer.", {}),
    _fn("market_quote",
        "Look up a stock or index price and its technical indicators. This "
        "one goes to the internet, so use it only when a specific ticker or "
        "market was asked about.",
        {"symbol": {"type": "string",
                    "description": "Ticker, e.g. AAPL or ^GSPC"}},
        ["symbol"]),
]


def _todo_list(scope: str = "all") -> str:
    got = {"today": P.todo_today, "overdue": P.todo_overdue}.get(
        scope, P.todo_all)()
    if not got:
        return f"No {scope} tasks." if scope != "all" else "No tasks."
    return "\n".join(P.todo_line(t) for t in got)


def _todo_add(content: str, project: str = "inbox", priority: int = 2,
              due_date: str | None = None) -> str:
    return P.todo_line(P.todo_add(content, project, priority, due_date))


def _todo_complete(id: int) -> str:
    t = P.todo_complete(int(id))
    return P.todo_line(t) if t else f"No task #{id}."


def _habit_list() -> str:
    board = P.habit_dashboard()
    if not board:
        return "No habits are being tracked."
    return "\n".join(P.habit_line(h) for h in board)


def _habit_check(id: int) -> str:
    P.habit_check(int(id))
    return f"Habit #{id} ticked. Streak: {P.habit_streak(int(id))} days."


def _goal_list() -> str:
    goals = P.goal_all()
    if not goals:
        return "No active goals."
    return "\n".join(P.goal_line(g) for g in goals)


def _pom_status() -> str:
    return pomodoro.line(pomodoro.status())


def _pom_start(task: str | None = None, minutes: int = 25) -> str:
    s = pomodoro.start(task, int(minutes))
    if s.get("error"):
        return f"A timer is already running: {s['session']['remaining']} left."
    return f"Started: {s['duration']} minutes" + (f" on {task}" if task else "") + "."


def _quote(symbol: str) -> str:
    from vibe import quant
    try:
        return quant.report(quant.quote(symbol))
    except quant.QuantError as e:
        # A sentence, so the model can say what went wrong instead of retrying
        # a ticker that does not exist.
        return f"Could not price {symbol}: {e}"


def _pom_stop() -> str:
    s = pomodoro.stop()
    if not s:
        return "No timer was running."
    return (f"Stopped after {s['elapsed_seconds'] // 60} minutes"
            + ("." if s["completed"] else " — it did not finish, so it is not counted."))


MAP = {
    "todo_list": lambda a: _todo_list(**a),
    "todo_add": lambda a: _todo_add(**a),
    "todo_complete": lambda a: _todo_complete(**a),
    "habit_list": lambda a: _habit_list(),
    "habit_check": lambda a: _habit_check(**a),
    "goal_list": lambda a: _goal_list(),
    "pomodoro_status": lambda a: _pom_status(),
    "pomodoro_start": lambda a: _pom_start(**a),
    "pomodoro_stop": lambda a: _pom_stop(),
    "market_quote": lambda a: _quote(**a),
}
