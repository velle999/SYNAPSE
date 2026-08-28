"""
wake.py — the assistant answers to its name.

⛔ SHIPS OFF, AND IT IS THE MOST SERIOUS SWITCH IN THIS PROGRAM. Everything else
here reads a file or opens a window when asked; this one leaves a microphone
open. It is per user, it is off until somebody turns it on, and while it is on
the bar says so — a listening microphone nobody can see is not a feature, it is
a thing that happened to somebody.

## What is heard, and what leaves the room

Every utterance is transcribed LOCALLY, by the whisper model already on the
disk. A line that does not wake the assistant is matched against the wake word
and dropped — it reaches no model, no log and no disk. Only a line that wakes it
becomes a turn, and then it goes wherever the backend goes, which on the default
backend is synapd on this machine and on a cloud backend is not. That last part
is why `wake` refuses to arm itself on a cloud backend without being told twice.

## The gating is chibi's design

Not its code — chibi wakes to "Chibi" and this wakes to "Synapse", so the
matcher is a different function with the same shape — but the design is hers and
it is worth saying why each part is there, because each was learned from a
television:

  * The wake word is matched FUZZILY. whisper-tiny hears "computer" as
    "computor" and "Synapse" as "sinaps"; an exact match makes the assistant
    deaf about a third of the time.
  * A rolling WINDOW stays open after a real exchange, so a follow-up does not
    need the name again. Talking to something that demands its name every
    sentence is exhausting.
  * …and the window is CAPPED at a few unaddressed turns. This is the part that
    is not obvious and is the whole defence against a room with a television in
    it: a human says the name again every so often, and a broadcast holding a
    conversation with itself never does.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
"""

import difflib
import re
import threading
import time

# The name, and the fallback. "computer" is kept for the reason chibi keeps it:
# whisper transcribes it correctly every time, where a product name is a coin
# toss. Both wake it.
# ⚠ "computer" IS A COMMON ENGLISH WORD AND THAT IS THE TRADE. Said in a
# sentence — "my computer is slow" — it wakes the assistant, which on a desktop
# assistant is usually the right answer anyway. It is kept for the reason chibi
# keeps it: whisper transcribes it correctly every time, where a product name is
# a coin toss. Somebody who does not want it can say so:
#
#     ~/.config/synui/wake.state    words = synapse
_DEFAULT_WAKE_WORDS = ("synapse", "computer")

# How long a follow-up may arrive without naming it again.
WINDOW_SECONDS = 22.0
# …and how many such turns in a row before it wants the name back. See above:
# this is the television defence.
MAX_UNADDRESSED = 4

# ⚠ SIX, MEASURED. At four, "snaps" (.833 against "synapse") wakes it, and
# "snaps of the party" is an ordinary sentence. A mishear of a seven-letter word
# that comes out shorter than six letters is too mangled to be worth catching,
# and every real one whisper produces — sinaps, cynaps, synaps, computor — is
# six or more.
_MIN_FUZZY_LEN = 6
# ⚠ 0.75, AND ONLY ON THE FIRST WORD. Both halves of that were measured rather
# than picked, because a fuzzy wake word is a trade and the numbers are not
# where intuition puts them:
#
#   want to wake   sinaps .769   cynaps .769   synaps .923   sinapse .857
#   must not       synopsis .667  synopses .800  collapse .533
#   …and computer  computor .875  computers .941  compute .933  commuter .875
#
# So a fuzzy pass over EVERY word at 0.8 — which is the obvious implementation,
# and chibi's — wakes on "compute", "computers", "commuter" and "snaps". In a
# conversation about computing that is an assistant that never stops answering.
# It also still misses "sinaps", which is what whisper-tiny actually produces.
#
# Restricting the fuzzy pass to the FIRST WORD fixes both ends at once. People
# say the name first — "Synapse, what time is it" — so a mishear of a spoken
# wake word is always word one, while "I need to compute the average" and "the
# synopsis of the film" never begin with the soundalike. That is what buys the
# room to drop the ratio to 0.75 and catch the mishears that matter.
#
# An EXACT wake word still counts anywhere in the line: "ask synapse about it"
# is addressed to it, and there is no ambiguity to protect against there.
_FUZZY_RATIO = 0.75


