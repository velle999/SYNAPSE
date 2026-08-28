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
import pathlib
import inspect
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

# ⛔ "open downloads" — the most ordinary request there is — resolved to
# NOTHING: lowercase `downloads` is not a panel, not a URL, not an existing
# relative path (the folder is `Downloads`) and not an installed app. It is
# tested by RESOLUTION, not by launching: _spawn is stubbed so the check is
# which path was chosen, on a machine that may have no file manager at all.
from vibe import desktop as _desk

_spawned = []
_real_spawn = _desk._spawn
_desk._spawn = lambda argv: _spawned.append(argv)
try:
    home = str(pathlib.Path.home())
    for phrase in ("downloads", "Downloads", "my downloads",
                   "the downloads folder", "Downloads folder"):
        _spawned.clear()
        out = _desk.desktop_open(phrase)
        got = _spawned[0][-1] if _spawned else out
        check(f"'{phrase}' finds the real Downloads folder",
              bool(_spawned) and got.lower().endswith("downloads")
              and got.startswith(home), out)

    # ⛔ AND IT IS ABSOLUTE. A relative name handed to the file manager is
    # resolved against the CHILD's cwd, so the existence check and the thing
    # actually opened can disagree — and this reports success either way.
    _spawned.clear()
    _desk.desktop_open("downloads")
    check("…and hands over an absolute path",
          bool(_spawned) and os.path.isabs(_spawned[0][-1]),
          _spawned)

    # "open desktop" is the Desktop FOLDER. Before the folder table it reached
    # the .desktop scan, which substring-matched an unrelated entry's Name and
    # launched whatever that was.
    _spawned.clear()
    out = _desk.desktop_open("desktop")
    check("'desktop' opens the folder, not a substring-matched .desktop entry",
          bool(_spawned) and _spawned[0][-1].lower().endswith("desktop"), out)

    # A panel still wins over everything: the table is consulted first.
    check("a panel name is still a panel",
          not _desk.desktop_open("control panel").startswith("Error: nothing here"))
finally:
    _desk._spawn = _real_spawn

# ⛔ xdg-user-dir PRINTS $HOME AND EXITS 0 when there is no user-dirs.dirs, so
# an answer equal to $HOME must count as no answer — otherwise a fresh install
# opens the home folder and calls it Downloads.
_d = _desk._user_dir("DOWNLOAD")
check("a user dir is never silently $HOME",
      _d is None or _d != pathlib.Path.home(), _d)

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
      withtools.count("emit the <tool_call> block now") == 1)
check("…and states BOTH branches, so it cannot push a fact question into a grep",
      "just answer it" in withtools)
# ⚠ THE SENTENCE CLOSEST TO THE QUESTION IS THE ONE A 7B FOLLOWS, so the ban on
# announcing a tool rides here too, not only in the system block far above.
check("…and forbids announcing a tool instead of emitting one",
      "do not announce it and do not ask first" in withtools)

# The guard that does not depend on getting any of the above right.
check("a reply that starts writing the user's turn is cut",
      sc.trim_hallucinated_turn("Answer.\n◁user▷ next?") == "Answer.")
check("…in every family's markers",
      sc.trim_hallucinated_turn("A.<|im_start|>user") == "A."
      and sc.trim_hallucinated_turn("B.[INST] x") == "B."
      and sc.trim_hallucinated_turn("C.\nUser: x") == "C.")
check("…and leaves an ordinary reply alone",
      sc.trim_hallucinated_turn("Just an answer.") == "Just an answer.")


# ── 4b. the turn that TALKED ABOUT a tool instead of calling one ────────────
#
# ⛔ THE FAILURE THIS IS FOR IS THE WHOLE OF "open downloads did nothing". The
# resolution was fixed and the window shows errors now, and the folder still
# did not open two turns in eight — because the model never emitted a call. It
# announced one ("I'll use the `desktop_open` tool"), it asked to be allowed to
# use one, or it wrote its own "Tool result:" and answered from the fiction.
# All three are text where an action belonged, and all three read as success.
from vibe import llm as _llm

