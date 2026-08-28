"""
desktop.py — the tools that act on the DESKTOP rather than on files.

An assistant that can read and write files and run bash can technically do all
of this already. It does it badly: `bash("xdg-open …")` is one string the model
has to get exactly right, it goes through the confine sandbox that exists for
code and not for the session bus, and when it fails it fails as a shell error
rather than as "there is no such panel". Naming the things this desktop can
actually do turns a guess into a choice from a list.

Three tools, split by what they can COST rather than by what they are about:

  desktop_open      opening things. A URL, a folder, a file, an app, or one of
                    this desktop's own panels. Runs without asking.
  desktop_action    the compositor's own verbs, whatever they are — which
                    includes `quit`, `lock` and `power`. Always asks.
  desktop_setting   a line in synuirc, applied live. Always asks.

⚠ THE ACTION LIST IS NOT WRITTEN DOWN HERE. `synctl binds` reports what this
compositor actually dispatches, so the list cannot drift from the build that is
running — a hardcoded table would be a list of verbs that were true once.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
"""

import json
import os
import re
import shutil
import subprocess
from pathlib import Path

# The desktop's own panels, by the words somebody would say for them. The value
# is a synctl dispatch action; the point of the table is that "open the control
# panel" is a thing to look up rather than a thing to guess a command for.
#
# ⚠ NOTHING DESTRUCTIVE IS IN HERE. This table is reachable without a
# confirmation, so `quit`, `lock`, `close` and `power` are deliberately absent —
# they are reachable through desktop_action, which asks.
PANELS = {
    "control panel": "control",
    "settings": "control",
    "system settings": "control",
    "task manager": "taskmgr",
    "displays": "displays",
    "display settings": "displays",
    "monitors": "displays",
    "network": "network",
    "wifi": "network",
    "bluetooth": "bluetooth",
    "wallpaper": "wallpaper",
    "theme": "theme",
    "themes": "theme",
    "widgets": "widgets",
    "clipboard": "clipboard",
    "clipboard history": "clipboard",
    "emoji": "emoji",
    "emoji picker": "emoji",
    "calculator": "calc",
    "calendar": "clock",
    "start menu": "menu",
    "app menu": "menu",
    "keyboard shortcuts": "keys",
    "shortcuts": "keys",
    "news": "news",
    "sounds": "sounds",
    "night light": "night_light",
    "filters": "filters",
    "screen filters": "filters",
    "volume": "volume",
    "record": "record",
    "screen recorder": "record",
}

# The synuirc keys desktop_setting may write, and what each accepts. A closed
# list, because this writes the file that decides whether the session comes up
# at all: a typo'd key is ignored by config.c, but a typo'd VALUE for a real key
# is a desktop that starts wrong and no note of why.
SETTINGS = {
    "bar_edge":   ("top", "bottom"),
    "dock_edge":  ("bottom", "top", "left", "right"),
    "bar_shell":  ("quickshell", "antiquity", "none"),
    "dock":       ("on", "off"),
    "bar":        ("on", "off"),
    "wallpaper_mode": ("fill", "fit", "stretch", "center", "tile"),
    "theme":      None,          # free text — the theme names are a long list
    "wallpaper":  None,          # a path
    "terminal":   None,
    "animation_ms": None,        # a number
}

_SYNUIRC = Path(
    os.environ.get("XDG_CONFIG_HOME") or (Path.home() / ".config")
) / "synui" / "synuirc"


def _run(argv: list, timeout: int = 15) -> subprocess.CompletedProcess:
    return subprocess.run(argv, capture_output=True, text=True, timeout=timeout)


def _spawn(argv: list) -> None:
    """Detached, and with both pipes on /dev/null.

    ⚠ NOT INHERITED PIPES. When vibe is driven by the chat window its stdout is
    a pipe the window reads; a child that inherits it keeps that pipe open after
    vibe exits, and the window then waits for an engine that is gone. Worse, the
    child takes a SIGPIPE when the window does close — an app launched from here
    would die a second later for no visible reason."""
    subprocess.Popen(argv, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                     stdin=subprocess.DEVNULL, start_new_session=True)


