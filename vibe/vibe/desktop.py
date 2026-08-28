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

# The user's own folders, by the words somebody actually says for them. The
# value is an **xdg-user-dir key, never a path**: where "Downloads" lives is
# localized and configurable, so a hardcoded ~/Downloads is a guess that is
# wrong on any machine not set up in English.
#
# ⚠ THIS TABLE EXISTS BECAUSE "open downloads" REACHED NONE OF THE BRANCHES
# BELOW. Lowercase `downloads` is not a panel, not a URL, not an existing
# relative path (the folder is `Downloads`), and not an installed application —
# so the tool returned an error for the single most ordinary thing anyone would
# ask it to open.
_DIR_WORDS = {
    "download": "DOWNLOAD",   "downloads": "DOWNLOAD",
    "document": "DOCUMENTS",  "documents": "DOCUMENTS", "docs": "DOCUMENTS",
    "picture": "PICTURES",    "pictures": "PICTURES",
    "photo": "PICTURES",      "photos": "PICTURES",     "images": "PICTURES",
    "music": "MUSIC",         "songs": "MUSIC",
    "video": "VIDEOS",        "videos": "VIDEOS",       "movies": "VIDEOS",
    "desktop": "DESKTOP",
    "public": "PUBLICSHARE",
    "templates": "TEMPLATES",
    "home": "HOME",           "home folder": "HOME",    "my files": "HOME",
    "file manager": "HOME",   "files": "HOME",
}

# ⛔ WHAT SEPARATES A HOSTNAME FROM A FILENAME, because a regex for "word dot
# word" cannot: `document.pdf` matched it, and desktop_open sent the user's own
# file to https://document.pdf in a browser. Reported 2026-08-28 as "I tried
# telling it to open a file it listed and it tried a website with the filename".
#
# ⚠ THE TWO SETS OVERLAP ON PURPOSE AND THE FILE EXTENSION WINS. `.zip`, `.mov`,
# `.sh` and `.py` are all real TLDs now. On a desktop assistant "open report.zip"
# means the archive essentially every time, so an overlap resolves to the file.
_TLDS = frozenset("""
com net org io dev app co uk edu gov mil info biz me tv us ca de fr jp au nl
ru ch it es se no fi pl br in cn xyz ai online site tech store blog cloud
page live news wiki gg fm to ly sh cc
""".split())

_FILE_EXTS = frozenset("""
pdf txt md rst log conf cfg ini json xml yaml yml toml csv tsv
png jpg jpeg gif svg webp bmp ico tiff heic
mp3 mp4 mkv avi mov wav flac ogg opus webm m4a m4v
zip tar gz bz2 xz 7z rar zst iso img
doc docx xls xlsx ppt pptx odt ods odp epub
sh py js ts jsx tsx html htm css c h cpp hpp rs go rb pl lua java kt
deb rpm pkg exe msi appimage desktop gguf bin so torrent
""".split())


def _looks_like_host(t: str) -> bool:
    """Whether a scheme-less string is a web address rather than a filename.

    ⚠ CALLED ONLY AFTER THE PATH CHECK, so anything that exists on disk has
    already won. This decides the leftovers, where the string names nothing
    real and the choice is between a browser and an error."""
    host = t.split("/", 1)[0]
    if "." not in host or host.startswith(".") or host.endswith("."):
        return False
    last = host.rsplit(".", 1)[1].lower()
    if not last.isalpha():
        return False
    # An explicit www. is somebody saying "web", whatever follows it.
    if host.lower().startswith("www."):
        return True
    # A path component after the host is a shape filenames do not have.
    if "/" in t:
        return last in _TLDS
    if last in _FILE_EXTS:
        return False
    return last in _TLDS


# ⚠ THE LAST DIRECTORY THE ASSISTANT SHOWED THE USER. "open the third one" and
# "open notes.txt" arrive as a BARE NAME with no directory, because that is how
# the listing above them read. Without this the name resolves against vibe's
# cwd — whatever launched the engine — and misses.
#
# It is only ever consulted AFTER every other branch has failed, so it can turn
# an error into the right file and can never override a real path, a panel or
# an app.
_last_dir: Path | None = None


def note_dir(p) -> None:
    """Remember a directory the user has just been shown."""
    global _last_dir
    try:
        d = Path(p)
        if d.is_dir():
            _last_dir = d
    except (OSError, TypeError, ValueError):
        pass


