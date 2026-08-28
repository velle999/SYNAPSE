"""
modes.py — Ask, Agent, Plan, and the automatic choice between them.

One assistant with three ways of behaving, rather than three assistants:

  ASK    answers. No tools at all, so it cannot touch anything and cannot
         stall on a tool loop. The fastest turn, and the right one for a
         question about the world.
  AGENT  answers and ACTS. Every tool, with the confirmation gate in front of
         the ones that write.
  PLAN   works it out first. Read-only tools, so it can look at the files and
         the machine, and it may not change either — the answer is the steps
         it would take.

⚠ AUTO IS THE DEFAULT AND IT IS NOT A FOURTH MODE. It picks one of the three
per turn and then behaves exactly as if it had been chosen by hand. A user who
never touches the selector should never learn this exists.

⛔ THE PICKER IS NOT THE MODEL. Asking the model which mode to use costs a
whole extra round trip — on a local 7B that is a second and a half before the
first token of the real answer — and it is a judgement the model gets wrong in
exactly the cases that matter, because the same weakness that makes it grep for
the speed of light makes it call that request agentic. So the routing is
evidence the process already has: whether synsh claims the line, whether the
words name a file or a setting or this machine, and whether the user asked for
a plan. Everything unmatched goes to AGENT, whose tools are optional anyway —
the system prompt's own rule decides from there, and a wrong guess there costs
nothing because AGENT can still simply answer.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
"""

import re

from vibe import desktop as _desktop

ASK = "ask"
AGENT = "agent"
PLAN = "plan"
AUTO = "auto"

# ⚠ SHELL IS A ROUTE, NOT A MODE. AUTO can resolve to it and the user cannot
# select it, because "behave like a shell from now on" is not a thing anybody
# wants a chat box to do — `synsh` is right there for that. It is here so a
# line that IS a command is answered by running it rather than by a model
# guessing what running it would say.
SHELL = "shell"

# ⚠ DIRECT IS THE OTHER ROUTE THAT IS NOT A MODE, and it is the one that runs
# most often. A line that plainly names a desktop action — "open downloads",
# "what is in my documents", "lock the screen" — is carried out by
# `intents.match()` with no model in the path at all. See intents.py for why:
# the local model was measured emitting the tool call for "open downloads" in
# 2 runs out of 8, and a desktop request that works three times in ten is not
# a feature. Named here so the routes have one home; it is claimed in serve.py
# before a model is ever built.
DIRECT = "direct"

MODES = (AUTO, ASK, AGENT, PLAN)

# ⛔ WHICH MODES LET THE DIRECT LAYER ANSWER. AUTO and AGENT do; ASK and PLAN
# do not, and that is not a safety rule but a plain reading of what they were
# picked for. Somebody in ASK asked for an answer and nothing else; somebody in
# PLAN asked what WOULD be done, and doing it is the one reply that cannot be.
DIRECT_MODES = frozenset({AUTO, AGENT})

# What each mode is allowed to reach for. PLAN's list is the whole of what
# makes it safe: it can look at anything and change nothing, so a plan can be
# read before any of it happens.
READ_ONLY_TOOLS = frozenset({"read_file", "glob", "grep", "list_dir",
                            "desktop_open", "system_info"})

DESCRIPTION_SHELL = "runs it, once you say so"
DESCRIPTION_DIRECT = "does it, with no model involved"

DESCRIPTION = {
    ASK:   "answers, and touches nothing",
    AGENT: "answers and does it, asking before anything is written",
    PLAN:  "looks, and writes out the steps instead of taking them",
    AUTO:  "picks one of the three per message",
}

# ── The routing evidence ────────────────────────────────────────────────────

# Asked for a plan, in the words people actually use for it.
_PLAN_RE = re.compile(
    r"\b(plan|walk me through|how would (you|i)|what would it take|"
    r"steps to|approach for|design (a|an|the)|outline)\b", re.I)