def dispatch_actions() -> list:
    """Every action this compositor answers to, from the running one."""
    if not shutil.which("synctl"):
        return []
    try:
        out = _run(["synctl", "binds"]).stdout
        data = json.loads(out)
    except Exception:
        return []
    rows = data if isinstance(data, list) else data.get("binds", [])
    return sorted({r.get("action", "") for r in rows if r.get("action")})


# ── opening things ──────────────────────────────────────────────────────────

def _desktop_entry(name: str) -> list | None:
    """An app's Exec argv, found by .desktop id or by its Name.

    Field codes are stripped rather than substituted: %f/%u and friends are
    placeholders for arguments this is not passing, and left in they reach the
    program as literal arguments — which is how an editor opens a file called
    "%F"."""
    want = name.strip().lower()
    dirs = [Path(d) / "applications" for d in (
        os.environ.get("XDG_DATA_HOME") or f"{Path.home()}/.local/share",
        *(os.environ.get("XDG_DATA_DIRS") or "/usr/local/share:/usr/share").split(":"),
    ) if d]
    best = None
    for d in dirs:
        if not d.is_dir():
            continue
        for f in sorted(d.glob("*.desktop")):
            try:
                text = f.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            entry_name = ""
            execline = ""
            for line in text.splitlines():
                if line.startswith("Name=") and not entry_name:
                    entry_name = line[5:].strip()
                elif line.startswith("Exec=") and not execline:
                    execline = line[5:].strip()
                elif line.startswith("NoDisplay=true"):
                    execline = ""
                    break
            if not execline:
                continue
            if f.stem.lower() == want or entry_name.lower() == want:
                best = execline
                break
            if best is None and want in entry_name.lower():
                best = execline
        if best and f.stem.lower() == want:
            break
    if not best:
        return None
    argv = [a for a in re.split(r"\s+", re.sub(r"%[a-zA-Z]", "", best).strip()) if a]
    return argv or None


def desktop_open(target: str) -> str:
    """Open a URL, a path, one of this desktop's panels, or an app by name."""
    t = (target or "").strip()
    if not t:
        return "Error: nothing to open"

    # A panel of this desktop's own, by the words a person uses for it.
    key = t.lower().strip(" ?.")
    if key in PANELS:
        if not shutil.which("synctl"):
            return "Error: synctl is not installed — is this a synui session?"
        r = _run(["synctl", "dispatch", PANELS[key]])
        if r.returncode != 0:
            return f"Error: synctl refused `{PANELS[key]}`: {r.stderr.strip()}"
        return f"Opened the {key}."

    # A web address.
    if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*://", t) or re.match(
            r"^[\w.-]+\.[a-z]{2,}(/|$)", t):
        url = t if "://" in t else f"https://{t}"
        if not shutil.which("xdg-open"):
            return "Error: xdg-open is not installed"
        _spawn(["xdg-open", url])
        return f"Opened {url} in the default browser."

    # A path. ⚠ synfiles FIRST — this desktop ships its own file manager, and a
    # list that starts at thunar opens whatever else happens to be installed.
    p = Path(t).expanduser()
    if p.exists():
        if p.is_dir():
            for mgr in ("synfiles", "thunar", "nautilus", "dolphin", "nemo", "pcmanfm"):
                if shutil.which(mgr):
                    _spawn([mgr, str(p)])
                    return f"Opened {p} in {mgr}."
        if shutil.which("xdg-open"):
            _spawn(["xdg-open", str(p)])
            return f"Opened {p}."
        return f"Error: nothing to open {p} with"

    # An application, by .desktop id or by name.
    argv = _desktop_entry(t)
    if argv:
        _spawn(argv)
        return f"Launched {t} ({' '.join(argv)})."
    if shutil.which(t.split()[0]):
        _spawn(t.split())
        return f"Ran {t}."

    return (f"Error: nothing here is called '{t}' — not a panel, a path, "
            f"a URL, or an installed application")


