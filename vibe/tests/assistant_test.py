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


# ── 5. Ask / Agent / Plan, and the automatic choice ─────────────────────────
from vibe import modes

for text, want in (
        ("what is the speed of light",             modes.ASK),
        ("who wrote the novel Dune",               modes.ASK),
        ("write me a haiku about a cat",           modes.ASK),
        # ⚠ CARRIES `write` AND `build`, and is still writing. The compose
        # verbs have to beat the machine words or every email becomes a task.
        ("write me an email about the build failing", modes.ASK),
        ("list the files in /etc/synapd",          modes.AGENT),
        ("move the bar to the bottom",             modes.AGENT),
        ("what is in ~/Downloads",                 modes.AGENT),
        ("how would you add dark mode to this app", modes.PLAN),
        ("plan a migration to postgres",           modes.PLAN)):
    check(f"auto routes {text[:34]!r} to {want}", modes.route(text) == want,
          modes.route(text))

check("a mode chosen by hand is honoured, not routed",
      modes.resolve(modes.ASK, "list the files in /etc") == modes.ASK)
check("…and AUTO is never the answer to which mode ran",
      modes.route("anything at all") in (modes.ASK, modes.AGENT, modes.PLAN))

# ⛔ PLAN'S SAFETY IS ITS TOOL LIST. If a tool that writes ever reaches it, the
# mode whose whole promise is "looks, and writes the steps instead of taking
# them" can take them.
plan_tools = {t["function"]["name"] for t in modes.tools_for(modes.PLAN, TOOL_SCHEMAS)}
check("plan gets only read-only tools", plan_tools <= modes.READ_ONLY_TOOLS, plan_tools)
check("…and none of them is behind the confirmation gate",
      not (plan_tools & VibeModel._CONFIRM_TOOLS))
check("ask gets no tools at all", modes.tools_for(modes.ASK, TOOL_SCHEMAS) is None)
check("agent gets all of them",
      len(modes.tools_for(modes.AGENT, TOOL_SCHEMAS)) == len(TOOL_SCHEMAS))


# ── 5b. the shell route ─────────────────────────────────────────────────────
#
# A line that IS a command is answered by running it, not by a model guessing
# what running it would have said. The judgement is synsh's — `synsh
# --classify` — because a second copy of it in Python would disagree with the
# real shell the first time either changed.
if keywords.available() and keywords.classify("ls -la"):
    check("synsh calls a command a command", keywords.classify("ls -la") == "shell")
    check("…a builtin a builtin", keywords.classify("cd /tmp") == "builtin")
    check("…and a question a question",
          keywords.classify("what is the speed of light") == "ai")
else:
    print("  note  this synsh has no --classify — the shell route is untested here")

# ⛔ SYNSH'S OWN PREFIXES WIN OVER EVERY OTHER RULE. `!` forces shell and `?`
# forces the model, in the shell this desktop ships; a chat box that ignored
# them would be teaching a second set of the same two characters.
check("a `!` line is the shell, whatever else it looks like",
      modes.route("!what is the speed of light", "ai") == modes.SHELL)
check("a `?` line is a question, even when synsh calls it a command",
      modes.route("?ls -la", "shell") == modes.ASK)
check("…and `!` beats a mode chosen by hand",
      modes.resolve(modes.ASK, "!ls", "shell") == modes.SHELL)
check("a hand-picked mode is otherwise still honoured",
      modes.resolve(modes.ASK, "ls -la", "shell") == modes.ASK)

# ⚠ THE ENGLISH FORMS COME FIRST. synsh answers "make me a sandwich" with
# `shell`, because `make` is a real program — the same trap that makes "play
# music" hijack `play music.wav`. What makes that survivable is the
# confirmation, and what makes it rare is this ordering.
check("a question is not run as a command just because synsh could",
      modes.route("what is make", "shell") == modes.ASK)
check("SHELL gets no tools — nothing is asked of a model on that path",
      modes.tools_for(modes.SHELL, TOOL_SCHEMAS) is None)
check("…and SHELL is not selectable by hand", modes.SHELL not in modes.MODES)

# ⛔ AND IT ALWAYS ASKS. This is the assertion the whole route rests on: no
# classifier gets "make me a sandwich" right from the words, so the answer is
# not a better classifier, it is showing the command and waiting.
out = io.StringIO()
sh = serve.Server(serve.Wire(out))
t = threading.Thread(target=sh._run_shell, args=("echo would-not-run",), daemon=True)
t.start()
for _ in range(60):
    if sh._pending:
        break
    time.sleep(0.05)
check("a shell line is not run until it is allowed", bool(sh._pending))
cid = next(iter(sh._pending), "")
sh.command(f"confirm {cid} no")
t.join(timeout=5)
check("…and a refusal runs nothing",
      "would-not-run" not in out.getvalue() or "not run" in out.getvalue())


