"""Does the assistant do what it says before it does anything at all?

Four questions, and none of them needs a model, a GPU or a network:

  1. The synsh keyword path claims what synsh claims and nothing else. It sits
     in FRONT of the conversation, so a line it takes wrongly is a question the
     user asked that was never asked of anything.
  2. The desktop tools REFUSE what they cannot do, in a sentence. A tool that
     answers a bad argument with a traceback teaches the model nothing and the
     user less.
  3. The confirmation gate holds. Every tool that writes is behind it and no
     tool that only reads is — and the serve loop must WAIT for the answer
     rather than proceeding on the assumption of a yes.
  4. The wire survives the bytes an answer can contain. Tabs and newlines are
     the record separators, and an answer full of both is the ordinary case.

Usage: assistant_test.py            (from the vibe source tree)
"""
import io
import os
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from vibe import keywords, serve
from vibe.tools import TOOL_SCHEMAS, TOOL_MAP, execute_tool
from vibe.llm import VibeModel

npass = nfail = 0


def check(name, cond, detail=""):
    global npass, nfail
    print(("  ok    " if cond else "  FAIL  ") + name + (f"  [{detail}]" if not cond and detail else ""))
    if cond:
        npass += 1
    else:
        nfail += 1


# ── 1. the keyword path ─────────────────────────────────────────────────────
#
# ⚠ ASKED OF SYNSH, not of a copy of its table. If synsh is not installed this
# whole section is vacuously true and says so, rather than testing a stub.
if keywords.available():
    check("synsh claims a line it answers itself", keywords.claims("what time is it"))
    check("…and declines one only a model can answer",
          not keywords.claims("write me a python decorator that retries on failure"))
    # ⛔ THE HIJACK THIS GUARDS. `play` and `install` are real programs, so a
    # substring match on "play music" eats `play music.wav`. synsh matches whole
    # normalised lines; this is the assertion that the front door inherits that.
    check("…and does not claim a line that merely CONTAINS a keyword",
          not keywords.claims("how do I play music with mpv"))
else:
    print("  note  synsh is not installed — the keyword path is untested here")


# ── 2. the desktop tools refuse in sentences ────────────────────────────────
bad = execute_tool("desktop_setting", {"key": "bar_edge", "value": "sideways"})
check("a setting refuses a value it does not take",
      bad.startswith("Error:") and "top" in bad, bad)

bad = execute_tool("desktop_setting", {"key": "rm -rf /", "value": "x"})
check("…and refuses a key that is not on its list", bad.startswith("Error:"), bad)

bad = execute_tool("desktop_action", {"action": "not_a_real_action"})
check("an action this compositor lacks is refused, not run", bad.startswith("Error:"), bad)

bad = execute_tool("desktop_open", {"target": "no-such-thing-anywhere"})
check("opening something that does not exist says so", bad.startswith("Error:"), bad)

# move_file's one destructive mistake: landing on a file that was already there.
import tempfile
work = tempfile.mkdtemp()
a = os.path.join(work, "a"); b = os.path.join(work, "b")
open(a, "w").write("A"); open(b, "w").write("B")
out = execute_tool("move_file", {"source": a, "dest": b})
check("a move REFUSES to overwrite the destination",
      out.startswith("Error:") and open(b).read() == "B", out)
out = execute_tool("move_file", {"source": a, "dest": os.path.join(work, "c")})
check("…and moves when the destination is free", out.startswith("Moved"), out)


# ── 3. the confirmation gate ────────────────────────────────────────────────
#
# ⛔ THE LINE IS "DOES IT WRITE". desktop_open is deliberately outside the gate:
# a confirmation on opening a folder trains the hand to press Enter without
# reading, which is what makes the confirmations that matter stop working.
gated = VibeModel._CONFIRM_TOOLS
for w in ("bash", "write_file", "edit_file", "desktop_setting", "desktop_action", "move_file"):
    check(f"{w} is behind the confirmation", w in gated)
for r in ("read_file", "glob", "grep", "list_dir", "desktop_open"):
    check(f"{r} is not", r not in gated)

# Every tool the model is offered is one the dispatcher can actually run — a
# schema with no implementation is a tool the model calls and is told does not
# exist, which it then tries again.
names = {s["function"]["name"] for s in TOOL_SCHEMAS}
check("every advertised tool has an implementation", names == set(TOOL_MAP),
      f"{names ^ set(TOOL_MAP)}")

