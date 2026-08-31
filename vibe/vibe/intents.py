"""
intents.py — the desktop things this assistant does WITHOUT asking a model.

⛔ THE MODEL WAS THE FAILING PART, NOT THE TOOLS. "open downloads" was measured
end to end at **2 runs in 8**: `desktop_open` resolved the folder correctly
every time and was simply never called. The shipped local model is a Mistral
with no structured tool-calling — the tools are prompt text and a `<tool_call>`
block is a shape it has to imitate from a description — so on a plain request
it announces the tool, or asks permission for a tool that never asks, or writes
its own `Tool result:` and answers from the fiction. It also left three Python
programs on disk trying to do it by hand, one of which called `os.startfile`,
which is a Windows function.

None of that is a prompt problem. Asking the smallest, most ordinary desktop
request to survive a round trip through a 12B model is the problem, and the fix
is to stop making it: **a line that plainly names a desktop action is answered
by DOING it.** No model is loaded, nothing is generated, and the answer is the
tool's own words. It is the same bargain the shell route and synsh's keywords
already make here, extended to the desktop's own verbs.

⚠ IT IS ALSO THE ANSWER TO THE STUTTER. On a box where the model shares the
GPU with the compositor, the assistant's first turn costs seconds of loading
and a visibly janky desktop while it runs. A direct turn touches neither: the
window never calls `ensure_model()`, so "open downloads" is now instant on a
cold assistant, and the model stays unloaded until something actually needs it.

## The three rules that keep a table like this honest

1. **WHOLE LINES ONLY.** Every pattern is anchored at both ends against a
   normalised line. This is synsh's own rule for its intents and it exists
   because `open`, `run`, `list` and `lock` are all real programs and real
   English: "how do I open my downloads from a script" must reach the model,
   and it does, because it is not the whole line.

2. **RESOLVE, THEN CLAIM.** A pattern matching is not enough — the thing it
   names has to exist. `desktop.resolve_open()` walks the real lookup chain
   (panel, user folder, path, listed name, host, application) and answers
   without opening anything, so a line this cannot actually carry out is
   handed straight on to the model instead of being answered wrongly. That is
   what makes the verb list allowed to be generous.

3. **A WRITE STILL ASKS.** The confirmation policy does not change because the
   model is out of the path — the line is DOES IT WRITE. `desktop_open` and
   `list_dir` go through; `desktop_action` and `desktop_setting` reach the same
   gate they always did, and it is the caller's gate, not one of ours.

⚠ NO TABLE HERE DUPLICATES ONE THAT EXISTS. The folders come from
`desktop._DIR_WORDS`, the panels from `desktop.PANELS`, the settings and their
legal values from `desktop.SETTINGS`, and the compositor verbs are checked
against `synctl binds` — the running build's own list. A phrase table is a list
of ways to SAY things; the things themselves stay where they already are.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
"""

import re
from dataclasses import dataclass, field

from vibe import desktop


@dataclass(frozen=True)
class Intent:
    """One resolved desktop request, ready to run without a model.

    `tool` and `args` are exactly what the model would have had to emit, so the
    window, the confirmation gate and the transcript all see a normal tool call
    — the only difference is that this one was certain.

    `answer` is for the handful of lines that need a fact rather than an
    action ("the bar can only go top or bottom"); `tool` is empty then.
    """
    tool: str = ""
    args: dict = field(default_factory=dict)
    answer: str = ""
    confirm: bool = False
    why: str = ""            # which rule claimed it — for `vibe intents`


# ── normalising a line ──────────────────────────────────────────────────────
#
# ⚠ POLITENESS IS NOT PART OF THE REQUEST. "hey synapse, could you open my
# downloads please" is the same line as "open downloads", and a table that only
# knows the second one is a table that works until somebody is polite to it.
# ⚠ EVERY NAME THE DESKTOP ANSWERS TO, not just this program's. chibi drives
# these same intents through assistant_bridge.py, and the people talking to it
# call it by ITS name — "chibi, open my downloads" reached the model as a
# request to open something called "chibi, downloads" and did nothing useful.
# One list, because the two front-ends must not disagree about what counts as
# being spoken to.
_ADDRESS_RE = re.compile(
    r"^(?:hey|ok|okay|yo)?[\s,]*(?:vibe|synapse|chibi|computer|assistant)\b[\s,:]*",
    re.I)