# ── 5c. the wake word ───────────────────────────────────────────────────────
#
# ⛔ THE SWITCH THAT LEAVES A MICROPHONE OPEN. Its correctness is not "does it
# hear the name" — it is that a room with a television in it does not hold a
# conversation with the assistant, and that the desktop says when it is on.
from vibe import wake as wakemod

for t in ("Synapse what is the time", "sinaps hello", "cynaps open files",
          "computor what time is it", "ask synapse about it"):
    check(f"{t[:26]!r} wakes it", wakemod.names_it(t))

# ⚠ MEASURED, NOT GUESSED. A fuzzy pass over every word at 0.8 — the obvious
# implementation — wakes on "compute" (.933), "computers" (.941), "commuter"
# (.875) and "snaps" (.833). Restricting it to the FIRST word and requiring six
# characters is what makes 0.75 safe, and 0.75 is what catches "sinaps" (.769),
# which is what whisper-tiny actually produces.
for t in ("the synopsis of the film", "I need to compute the average",
          "snaps of the party were great", "the commuter rail is delayed",
          "collapse the window", "we should discuss synergy",
          "what is the weather"):
    check(f"{t[:26]!r} does not", not wakemod.names_it(t))

check("the name is taken off the front before the question is asked",
      wakemod.strip_name("Synapse, what is the time") == "what is the time")
check("…but not out of the middle, where it is the subject",
      wakemod.strip_name("ask synapse about it") == "ask synapse about it")

# ⛔ THE TELEVISION DEFENCE. A window that stayed open for anything that spoke
# would let a broadcast talk to the assistant all evening. It closes after a few
# turns that never name it — a human says the name again now and then.
g = wakemod.Gate(window=10, cap=2)
t0 = 1000.0
check("the name opens the window", g.accept("synapse hello", now=t0))
check("…a follow-up needs no name", g.accept("and the weather", now=t0 + 1))
check("…and a second", g.accept("and after that", now=t0 + 2))
check("…but the third unaddressed turn closes it",
      not g.accept("and then", now=t0 + 3))
check("…and the name opens it again", g.accept("synapse again", now=t0 + 4))
check("a line after the window has expired is ignored",
      not wakemod.Gate(window=1, cap=4).accept("no name here", now=t0))

# The disclosure. A bar left claiming a microphone is open by an engine that has
# exited is worse than no indicator — it is a false one nobody can turn off.
import tempfile, pathlib
cfgdir = tempfile.mkdtemp()
old_env = os.environ.get("XDG_CONFIG_HOME")
os.environ["XDG_CONFIG_HOME"] = cfgdir
import importlib
import vibe.serve as _sv
importlib.reload(_sv)
try:
    o = io.StringIO()
    _sv.Server(_sv.Wire(o)).serve(io.StringIO("quit\n"))
    statefile = pathlib.Path(cfgdir) / "synui" / "assistant.state"
    check("the engine publishes its wake state where the bar can see it",
          statefile.exists())
    check("…and leaves it OFF when it exits",
          "wake = off" in statefile.read_text())
finally:
    if old_env is None:
        os.environ.pop("XDG_CONFIG_HOME", None)
    else:
        os.environ["XDG_CONFIG_HOME"] = old_env
    importlib.reload(_sv)


# ── 5d. where an API key lives ──────────────────────────────────────────────
#
# CodeQL alert #15 (py/clear-text-storage-sensitive-data) is about the file this
# falls back to. The file is not the interesting part — it is mode 0600 under
# the user's own home, which is what ~/.aws/credentials, ~/.docker/config.json
# and ~/.config/gh/hosts.yml all are, and an attacker who can read it can also
# read this process's memory. What IS interesting is everything around it, and
# two of those were real bugs.
import stat as _stat
import subprocess as _sp
import tempfile as _tf

_keycfg = _tf.mkdtemp()
_old_cfg = os.environ.get("XDG_CONFIG_HOME")
os.environ["XDG_CONFIG_HOME"] = _keycfg
import importlib
import vibe.config as _cfg
importlib.reload(_cfg)
from vibe import secrets as _sec
importlib.reload(_sec)