# …and the gate is a real wait. A `confirm` arrives on the reader while the
# turn's thread is parked inside the callback, so the reader must not be the
# thing that is blocked — the first version of this deadlocked on its own
# question, which looks exactly like a model that stopped answering.
out = io.StringIO()
srv = serve.Server(serve.Wire(out))
answered = {}


def ask_gate():
    answered["allowed"] = srv._confirm("bash", {"command": "echo hi"})


t = threading.Thread(target=ask_gate, daemon=True)
t.start()
for _ in range(50):
    if srv._pending:
        break
    time.sleep(0.02)
check("a gated tool announces itself and waits", bool(srv._pending))
cid = next(iter(srv._pending), "")
srv.command(f"confirm {cid} yes")
t.join(timeout=5)
check("…and proceeds on the answer, not before", answered.get("allowed") is True)
check("…and the C record carries the tool and its arguments",
      "\tbash\t" in out.getvalue() and "echo" in out.getvalue())


# ── 4. the prompt is in the model's own template ────────────────────────────
#
# ⛔ THE BUG THIS IS FOR REACHED THE USER. This client emitted
# `<|system|>/<|user|>/<|assistant|>` at every model regardless, and SynapseOS
# ships a MISTRAL — which has never seen those tokens. It does not error: the
# model reads the lot as prose, keeps writing, and invents turn markers of its
# own. What appeared in the chat window was `◁user▷` and `◁assistant▷` wrapped
# around answers to questions nobody had typed.
from vibe import synapd_client as sc

MSGS = [{"role": "system", "content": "SYS"},
        {"role": "user", "content": "hi"},
        {"role": "assistant", "content": "yo"},
        {"role": "user", "content": "and now"}]

inst = sc.flatten_messages(MSGS, None, "[INST]")
check("a Mistral prompt uses [INST]", "[INST]" in inst and "<|user|>" not in inst, inst[:80])
# ⚠ Mistral has NO system role — v0.2's own template rejects one outright, so
# the system prompt has to ride into the first user turn.
check("…with the system prompt folded into the first user turn",
      inst.startswith("[INST] SYS") and inst.count("SYS") == 1, inst[:60])
check("…and its assistant turns close with </s>", "yo</s>" in inst, inst[:120])

chatml = sc.flatten_messages(MSGS, None, "<|im_start|>user")
check("a ChatML prompt uses <|im_start|>",
      "<|im_start|>system" in chatml and chatml.endswith("<|im_start|>assistant\n"))

plain = sc.flatten_messages(MSGS, None, "a-template-nobody-knows")
check("an unknown template falls back to a plain transcript, with no markers",
      "User: hi" in plain and "<|" not in plain and "[INST]" not in plain)

# The nudge rides on the LAST user turn only — the tool rules live in the system
# block, which for Mistral is thousands of tokens behind the question.
withtools = sc.flatten_messages(MSGS, TOOL_SCHEMAS, "[INST]")
check("the tool reminder lands on the last turn, once",
      withtools.count("emit a <tool_call> block now") == 1)
check("…and states BOTH branches, so it cannot push a fact question into a grep",
      "just answer it" in withtools)

# The guard that does not depend on getting any of the above right.
check("a reply that starts writing the user's turn is cut",
      sc.trim_hallucinated_turn("Answer.\n◁user▷ next?") == "Answer.")
check("…in every family's markers",
      sc.trim_hallucinated_turn("A.<|im_start|>user") == "A."
      and sc.trim_hallucinated_turn("B.[INST] x") == "B."
      and sc.trim_hallucinated_turn("C.\nUser: x") == "C.")
check("…and leaves an ordinary reply alone",
      sc.trim_hallucinated_turn("Just an answer.") == "Just an answer.")


# ── 5. the wire ─────────────────────────────────────────────────────────────
#
# Tabs and newlines ARE the record separators, so an answer containing them has
# to survive as data. A shell transcript contains both, every time.
for s in ("plain", "a\tb", "one\ntwo", "100%", "é ☃", "", "a\\b\"c"):
    check(f"the wire round-trips {s!r}", serve.dec(serve.enc(s)) == (s or "\x00"))

rec = io.StringIO()
serve.Wire(rec).rec("T", "col1\tcol2\nline2")
line = rec.getvalue().rstrip("\n")
check("a record is ONE line whatever is in the field", "\n" not in line)
check("…and its fields are still two", len(line.split("\t")) == 2, line)

print(f"\n{npass} passed, {nfail} failed")
sys.exit(1 if nfail else 0)
