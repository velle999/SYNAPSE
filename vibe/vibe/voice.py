"""
voice.py — the assistant speaks and listens, using the stack already on the box.

⛔ THIS IMPLEMENTS NEITHER HALF. chibi already ships a working voice stack —
piper for speech, faster-whisper for hearing, both vendored with their models —
and it contains a year of things that are only learned by using it: that
libportaudio hard-links libjack and merely initialising it can segfault the
process, that whisper-tiny hallucinates "thanks for watching" at room noise, how
to find an ALSA capture device on a box whose card 0 cannot record. A second
implementation here would be a second set of those lessons, learned again.

So this imports chibi's and wraps it. One voice stack on the machine, two
programs using it. Where chibi is not installed, speech falls back to espeak-ng
and hearing says what is missing rather than pretending.

⚠ CHIBI'S MODULES PRINT TO STDOUT — "[TTS] Found piper Python module" and
friends — and vibe's stdout IS the chat window's protocol pipe. A stray line
there is not a cosmetic problem: the window parses every line as a TSV record,
so one banner is a malformed record and the tag it starts with decides what the
window does with it. Every call into chibi below is made with stdout redirected
to stderr for that reason.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
"""

import contextlib
import os
import shutil
import subprocess
import sys
import threading
import time

_CHIBI_APP = "/usr/lib/chibi/app"
_CHIBI_DEPS = "/usr/lib/chibi/pydeps"
_STT_MODEL = "/usr/share/faster-whisper/tiny"


@contextlib.contextmanager
def _quiet():
    """chibi's modules talk. Send it to stderr, where it belongs."""
    with contextlib.redirect_stdout(sys.stderr):
        yield


def chibi_available() -> bool:
    return os.path.isdir(_CHIBI_APP) and os.path.isdir(_CHIBI_DEPS)


def _add_chibi_path():
    for p in (_CHIBI_DEPS, _CHIBI_APP):
        if p not in sys.path:
            sys.path.insert(0, p)


# How long one dictation listens for. Named because the indicator has to say
# it — a countdown that disagreed with the recorder would be worse than none —
# and a number spelled twice is a number that drifts.
LISTEN_SECONDS = 12.0