def wake_words() -> tuple:
    """The names it answers to, from wake.state if it says."""
    try:
        import vibe.config as cfg
        path = cfg.KEY_DIR.parent / "wake.state"
        for line in path.read_text(encoding="utf-8").splitlines():
            if line.strip().startswith("words"):
                _, _, v = line.partition("=")
                got = tuple(w.strip().lower() for w in v.split(",") if w.strip())
                if got:
                    return got
    except Exception:
        pass
    return _DEFAULT_WAKE_WORDS


# Kept as a name because the tests and the docstrings above reference it.
WAKE_WORDS = _DEFAULT_WAKE_WORDS


def names_it(text: str) -> bool:
    """Does this line address the assistant?"""
    lower = (text or "").lower()
    words_wanted = wake_words()
    for w in words_wanted:
        if w in lower:
            return True
    words = re.findall(r"[a-z']+", lower)
    if not words:
        return False
    first = words[0]
    if len(first) < _MIN_FUZZY_LEN:
        return False
    return any(difflib.SequenceMatcher(None, first, want).ratio() >= _FUZZY_RATIO
               for want in words_wanted)


def strip_name(text: str) -> str:
    """The line with the wake word taken off the front.

    "synapse what is the time" is a question about the time, and leaving the
    name in makes the model answer as though it had been asked about itself.
    Only a LEADING name is removed — "ask synapse about it" is about Synapse.
    """
    t = (text or "").strip()
    low = t.lower()
    for w in wake_words():
        if low.startswith(w):
            rest = t[len(w):].lstrip(" ,.:;—-")
            return rest or t
    return t


class Gate:
    """Which utterances become turns. Holds the window and the streak."""

    def __init__(self, window=WINDOW_SECONDS, cap=MAX_UNADDRESSED):
        self.window = window
        self.cap = cap
        self._open_until = 0.0
        self._unaddressed = 0

    def accept(self, text: str, now: float | None = None) -> bool:
        """True if this line should reach the assistant."""
        now = time.time() if now is None else now
        if not (text or "").strip():
            return False
        if names_it(text):
            self._unaddressed = 0
            self._open_until = now + self.window
            return True
        if now < self._open_until:
            # ⚠ THE CAP CLOSES THE WINDOW, it does not merely refuse the line.
            # A television talks for hours; refusing one line of it and leaving
            # the window open refuses nothing.
            if self.cap and self._unaddressed >= self.cap:
                self._open_until = 0.0
                self._unaddressed = 0
                return False
            self._unaddressed += 1
            self._open_until = now + self.window
            return True
        return False

    def close(self):
        self._open_until = 0.0
        self._unaddressed = 0

    @property
    def open(self) -> bool:
        return time.time() < self._open_until


class Listener:
    """The loop. Hears, gates, and hands accepted lines to a callback."""

    def __init__(self, voice, on_line, on_state=None):
        self.voice = voice
        self.on_line = on_line
        self.on_state = on_state or (lambda *_: None)
        self.gate = Gate()
        self._thread = None
        self._stop = threading.Event()

    @property
    def running(self) -> bool:
        return self._thread is not None and self._thread.is_alive()

    def start(self) -> str:
        """Arm it. Returns "" or a sentence saying why it could not."""
        if self.running:
            return ""
        if self.voice.status().get("listen") == "no":
            return self.voice.why_deaf()
        self._stop.clear()
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()
        return ""

    def stop(self):
        self._stop.set()
        self.gate.close()
        self.on_state("wake", "off")

    def _loop(self):
        self.on_state("wake", "on")
        try:
            while not self._stop.is_set():
                # ⚠ ONE UTTERANCE AT A TIME, through the same listen() the
                # microphone button uses. A second capture path would be a
                # second set of the recorder's failure modes.
                text, err = self.voice.listen(seconds=20.0)
                if self._stop.is_set():
                    break
                if err:
                    # "heard nothing" is the ordinary case in a quiet room and
                    # must not be reported as a fault or logged per cycle.
                    if err != "heard nothing":
                        self.on_state("wakeerror", err)
                        time.sleep(2.0)
                    continue
                if not self.gate.accept(text):
                    continue
                self.on_line(strip_name(text))
        finally:
            self.on_state("wake", "off")