# Names something on this machine. Deliberately broad — a false AGENT costs
# nothing (its tools are optional), while a false ASK is an assistant that
# cannot do the thing it was asked to do.
_MACHINE_RE = re.compile(
    r"(^|\s)(/|~/|\./)|"                                  # a path
    r"\b(file|files|folder|directory|dir|disk|drive|"
    r"install|uninstall|remove|update|upgrade|package|"
    r"open|launch|start|run|close|kill|"
    # ⚠ SHOWING IS DOING. "show me my downloads" is a request to open or list
    # something real, not a question — and with no verb here it fell through to
    # ASK, whose only way to answer was to make the contents up.
    r"show|list|display|view|browse|find|"
    r"bar|dock|wallpaper|theme|panel|window|workspace|monitor|screen|"
    r"setting|settings|config|configure|"
    r"service|daemon|systemd|log|logs|journal|process|"
    # ⚠ WHAT THE MACHINE IS, not only what is on it. "pc stats?" carried none
    # of the words above, routed to ASK, and was answered with a spec sheet for
    # a computer that does not exist. intents.py claims most of these outright
    # now; these are here so the ones it does not still reach a mode that has
    # `system_info` rather than a mode that has to guess.
    r"stats|statistics|specs|spec|specifications|hardware|"
    r"cpu|processor|gpu|ram|vram|uptime|motherboard|"
    r"git|commit|branch|build|compile|test|tests|"
    r"write|create|make|edit|change|move|rename|delete|fix|refactor)\b",
    re.I)

# ⛔ NAMES SOMETHING ON *THIS* MACHINE, which is a narrower question than
# _MACHINE_RE asks and is the one that gets to veto the question form below.
#
# "show me my downloads" and "what is in my downloads folder" both routed to
# ASK, which has no tools — so the model, asked for the contents of a real
# folder with no way to look, INVENTED THEM: example.zip, document.pdf,
# image.jpg, presented as fact with no tell. Reported 2026-08-28. A listing
# nobody read is the worst answer this assistant can give, and it was the
# router that made it the only one available.
#
# ⚠ THE FOLDER WORDS COME FROM desktop._DIR_WORDS, not from a copy here. A
# second list is a list that stops matching the day a folder is added to the
# other one.
_THIS_MACHINE_RE = re.compile(
    r"(^|\s)(/|~/|\./)|"                                   # a path
    r"\b(?:my|this|the)\s+(?:machine|computer|box|system|pc|laptop|desktop)\b|"
    # ⚠ AND WHAT IT IS MADE OF. "what are my specs" is the question form, so
    # without a veto here it goes to ASK — which is exactly how the assistant
    # came to describe an i7-9700K that is not in this machine.
    r"\b(?:my|this|the)\s+(?:specs|spec|specifications|stats|statistics|"
    r"hardware|cpu|processor|gpu|ram|vram|motherboard)\b|"
    r"\b(?:pc|system|computer|machine|hardware)\s+"
    r"(?:stats|statistics|specs|spec|specifications)\b|"
    r"\b(?:installed|running|on this (?:machine|box|system|computer))\b|"
    r"\b(?:" + "|".join(sorted((re.escape(w) for w in _desktop._DIR_WORDS),
                               key=len, reverse=True)) + r")\b",
    re.I)

# Plainly a question about the world, or a piece of writing. Checked BEFORE the
# machine words, because "write a poem about a file system" is writing.
_ASK_RE = re.compile(
    r"^\s*(who|what|when|where|why|how)\s+(is|are|was|were|does|do|did|"
    r"can|could|should|would)\b|"
    r"\b(explain|define|summarise|summarize|translate|"
    r"write (me )?(a|an|the)? ?(poem|haiku|story|song|joke|essay|email|"
    r"letter|reply|message|note))\b", re.I)


def route(text: str, shell_class: str = "") -> str:
    """Which mode this line wants. Never AUTO — this is what AUTO resolves to.

    `shell_class` is synsh's own answer for the line (keywords.classify): the
    assistant asks the shell what a line is rather than growing a second
    opinion about it. Empty means synsh had none, which is also what an older
    synsh says.
    """
    t = (text or "").strip()
    if not t:
        return ASK

    # ⛔ SYNSH'S EXPLICIT PREFIXES WIN OVER EVERYTHING, including the plan and
    # question words below. `!` means shell and `?` means ask, in the shell
    # this desktop ships — a chat box that ignored them would be teaching a
    # second set of the same two characters.
    if t[0] == "!":
        return SHELL
    if t[0] == "?":
        return ASK

    if _PLAN_RE.search(t):
        return PLAN
    # ⚠ ORDER MATTERS HERE. "write me an email about the build failing" carries
    # `write` and `build`, and it is writing, not a task. The question form and
    # the compose verbs win over the machine words for that reason.
    # ⛔ THE QUESTION FORM DOES NOT WIN OVER A QUESTION ABOUT THIS MACHINE.
    # "what is in my downloads folder" is the question form and it is not a
    # question about the world; sending it to ASK leaves the model no way to
    # answer except to invent the folder's contents, which is what it did.
    # A false AGENT still just answers — see this module's opening note — so
    # the veto is cheap and the failure it prevents is not.
    if _ASK_RE.search(t) and not _THIS_MACHINE_RE.search(t):
        return ASK
    # A line the shell recognises as a command is answered by RUNNING it —
    # deterministic, instant, and with no model in the path to invent what the
    # output might have been. It still asks first; see serve.py.
    #
    # ⚠ AFTER the question and compose forms, never before. synsh answers
    # "make me a sandwich" with `shell`, because `make` is a real program —
    # the same trap that makes "play music" hijack `play music.wav`. The
    # confirmation is what makes that survivable, and putting the English
    # forms first is what keeps it rare.
    if shell_class in ("shell", "builtin"):
        return SHELL

    # ⚠ _THIS_MACHINE_RE ROUTES AS WELL AS VETOES, or the evidence is used
    # once and ignored once: "what is on this machine" was strong enough to
    # stop the question form winning above, then fell past the machine WORDS
    # here — which do not include "machine" — and landed back in ASK anyway.
    if _MACHINE_RE.search(t) or _THIS_MACHINE_RE.search(t):
        return AGENT
    return ASK


