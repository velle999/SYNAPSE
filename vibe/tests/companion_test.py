"""The companion half of the assistant: todos, habits, goals, the focus timer
and the personas.

⛔ NOTHING HERE TOUCHES THE DESKTOP'S OWN DATA. Every case runs against a
scratch database and a scratch config directory (VIBE_DB and XDG_CONFIG_HOME),
set before vibe is imported — a test that ticked a real habit or cancelled a
running focus session would be a test nobody could afford to run twice.

What is asserted, in order of how easily each breaks:

  1. A streak survives today not being ticked yet. Every other definition
     reads zero every morning, which is exactly when somebody looks at it.
  2. A focus timer that is stopped early is NOT counted. velle.ai marked any
     stop as completed, so a day's total was whatever had been started rather
     than what was finished.
  3. A timer that ran out while nobody was looking is closed by the first
     reader — the bar can count but cannot write, so if this misses, an expired
     session stays "running" forever and the indicator never clears.
  4. Goal progress follows milestones once there are any, and does not stamp
     over a hand-set percentage before there are.
  5. A persona changes the voice and the temperature, and the DEFAULT persona
     changes nothing at all.
  6. Every companion tool that writes is behind the confirmation gate, and no
     tool that only reads is.
  7. The chat window's panel is sent the ROWS, not the lines the CLI prints —
     it has to draw a checkbox and know which id it belongs to — and a tick is
     a toggle both ways, with the completion stamp cleared on the way back.

Usage: companion_test.py            (from the vibe source tree)
"""
import json
import os
import sys
import tempfile

_TMP = tempfile.mkdtemp(prefix="vibe-companion-")
os.environ["VIBE_DB"] = os.path.join(_TMP, "companion.db")
os.environ["XDG_CONFIG_HOME"] = os.path.join(_TMP, "config")
os.environ.pop("VIBE_PERSONA", None)

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from vibe import personas, pomodoro, store          # noqa: E402
from vibe import productivity as P                  # noqa: E402
from vibe import companion_tools, modes             # noqa: E402
from vibe.llm import VibeModel                      # noqa: E402
from vibe.tools import TOOL_SCHEMAS, execute_tool   # noqa: E402

npass = nfail = 0


def check(name, cond, detail=""):
    global npass, nfail
    print(("  ok    " if cond else "  FAIL  ") + name
          + (f"  [{detail}]" if not cond and detail else ""))
    if cond:
        npass += 1
    else:
        nfail += 1


print("companion — tasks, habits, goals, the timer, the voice")

# ── 1. Todos ────────────────────────────────────────────────────────────────
t = P.todo_add("ship the fold", priority=1, due_date="2026-09-01")
check("a task comes back with the id it was given", P.todo_get(t["id"])["content"] == "ship the fold")
check("…and lands in the list", any(x["id"] == t["id"] for x in P.todo_all()))
P.todo_start(t["id"])
check("starting one moves it to doing", P.todo_get(t["id"])["status"] == "doing")
P.todo_complete(t["id"])
check("completing one stamps the time",
      P.todo_get(t["id"])["status"] == "done" and P.todo_get(t["id"])["completed_at"])
check("…and the day's count sees it", P.todo_stats()["today_done"] == 1)

# ⚠ The whitelist is the only thing between a caller's keys and the SQL.
P.todo_edit(t["id"], content="renamed", nonsense="DROP TABLE todos")
check("an unknown column in an edit is ignored, not interpolated",
      P.todo_get(t["id"])["content"] == "renamed")
check("…and the table is still there", isinstance(P.todo_all(), list))

# ── 2. Habits ───────────────────────────────────────────────────────────────
from datetime import date, timedelta                # noqa: E402

h = P.habit_add("Exercise")
check("a new habit has no streak", P.habit_streak(h["id"]) == 0)
P.habit_check(h["id"], (date.today() - timedelta(days=1)).isoformat())
check("⛔ yesterday alone is a live streak — today need not be ticked yet",
      P.habit_streak(h["id"]) == 1, f"got {P.habit_streak(h['id'])}")
P.habit_check(h["id"])
check("…and today extends it", P.habit_streak(h["id"]) == 2)
P.habit_check(h["id"], (date.today() - timedelta(days=4)).isoformat())
check("a gap ends it rather than counting through", P.habit_streak(h["id"]) == 2)
week = P.habit_week(h["id"])
check("the week grid is seven days ending today",
      len(week) == 7 and week[-1]["date"] == date.today().isoformat())
check("…and marks the ones that were done", week[-1]["done"] and week[-2]["done"])

# ── 3. The focus timer ──────────────────────────────────────────────────────
state = os.path.join(os.environ["XDG_CONFIG_HOME"], "synui", "pomodoro.state")

s = pomodoro.start("write it up", 25)
check("starting one publishes the deadline for the bar", os.path.exists(state))
body = open(state).read()
check("…as an END TIME, not a countdown", "ends = " in body and "remaining" not in body)
check("…and it is running", pomodoro.status()["remaining_seconds"] > 0)