# What each key is called when xdg-user-dir cannot say — the freedesktop
# defaults, matched case-insensitively against $HOME's real children.
_DIR_DEFAULTS = {
    "DOWNLOAD": "Downloads", "DOCUMENTS": "Documents", "PICTURES": "Pictures",
    "MUSIC": "Music", "VIDEOS": "Videos", "DESKTOP": "Desktop",
    "PUBLICSHARE": "Public", "TEMPLATES": "Templates",
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


def _user_dir(key: str) -> Path | None:
    """Where one XDG user directory actually is, or None if it is not there.

    ⛔ `xdg-user-dir DOWNLOAD` PRINTS $HOME WHEN THERE IS NO user-dirs.dirs,
    and it exits 0 doing it. A fresh SynapseOS install has no user-dirs.dirs at
    all, so taking that answer at face value opens the home folder and reports
    it as Downloads — the same failure as before, wearing a success message.
    An answer equal to $HOME is therefore treated as NO answer, and the
    fallback is a case-insensitive look at $HOME's own children, which is where
    `~/Downloads` is on exactly the boxes xdg-user-dir gives up on.
    """
    home = Path.home()
    if key == "HOME":
        return home
    if shutil.which("xdg-user-dir"):
        try:
            out = _run(["xdg-user-dir", key], timeout=5).stdout.strip()
        except Exception:
            out = ""
        if out:
            cand = Path(out).expanduser()
            if cand != home and cand.is_dir():
                return cand
    want = _DIR_DEFAULTS.get(key, key.title()).lower()
    try:
        for child in home.iterdir():
            if child.name.lower() == want and child.is_dir():
                return child
    except OSError:
        pass
    return None


def _folder_key(text: str) -> str | None:
    """The xdg-user-dir key somebody meant, from the words they used.

    People say "my downloads", "the downloads folder" and "downloads" for one
    place. Stripping the filler is what makes those one target rather than
    three misses."""
    k = text.lower().strip(" ?.")
    k = re.sub(r"^(?:open\s+|my\s+|the\s+)+", "", k)
    k = re.sub(r"\s+(?:folder|directory|dir)$", "", k).strip()
    return _DIR_WORDS.get(k)


def _open_dir(p: Path) -> str:
    """Hand a directory to this desktop's file manager.

    ⚠ synfiles FIRST — this desktop ships its own file manager, and a list that
    starts at thunar opens whatever else happens to be installed."""
    note_dir(p)
    for mgr in ("synfiles", "thunar", "nautilus", "dolphin", "nemo", "pcmanfm"):
        if shutil.which(mgr):
            _spawn([mgr, str(p)])
            return f"Opened {p} in {mgr}."
    if shutil.which("xdg-open"):
        _spawn(["xdg-open", str(p)])
        return f"Opened {p}."
    return f"Error: no file manager is installed to open {p} with"


def desktop_open(target: str) -> str:
    """Open a URL, a path, one of this desktop's panels, or an app by name."""
    t = (target or "").strip()
    if not t:
        return "Error: nothing to open"

    # An explicit scheme is somebody saying "web", and is the one form that
    # cannot also be a filename, a panel or an app. It goes first for that
    # reason and needs none of the guessing below.
    if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*://", t):
        if not shutil.which("xdg-open"):
            return "Error: xdg-open is not installed"
        _spawn(["xdg-open", t])
        return f"Opened {t} in the default browser."

    # A panel of this desktop's own, by the words a person uses for it.
    key = t.lower().strip(" ?.")
    if key in PANELS:
        if not shutil.which("synctl"):
            return "Error: synctl is not installed — is this a synui session?"
        r = _run(["synctl", "dispatch", PANELS[key]])
        if r.returncode != 0:
            return f"Error: synctl refused `{PANELS[key]}`: {r.stderr.strip()}"
        return f"Opened the {key}."

    # One of the user's own folders, by the words people use for it. Before
    # the app lookup on purpose: "open desktop" means the Desktop folder, and
    # left to the .desktop scan below it substring-matched an entry whose Name
    # merely contained the word and launched that instead.
    fkey = _folder_key(t)
    if fkey:
        d = _user_dir(fkey)
        if d is not None:
            return _open_dir(d)
        # Say the folder is missing rather than falling through to a wrong
        # app — but only for words that mean nothing else. "music" is also a
        # player, so an absent ~/Music should still reach the app lookup.
        if fkey in ("DOWNLOAD", "DOCUMENTS", "HOME"):
            return f"Error: this account has no {fkey.lower()} folder"

    # A path.
    #
    # ⛔ BEFORE THE WEB ADDRESS CHECK, WHICH IS THE WAY ROUND THIS WAS NOT.
    # A thing that exists on this disk is that thing. The old order asked
    # "does it look like a domain" first, and `document.pdf` looks exactly
    # like one, so a file the assistant had just listed was opened as
    # https://document.pdf. Nothing that exists can be a guess.
    #
    # ⚠ MADE ABSOLUTE BEFORE IT IS HANDED OVER. A relative name is resolved
    # against vibe's cwd, which is whatever launched the engine — the bar, a
    # shell, `/`. Passing it on unresolved means the check and the child
    # disagree about which directory that is, and the file manager opens
    # nothing while this reports success.
    p = Path(t).expanduser()
    if p.exists():
        p = Path(os.path.abspath(p))
        if p.is_dir():
            return _open_dir(p)
        if shutil.which("xdg-open"):
            _spawn(["xdg-open", str(p)])
            return f"Opened {p}."
        return f"Error: nothing to open {p} with"

    # A bare name from the listing the user is looking at.
    #
    # ⚠ A LISTING IS A CONTEXT, AND THE NEXT LINE INHERITS IT. Somebody shown
    # the contents of ~/Downloads says "open notes.txt", not the full path, and
    # the model repeats the name it was given. Consulted only here, after every
    # branch that could match something real, so it can rescue an error and can
    # never shadow one.
    if _last_dir is not None and "/" not in t:
        cand = _last_dir / t
        if cand.exists():
            cand = Path(os.path.abspath(cand))
            if cand.is_dir():
                return _open_dir(cand)
            if shutil.which("xdg-open"):
                _spawn(["xdg-open", str(cand)])
                return f"Opened {cand}."

    # A web address — one with a scheme, or a string that is shaped like a host
    # and is not shaped like a filename. See _looks_like_host.
    if _looks_like_host(t):
        url = f"https://{t}"
        if not shutil.which("xdg-open"):
            return "Error: xdg-open is not installed"
        _spawn(["xdg-open", url])
        return f"Opened {url} in the default browser."

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