class Voice:
    """Speech out and speech in, whichever engines this box actually has.

    ⚠ EVERYTHING IS LAZY. onnxruntime and ctranslate2 are tens of megabytes of
    shared objects between them, and a desktop that never turns voice on must
    not pay for loading them — which on a cold cache is seconds, in the middle
    of the first answer.
    """

    def __init__(self):
        self._out = None            # chibi VoiceOutput, or None
        self._in = None             # chibi VoiceInput, or None
        self._out_tried = False
        self._in_tried = False
        self._espeak_pid = None
        self._lock = threading.Lock()

    # ── speaking ────────────────────────────────────────────────────────
    def _tts(self):
        if self._out_tried:
            return self._out
        self._out_tried = True
        if chibi_available():
            try:
                _add_chibi_path()
                with _quiet():
                    from voice_output import VoiceOutput
                    self._out = VoiceOutput()
            except Exception:
                self._out = None
        return self._out

    def speak(self, text: str) -> str:
        """Say it. Returns which engine did, or "" if none could."""
        text = (text or "").strip()
        if not text:
            return ""
        tts = self._tts()
        if tts is not None:
            try:
                with _quiet():
                    tts.speak_now(text)
                return "piper"
            except Exception:
                pass
        if shutil.which("espeak-ng"):
            self.stop()
            # Detached with the pipes on /dev/null: the caller may be the serve
            # loop, whose stdout is the protocol, and a child that inherits it
            # can write into the record stream.
            p = subprocess.Popen(["espeak-ng", "--", text],
                                 stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL,
                                 stdin=subprocess.DEVNULL,
                                 start_new_session=True)
            self._espeak_pid = p.pid
            return "espeak-ng"
        return ""

    def stop(self):
        """Silence whatever is being said."""
        if self._out is not None:
            try:
                with _quiet():
                    self._out.stop()
            except Exception:
                pass
        if self._espeak_pid:
            try:
                os.kill(self._espeak_pid, 15)
            except OSError:
                pass
            self._espeak_pid = None

    def wait(self, timeout: float = 120.0) -> None:
        """Block until what was queued has actually been said.

        ⛔ speak() IS NON-BLOCKING AND THE WORKER IS A DAEMON THREAD. chibi's
        VoiceOutput takes an utterance onto a queue and synthesises it on a
        background thread — right for a window that runs for hours, fatal for a
        one-shot CLI, because the interpreter kills a daemon thread on the way
        out. `vibe voice say` enqueued, returned "piper" and exited in
        milliseconds, so the utterance died mid-queue and NOTHING WAS EVER
        HEARD: every syn-speak surface (the Super+U selection key, the "Speech
        is on" confirmation, and the whole syn-speak.service announcer) called
        this and reported success in silence. Exit 0, no error, no sound.

        Only the piper path needs it. The espeak-ng fallback is started with
        start_new_session=True precisely so it outlives us, and waiting on that
        would undo the thing that makes it work.

        Bounded rather than open-ended: a synthesiser that wedges must cost a
        delay, not a `vibe` that never returns. The ceiling matches the 120s
        voice_output already puts on its own subprocess calls.
        """
        out = self._out
        if out is None:
            return
        deadline = time.monotonic() + timeout
        try:
            while out.busy and time.monotonic() < deadline:
                time.sleep(0.05)
        except Exception:
            # `busy` reaches into chibi's object; a speech tool that raised on
            # the way out would be worse than one that stops waiting.
            pass

    # ── listening ───────────────────────────────────────────────────────
    def _stt(self):
        if self._in_tried:
            return self._in
        self._in_tried = True
        if not chibi_available():
            return None
        try:
            _add_chibi_path()
            with _quiet():
                from voice_input import VoiceInput
                # ⚠ THE MODEL DIRECTORY, NOT THE MODEL NAME. A bare name sends
                # faster-whisper to HuggingFace for ~75MB on first use, so a
                # box with no network comes up deaf — and the failure is a
                # stall, not a message. chibi ships the converted model.
                self._in = VoiceInput(model_dir=_STT_MODEL if
                                      os.path.isdir(_STT_MODEL) else "")
        except Exception:
            self._in = None
        return self._in

    def prepare(self) -> bool:
        """Load the speech engine up front. True when this box can listen.

        listen() does this on its way in, which is right for a library and
        wrong for an indicator: faster-whisper takes seconds to load on a cold
        cache and THE MICROPHONE IS NOT OPEN FOR ANY OF IT. "Speak now" shown
        before this returns is an invitation to talk into nothing, and the
        words are simply gone. Idempotent — listen() finds the same cached
        engine, so calling both costs one load.
        """
        return self._stt() is not None

    def why_deaf(self) -> str:
        """One sentence naming what is missing, for a box that cannot listen."""
        if not chibi_available():
            return ("dictation needs chibi's speech engine — "
                    "synpkg install chibi")
        if not os.path.isdir(_STT_MODEL):
            return (f"the speech model is missing ({_STT_MODEL}) — "
                    "reinstall chibi")
        return "the speech engine would not start (see the log)"

    def listen(self, seconds: float = LISTEN_SECONDS) -> tuple[str, str]:
        """Capture one utterance and transcribe it.

        Returns (text, error). Exactly one of them is set.

        ⚠ BOUNDED. chibi's recorder ends a capture on silence, but a room with
        a fan in it can hold the threshold open — and a dictation that never
        returns is indistinguishable from one that failed.
        """
        stt = self._stt()
        if stt is None:
            return "", self.why_deaf()
        with self._lock:
            try:
                with _quiet():
                    stt.start_listening()
                deadline = time.time() + seconds
                while time.time() < deadline:
                    with _quiet():
                        text = stt.get_transcription()
                    if text:
                        return text.strip(), ""
                    time.sleep(0.1)
                return "", "heard nothing"
            except Exception as e:
                return "", f"the microphone could not be opened: {e}"
            finally:
                try:
                    with _quiet():
                        stt.stop_listening()
                except Exception:
                    pass

    # ── what this box can do ────────────────────────────────────────────
    def status(self) -> dict:
        return {
            "speak": ("piper" if (chibi_available() and self._tts_possible())
                      else ("espeak-ng" if shutil.which("espeak-ng") else "no")),
            "listen": "faster-whisper" if (chibi_available()
                                           and os.path.isdir(_STT_MODEL)) else "no",
            "chibi": "yes" if chibi_available() else "no",
        }

    def _tts_possible(self) -> bool:
        # Asked without LOADING it: status is called to draw a button, and
        # loading piper to answer whether piper is there would cost the second
        # this is trying to report on.
        return os.path.exists(os.path.join(_CHIBI_APP, "voice_output.py"))


_shared = None


def shared() -> Voice:
    """One Voice for the process. Two would be two piper models in memory."""
    global _shared
    if _shared is None:
        _shared = Voice()
    return _shared
