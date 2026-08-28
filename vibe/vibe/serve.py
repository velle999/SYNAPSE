"""
serve.py — vibe as a long-lived process the chat window talks to.

── Why the window is not its own assistant ──────────────────────────────────

The obvious way to build a chat window on quickshell is to let QML hold the
conversation and call the model itself. That would have been a second
assistant: a second system prompt, a second tool loop, a second idea of when to
ask before writing a file — and none of the tools, because they are Python.

So the window owns no conversation. It sends lines and draws what comes back.
The synsh keyword path, the tool loop, the confirmation gate and every backend
work in the window because they are not implemented in the window.

── The protocol ─────────────────────────────────────────────────────────────

Commands arrive one per line on stdin. Every reply line is a TSV record whose
FIRST field is a one-letter tag, and every field after the tag is
percent-encoded — the same rule syn-edit's engine uses, and load-bearing for
the same reason twice over: an answer can contain tabs, and a file the model
quotes can contain bytes that are not valid UTF-8.

  S  key value           one fact about the engine's state
  M  mode                 the mode this turn actually ran in
  V  key value            what the voice can do, and what it heard
  U  text                the line being answered, echoed
  K  text                synsh answered this one; no model was involved
  T  text                a chunk of the assistant's reply
  C  id name args        a tool is waiting to be allowed to run
  X  name result         a tool ran, and what it said
  A  text                something went wrong, in a sentence
  E  serial              end of turn — the window re-enables its input on this

Commands:

  ask TEXT               answer this
  mode auto|ask|agent|plan   how it should behave
  speak on|off           read the answers aloud
  listen                 take one spoken line and answer it
  wake on|off            answer to its name, hands-free
  hush                   stop talking, now
  confirm ID yes|no      allow or refuse the tool waiting under ID
  reset                  forget the conversation
  provider NAME          switch backend, for this process
  state                  re-emit the S records
  quit

⚠ THE TURN RUNS ON ITS OWN THREAD. `confirm` arrives while the model is
waiting for it, so the reader cannot be the thing that is blocked — the first
version of this deadlocked on its own confirmation, which looks exactly like a
model that has stopped answering.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
"""

import itertools
import json
import os
import sys
import threading
from pathlib import Path

import vibe.config as cfg
from vibe import intents, keywords, modes, tools, voice as voicemod, wake as wakemod


# ── the wire ────────────────────────────────────────────────────────────────

_SAFE = frozenset(
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    " ._-+:/@,()[]{}!?'\"=<>*#$&;|~^`\\"
)


def enc(v) -> str:
    """Percent-encode a field. Everything a tab, a newline or a % could break."""
    out = []
    for ch in str(v):
        if ch in _SAFE:
            out.append(ch)
        else:
            out.extend(f"%{b:02X}" for b in ch.encode("utf-8"))
    return "".join(out) or "%00"


def dec(v: str) -> str:
    # ⛔ `%00` IS enc()'s EMPTY FIELD, NOT A NUL CHARACTER. Decoded as one, an
    # empty field arrives on the far side as "\u0000" — which is not `""`.
    # That is what broke the window's tool records a second time: the engine
    # sends a tool CALL as ("X", name, "…") and its RESULT as ("X", "",
    # result), and the QML tells them apart by asking whether the name is
    # empty. It never was, so every result took the call's branch and rendered
    # as a bare "…" — errors included, again.
    #
    # ⚠ enc AND dec ARE INVERSES OR THEY ARE NOTHING. The sentinel stays — a
    # field that encoded to "" would be at the mercy of anything that trims a
    # line — so the round trip is closed here, and in the QML's dec() to match.
    if v == "%00":
        return ""
    if "%" not in v:
        return v
    out = bytearray()
    i = 0
    while i < len(v):
        if v[i] == "%" and i + 2 < len(v) + 1:
            try:
                out.append(int(v[i + 1:i + 3], 16))
                i += 3
                continue
            except ValueError:
                pass
        out.extend(v[i].encode("utf-8"))
        i += 1
    return out.decode("utf-8", "replace")