_PREFIX_RE = re.compile(
    r"^(?:please|pls|can you|could you|would you|will you|i want you to|"
    r"i'd like you to|i would like you to|let's|lets|just)\s+", re.I)
_SUFFIX_RE = re.compile(
    r"[\s,]*(?:please|pls|for me|thanks|thank you|thx)$", re.I)


def normalise(text: str) -> str:
    """The line with the manners taken off and one space between words.

    ⛔ CASE IS KEPT, AND IT HAS TO BE. This line is where the target comes from,
    and a target is a real name on a real disk: lowercasing it turns
    `open ~/Downloads` into `~/downloads`, which does not exist, and the request
    that was typed most precisely is the one that stops working. Every pattern
    below is case-insensitive instead, and the two places that key a TABLE off
    the line lowercase it there, where it costs nothing.
    """
    t = " ".join((text or "").split()).strip()
    t = t.strip("!?. \t")
    for _ in range(4):                      # "hey synapse, please could you …"
        before = t
        t = _ADDRESS_RE.sub("", t)
        t = _PREFIX_RE.sub("", t)
        t = _SUFFIX_RE.sub("", t)
        t = t.strip("!?., \t")
        if t == before:
            break
    return t


# ── what a target might be called ───────────────────────────────────────────

_DETERMINER_RE = re.compile(r"^(?:my|the|our|a|an|some)\s+", re.I)
_NOUN_TAIL_RE = re.compile(
    r"\s+(?:folder|directory|dir|panel|window|screen|app|application|program)$", re.I)


def _forms(what: str):
    """The ways one captured target could be written, most literal first.

    ⚠ THE LITERAL FORM IS TRIED FIRST AND THAT MATTERS. Stripping words up
    front would turn a real file called `the report` into `report`; stripping
    them only after the literal form has failed cannot lose anything that
    exists.
    """
    seen = []
    for form in (what,
                 _DETERMINER_RE.sub("", what),
                 _NOUN_TAIL_RE.sub("", what),
                 _NOUN_TAIL_RE.sub("", _DETERMINER_RE.sub("", what))):
        form = form.strip()
        if form and form not in seen:
            seen.append(form)
    return seen


# ── 1. opening something ────────────────────────────────────────────────────

# Verbs that mean "put it in front of me", where an application is a perfectly
# good answer: "open firefox" launches a browser and everyone expects it to.
_OPEN_VERBS = (
    "open up", "open", "launch", "start", "bring up", "pull up", "pop open",
    "go to", "take me to", "jump to", "get me to", "navigate to",
)

# ⛔ VERBS THAT ALSO MEAN "TELL ME", and are therefore NOT allowed to launch a
# program. "show me the code" and "view the log" are requests to be told
# something; letting them reach the application lookup means "show me the code"
# substring-matches an editor called Code and the assistant answers a question
# by opening a text editor. These may only resolve to a folder, a file or one
# of this desktop's own panels.
_SHOW_VERBS = ("show me", "show", "display", "view", "browse", "let me see")
_SHOW_KINDS = frozenset({"dir", "file", "panel", "error"})

_OPEN_RE = re.compile(
    r"^(?:" + "|".join(re.escape(v) for v in
                       sorted(_OPEN_VERBS + _SHOW_VERBS, key=len, reverse=True)) +
    r")\s+(?P<what>.+)$", re.I)


def _match_open(line: str) -> Intent | None:
    m = _OPEN_RE.match(line)
    if not m:
        return None
    verb = line[:m.start("what")].strip()
    showing = any(verb.startswith(v) for v in _SHOW_VERBS)
    for form in _forms(m.group("what")):
        r = desktop.resolve_open(form, bare_relative=False)
        if r is None:
            continue
        if showing and r.kind not in _SHOW_KINDS:
            continue
        # ⚠ THE TOOL IS CALLED WITH THE WORDS, NOT WITH THE RESOLUTION. It
        # walks the same chain again and reaches the same place, and passing
        # the phrase keeps one code path between this and the model's own call.
        return Intent("desktop_open", {"target": form}, why="open")
    return None


