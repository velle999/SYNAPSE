"""
keywords.py — the lines synsh answers, answered by synsh.

"what time is it", "open youtube", "play music", "is firefox installed", "where
am i" — synsh already answers all of these, from a table, in milliseconds, with
no model involved. Sending them to an LLM instead is slower, less accurate, and
occasionally invents an answer.

⚠ SYNSH IS ASKED, NOT IMITATED. `synsh --intent-check LINE` exits 0 when synsh
claims a line and 70 when it does not — it runs nothing and prints nothing. So
the list of what counts as a keyword lives in synsh, where it already is, and a
new intent added there reaches this without anything here being edited. A copy
of that table would be a second answer to the same question, drifting.

⚠ WHOLE LINES ONLY, which is synsh's own rule and the reason this is safe to
put in front of a conversation: `play` and `install` are real programs, so
intents match a normalised whole line rather than a substring. "how do I play
music with mpv" is not "play music" and reaches the model, which is right.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
"""

import shutil
import subprocess

_CHECK_TIMEOUT = 5
_RUN_TIMEOUT = 30


def available() -> bool:
    return shutil.which("synsh") is not None


def claims(line: str) -> bool:
    """Does synsh answer this line itself?"""
    line = (line or "").strip()
    if not line or not available():
        return False
    try:
        r = subprocess.run(["synsh", "--intent-check", line],
                           capture_output=True, text=True, timeout=_CHECK_TIMEOUT)
    except Exception:
        return False
    return r.returncode == 0


def run(line: str) -> str:
    """Let synsh answer it. The text it printed, or an error line."""
    try:
        r = subprocess.run(["synsh", "-c", line, "--intent", "--no-color"],
                           capture_output=True, text=True, timeout=_RUN_TIMEOUT)
    except subprocess.TimeoutExpired:
        return "(synsh took too long and was stopped)"
    except Exception as e:
        return f"(synsh failed: {e})"
    out = (r.stdout or "").strip()
    err = (r.stderr or "").strip()
    if out and err:
        return f"{out}\n{err}"
    return out or err or "(done)"


def answer(line: str) -> str | None:
    """synsh's answer for this line, or None if it does not claim it."""
    return run(line) if claims(line) else None