# The block, however the model punctuated it. A missing </tool_call> is the
# common case, not a corner one: it opens the tag inside a ```json fence and
# closes the fence instead.
_CALL = '{"name": "desktop_open", "arguments": {"target": "downloads"}}'
for label, text in [
    ("closed", f"<tool_call>{_CALL}</tool_call>"),
    ("fenced and never closed", f"Here:\n```json\n<tool_call>\n{_CALL}\n```\nDone."),
    ("cut off after the object", f"<tool_call>{_CALL}"),
]:
    got = _llm._parse_text_tool_calls(text)
    check(f"a {label} tool call is read",
          len(got) == 1 and got[0]["function"]["name"] == "desktop_open", str(got))

check("a brace inside a string does not end the block early",
      _llm._parse_text_tool_calls(
          '<tool_call>{"name": "bash", "arguments": {"command": "echo }"}}'
      )[0]["function"]["arguments"].endswith('}"}'))
# ⛔ AND A TRUNCATED ONE IS NOT A FILE. A fenced tool call that parses as
# neither used to be auto-saved as code: `program.json`, in the user's working
# directory, containing a failed attempt to open their Downloads folder.
check("a fenced tool call is never mistaken for code to save",
      _llm._auto_save_code_blocks(
          '```json\n<tool_call>\n{"name": "desktop_open", "arguments": {\n```') == [])
check("…and an ordinary code block still is",
      [p for p, _ in _llm._auto_save_code_blocks(
          "```python\n# file: hello.py\nprint('hi')\n```")] == ["hello.py"])
check("prose that merely mentions the tag is not a call",
      _llm._parse_text_tool_calls("emit a <tool_call> block when you need one") == [])

# What a stalled turn looks like — every string here was produced by the
# shipped local model for "open downloads".
for said in ("Understood. I'll use the `desktop_open` tool. Please confirm if "
             "you'd like me to proceed.",
             '**Tool result:** ["Documents", "Downloads"]  It is open now.',
             "Here are the files and folders in your Downloads directory:"):
    check(f"a turn that only talks is caught: {said[:34]!r}",
          bool(_llm._TOOL_STALL_RE.search(said)))
for said in ("The speed of light is 299,792,458 m/s.",
             "Dune was written by Frank Herbert.",
             "Opened /home/velle/Downloads in synfiles."):
    check(f"…and an ordinary answer is not: {said[:28]!r}",
          not _llm._TOOL_STALL_RE.search(said))

# ⚠ `/no_think` IS QWEN'S SYNTAX AND NOTHING ELSE'S. SynapseOS ships a Mistral,
# which reads it as the first two words the user said.
_m = VibeModel.__new__(VibeModel)
_m._synapd_model = "Mistral Nemo Instruct 2407"
import vibe.config as _cfg
_was, _cfg.BACKEND, _cfg.THINKING = (_cfg.BACKEND, "synapd", False)
check("a Mistral is asked the question and nothing else",
      _m._user_content("open downloads") == "open downloads")
_m._synapd_model = "Qwen3 8B"
check("…and a Qwen still gets its directive",
      _m._user_content("open downloads") == "/no_think open downloads")
_cfg.BACKEND = _was

# The rules the model reads. Announcing a tool costs a whole turn, so the
# protocol says so before it happens rather than after.
_proto = sc.tool_protocol_text(TOOL_SCHEMAS)
check("the protocol says text is not an action", "TEXT IS NOT" in _proto)
check("…that permission is not the model's to ask for",
      "Do not ask permission" in _proto)
check("…that it may never write a tool result itself",
      "Never write 'Tool result:'" in _proto)
check("…and that opening a folder is desktop_open, not list_dir",
      "desktop_open" in _proto and "opens nothing" in _proto)


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
# ⛔ AND THE EMPTY STRING IS NOT A NUL. This line used to read
# `== (s or "\x00")` — it asserted the bug. enc("") is the sentinel "%00", a
# plain percent-decode makes that a NUL character, and the window tells a tool
# CALL from its RESULT by asking whether the name field is empty. It never was,
# so every tool result rendered as the call's "…" and every tool ERROR was
# dropped — with a green test saying the wire was fine.
for s in ("plain", "a\tb", "one\ntwo", "100%", "é ☃", "", "a\\b\"c"):
    check(f"the wire round-trips {s!r}", serve.dec(serve.enc(s)) == s)
check("…and an empty field arrives EMPTY, not as a NUL",
      serve.dec(serve.enc("")) == "" and serve.enc("") == "%00")

