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
  U  text                the line being answered, echoed
  K  text                synsh answered this one; no model was involved
  T  text                a chunk of the assistant's reply
  C  id name args        a tool is waiting to be allowed to run
  X  name result         a tool ran, and what it said
  A  text                something went wrong, in a sentence
  E  serial              end of turn — the window re-enables its input on this

Commands:

  ask TEXT               answer this
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
import sys
import threading

import vibe.config as cfg
from vibe import keywords


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

    # ── state ───────────────────────────────────────────────────────────
    def emit_state(self):
        self.wire.rec("S", "backend", cfg.BACKEND)
        self.wire.rec("S", "model", self.model_label())
        self.wire.rec("S", "cloud", "yes" if cfg.BACKEND in ("anthropic", "openai") else "no")
        self.wire.rec("S", "keywords", "yes" if keywords.available() else "no")
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
                return

            if not self.ensure_model():
                return

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
        except Exception as e:
            self.wire.rec("A", f"{type(e).__name__}: {e}")
        finally:
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
        if verb == "provider":
            self.set_provider(dec(rest).strip())
            return True
        if verb == "state":
            self.emit_state()
            return True
        self.wire.rec("A", f"unknown command: {verb}")
        return True

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
        self.emit_state()
        stream = stream or sys.stdin
        for line in stream:
            if not self.command(line):
                break


def main() -> int:
    Server().serve()
    return 0