def direct_allowed(mode: str) -> bool:
    """Whether this mode lets a plain desktop request be carried out directly."""
    return (mode or AUTO) in DIRECT_MODES


def resolve(mode: str, text: str, shell_class: str = "") -> str:
    """The mode to actually run this turn.

    ⚠ THE DIRECT ROUTE IS NOT DECIDED HERE. It is claimed before this is
    called, because claiming it means having already resolved the folder or the
    panel the line names — and a router that returned DIRECT would make its
    caller do that lookup a second time to find out WHAT to do. See serve.py.
    """
    if mode in (AUTO, "", None):
        return route(text, shell_class)
    # ⚠ A HAND-PICKED MODE STILL HONOURS `!`. Somebody in Ask mode who types a
    # command with a bang meant the command; refusing it because a menu says
    # "Ask" would be the menu arguing with the keyboard.
    if (text or "").strip()[:1] == "!":
        return SHELL
    return mode


def tools_for(mode: str, all_schemas: list) -> list | None:
    """The tools a mode may use. None means the model is told of none at all."""
    if mode in (ASK, SHELL):
        return None
    if mode == PLAN:
        return [s for s in all_schemas
                if s.get("function", {}).get("name") in READ_ONLY_TOOLS] or None
    return all_schemas


# What each mode adds to the system prompt. Short on purpose: a local model has
# eight thousand tokens of context and the tool schemas already spend six
# hundred of them.
PROMPT = {
    ASK: (
        "\n\n## This turn\n"
        "Answer directly, from what you know. You have no tools this turn — do "
        "not describe calling one, and do not say you cannot look something up "
        "unless it genuinely needs this machine, in which case say so in one "
        "line and stop."
    ),
    PLAN: (
        "\n\n## This turn — PLAN\n"
        "Work out what SHOULD be done and write it as short numbered steps. "
        "You may read files and look around; you may NOT change anything, and "
        "the tools that could are not available to you this turn. End with the "
        "one thing you would do first. Do not ask to proceed — the user will."
    ),
    AGENT: "",
}


# ── The one line ASK needs when the question is about THIS machine ──────────
#
# ⛔ A PARAGRAPH IN THE MODE PROMPT DID NOT HOLD, AND THIS IS WHY IT IS SEPARATE.
# Asked to show a real folder with no way to look, the model wrote
# example.zip / document.pdf / image.jpg — and, told at length not to invent,
# switched to "Your Downloads folder is empty". Both are machine facts it could
# not have. The rule was there; it was six sentences deep in text that rides on
# EVERY ask turn, including "what is the speed of light", which needs none of it.
#
# So it fires only when the line actually names this machine. That keeps it two
# sentences long and puts it directly against the question — the same reason
# the tool reminder rides on the last user turn rather than in the system block.
#
# ⚠ IT NAMES THE FAILURE INSTEAD OF FORBIDDING A CATEGORY. "Do not invent" left
# "empty" available, because saying a folder is empty does not feel like
# inventing a listing. Saying which two answers are both wrong is what closes it.
_ASK_NO_INVENT = (
    "\n\nYou cannot see this computer this turn. You do not know what is in "
    "that folder — not its files, and not whether it is empty. Do not name a "
    "file, a count or a path. Say you need to look, in one line."
)


def ask_addendum(text: str) -> str:
    """The extra line an ASK turn needs, or "" — see _ASK_NO_INVENT."""
    return _ASK_NO_INVENT if _THIS_MACHINE_RE.search(text or "") else ""