rec = io.StringIO()
serve.Wire(rec).rec("T", "col1\tcol2\nline2")
line = rec.getvalue().rstrip("\n")
check("a record is ONE line whatever is in the field", "\n" not in line)
check("…and its fields are still two", len(line.split("\t")) == 2, line)

# ── 8. a filename is not a web address ──────────────────────────────────────
#
# ⛔ REPORTED AS "IT TRIED A WEBSITE WITH THE FILENAME". desktop_open asked
# "does this look like a domain" BEFORE it asked "does this exist", and the
# test for a domain was `word dot word` — which `document.pdf` passes. So a
# file the assistant had just listed for the user was opened as
# https://document.pdf in a browser.
#
# ⚠ THE TWO SETS OVERLAP AND THE FILE EXTENSION WINS: .zip, .mov, .sh and .py
# are all real TLDs. On a desktop assistant those name files.
from vibe.desktop import _looks_like_host

for name in ("document.pdf", "report.zip", "notes.txt", "clip.mov",
             "photo.jpeg", "archive.tar.gz", "build.sh", "script.py"):
    check(f"'{name}' is a file, not a web address", not _looks_like_host(name))
for host in ("example.com", "claude.ai", "www.anything.internal",
             "github.com/velle999/SYNAPSE"):
    check(f"'{host}' is still a web address", _looks_like_host(host))

# The routing order itself: something that EXISTS can never be a guess.
_spawned = []
_desk._spawn = lambda argv: _spawned.append(argv)
try:
    with tempfile.TemporaryDirectory() as td:
        real = pathlib.Path(td) / "document.pdf"
        real.write_text("not a website")

        _spawned.clear()
        _desk.desktop_open(str(real))
        check("a real file named like a domain opens as a FILE",
              bool(_spawned) and _spawned[0][-1] == str(real), _spawned)

        # ⛔ A LISTING IS A CONTEXT THE NEXT LINE INHERITS. Shown a folder, the
        # user says "open document.pdf" — a BARE name, because that is how the
        # listing read. Without the note it resolves against vibe's cwd and
        # misses; with it, it is the file they were just looking at.
        _desk.note_dir(td)
        _spawned.clear()
        out = _desk.desktop_open("document.pdf")
        check("…and a bare name from the last listing finds it",
              bool(_spawned) and _spawned[0][-1] == str(real), out)

        # It rescues an error; it must never shadow a real answer.
        _spawned.clear()
        _desk.desktop_open("downloads")
        check("…and the folder table still wins over the last listing",
              bool(_spawned) and _spawned[0][-1].lower().endswith("downloads"),
              _spawned)

    _desk._last_dir = None
    _spawned.clear()
    out = _desk.desktop_open("document.pdf")
    check("with nothing listed, a bare filename is an error, not a browser",
          not _spawned and out.startswith("Error:"), out)
finally:
    _desk._spawn = _real_spawn
    _desk._last_dir = None


# ── 9. list_dir says WHICH directory ────────────────────────────────────────
#
# It returned bare names, so the only thing the model could hand back on the
# next turn was a bare name — and a bare name resolves against vibe's cwd, not
# against the folder the user was just shown.
with tempfile.TemporaryDirectory() as td:
    (pathlib.Path(td) / "a.txt").write_text("x")
    out = execute_tool("list_dir", {"path": td})
    check("a listing names the absolute directory it listed",
          out.splitlines()[0] == f"{td}:", out.splitlines()[:1])
    check("…and still lists the entries", "a.txt" in out, out)
    check("…and records it for the next turn", _desk._last_dir == pathlib.Path(td),
          _desk._last_dir)
_desk._last_dir = None


# ── 10. a question about THIS machine never lands in a mode with no tools ───
#
# ⛔ THE WORST ANSWER THIS ASSISTANT CAN GIVE, AND THE ROUTER MADE IT THE ONLY
# ONE AVAILABLE. "show me my downloads" matched no machine word, so AUTO sent
# it to ASK, which has no tools — and the model, asked for the contents of a
# real folder with no way to look, invented them: example.zip, document.pdf,
# image.jpg, correct in shape and wrong in every entry.
for line in ("show me my downloads", "what is in my downloads folder",
             "list my downloads", "what files are in my downloads",
             "open my downloads", "show me my desktop folder",
             "what is on this machine"):
    check(f"'{line}' reaches a mode that can look",
          modes.route(line) != modes.ASK, modes.route(line))

