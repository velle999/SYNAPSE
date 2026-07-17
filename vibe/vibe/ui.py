import json
import sys
import time
from pathlib import Path
from rich.console import Console
from rich.panel import Panel
from rich.syntax import Syntax
from rich import box
from prompt_toolkit import PromptSession
from prompt_toolkit.history import FileHistory
from prompt_toolkit.key_binding import KeyBindings
from prompt_toolkit.styles import Style

HISTORY_FILE = Path.home() / ".vibe_history"

console = Console()

_prompt_style = Style.from_dict({
    "prompt": "bold #7c6af7",
})

_bindings = KeyBindings()

# Enter submits; Escape+Enter (Alt+Enter) inserts a newline
@_bindings.add("enter")
def _submit(event):
    event.current_buffer.validate_and_handle()

@_bindings.add("escape", "enter")
def _newline(event):
    event.current_buffer.insert_text("\n")

_session = PromptSession(
    history=FileHistory(str(HISTORY_FILE)),
    style=_prompt_style,
    mouse_support=False,
    key_bindings=_bindings,
    multiline=True,
)


def get_input(cwd: str) -> str | None:
    """Get user input. Returns None on EOF/exit."""
    try:
        text = _session.prompt([("class:prompt", "vibe ❯ ")])
        return text.strip()
    except (EOFError, KeyboardInterrupt):
        return None


_BUG = """\
[#7c6af7]  ╱|  |╲[/]
[#7c6af7] (  ˘ᴗ˘ )[/]  [bold #7c6af7]Vibe Code[/]
[#7c6af7]  ╲|__|╱[/]  [dim]{model_label}[/]
[#7c6af7]  ╱╱  ╲╲[/]\
"""


def print_welcome(model_label: str):
    console.print(Panel(
        _BUG.format(model_label=model_label) + "\n\n"
        "[dim]Commands: /reset  /save  /memory  /think  /nothink  /tokens  /sys  /gpu  /ps  /exit  /help[/]\n"
        "[dim]Tip: /save before /reset to keep session memory · Alt+Enter for newline[/]",
        border_style="#7c6af7",
        box=box.ROUNDED,
        padding=(0, 2),
    ))


def print_help():
    console.print(Panel(
        "[bold]Conversation[/]\n"
        "  [cyan]/reset[/]                 — clear conversation history\n"
        "  [cyan]/think[/]                 — enable Qwen3 chain-of-thought\n"
        "  [cyan]/nothink[/]               — disable chain-of-thought (faster)\n"
        "  [cyan]/save[/]                  — summarize session to .vibe/memory.md\n"
        "  [cyan]/memory[/]                — show current memory file\n"
        "  [cyan]/tokens[/]                — show context token usage\n"
        "  [cyan]/model[/]                 — show current model info\n\n"
        "[bold]System Info[/]\n"
        "  [cyan]/sys[/]                   — CPU, RAM, disk, uptime\n"
        "  [cyan]/gpu[/]                   — GPU utilization, VRAM, temperature\n"
        "  [cyan]/net[/]                   — network interfaces and listening ports\n"
        "  [cyan]/ps [filter][/]           — top processes by CPU (optional name filter)\n"
        "  [cyan]/files [path][/]          — open file manager (default: cwd)\n\n"
        "[bold]Process Control[/]\n"
        "  [cyan]/kill <pid|name>[/]       — send SIGTERM to PID or matching processes\n\n"
        "[bold]Service Control[/]\n"
        "  [cyan]/service <name> [action][/] — systemctl action (default: status)\n"
        "                           actions: start stop restart reload enable disable\n"
        "  [cyan]/services [filter][/]     — list running services\n\n"
        "[bold]Runtime Config[/]\n"
        "  [cyan]/offload <n>[/]           — GPU layers (-1=all, 0=CPU only, N=partial)\n"
        "  [cyan]/set temp <0.0-2.0>[/]   — generation temperature\n"
        "  [cyan]/set tokens <n>[/]        — max output tokens\n"
        "  [cyan]/set top_p <0.0-1.0>[/]  — nucleus sampling\n"
        "  [cyan]/set top_k <n>[/]         — top-k sampling\n"
        "  [cyan]/set repeat_penalty <n>[/] — repetition penalty\n\n"
        "[bold]Other[/]\n"
        "  [cyan]/exit[/]                  — quit\n"
        "  [cyan]/help[/]                  — show this\n\n"
        "[bold]Tips[/]\n"
        "  • Give a directory with:  [dim]vibe /path/to/project[/]\n"
        "  • Up/Down arrow for history\n"
        "  • Alt+Enter (Esc then Enter) for multiline input\n"
        "  • Ctrl+C to cancel a response",
        title="Help",
        border_style="dim",
        box=box.ROUNDED,
    ))


def print_tool_call(name: str, args: dict):
    if name == "bash":
        arg_str = args.get("command", "")
        label = f"[bold yellow]❯[/] [yellow]{name}[/]  [dim]{arg_str[:80]}[/]"
    elif name in ("read_file", "write_file", "edit_file"):
        path = args.get("path", "")
        label = f"[bold cyan]❯[/] [cyan]{name}[/]  [dim]{path}[/]"
    elif name in ("glob", "grep"):
        label = f"[bold green]❯[/] [green]{name}[/]  [dim]{args.get('pattern', '')}[/]"
    else:
        label = f"[bold blue]❯[/] [blue]{name}[/]  [dim]{json.dumps(args)[:80]}[/]"
    console.print(label)