# ── 2. listing a folder ─────────────────────────────────────────────────────
#
# ⛔ THIS IS THE ONE THE ROUTER USED TO SEND TO A MODE WITH NO TOOLS, which had
# no way to answer except to invent `example.zip`, `document.pdf`, `image.jpg`.
# Told not to invent, it answered "your Downloads folder is empty" instead —
# equally a fact about a folder it could not see. There is no prompt that fixes
# a question the machine can answer and the model cannot; reading the directory
# is the fix.
_LIST_RES = tuple(re.compile(p, re.I) for p in (
    r"^what(?:'?s| is| are)?\s+(?:in|inside)\s+(?P<what>.+)$",
    r"^(?:show|tell)\s+me\s+what(?:'?s| is| are)?\s+(?:in|inside)\s+(?P<what>.+)$",
    r"^(?:what|which)\s+files\s+(?:are\s+|do\s+i\s+have\s+)?(?:in|inside)\s+(?P<what>.+)$",
    r"^(?:list|ls)\s+(?:the\s+)?(?:contents\s+of\s+|files\s+in\s+)?(?P<what>.+)$",
    r"^(?:the\s+)?contents\s+of\s+(?P<what>.+)$",
    r"^what\s+do\s+i\s+have\s+in\s+(?P<what>.+)$",
    r"^(?:show|list)\s+me\s+the\s+(?:contents|files)\s+(?:in|of)\s+(?P<what>.+)$",
))


def _match_list(line: str) -> Intent | None:
    for rx in _LIST_RES:
        m = rx.match(line)
        if not m:
            continue
        for form in _forms(m.group("what")):
            d = desktop.dir_target(form, bare_relative=False)
            if d is not None:
                # An absolute path, because list_dir resolves a relative one
                # against vibe's cwd — whatever launched the engine.
                return Intent("list_dir", {"path": str(d)}, why="list")
    return None


# ── 3. the compositor's own verbs ───────────────────────────────────────────
#
# ⚠ TWO WORDS MINIMUM, EVERY ONE OF THEM. `lock`, `record` and `screenshot`
# are all programs somebody might have installed and might mean literally, and
# the shell route is right there for a line that is a command. An English
# phrase is not a command, which is what makes claiming it safe.
#
# ⚠ THE ACTION NAMES ARE CHECKED AGAINST `synctl binds` BEFORE ANY OF THIS IS
# OFFERED — this table says how people ask, the running compositor says what
# exists. A verb this build does not have falls through to the model rather
# than being dispatched into an error.
ACTIONS = {
    "lock the screen": "lock", "lock my screen": "lock",
    "lock the computer": "lock", "lock this machine": "lock",
    "lock the desktop": "lock", "lock it": "lock",

    "take a screenshot": "screenshot", "take a screen shot": "screenshot",
    "grab a screenshot": "screenshot", "capture the screen": "screenshot",
    "screenshot the desktop": "screenshot",

    "start the screensaver": "saver", "start the screen saver": "saver",
    "record the screen": "record", "start recording": "record",
    "stop recording": "record", "start recording the screen": "record",

    "close this window": "close", "close the window": "close",
    "maximise this window": "maximize_toggle",
    "maximize this window": "maximize_toggle",
    "maximise the window": "maximize_toggle",
    "maximize the window": "maximize_toggle",
    "minimise this window": "minimize_toggle",
    "minimize this window": "minimize_toggle",
    "make this fullscreen": "fullscreen_toggle",
    "make it fullscreen": "fullscreen_toggle",
    "go fullscreen": "fullscreen_toggle",
    "switch windows": "alt_tab", "next window": "alt_tab",
    "previous window": "alt_tab_prev", "last window": "alt_tab_prev",
    "cycle the layout": "layout_cycle", "next layout": "layout_cycle",
    "tile the windows": "retile", "retile the windows": "retile",
    "cascade the windows": "cascade",

    "turn the brightness up": "brightness_up", "brightness up": "brightness_up",
    "make the screen brighter": "brightness_up",
    "turn the brightness down": "brightness_down",
    "brightness down": "brightness_down",
    "make the screen dimmer": "brightness_down",
    "turn on night light": "night_light",
    "turn on the night light": "night_light",
    "turn on do not disturb": "dnd", "do not disturb": "dnd",
    "turn on game mode": "game", "turn off game mode": "game",

    "log out": "quit", "log me out": "quit",
}

_actions_available: frozenset | None = None


def _available_actions() -> frozenset:
    """What the running compositor dispatches. Asked once per process.

    ⚠ CACHED BECAUSE IT IS A SUBPROCESS. This runs on the path of every line
    typed at the assistant, and `synctl binds` on each of them would put a
    fork between the user and their own keystroke."""
    global _actions_available
    if _actions_available is None:
        _actions_available = frozenset(desktop.dispatch_actions())
    return _actions_available