# ⚠ AND THE OTHER DIRECTION, which is the half a broad rule breaks. These need
# no tools and must not grow any.
for line in ("what is the speed of light", "who wrote the novel Dune",
             "write me a haiku about a cat", "explain what a pdf is",
             "write me an email about the build failing"):
    check(f"'{line}' still answers without tools",
          modes.route(line) == modes.ASK, modes.route(line))

# The one line ASK gets when the question is about this machine, and only then.
check("an ASK turn about this machine is told it cannot see it",
      "cannot see this computer" in modes.ask_addendum("show me my downloads"))
check("…and a question about the world is not",
      modes.ask_addendum("what is the speed of light") == "")


# ── 6. the direct layer: the model is not asked, so it cannot decline ───────
#
# ⛔ THIS IS THE SECTION THAT REPLACES A MEASUREMENT OF THE MODEL. "open
# downloads" was measured at 2 successful runs in 8 with the resolution already
# correct — the local Mistral announced the tool, or asked permission for a
# tool that never asks, or wrote its own `Tool result:` and answered from the
# fiction. A row that is 2/8 is not a row to tune; it is a request that should
# never have been put to a model. These assertions are deterministic because
# the thing they test is.
from vibe import intents, desktop

# The phrasings people actually use for the one request that failed. Every one
# of them must resolve to the SAME folder — that is the whole claim.
_downloads = desktop.dir_target("downloads")
if _downloads is None:
    print("  note  this account has no Downloads folder — the open path is untested")
else:
    for line in ("open downloads", "open my downloads", "open the downloads folder",
                 "show me my downloads", "go to downloads", "take me to my downloads",
                 "launch my downloads folder", "pop open downloads",
                 "hey synapse, could you open my downloads please",
                 "Open Downloads!", "open downloads please"):
        hit = intents.match(line)
        check(f"'{line}' opens the folder itself",
              hit is not None and hit.tool == "desktop_open"
              and desktop.dir_target(hit.args.get("target", "")) == _downloads,
              repr(hit))

    # ⚠ AND THE READING FORMS GO TO A READ, not to an open and not to a model.
    # This is the request that used to be answered with an invented listing.
    for line in ("what is in my downloads", "what's in downloads",
                 "list my downloads", "show me what is in my downloads folder",
                 "contents of downloads", "what files are in my downloads"):
        hit = intents.match(line)
        check(f"'{line}' reads the folder",
              hit is not None and hit.tool == "list_dir"
              and hit.args.get("path") == str(_downloads), repr(hit))

# ⛔ THE HALF THAT A GENEROUS VERB LIST BREAKS. None of these may be claimed:
# the layer answers the requests it is certain of and gets out of the way, and
# a false claim does something nobody asked for, which is worse than a slow turn.
for line in ("who wrote the novel Dune", "what is the speed of light",
             "write me a haiku about my downloads folder",
             "how do I open my downloads from a script",
             "explain what the downloads folder is for",
             "open a pull request", "show me the code",
             "what is in this function", "list the packages I have installed",
             "?open downloads", "!ls ~/Downloads"):
    check(f"'{line}' is left to the assistant", intents.match(line) is None,
          repr(intents.match(line)))

# ⚠ RESOLVE, THEN CLAIM. A verb that matched but names nothing real must fall
# through — that is what lets the verb list be as broad as it is.
check("an open of something that does not exist is not claimed",
      intents.match("open zzznotathing") is None)

# ⛔ CASE SURVIVES THE NORMALISER. The line is where the target comes from, and
# a target is a real name on a real disk — lowercasing the whole line turns
# `open ~/Downloads` into a path that does not exist, so the most precisely
# typed request is the one that breaks.
check("normalising keeps the case of a path",
      "Downloads" in intents.normalise("please open ~/Downloads"))
_cased = intents.match("open ~/Downloads")
check("…and a capitalised path still opens",
      _cased is not None and _cased.args.get("target") == "~/Downloads", repr(_cased))
check("…while a table lookup is still case-insensitive",
      intents.match("LOCK THE SCREEN") is not None)