# ── the compositor's own verbs ──────────────────────────────────────────────

def desktop_action(action: str, arg: str | None = None) -> str:
    """One synctl dispatch, checked against what this build actually answers."""
    a = (action or "").strip()
    if not a:
        return "Error: no action given"
    known = dispatch_actions()
    if known and a not in known:
        near = [k for k in known if a in k or k in a][:6]
        hint = f" Did you mean: {', '.join(near)}?" if near else ""
        return f"Error: this compositor has no `{a}` action.{hint}"
    if not shutil.which("synctl"):
        return "Error: synctl is not installed — is this a synui session?"
    argv = ["synctl", "dispatch", a] + ([arg] if arg else [])
    r = _run(argv)
    if r.returncode != 0:
        return f"Error: {' '.join(argv)} failed: {r.stderr.strip() or r.stdout.strip()}"
    return f"Dispatched {a}{' ' + arg if arg else ''}."


# ── a line of synuirc ───────────────────────────────────────────────────────

def desktop_setting(key: str, value: str) -> str:
    """Write one synuirc key and apply it to the running session.

    ⚠ `wallpaper_reload` IS THE WHOLE CONFIG RELOAD, whatever its name suggests
    — input.c calls synui_config_reload() for it. That is what makes this a
    setting that takes effect rather than a note for the next login.
    """
    k = (key or "").strip()
    v = (value or "").strip()
    if k not in SETTINGS:
        return (f"Error: `{k}` is not a setting this can change. "
                f"It knows: {', '.join(sorted(SETTINGS))}")
    allowed = SETTINGS[k]
    if allowed and v not in allowed:
        return f"Error: `{k}` takes one of: {', '.join(allowed)} (not '{v}')"
    if not v:
        return f"Error: `{k}` needs a value"

    try:
        _SYNUIRC.parent.mkdir(parents=True, exist_ok=True)
        old = _SYNUIRC.read_text(encoding="utf-8") if _SYNUIRC.exists() else ""
    except OSError as e:
        return f"Error: cannot read {_SYNUIRC}: {e}"

    line_re = re.compile(rf"^(\s*){re.escape(k)}(\s*)=.*$", re.M)
    if line_re.search(old):
        new = line_re.sub(lambda m: f"{m.group(1)}{k}{m.group(2) or ' '}= {v}", old, count=1)
    else:
        new = old + ("" if old.endswith("\n") or not old else "\n") + f"{k} = {v}\n"

    # Written through a temporary file in the same directory and renamed, so a
    # session that reloads mid-write never reads half a config.
    tmp = _SYNUIRC.with_suffix(".tmp")
    try:
        tmp.write_text(new, encoding="utf-8")
        os.replace(tmp, _SYNUIRC)
    except OSError as e:
        return f"Error: cannot write {_SYNUIRC}: {e}"

    if shutil.which("synctl"):
        r = _run(["synctl", "dispatch", "wallpaper_reload"])
        if r.returncode == 0:
            return f"{k} = {v}, and the session has reloaded."
    return f"{k} = {v} in {_SYNUIRC}. It applies at the next login."


# ── moving a file ───────────────────────────────────────────────────────────

def move_file(source: str, dest: str) -> str:
    """Move or rename a file, refusing to overwrite anything."""
    try:
        src = Path(source).expanduser()
        dst = Path(dest).expanduser()
    except Exception as e:
        return f"Error: {e}"
    if not src.exists():
        return f"Error: {src} does not exist"
    if dst.is_dir():
        dst = dst / src.name
    # ⛔ NEVER OVER A FILE THAT IS ALREADY THERE. A move is the one file
    # operation whose mistake destroys something that was not being edited, and
    # "it renamed over my notes" is not recoverable from a chat log.
    if dst.exists():
        return f"Error: {dst} already exists — refusing to overwrite it"
    try:
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(src), str(dst))
    except Exception as e:
        return f"Error: {e}"
    return f"Moved {src} → {dst}"