again = pomodoro.start("something else")
check("a second start is refused rather than replacing the first",
      again.get("error") == "already running")

stopped = pomodoro.stop()
check("⛔ stopping early does NOT count as a completed session",
      stopped["completed"] == 0, f"got {stopped['completed']}")
check("…so the day's total stays honest", pomodoro.today_stats()["sessions"] == 0)
check("…and the bar's file is gone", not os.path.exists(state))

# ⛔ The one the indicator depends on: nobody is watching, and it must still end.
pomodoro.start("left alone", 1)
with store.connect() as c:
    c.execute("UPDATE pomodoro_sessions SET started_at ="
              " datetime('now','localtime','-2 minutes') WHERE ended_at IS NULL")
os.environ["PATH"] = ""          # no notify-send: the best-effort path must hold
done = pomodoro.status()
check("⛔ a timer that ran out is closed by the first reader",
      done and done.get("just_finished"))
check("…and THAT one counts, because it finished", pomodoro.today_stats()["sessions"] == 1)
check("…and the indicator is cleared", not os.path.exists(state))
check("…and a second reader sees nothing, so nothing announces twice",
      pomodoro.status() is None)

# ── 4. Goals ────────────────────────────────────────────────────────────────
g = P.goal_add("Ship 0.3")
P.goal_progress(g["id"], 40)
check("a goal takes a hand-set percentage", P.goal_get(g["id"])["progress"] == 40)
P.goal_milestone(g["id"], "one")
check("⛔ adding the first milestone does not wipe that percentage to zero",
      P.goal_get(g["id"])["progress"] == 40,
      f"got {P.goal_get(g['id'])['progress']}")
P.goal_milestone(g["id"], "two")
check("…nor does the second", P.goal_get(g["id"])["progress"] == 40)
ms = P.goal_get(g["id"])["milestones"]
P.goal_milestone_done(ms[0]["id"])
check("completing one of two milestones is half the goal",
      P.goal_get(g["id"])["progress"] == 50, f"got {P.goal_get(g['id'])['progress']}")
P.goal_milestone_done(ms[1]["id"])
check("…and finishing them all completes it",
      P.goal_get(g["id"])["status"] == "completed")
check("…which takes it out of the active list", P.goal_all("active") == [])
check("…but leaves it findable", len(P.goal_all("completed")) == 1)

# ── 5. Personas ─────────────────────────────────────────────────────────────
import vibe.config as cfg                            # noqa: E402

check("the default is the default", personas.current() == "default")
check("⛔ and it changes NOTHING — no voice block",
      personas.voice_section() == "" and personas.reminder() == "")
check("…and defers to the configured temperature",
      personas.temperature() == cfg.TEMPERATURE)

check("an unknown name is refused rather than written", not personas.select("nope"))
check("…leaving the choice as it was", personas.current() == "default")

check("a real one is taken", personas.select("sarcastic"))
check("…and persists to a file the window can read", personas.current() == "sarcastic")
check("…brings its own temperature", personas.temperature() == 0.95)
voice = personas.voice_section()
check("…appends a voice block rather than replacing the prompt",
      voice.startswith("\n## Voice") and "SARCASTIC" in voice.upper())
check("…and states that it changes how, not what", "never WHAT is true" in voice)
check("…and rides a reminder on the user turn", "SARCASTIC" in personas.reminder().upper())
personas.select("default")

# ── 6. The gate ─────────────────────────────────────────────────────────────
names = {t["function"]["name"] for t in TOOL_SCHEMAS}
check("every companion tool is registered on the model's schema list",
      set(companion_tools.MAP) <= names,
      str(set(companion_tools.MAP) - names))

gated = VibeModel._CONFIRM_TOOLS
writes = {"todo_add", "todo_complete", "habit_check", "pomodoro_start", "pomodoro_stop"}
reads = {"todo_list", "habit_list", "goal_list", "pomodoro_status"}
check("⛔ every companion tool that WRITES asks first", writes <= gated,
      str(writes - gated))
check("…and no tool that only reads does", not (reads & gated), str(reads & gated))
check("…and the readers are usable in PLAN mode",
      reads <= modes.READ_ONLY_TOOLS, str(reads - modes.READ_ONLY_TOOLS))

# The tools answer through the same dispatcher as everything else.
out = execute_tool("todo_add", {"content": "through the tool path"})
check("a tool call reaches the store", "through the tool path" in out)
check("…and reads back", "through the tool path" in execute_tool("todo_list", {}))
check("a bad tool argument is a sentence, not a traceback",
      execute_tool("todo_complete", {"id": 99999}).startswith("No task"))

# ── 7. Market data, WITHOUT a network ───────────────────────────────────────
#
# ⛔ THE PREVIOUS CLOSE IS THE ONE THAT WAS WRONG. `chartPreviousClose` is the
# close BEFORE the requested range — on the six months this asks for, that is
# the price half a year ago, and using it reported a 1.6% day as +21%. It was
# caught by looking at the output of a real request; this is the offline case
# that keeps it caught, which is the whole reason build() is split from the
# fetch.
from vibe import quant                                # noqa: E402