# ⛔ A NAKED WORD IS NOT A PATH HERE. vibe's working directory is whatever
# launched it — in the REPL, the project you pointed it at — so "start the
# tests" would resolve `tests` against it and open a file manager on the test
# directory, having been asked to run something.
_here = pathlib.Path(__file__).resolve().parent
_cwd = os.getcwd()
os.chdir(_here.parent)
try:
    check("a bare relative name is not opened by the direct layer",
          intents.match("start the tests") is None, repr(intents.match("start the tests")))
    check("…but a path written as one still is",
          intents.match("open ./tests") is not None)
    # …and the model's own tool is unchanged: it passes what it resolved.
    check("…and the tool itself still takes a bare relative path",
          desktop.resolve_open("tests") is not None)
finally:
    os.chdir(_cwd)

# The confirmation policy does not change because the model left the path.
_writes = intents.match("move the bar to the bottom")
check("a setting still asks first", _writes is not None and _writes.confirm)
_lock = intents.match("lock the screen")
check("a compositor verb still asks first", _lock is not None and _lock.confirm)
_open = intents.match("open downloads")
check("…and opening a folder still does not",
      _open is not None and not _open.confirm)

# ⚠ A VALUE THE SETTING CANNOT TAKE IS ANSWERED, not confirmed and refused.
_bad = intents.match("put the bar on the left")
check("an impossible setting is answered in a sentence",
      _bad is not None and _bad.tool == "" and "top" in _bad.answer, repr(_bad))

# ⛔ ASK AND PLAN DO NOT ACT. Somebody in Ask mode asked for an answer; somebody
# in Plan asked what WOULD be done, and doing it is the one reply that cannot be.
check("Ask mode does not carry out a desktop request",
      not modes.direct_allowed(modes.ASK))
check("Plan mode does not either", not modes.direct_allowed(modes.PLAN))
check("Auto and Agent do", modes.direct_allowed(modes.AUTO)
      and modes.direct_allowed(modes.AGENT))

# ⚠ EVERY PHRASE IN THE ACTION TABLE NAMES A VERB THIS COMPOSITOR HAS, and the
# list of verbs comes from `synctl binds` rather than from a copy. Vacuous
# where synui is not running, which is the honest answer there.
_known = set(desktop.dispatch_actions())
if _known:
    _unknown = sorted({a for a in intents.ACTIONS.values() if a not in _known})
    check("every phrase maps to a verb this build dispatches", not _unknown,
          ", ".join(_unknown))
else:
    print("  note  synctl is not answering — the action table is untested here")

# ⚠ TWO WORDS MINIMUM. `lock`, `record` and `screenshot` are real programs, and
# a one-word phrase in this table would take a line the shell route should have.
check("no action phrase is a single word",
      all(len(k.split()) >= 2 for k in intents.ACTIONS))

# ⛔ AND IT MUST NOT LOAD A MODEL. The whole point of the layer on a box whose
# GPU the compositor shares: a direct turn touches neither the weights nor the
# tokeniser. `vibe.llm` is imported by this test file, so the check is that the
# ENGINE does not build one — ensure_model() is what costs the seconds.
_out = io.StringIO()
_eng = serve.Server(serve.Wire(_out))
_spawned = []
_real_spawn = desktop._spawn
desktop._spawn = lambda argv: _spawned.append(argv)
try:
    if _downloads is not None:
        _eng._run_turn("open downloads")
        _eng._turn = None
finally:
    desktop._spawn = _real_spawn
if _downloads is not None:
    check("a direct turn actually reaches the file manager",
          bool(_spawned) and str(_downloads) in " ".join(_spawned[-1]),
          repr(_spawned))
    check("…and no model was built to do it", _eng.model is None)
    check("…and the window is told which route answered",
          "\tdirect" in _out.getvalue(), _out.getvalue()[:200])


# ── 12. the launcher, and whether it can fail ───────────────────────────────
#
# ⛔ THE ARGV, NOT JUST THE PATH. The check above asks whether the folder's path
# reached the file manager, and `['synfiles', '/home/velle/Downloads']` passes
# it — while synfiles answers `unknown command`, prints its usage and exits 2.
# synfiles is a VERB CLI: its window is `gui`. Every other manager in the list
# takes a bare path, which is what made one shared argv look right for years.
_fm = dict(desktop._FILE_MANAGERS)
check("synfiles is asked for its GUI, not handed a bare path",
      _fm["synfiles"](pathlib.Path("/tmp/x")) == ["synfiles", "gui", "/tmp/x"],
      repr(_fm["synfiles"](pathlib.Path("/tmp/x"))))