def _match_action(line: str) -> Intent | None:
    # ⚠ LOWERCASED HERE, not in normalise() — this is a table lookup, and the
    # only other thing the line is used for is a filename. See normalise().
    action = ACTIONS.get(line.lower())
    if not action:
        return None
    known = _available_actions()
    if known and action not in known:
        return None
    return Intent("desktop_action", {"action": action}, confirm=True, why="action")


# ── 4. a setting, in the words people use for it ────────────────────────────
#
# ⚠ THE KEYS AND THE LEGAL VALUES COME FROM `desktop.SETTINGS`, so a value that
# is not allowed is caught here and SAID, rather than confirmed and then
# refused. "put the bar on the left" has an answer — the bar does not go there
# — and it is a worse assistant that asks permission before telling you so.
_SETTING_RES = (
    (re.compile(r"^(?:move|put|shift)\s+(?:the\s+)?(?P<what>bar|dock|taskbar|panel)"
                r"\s+(?:to|on|at|over to)\s+(?:the\s+)?(?P<where>top|bottom|left|right)"
                r"(?:\s+(?:of\s+the\s+)?screen)?$", re.I), "edge"),
    (re.compile(r"^(?:turn|switch)\s+(?:the\s+)?(?P<what>bar|dock|taskbar)"
                r"\s+(?P<state>on|off)$", re.I), "state"),
    (re.compile(r"^(?:turn|switch)\s+(?P<state>on|off)\s+(?:the\s+)?"
                r"(?P<what>bar|dock|taskbar)$", re.I), "state"),
    (re.compile(r"^(?P<state>hide|show)\s+(?:the\s+)?(?P<what>bar|dock|taskbar)$",
                re.I), "hideshow"),
)

# "taskbar" and "panel" are what people call the bar when they have come from
# somewhere else. One desktop's word, several people's words.
_SETTING_NAMES = {"bar": "bar", "taskbar": "bar", "panel": "bar", "dock": "dock"}


def _match_setting(line: str) -> Intent | None:
    for rx, shape in _SETTING_RES:
        m = rx.match(line)
        if not m:
            continue
        what = _SETTING_NAMES[m.group("what").lower()]
        if shape == "edge":
            key, value = f"{what}_edge", m.group("where").lower()
        elif shape == "state":
            key, value = what, m.group("state").lower()
        else:
            key = what
            value = "off" if m.group("state").lower() == "hide" else "on"
        allowed = desktop.SETTINGS.get(key)
        if allowed and value not in allowed:
            return Intent(
                answer=f"The {what} can only go on the "
                       f"{' or the '.join(allowed)} — not the {value}.",
                why="setting")
        return Intent("desktop_setting", {"key": key, "value": value},
                      confirm=True, why="setting")
    return None


# ── 5. what this machine is ─────────────────────────────────────────────────
#
# ⛔ THE OTHER QUESTION A MODEL CANNOT ANSWER AND WILL ANSWER ANYWAY. Asked
# "pc stats?", the assistant produced a spec sheet — i7-9700K, 16 GB DDR4,
# GTX 1650, 512 GB NVMe — for a machine that is a Ryzen 5 5600X with 32 GB and
# an RTX 3060. It is the same fault as the invented directory listing above,
# with the same cause: the router sent it to a mode with no tools, and the only
# answer available to a model with no way to look is a plausible one.
#
# ⚠ THE NARROW QUESTIONS COME HERE TOO. "how much ram do i have" is answered by
# the same reading of the same machine, so it is claimed rather than left to a
# model that would have to guess — the answer simply contains more than was
# asked, which is the right way round for a fact.
_FACTS_THING = r"(?:pc|system|computer|machine|hardware|box|laptop|rig)"
_FACTS_WORD = (r"(?:stats|statistics|specs|spec|specifications|info|"
               r"information|details)")
_FACTS_PART = r"(?:cpu|processor|gpu|graphics card|graphics|ram|memory|"    \
              r"motherboard|mainboard|board|kernel|uptime|disk space|"      \
              r"disks|drives|storage)"