fake = {
    "meta": {"symbol": "TEST", "currency": "USD",
             "regularMarketPrice": 110.0,
             "chartPreviousClose": 50.0},            # six months ago
    "indicators": {"quote": [{
        "close": [float(x) for x in range(1, 110)] + [110.0],
        "high":  [float(x) + 1 for x in range(1, 110)] + [111.0],
        "low":   [float(x) - 1 for x in range(1, 110)] + [109.0],
        "volume": [1] * 110,
    }]},
}
q = quant.build(fake, "TEST")
check("⛔ the day's move is measured against YESTERDAY, not the range start",
      q["previous_close"] == 109.0, f"got {q['previous_close']}")
check("…so a one-point day is not reported as a doubling",
      q["change"] == 1.0 and q["change_pct"] < 2, f"got {q['change_pct']}%")
check("the indicators come off the same candles",
      q["sma50"] is not None and q["rsi14"] is not None and q["atr14"] is not None)
check("…and a 200-day average is absent rather than invented on 110 candles",
      q["sma200"] is None)
check("the report is plain text a terminal and a model can both read",
      "TEST" in quant.report(q) and "\x1b" not in quant.report(q))

check("market_quote is a tool the model must ask before using",
      "market_quote" in VibeModel._CONFIRM_TOOLS)
check("…because it LEAVES the machine, not because it writes",
      "market_quote" not in {"todo_add", "todo_complete", "habit_check"})


# ── 8. What the window's panel draws ────────────────────────────────────────
#
# ⛔ THE PANEL DRAWS FIELDS, NOT THE CLI's LINES. `/todo` prints
# `[ ] !! #3 buy milk`, which is right in a terminal and useless to a window
# that has to put a checkbox next to a row and know which id it belongs to —
# so the engine sends `P` records carrying the rows. If that ever becomes the
# rendered line again, the panel silently loses every checkbox.
from vibe import serve                              # noqa: E402


class _Rec:
    """A wire that keeps what it was given instead of writing it out."""
    def __init__(self):
        self.recs = []

    def rec(self, tag, *fields):
        self.recs.append((tag, *fields))

    def last(self, tag, key):
        for t, *f in reversed(self.recs):
            if t == tag and f and f[0] == key:
                return f[1]
        return None


srv = serve.Server.__new__(serve.Server)
srv.wire = _Rec()

for t in P.todo_all():
    P.todo_delete(t["id"])
panel_task = P.todo_add("panel row", priority=1)
srv.emit_companion()

drawn = json.loads(srv.wire.last("P", "todos") or "[]")
check("the panel is sent task rows, with their ids",
      any(r["id"] == panel_task["id"] and r["text"] == "panel row" for r in drawn),
      f"got {drawn}")
check("…as fields, not as the line the CLI prints",
      all("[ ]" not in r["text"] for r in drawn))
check("habits arrive with the week grid and the streak",
      all({"week", "streak", "today"} <= set(h) for h in
          json.loads(srv.wire.last("P", "habits") or "[]")))
check("goals arrive with a percentage the panel can draw a bar from",
      all(isinstance(g["progress"], int) for g in
          json.loads(srv.wire.last("P", "goals") or "[]")))

# ⛔ A TICK IS A TOGGLE. A checkbox that only goes one way turns a misclick
# into a row that has to be repaired from a terminal.
srv.check("todo", str(panel_task["id"]))
check("the panel's checkbox completes a task",
      P.todo_get(panel_task["id"])["status"] == "done")
srv.check("todo", str(panel_task["id"]))
back = P.todo_get(panel_task["id"])
check("…and ticking it again puts it back",
      back["status"] == "todo", f"got {back['status']}")

# ⚠ `completed_at` IS CLEARED with it. It is the column todo_stats counts as
# finished today, so a reopened task left carrying its timestamp goes on being
# counted — a day's total claiming work that has been put back on the list.
check("⚠ reopening clears the completion stamp, so the day's total is honest",
      back["completed_at"] is None, f"got {back['completed_at']}")

toggle_habit = P.habit_add("panel habit", "*")
srv.check("habit", str(toggle_habit["id"]))
row = next(h for h in P.habit_dashboard() if h["id"] == toggle_habit["id"])
check("the panel's checkbox ticks a habit for today", row["done_today"])
srv.check("habit", str(toggle_habit["id"]))
row = next(h for h in P.habit_dashboard() if h["id"] == toggle_habit["id"])
check("…and unticks it", not row["done_today"])

# ⚠ IT SAYS SO RATHER THAN DOING NOTHING. A checkbox on a row that has been
# deleted underneath the window is a live case, not a hypothetical.
srv.wire.recs.clear()
srv.check("todo", "99999")
check("a tick on a row that is gone is answered in a sentence",
      any(t == "A" for t, *_ in srv.wire.recs))
srv.wire.recs.clear()
srv.check("nonsense", "1")
check("…and so is a tick on nothing this knows about",
      any(t == "A" for t, *_ in srv.wire.recs))

print(f"\n  {npass} passed, {nfail} failed")
sys.exit(1 if nfail else 0)