check("…and the managers that DO take a bare path still get one",
      _fm["thunar"](pathlib.Path("/tmp/x")) == ["thunar", "/tmp/x"])
check("…and synfiles is still first", desktop._FILE_MANAGERS[0][0] == "synfiles")

# ⛔ A LAUNCHER THAT CANNOT FAIL CANNOT SAY IT WORKED. Popen succeeds for
# anything that execs, so a program that rejects its arguments and exits two
# milliseconds later was reported as "Opened your Downloads folder" with its
# complaint on /dev/null. That is the whole reason the wrong argv above
# survived: nothing in the path could contradict it.
check("a child that exits non-zero is reported, not claimed",
      (desktop._spawn(["sh", "-c", "exit 2"]) or "").startswith("Error:"),
      repr(desktop._spawn(["sh", "-c", "exit 2"])))
check("…a program that is not there is too",
      (desktop._spawn(["definitely-not-a-program-xyz"]) or "").startswith("Error:"))
check("…a child still running is a success", desktop._spawn(["sleep", "5"]) is None)
# ⚠ xdg-open HANDS OFF AND RETURNS. A fast exit 0 is the normal shape of a
# successful launch, not a failure — treating it as one swaps this bug for its
# mirror image.
check("…and so is one that did its job and exited", desktop._spawn(["true"]) is None)

_err = desktop._open_dir.__globals__["_spawn"]
try:
    desktop._open_dir.__globals__["_spawn"] = lambda argv: "Error: exited with status 2"
    check("a file manager that died does not report an open folder",
          desktop._open_dir(pathlib.Path("/tmp")).startswith("Error:"))
finally:
    desktop._open_dir.__globals__["_spawn"] = _err


# ── 13. what this machine is ────────────────────────────────────────────────
#
# ⛔ ASKED "pc stats?", THE ASSISTANT INVENTED A SPEC SHEET — an i7-9700K with
# 16 GB and a GTX 1650, on a Ryzen 5 5600X with 32 GB and an RTX 3060. Not one
# field was right and nothing marked it a guess. The turn had routed to ASK,
# which has no tools, so the only answer available was a plausible one.
from vibe import system

_facts = system.machine_facts()
check("the machine answers with facts about itself", bool(_facts) and
      not _facts.startswith("Error:"), _facts[:120])
# ⚠ COMPARED AGAINST THE KERNEL'S OWN COPY, because "it returned some text" is
# what the invented answer also did. The CPU line has to be the one in
# /proc/cpuinfo or it is prose again.
_cpuinfo = pathlib.Path("/proc/cpuinfo").read_text()
_model = ""
for _l in _cpuinfo.splitlines():
    if _l.startswith("model name"):
        _model = _l.split(":", 1)[1].strip()
        break
if _model:
    check("…and the CPU it names is the one in /proc/cpuinfo",
          _model in _facts, f"{_model!r} not in {_facts[:200]!r}")
check("…and no field is stated that could not be read",
      all(l.split(": ", 1)[1].strip() for l in _facts.splitlines() if ": " in l))
# btrfs subvolumes are one filesystem; five rows for one 235 GB disk reads as
# five drives.
check("…and one filesystem is listed once",
      len(system.disks()) == len({d.split(":")[0] for d in system.disks()}))

check("system_info is a tool the model can reach", "system_info" in TOOL_MAP)
check("…described in the schemas it is shown",
      any(s["function"]["name"] == "system_info" for s in TOOL_SCHEMAS))
check("…and it changes nothing, so it does not ask",
      "system_info" not in VibeModel._CONFIRM_TOOLS)
check("…and Plan mode may still use it", "system_info" in modes.READ_ONLY_TOOLS)

for _line in ("pc stats?", "pc stats", "system specs", "specs", "hardware",
              "computer stats", "what are my specs", "what cpu do i have",
              "how much ram do i have", "what machine is this",
              "show me my system specs", "what's my graphics card"):
    _h = intents.match(_line)
    check(f"{_line!r} is answered from the machine, not from a model",
          _h is not None and _h.tool == "system_info",
          repr(_h))

