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

MODES = (AUTO, ASK, AGENT, PLAN)

# What each mode is allowed to reach for. PLAN's list is the whole of what
# makes it safe: it can look at anything and change nothing, so a plan can be
# read before any of it happens.
READ_ONLY_TOOLS = frozenset({"read_file", "glob", "grep", "list_dir", "desktop_open"})

DESCRIPTION_SHELL = "runs it, once you say so"

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
    r"bar|dock|wallpaper|theme|panel|window|workspace|monitor|screen|"
    r"setting|settings|config|configure|"
    r"service|daemon|systemd|log|logs|journal|process|"
    r"git|commit|branch|build|compile|test|tests|"
    r"write|create|make|edit|change|move|rename|delete|fix|refactor)\b",
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
    if _ASK_RE.search(t) and not re.search(r"(^|\s)(/|~/|\./)", t):
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

    if _MACHINE_RE.search(t):
        return AGENT
    return ASK


def resolve(mode: str, text: str, shell_class: str = "") -> str:
    """The mode to actually run this turn."""
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