def print_tool_result(result: str, name: str):
    lines = result.strip().splitlines()
    preview_lines = 6
    if len(lines) <= preview_lines:
        preview = result.strip()
    else:
        preview = "\n".join(lines[:preview_lines]) + f"\n… +{len(lines) - preview_lines} lines"

    if name == "read_file":
        # Try to syntax-highlight based on file extension in the result header
        ext = ""
        if lines and lines[0].startswith("<file path="):
            import re
            m = re.search(r'path=([^\s>]+)', lines[0])
            if m:
                ext = Path(m.group(1)).suffix.lstrip(".")
        if ext:
            try:
                snippet = "\n".join(lines[1:preview_lines + 1])
                console.print(Syntax(snippet, ext, theme="monokai", line_numbers=False))
                if len(lines) > preview_lines + 1:
                    console.print(f"  [dim]… +{len(lines) - preview_lines - 1} lines[/]")
                return
            except Exception:
                pass

    console.print(f"  [dim]{preview}[/]")


def stream_response(token_iter) -> str:
    """
    Consume token iterator, handle tool markers, render output.
    Returns the full assistant text.

    Tool markers from llm.py use NUL-separated fields but the result
    may itself contain NUL bytes, so we join everything after field 2.
    """
    full_text = ""
    current_tool_name = ""
    pending = ""
    session_start = time.monotonic()  # tracks total wall time
    t_start = time.monotonic()        # tracks per-segment timing
    think_elapsed: float | None = None
    first_token = True
    tool_count = 0

    console.print()  # blank line before response

    # Typing indicator — show something while waiting for first token
    with console.status("[dim]thinking…[/]", spinner="dots") as status:
        try:
            for token in token_iter:
                if first_token:
                    think_elapsed = time.monotonic() - t_start
                    first_token = False
                # Stop the spinner as soon as content arrives
                status.stop()

                # ── Retry signal: model stalled, retrying ─────────────────────
                if token == "\x00RETRY\x00":
                    if pending:
                        pending = ""  # discard stalled text
                    console.print("\n[dim]retrying…[/]")
                    first_token = True
                    t_start = time.monotonic()
                    status.start()
                    continue

                # ── Tool signal: TOOL_START ───────────────────────────────────
                if token.startswith("\x00TOOL_START\x00"):
                    if pending:
                        console.print(pending, end="", markup=False, highlight=False)
                        full_text += pending
                        pending = ""
                    console.print()

                    parts = token.split("\x00")
                    # fields: ['', 'TOOL_START', name, args_json, '']
                    name = parts[2] if len(parts) > 2 else "?"
                    args_str = "\x00".join(parts[3:]).rstrip("\x00") if len(parts) > 3 else "{}"
                    try:
                        args = json.loads(args_str)
                    except Exception:
                        args = {}
                    print_tool_call(name, args)
                    current_tool_name = name
                    tool_count += 1
                    continue

                # ── Tool signal: TOOL_END ─────────────────────────────────────
                if token.startswith("\x00TOOL_END\x00"):
                    parts = token.split("\x00", 2)  # max 2 splits → ['', 'TOOL_END', result]
                    result = parts[2] if len(parts) > 2 else ""
                    print_tool_result(result, current_tool_name)
                    current_tool_name = ""
                    console.print()

                    # Restart spinner for the next model turn
                    first_token = True
                    t_start = time.monotonic()
                    status.start()
                    continue

                # ── Error/warning from llm.py ────────────────────────────────
                if token.startswith("\n[") and (
                    "Error:" in token or "Warning:" in token or "max tool" in token
                ):
                    if pending:
                        console.print(pending, end="", markup=False, highlight=False)
                        full_text += pending
                        pending = ""
                    console.print(f"[yellow]{token.strip()}[/]")
                    continue

                # ── Regular text token ────────────────────────────────────────
                pending += token
                full_text += token

                if "\n" in pending or len(pending) > 40:
                    console.print(pending, end="", markup=False, highlight=False)
                    pending = ""

        except KeyboardInterrupt:
            console.print("\n[dim]interrupted[/]")

    # Flush remainder
    if pending:
        console.print(pending, end="", markup=False, highlight=False)

    console.print()
    total = time.monotonic() - session_start
    parts = []
    if think_elapsed is not None and think_elapsed > 0.5:
        parts.append(f"thought {think_elapsed:.1f}s")
    if tool_count:
        parts.append(f"{tool_count} tool call{'s' if tool_count != 1 else ''}")
    parts.append(f"total {total:.1f}s")
    console.print(f"[dim]⏱ {' · '.join(parts)}[/]")
    return full_text


def print_error(msg: str):
    console.print(f"[bold red]Error:[/] {msg}", markup=True)


def print_info(msg: str):
    console.print(f"[dim]{msg}[/]", markup=True)