# ⛔ AND NOT A QUESTION ABOUT THE WORLD. "what is a gpu" is a thing to explain,
# not this machine's graphics card; a direct layer that claimed it would answer
# a question nobody asked with a hardware listing.
for _line in ("what is a gpu", "explain how cpu caches work",
              "how much ram does a raspberry pi have", "write a spec for the parser",
              "how do i check my specs from a script",
              "what are the specs of the new macbook", "?pc stats"):
    _h = intents.match(_line)
    check(f"{_line!r} is left to the assistant",
          _h is None or _h.tool != "system_info", repr(_h))

# The router is the second line of defence: a phrasing the table above misses
# must still reach a mode that HAS system_info rather than one that must guess.
for _line in ("pc stats?", "what are my specs", "how much ram do i have",
              "system specs"):
    check(f"{_line!r} routes somewhere with tools", modes.route(_line) != modes.ASK,
          modes.route(_line))
for _line in ("who wrote Dune", "what is the speed of light", "what is a gpu",
              "write a poem about a cpu"):
    check(f"…while {_line!r} still just gets an answer",
          modes.route(_line) == modes.ASK, modes.route(_line))

# ⛔ ONE FILE-MANAGER LIST, TWO FRONT ENDS. system.py carried a second one —
# thunar first, no synfiles — for the REPL's `/files`, so the terminal and the
# chat window opened different programs for the same request, and only one of
# them was ever fixed. `/files` calls desktop.py now.
_seen = []
_real = desktop._open_dir
try:
    desktop._open_dir = lambda p: _seen.append(p) or "ok"
    system.open_file_manager("/tmp")
finally:
    desktop._open_dir = _real
check("the REPL's /files uses the assistant's file-manager list", bool(_seen),
      repr(_seen))
# ⚠ THE ROSTER, NOT THE PROSE. The docstring names synfiles precisely to say
# why the code must not — so the check is that no VALUE in this module is a
# list of file managers.
check("…and system.py holds no roster of its own",
      not any(isinstance(v, (list, tuple)) and
              any("thunar" in str(x) or "pcmanfm" in str(x) for x in v)
              for v in vars(system).values()))


# ── 14. opening a panel is not toggling one ─────────────────────────────────
#
# ⛔ "OPEN THE TASK MANAGER" WITH IT ALREADY OPEN CLOSED IT, and said it had
# opened it. Every panel synui draws is one toggling action, which is right for
# the key and wrong for a sentence. `show` is asked for first.
_calls = []
_real_run = desktop._run


class _Reply:
    def __init__(self, out): self.stdout, self.stderr, self.returncode = out, "", 0


try:
    desktop._run = lambda argv, **k: (_calls.append(argv), _Reply('{"ok":true}'))[1]
    check("a panel is asked to SHOW, not to toggle",
          desktop._dispatch_panel("taskmgr") is None and
          _calls[0] == ["synctl", "dispatch", "show", "taskmgr"], repr(_calls))
    check("…and one call is enough when it works", len(_calls) == 1, repr(_calls))

    # ⚠ AN OLDER synui HAS NO `show`, and refuses it the same way it refuses a
    # typo — so the retry is what keeps this working across the upgrade.
    _calls.clear()
    _seq = ['{"error":"unknown action","action":"show"}', '{"ok":true}']
    desktop._run = lambda argv, **k: (_calls.append(argv), _Reply(_seq[len(_calls) - 1]))[1]
    check("…a compositor without the verb falls back to the action",
          desktop._dispatch_panel("taskmgr") is None and len(_calls) == 2 and
          _calls[1] == ["synctl", "dispatch", "taskmgr"], repr(_calls))

    # ⛔ synctl EXITS 0 WHEN IT REFUSES — the reply is the answer, never the
    # status. A returncode check here reported a typo'd panel as opened.
    _calls.clear()
    desktop._run = lambda argv, **k: (_calls.append(argv),
                                      _Reply('{"error":"unknown action"}'))[1]
    _bad = desktop._dispatch_panel("not_a_panel")
    check("a refused panel is an error, though synctl exited 0",
          _bad is not None and _bad.startswith("Error:"), repr(_bad))
    check("…and it was not claimed as opened after two tries", len(_calls) == 2,
          repr(_calls))
finally:
    desktop._run = _real_run


print(f"\n{npass} passed, {nfail} failed")
sys.exit(1 if nfail else 0)