try:
    # ⛔ THE ONE THAT WOULD HAVE LOST A KEY. `secret-tool` prints "The name is
    # not activatable" to stderr and EXITS 0 when no keyring is running — so a
    # store that went nowhere is indistinguishable from one that worked, if you
    # believe the status. Every write is verified by reading it back, and this
    # is that assertion: a secret-tool that succeeds and stores nothing must
    # make put() fall through to the file, not report success.
    _fake = _tf.mkdtemp()
    with open(os.path.join(_fake, "secret-tool"), "w") as f:
        f.write("#!/bin/sh\n"
                "echo 'secret-tool: The name is not activatable' >&2\n"
                "exit 0\n")
    os.chmod(os.path.join(_fake, "secret-tool"), 0o755)
    _path_was = os.environ["PATH"]
    os.environ["PATH"] = _fake + ":" + _path_was
    try:
        where = _sec.put("openai", "sk-liar")
        check("a secret-tool that exits 0 and stores nothing is not believed",
              where == "file", where)
        check("…and the key is still retrievable afterwards",
              _sec.get("openai") == "sk-liar")
    finally:
        os.environ["PATH"] = _path_was
    _sec.clear("openai")

    # ⛔ THE SECOND REAL BUG. write_text() onto an existing path does not change
    # its mode, so a key file left 0644 by an older version or a restored backup
    # took the new key in the clear and was chmod'd a moment later.
    kp = _cfg.key_path("anthropic")
    kp.parent.mkdir(parents=True, exist_ok=True)
    kp.write_text("stale\n")
    os.chmod(kp, 0o644)
    _sec.file_put("anthropic", "sk-fresh")
    mode = _stat.S_IMODE(kp.stat().st_mode)
    check("a key written over a 0644 file ends up 0600", mode == 0o600, oct(mode))
    check("…and it is the new key", _sec.file_get("anthropic") == "sk-fresh")

    # …and no process-global side effect to get there. The previous version set
    # umask(0o077) and never put it back.
    before = os.umask(0o022); os.umask(before)
    _sec.file_put("anthropic", "sk-again")
    after = os.umask(0o022); os.umask(after)
    check("writing a key does not change the process umask", before == after,
          f"{oct(before)} -> {oct(after)}")

    # The order. An environment variable is this run only and must win.
    os.environ["ANTHROPIC_API_KEY"] = "sk-from-env"
    check("the environment beats the file", _sec.get("anthropic") == "sk-from-env")
    check("…and `where` says so", _sec.where("anthropic") == "environment")
    del os.environ["ANTHROPIC_API_KEY"]
    check("without it, the file answers", _sec.get("anthropic") == "sk-again")

    _sec.clear("anthropic")
    check("clearing removes it everywhere", _sec.where("anthropic") == "not set")
    check("…and takes the file with it", not kp.exists())
finally:
    if _old_cfg is None:
        os.environ.pop("XDG_CONFIG_HOME", None)
    else:
        os.environ["XDG_CONFIG_HOME"] = _old_cfg
    importlib.reload(_cfg)
    importlib.reload(_sec)


# ── 6. the voice, and the pipe it must not write into ───────────────────────
#
# ⛔ THE TRAP THIS SECTION EXISTS FOR: chibi's voice modules print to stdout
# ("[TTS] Found piper Python module"), and vibe serve's stdout IS the window's
# protocol pipe. One banner is not a cosmetic problem — the window parses every
# line as a TSV record, and the tag a banner happens to start with decides what
# it does with it.
from vibe import voice as voicemod

vstat = voicemod.shared().status()
check("the voice reports what this box can do without loading it",
      set(vstat) == {"speak", "listen", "chibi"}, str(vstat))
check("…and every answer is a word, not a crash",
      all(isinstance(x, str) and x for x in vstat.values()), str(vstat))

# ⚠ Asked of the SHARED instance and asked twice: two Voice objects would be
# two piper models resident, and the second load is the one that is slow.
check("the voice is one object per process",
      voicemod.shared() is voicemod.shared())

if vstat["listen"] == "no":
    check("a box that cannot hear says what is missing, in a sentence",
          "install" in voicemod.shared().why_deaf().lower())
else:
    check("a box that CAN hear still has an answer for why it might not",
          bool(voicemod.shared().why_deaf()))

# The guard itself: anything chibi prints must land on stderr.
import contextlib
buf_out, buf_err = io.StringIO(), io.StringIO()
with contextlib.redirect_stdout(buf_out), contextlib.redirect_stderr(buf_err):
    with voicemod._quiet():
        print("a banner chibi would print")
check("chibi's chatter is redirected off stdout", buf_out.getvalue() == "",
      repr(buf_out.getvalue()))
check("…and onto stderr, where it is still readable",
      "banner" in buf_err.getvalue())

# And end to end: the protocol survives the voice commands.
out = io.StringIO()
vs = serve.Server(serve.Wire(out))
vs.serve(io.StringIO("state\nspeak on\nspeak off\nhush\nquit\n"))
lines = [l for l in out.getvalue().splitlines()]
malformed = [l for l in lines if not l or l[0] not in "SUKTCXAEMV" or l[1:2] != "\t"]
check("every record the voice path emits is well formed", not malformed,
      str(malformed[:2]))
check("…and the window is told whether it may offer a microphone",
      any(l.startswith("V\tlisten\t") for l in lines))
check("…and whether reading aloud is on", any(l == "V\treading\tyes" for l in lines))


# ── 7. the wire ─────────────────────────────────────────────────────────────
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