_FACTS_RES = tuple(re.compile(p, re.I) for p in (
    # "pc stats", "system specs", "hardware info"
    rf"^{_FACTS_THING}\s+{_FACTS_WORD}$",
    # "specs", "hardware", "system info" on their own
    # ⚠ "stats" ALONE IS A WHOLE LINE HERE BECAUSE normalise() MAKES ONE.
    # `computer` is an address word — "hey computer" — so _ADDRESS_RE strips it
    # and "computer stats" reaches this table as "stats".
    rf"^(?:specs|specifications|hardware|stats|statistics)$",
    rf"^{_FACTS_THING}$",
    # "what are my pc specs", "what's the system info", "what are my specs"
    rf"^what(?:'?s| is| are)?\s+(?:my|the|this)\s+(?:{_FACTS_THING}\s+)?"
    rf"{_FACTS_WORD}$",
    # "show me my specs", "tell me my pc stats", "list my system specs"
    rf"^(?:show|tell|give|list)\s+me\s+(?:my|the|this)\s+"
    rf"(?:{_FACTS_THING}\s+)?{_FACTS_WORD}$",
    # "what machine is this", "what computer is this"
    rf"^what\s+{_FACTS_THING}\s+(?:is\s+this|am\s+i\s+(?:on|using|running))$",
    # "tell me about this machine", "what is this machine"
    rf"^(?:tell me about|what(?:'?s| is)?)\s+this\s+{_FACTS_THING}$",
    # "what cpu do i have", "how much ram do i have", "what gpu is in this pc"
    rf"^what\s+{_FACTS_PART}\s+(?:do i have|is this|have i got|"
    rf"am i (?:running|using)|is in (?:this|my) {_FACTS_THING})$",
    rf"^how much\s+{_FACTS_PART}\s+(?:do i have|have i got|is there|"
    rf"does this {_FACTS_THING} have)$",
    # "what is my cpu", "what's my graphics card"
    rf"^what(?:'?s| is| are)?\s+my\s+{_FACTS_PART}$",
))


def _match_facts(line: str) -> Intent | None:
    for rx in _FACTS_RES:
        if rx.match(line):
            return Intent("system_info", why="stats")
    return None


# ── the one entry point ─────────────────────────────────────────────────────
#
# ⚠ ORDER IS MEANING. The listing forms are tried before the opening ones
# because "show me what's in downloads" starts with a verb both would claim,
# and reading a folder is the narrower reading of it. Settings and compositor
# verbs go before the general open, so "show the dock" is a dock that comes
# back rather than a search for something called "dock".
_RULES = (_match_facts, _match_list, _match_action, _match_setting, _match_open)


def match(text: str) -> Intent | None:
    """The desktop request this line plainly is, or None to ask the model.

    ⛔ None IS THE COMMON ANSWER AND THAT IS THE DESIGN. This layer is not a
    natural-language understander and must never behave like one — it takes the
    handful of things people ask a desktop assistant to do all day, does them
    exactly, and lets everything else past untouched. A false claim here is
    worse than a missed one: a missed one costs a slow turn, a false one does
    something nobody asked for.
    """
    # ⛔ SYNSH'S PREFIXES OUTRANK THIS, both of them, and they are read off the
    # RAW line — normalise() strips leading punctuation along with the
    # politeness, so asking it first would be asking after the evidence had
    # been removed. `!` means "this is a command, run it" and `?` means "just
    # answer me"; a layer that quietly did a desktop action for a line
    # beginning with `?` would be overruling the one piece of punctuation the
    # user has to say so with.
    if (text or "").lstrip()[:1] in ("!", "?"):
        return None
    line = normalise(text)
    if not line:
        return None
    for rule in _RULES:
        hit = rule(line)
        if hit is not None:
            return hit
    return None


def describe() -> list[str]:
    """One line per kind of thing this answers directly — for `vibe intents`."""
    folders = sorted({w for w in desktop._DIR_WORDS})
    return [
        "open <folder|app|panel|path|url>   " +
        f"— folders: {', '.join(folders[:8])}…",
        "show me / go to / launch <thing>   — the same, in other words",
        "what's in <folder> / list <folder> — reads the directory, does not guess",
        f"{len(ACTIONS)} phrases for the compositor's own verbs "
        "(lock the screen, take a screenshot, …) — asks first",
        "move the bar to the bottom / turn the dock off — asks first",
        "pc stats / what are my specs / how much ram do i have "
        "— reads the hardware, does not guess",
    ]