class Wire:
    """Records out, one line each, flushed.

    ⚠ FLUSHED EVERY TIME, and locked. Python buffers a pipe by default, so an
    unflushed stream is a window that shows nothing until the turn ends — which
    is the whole difference between a chat that streams and one that appears to
    have hung."""

    def __init__(self, out=None):
        self._out = out or sys.stdout
        self._lock = threading.Lock()

    def rec(self, tag: str, *fields):
        line = "\t".join([tag] + [enc(f) for f in fields])
        with self._lock:
            self._out.write(line + "\n")
            self._out.flush()


# ── the engine ──────────────────────────────────────────────────────────────

class Server:
    def __init__(self, wire: Wire | None = None):
        self.wire = wire or Wire()
        self.model = None
        self._serial = itertools.count(1)
        self._turn: threading.Thread | None = None
        self._pending: dict[str, dict] = {}
        self._ids = itertools.count(1)
        self._stop = threading.Event()
        self.mode = modes.AUTO
        self.speaking = False           # read answers aloud
        self._voice = None
        self._said = ""                 # what has already been spoken this turn
        self._wake = None
        self._facts = {}

    # ── state ───────────────────────────────────────────────────────────
    def emit_state(self):
        self.wire.rec("S", "backend", cfg.BACKEND)
        self.wire.rec("S", "model", self.model_label())
        self.wire.rec("S", "cloud", "yes" if cfg.BACKEND in ("anthropic", "openai") else "no")
        self.wire.rec("S", "keywords", "yes" if keywords.available() else "no")
        self.wire.rec("S", "mode", self.mode)
        # ⚠ ASKED WITHOUT LOADING THE ENGINES. status() reports what is
        # installed; building piper to answer "is piper there" would cost a
        # second of model loading every time the window opens.
        for k, v in voicemod.shared().status().items():
            self.wire.rec("V", k, v)
        self.wire.rec("V", "reading", "yes" if self.speaking else "no")
        self.wire.rec("V", "wake", "on" if (self._wake and self._wake.running) else "off")
        self.wire.rec("S", "ready", "yes" if self.model else "no")

    def model_label(self) -> str:
        if cfg.BACKEND == "ollama":
            return cfg.OLLAMA_MODEL
        if cfg.BACKEND == "synapd":
            return "synapd (local)"
        if cfg.BACKEND == "anthropic":
            return cfg.ANTHROPIC_MODEL
        if cfg.BACKEND == "openai":
            return cfg.OPENAI_MODEL
        return "llama.cpp (local)"

    def ensure_model(self) -> bool:
        """Build the model on first use.

        ⚠ NOT IN __init__. A backend that cannot start — no key, synapd down —
        must not stop the window from opening: a chat box that never appears
        cannot tell you why, and this one has an A record to say it in."""
        if self.model is not None:
            return True
        try:
            from vibe.llm import VibeModel
            self.model = VibeModel(verbose=False)
            self.model.confirm_tool = self._confirm
            self.model.mode = self.mode
        except Exception as e:
            self.wire.rec("A", str(e))
            return False
        self.emit_state()
        return True

    # ── the confirmation gate ───────────────────────────────────────────
    def _confirm(self, name: str, args: dict) -> bool:
        """Ask the window, and wait. Called on the turn's thread."""
        cid = f"c{next(self._ids)}"
        ev = threading.Event()
        slot = {"event": ev, "allow": False}
        self._pending[cid] = slot
        try:
            pretty = json.dumps(args, ensure_ascii=False)
        except Exception:
            pretty = str(args)
        self.wire.rec("C", cid, name, pretty)
        # ⚠ A TIMEOUT, and it REFUSES. A window that was closed mid-question
        # would otherwise leave this thread parked forever holding the turn
        # lock; and the safe answer to "nobody is there to say yes" is no.
        if not ev.wait(timeout=300):
            self._pending.pop(cid, None)
            return False
        self._pending.pop(cid, None)
        return bool(slot["allow"])

    def answer_confirm(self, cid: str, allow: bool):
        slot = self._pending.get(cid)
        if not slot:
            return
        slot["allow"] = allow
        slot["event"].set()

    # ── a turn ──────────────────────────────────────────────────────────
    def ask(self, text: str):
        if self._turn and self._turn.is_alive():
            self.wire.rec("A", "still answering the last one")
            return
        self._turn = threading.Thread(target=self._run_turn, args=(text,), daemon=True)
        self._turn.start()

    def _run_turn(self, text: str):
        serial = next(self._serial)
        self.wire.rec("U", text)
        try:
            # synsh first, and only for a line it claims whole. See keywords.py.
            answered = keywords.answer(text)
            if answered is not None:
                self.wire.rec("K", answered)
                self._said += answered
                return

            # ── Is it a desktop request? Then DO it ─────────────────────────
            #
            # ⛔ BEFORE `ensure_model()`, WHICH IS THE POINT. "open downloads"
            # now costs a resolved path and a spawn: no model is loaded, no
            # tokens are generated, and on a box whose GPU the compositor is
            # sharing, the seconds of loading and the stutter that came with
            # them do not happen at all for the requests people make most.
            # See intents.py for the measurement that made this necessary.
            if modes.direct_allowed(self.mode):
                hit = intents.match(text)
                if hit is not None:
                    self.wire.rec("M", modes.DIRECT)
                    self._run_direct(hit)
                    return

            # ── Is it a command? Then run it, rather than describe it ───────
            #
            # Asked of synsh, whose classifier knows this shell's builtins and
            # operators. An older synsh answers "" and nothing below fires,
            # which is the behaviour before this existed.
            shell_class = keywords.classify(text)
            active = modes.resolve(self.mode, text, shell_class)
            if active == modes.SHELL:
                self.wire.rec("M", modes.SHELL)
                self._run_shell(text)
                return

            if not self.ensure_model():
                return

            self.model.mode = self.mode
            # ⚠ ANNOUNCED BEFORE THE ANSWER, not after. On AUTO the mode is a
            # decision the user did not make, and a window that showed it only
            # once the reply had finished would be explaining a choice whose
            # consequences had already scrolled past.
            self.wire.rec("M", active)

            for piece in self.model.chat(text):
                if self._stop.is_set():
                    return
                # llm.py marks tool boundaries inside the token stream with
                # \x00-delimited records. They are structure, not text, and a
                # window that printed them would show the machinery.
                if piece.startswith("\x00TOOL_START\x00"):
                    parts = piece.split("\x00")
                    self.wire.rec("X", parts[2] if len(parts) > 2 else "?", "…")
                    continue
                if piece.startswith("\x00TOOL_END\x00"):
                    parts = piece.split("\x00")
                    self.wire.rec("X", "", parts[2] if len(parts) > 2 else "")
                    continue
                if piece:
                    self.wire.rec("T", piece)
                    self._said += piece
        except Exception as e:
            self.wire.rec("A", f"{type(e).__name__}: {e}")
        finally:
            # ⚠ SPOKEN AT THE END, NOT PER TOKEN. The reply arrives in
            # twenty-character chunks and a sentence handed to piper in
            # twenty-character pieces is twenty overlapping utterances. The
            # whole answer, once, when there is one.
            if self.speaking and self._said.strip():
                try:
                    voicemod.shared().speak(self._said)
                except Exception:
                    pass
            self._said = ""
            self.wire.rec("E", serial)

    # ── commands ────────────────────────────────────────────────────────
    def command(self, line: str) -> bool:
        """False to stop serving."""
        line = line.rstrip("\n")
        if not line:
            return True
        verb, _, rest = line.partition(" ")
        if verb == "quit":
            self._stop.set()
            return False
        if verb == "ask":
            self.ask(dec(rest))
            return True
        if verb == "confirm":
            cid, _, ans = rest.partition(" ")
            self.answer_confirm(cid.strip(), ans.strip() == "yes")
            return True
        if verb == "reset":
            if self.model:
                self.model.reset()
            self.wire.rec("S", "reset", "yes")
            return True
        if verb == "speak":
            self.speaking = rest.strip() == "on"
            if not self.speaking:
                voicemod.shared().stop()
            self.wire.rec("V", "reading", "yes" if self.speaking else "no")
            return True
        if verb == "hush":
            voicemod.shared().stop()
            return True
        if verb == "wake":
            self.set_wake(rest.strip() == "on")
            return True
        if verb == "listen":
            # On its own thread: capture runs for seconds and the reader must
            # stay live, exactly as it must for a confirmation.
            threading.Thread(target=self._listen_turn, daemon=True).start()
            return True
        if verb == "mode":
            self.set_mode(dec(rest).strip())
            return True
        if verb == "provider":
            self.set_provider(dec(rest).strip())
            return True
        if verb == "state":
            self.emit_state()
            return True
        self.wire.rec("A", f"unknown command: {verb}")
        return True

    def _run_direct(self, hit):
        """Carry out a desktop request the words plainly named. No model.

        ⚠ THE CONFIRMATION POLICY IS UNCHANGED AND IS NOT RELAXED HERE. The
        line has always been DOES IT WRITE, not "was a model involved" — so
        opening a folder still goes through without asking and changing a
        setting still asks, through the same gate the model's own calls use.
        Certainty about what was meant is not permission to do more.

        ⚠ THE ANSWER IS SENT AS A TOOL RECORD, not as assistant text. Nothing
        composed it, so nothing should render as though something had — and it
        is what makes an `Error:` show as an error in the window rather than as
        a sentence claiming success.
        """
        if hit.answer:
            self.wire.rec("K", hit.answer)
            self._said += hit.answer
            return
        self.wire.rec("X", hit.tool, "…")
        if hit.confirm and not self._confirm(hit.tool, hit.args):
            self.wire.rec("X", "", "not done")
            return
        try:
            result = tools.execute_tool(hit.tool, dict(hit.args))
        except Exception as e:
            result = f"Error: {type(e).__name__}: {e}"
        self.wire.rec("X", "", result)
        self._said += result

    def _run_shell(self, line: str):
        """Run a line the shell recognised, once the user allows it.

        ⛔ IT ALWAYS ASKS, and that is what makes the route safe enough to be
        automatic. synsh answers "make me a sandwich" with `shell`, because
        `make` is a real program — the same trap that makes "play music"
        hijack `play music.wav`. No classifier gets that right from the words
        alone, so the answer is not a better classifier: it is showing the
        command and waiting. A refusal costs one keypress; a wrong guess run
        silently costs whatever the command did.
        """
        # Strip synsh's own force-shell prefix before running it — `!` is an
        # instruction to this router, not part of the command.
        cmd = line.lstrip()
        if cmd[:1] == "!":
            cmd = cmd[1:].lstrip()
        if not cmd:
            return
        if not self._confirm("run", {"command": cmd}):
            self.wire.rec("X", "", "not run")
            return
        out, rc = keywords.run_shell(cmd)
        # The output is the answer. Sent as a tool record rather than as
        # assistant text, so the window renders it monospaced and nobody reads
        # a directory listing as something a model said.
        self.wire.rec("X", "", out if rc == 0 else f"{out}\n(exit {rc})")
        self._said += out if rc == 0 else f"exit status {rc}"

    # ⛔ THE DESKTOP HAS TO SAY SO, NOT JUST THE WINDOW. The chat window can be
    # closed, minimised, or on another workspace while the microphone stays
    # open — so the state goes in a file the BAR watches, and the bar's
    # assistant button changes while it is listening. A window nobody is
    # looking at is not a disclosure.
    _STATE = (Path(os.environ.get("XDG_CONFIG_HOME") or
                   (Path.home() / ".config")) / "synui" / "assistant.state")

    def _publish(self, **facts):
        try:
            self._STATE.parent.mkdir(parents=True, exist_ok=True)
            self._facts.update(facts)
            body = "".join(f"{k} = {v}\n" for k, v in sorted(self._facts.items()))
            tmp = self._STATE.with_suffix(".tmp")
            tmp.write_text("# Written by `vibe serve`. The bar reads it.\n" + body,
                           encoding="utf-8")
            os.replace(tmp, self._STATE)
        except OSError:
            # A desktop whose config dir is unwritable still gets an assistant;
            # it just gets no bar indicator, and that is not worth failing over.
            pass

    def set_wake(self, on: bool):
        """Arm or disarm the hands-free listener.

        ⛔ THE LOUDEST THING THIS PROGRAM DOES, and it announces itself both
        ways. A microphone that is open without the desktop saying so is not a
        feature; the V record is what puts the indicator on the bar and in the
        window, and it is emitted before the loop starts and after it stops.
        """
        if self._wake is None:
            self._wake = wakemod.Listener(
                voicemod.shared(),
                on_line=self._wake_line,
                on_state=self._wake_state)
        if on:
            # ⚠ A CLOUD BACKEND MEANS THE ROOM GOES TO SOMEBODY ELSE'S
            # COMPUTER. The transcription is local either way, but a line that
            # WAKES it becomes a request, and on a paid backend that request
            # leaves the machine. Saying so once is the least this can do.
            if cfg.BACKEND in ("anthropic", "openai"):
                self.wire.rec("A", f"wake is on, and the backend is {cfg.BACKEND} — "
                                   f"anything it hears you say to it goes to them. "
                                   f"`provider synapd` keeps it on this machine.")
            err = self._wake.start()
            if err:
                self.wire.rec("A", err)
                self.wire.rec("V", "wake", "off")
        else:
            self._wake.stop()

    def _wake_state(self, key: str, value: str):
        self.wire.rec("V", key, value)
        if key == "wake":
            self._publish(wake=value)

    def _wake_line(self, text: str):
        """A line that named the assistant. Answer it, out loud."""
        # ⚠ SPEAKING IS TURNED ON FOR A SPOKEN TURN, whatever the button says.
        # Somebody talking to the room is not looking at the window, and an
        # answer that only appears in text is no answer at all.
        was = self.speaking
        self.speaking = True
        try:
            self._run_turn(text)
        finally:
            self.speaking = was

    def _listen_turn(self):
        """Hear one line, show it, and answer it."""
        self.wire.rec("V", "listening", "yes")
        try:
            text, err = voicemod.shared().listen()
        finally:
            self.wire.rec("V", "listening", "no")
        if err:
            self.wire.rec("A", err)
            return
        # ⚠ ECHOED AS A HEARD LINE BEFORE IT IS ANSWERED. Speech recognition
        # gets words wrong, and an assistant that answers a misheard question
        # without showing what it heard is impossible to argue with.
        self.wire.rec("V", "heard", text)
        self._run_turn(text)

    def set_mode(self, name: str):
        if name not in modes.MODES:
            self.wire.rec("A", f"no such mode: {name} (try {', '.join(modes.MODES)})")
            return
        self.mode = name
        if self.model:
            self.model.mode = name
        # ⚠ THE CONVERSATION IS KEPT. A mode is how the assistant behaves on
        # the next turn, not who it is — switching to Plan to think about what
        # was just discussed is the whole point, and dropping the history would
        # make that impossible.
        self.wire.rec("S", "mode", name)

    def set_provider(self, name: str):
        known = ("synapd", "ollama", "llama_cpp", "anthropic", "openai")
        if name not in known:
            self.wire.rec("A", f"no such backend: {name} (try {', '.join(known)})")
            return
        cfg.BACKEND = name
        # ⚠ DROPPED, NOT RELOADED IN PLACE. The conversation so far was held by
        # the old backend's model object, and a provider switch that carried it
        # over would hand one model's tool-call transcript to another whose
        # wire shape for those is different. A new provider is a new thread.
        self.model = None
        self.wire.rec("S", "reset", "yes")
        self.emit_state()

    def serve(self, stream=None):
        self._publish(wake="off")
        self.emit_state()
        stream = stream or sys.stdin
        try:
            for line in stream:
                if not self.command(line):
                    break
        finally:
            # ⛔ CLEARED ON THE WAY OUT, whatever the way out was. A bar left
            # saying "listening" by an engine that has exited is worse than no
            # indicator: it is a false one, and the next person to see it has
            # no way to turn it off.
            if self._wake is not None:
                self._wake.stop()
            self._publish(wake="off")


def main() -> int:
    """Serve, with the protocol pipe held privately.

    ⛔ THE WIRE TAKES THE REAL STDOUT AND EVERYTHING ELSE GETS STDERR, and this
    is the only version of this guard that actually holds. The first one wrapped
    each call into chibi's voice stack in a redirect_stdout block — which works
    for a call that prints while it runs, and does nothing at all for chibi's
    capture loop, which prints "[Voice] Capturing audio via arecord …" from a
    BACKGROUND THREAD seconds later. redirect_stdout is a process-wide
    swap for the duration of a block; a thread that outlives the block prints
    into the real stream, and the real stream is the window's protocol.

    It leaked exactly once, in testing, the first time the wake word armed a
    capture. Doing it here instead means no library on any thread can reach the
    pipe: the only writer is Wire, which was handed the descriptor before it
    stopped being reachable by anybody else.
    """
    real = sys.stdout
    sys.stdout = sys.stderr
    Server(Wire(real)).serve()
    return 0
