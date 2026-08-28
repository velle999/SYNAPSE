"""
Vibe Code — local AI coding assistant
REPL entry point with slash commands
"""

import os
import sys
from pathlib import Path

from rich.console import Console
from rich.panel import Panel
from rich import box

import vibe.config as cfg
from vibe.llm import VibeModel
from vibe.ui import (
    get_input,
    print_welcome,
    print_help,
    stream_response,
    print_error,
    print_info,
    console,
)
from vibe.system import (
    sys_info,
    gpu_info,
    net_info,
    ps_list,
    kill_process,
    service_control,
    services_list,
    open_file_manager,
)


def _model_label() -> str:
    if cfg.BACKEND == "ollama":
        return f"ollama · {cfg.OLLAMA_MODEL}"
    if cfg.BACKEND == "synapd":
        target = cfg.SYNAPD_HOST or cfg.SYNAPD_SOCKET
        return f"synapd · {target}"
    return f"llama-cpp · {cfg.MODEL_PATH.name}"


def _save_memory(model: VibeModel):
    """Ask the model to summarise the session into .vibe/memory.md."""
    vibe_dir = Path.cwd() / ".vibe"
    vibe_dir.mkdir(exist_ok=True)
    mem_path = vibe_dir / "memory.md"

    existing = ""
    if mem_path.exists():
        try:
            existing = mem_path.read_text(encoding="utf-8").strip()
        except Exception:
            pass

    summary_prompt = (
        "Summarise this session concisely for future context. "
        "Include: key decisions, file layout, current status, any bugs/blockers. "
        "Output ONLY the markdown summary — no preamble, no fences.\n"
    )
    if existing:
        summary_prompt += f"\nPrevious memory:\n{existing}\n\nMerge with new info."

    # Use tool-free summarization to prevent accidental file modifications
    print_info("Summarizing session...")
    tokens = []
    try:
        for tok in model.summarize(summary_prompt):
            tokens.append(tok)
    except Exception as e:
        print_error(f"Summarization failed: {e}")
        return

    summary = "".join(tokens).strip()
    # Strip thinking blocks if present
    import re
    summary = re.sub(r"<think>[\s\S]*?</think>", "", summary).strip()
    if summary:
        mem_path.write_text(summary + "\n", encoding="utf-8")
        print_info(f"Session saved to {mem_path}")
    else:
        print_error("Empty summary — nothing saved.")


def _show_memory():
    mem_path = Path.cwd() / ".vibe" / "memory.md"
    if mem_path.exists():
        content = mem_path.read_text(encoding="utf-8").strip()
        if content:
            console.print(Panel(content, title=".vibe/memory.md", border_style="dim", box=box.ROUNDED))
        else:
            print_info("Memory file is empty.")
    else:
        print_info("No memory file yet. Use /save to create one.")


def _show_tokens(model: VibeModel):
    used = model.token_count()
    if cfg.BACKEND == "ollama":
        limit = cfg.OLLAMA_CTX
    else:
        limit = cfg.N_CTX
    pct = min(used / limit, 1.0) if limit else 0
    bar_len = 30
    filled = int(bar_len * pct)
    bar = "█" * filled + "░" * (bar_len - filled)
    console.print(f"  [bold]Context:[/] [{bar}] {used:,} / {limit:,} tokens ({pct:.0%})")
    if pct > 0.8:
        console.print("  [yellow]Warning: context is getting full. Consider /reset or /save then /reset.[/]")


def _handle_offload(args: str, model: VibeModel):
    """Set GPU layer count and reload the model."""
    args = args.strip()
    if not args:
        # Show current state
        if cfg.BACKEND == "ollama":
            n = cfg.OLLAMA_NUM_GPU
        else:
            n = cfg.N_GPU_LAYERS
        label = "all" if n == -1 else ("CPU only" if n == 0 else f"{n} layers")
        print_info(f"GPU offload: {label}")
        print_info("Usage: /offload <n>  (-1 = all GPU, 0 = CPU only, N = N layers on GPU)")
        return

    try:
        n = int(args)
    except ValueError:
        print_error("Expected a number: -1 (all GPU), 0 (CPU only), or N (layers on GPU)")
        return

    if cfg.BACKEND == "ollama":
        cfg.OLLAMA_NUM_GPU = n
        label = "all" if n == -1 else ("CPU only" if n == 0 else f"{n} layers")
        print_info(f"Ollama GPU layers set to: {label}")
        print_info("Takes effect on next request (Ollama reloads the model automatically).")
    else:
        cfg.N_GPU_LAYERS = n
        label = "all" if n == -1 else ("CPU only" if n == 0 else f"{n} layers")
        print_info(f"Reloading model with GPU layers: {label} ...")
        try:
            model.reload()
            print_info("Model reloaded.")
        except Exception as e:
            print_error(f"Reload failed: {e}")



    parts = args.strip().split(None, 1)
    if len(parts) < 2:
        print_error("Usage: /set <param> <value>  (temp, tokens, top_p, top_k, repeat_penalty)")
        return
    param, val = parts[0].lower(), parts[1]
    try:
        if param == "temp":
            cfg.TEMPERATURE = max(0.0, min(2.0, float(val)))
            print_info(f"temperature = {cfg.TEMPERATURE}")
        elif param == "tokens":
            cfg.MAX_TOKENS = max(1, int(val))
            print_info(f"max_tokens = {cfg.MAX_TOKENS}")
        elif param == "top_p":
            cfg.TOP_P = max(0.0, min(1.0, float(val)))
            print_info(f"top_p = {cfg.TOP_P}")
        elif param == "top_k":
            cfg.TOP_K = max(1, int(val))
            print_info(f"top_k = {cfg.TOP_K}")
        elif param == "repeat_penalty":
            cfg.REPEAT_PENALTY = max(0.0, float(val))
            print_info(f"repeat_penalty = {cfg.REPEAT_PENALTY}")
        else:
            print_error(f"Unknown param '{param}'. Options: temp, tokens, top_p, top_k, repeat_penalty")
    except ValueError:
        print_error(f"Invalid value: {val}")


# ── Tool confirmation gate ──────────────────────────────────────────────────
# Destructive tools (bash / write_file / edit_file) pause for confirmation
# before running. Read-only tools never do. Disabled with --yolo or /trust, and
# 'a' at the prompt disables it for the rest of the session.
_gate_enabled = True


def _confirm_tool(name: str, args: dict) -> bool:
    """Interactive gate for a destructive tool. Returns True to run it."""
    global _gate_enabled
    if not _gate_enabled:
        return True
    if name == "bash":
        detail = args.get("command", "")
    else:  # write_file / edit_file
        detail = args.get("path", "")
    console.print(f"  [yellow]⚠ about to run[/] [bold]{name}[/] [dim]{detail}[/]")
    try:
        ans = console.input(
            "  [Y]es / [n]o / [a]lways for this session: "
        ).strip().lower()
    except (EOFError, KeyboardInterrupt):
        console.print("  [dim]skipped[/]")
        return False
    if ans in ("a", "always"):
        _gate_enabled = False
        console.print("  [dim]tool confirmations off for this session[/]")
        return True
    return ans in ("", "y", "yes")


# ── The verbs that are not the REPL ─────────────────────────────────────────
#
# ⚠ CHECKED BEFORE ANYTHING IS BUILT. `vibe serve` must not print a welcome
# banner down the pipe its window is parsing, and `vibe key` must work on a
# desktop whose backend cannot start — which is exactly the desktop somebody is
# on when they are writing their first key.
def _verb_serve(argv) -> int:
    from vibe.serve import main as serve_main
    return serve_main()


def _verb_gui(argv) -> int:
    """The chat window: quickshell, driving `vibe serve`."""
    import shutil
    if not (os.environ.get("WAYLAND_DISPLAY") or os.environ.get("DISPLAY")):
        print_error("no display — `vibe gui` needs a graphical session "
                    "(plain `vibe` is the terminal assistant)")
        return 1
    if not shutil.which("quickshell"):
        print_error("quickshell is not installed — synpkg install quickshell")
        return 1

    # The window's Wayland app_id. Without it quickshell names every window
    # org.quickshell, which is the generic icon in the dock AND the reason the
    # dock cannot resolve a .desktop for it — see syn-edit's cmd_gui, where the
    # same omission merged two apps into one dock entry.
    os.environ["QS_APP_ID"] = "vibe"

    here = Path(__file__).resolve().parent
    for cand in (Path("/usr/share/vibe/vibe.qml"), here / "data" / "vibe.qml"):
        if cand.is_file():
            os.execvp("quickshell", ["quickshell", "-p", str(cand)])
    print_error("vibe.qml is missing — the package did not install its window")
    return 1


def _verb_provider(argv) -> int:
    """Persist the backend choice for the next start.

    Written to the launcher's own env file rather than into config.py: the
    package's config is read-only at runtime, and a setting that needs root to
    change is not a setting."""
    known = ("synapd", "ollama", "llama_cpp", "anthropic", "openai")
    if not argv:
        print_info(f"backend: {cfg.BACKEND}   (choices: {', '.join(known)})")
        return 0
    name = argv[0]
    if name not in known:
        print_error(f"no such backend: {name} (try {', '.join(known)})")
        return 1
    envf = cfg.KEY_DIR.parent / "vibe.env"
    try:
        envf.parent.mkdir(parents=True, exist_ok=True)
        envf.write_text(f"VIBE_BACKEND={name}\n", encoding="utf-8")
    except OSError as e:
        print_error(f"cannot write {envf}: {e}")
        return 1
    print_info(f"backend is {name} — it applies to the next `vibe`")
    return 0


def _verb_key(argv) -> int:
    """Store one provider's API key, 0600, in its own file."""
    if not argv:
        for prov in ("anthropic", "openai"):
            have = "set" if cfg.api_key(prov) else "not set"
            print_info(f"{prov}: {have}   ({cfg.key_path(prov)})")
        return 0
    prov = argv[0]
    if prov not in ("anthropic", "openai"):
        print_error("which provider? anthropic or openai")
        return 1
    key = argv[1] if len(argv) > 1 else ""
    if not key:
        import getpass
        # ⚠ Prompted rather than taken from the command line where possible: an
        # argument is in the shell history and in `ps` for as long as this runs.
        key = getpass.getpass(f"{prov} API key (not echoed): ").strip()
    if not key:
        print_error("no key given")
        return 1
    path = cfg.key_path(prov)
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        os.umask(0o077)
        path.write_text(key + "\n", encoding="utf-8")
        os.chmod(path, 0o600)
    except OSError as e:
        print_error(f"cannot write {path}: {e}")
        return 1
    print_info(f"stored in {path} (0600)")
    return 0


def _verb_voice(argv) -> int:
    """Speech in and out, on the command line.

    The same stack the chat window uses, so `syn-speak`, the screen reader and
    the assistant all say things in one voice rather than three."""
    from vibe.voice import shared
    v = shared()
    sub = argv[0] if argv else "status"
    if sub == "say":
        text = " ".join(argv[1:])
        if not text:
            print_error("say what?")
            return 1
        engine = v.speak(text)
        if not engine:
            print_error("nothing on this box can speak — synpkg install espeak-ng")
            return 3
        return 0
    if sub == "listen":
        text, err = v.listen()
        if err:
            print_error(err)
            return 3
        print(text)
        return 0
    if sub == "stop":
        v.stop()
        return 0
    if sub == "status":
        for k, val in v.status().items():
            print(f"{k}\t{val}")
        return 0
    print_error(f"unknown: vibe voice {sub}  (say | listen | stop | status)")
    return 2


_VERBS = {
    "serve": _verb_serve,
    "voice": _verb_voice,
    "gui": _verb_gui,
    "provider": _verb_provider,
    "key": _verb_key,
}


def main():
    global _gate_enabled

    argv = sys.argv[1:]
    if argv and argv[0] in _VERBS:
        sys.exit(_VERBS[argv[0]](argv[1:]))

    verbose = "--verbose" in sys.argv
    if "--yolo" in sys.argv:
        _gate_enabled = False

    # Optional: set working directory from first positional arg
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    if args:
        target = Path(args[0]).expanduser().resolve()
        if target.is_dir():
            os.chdir(target)
        else:
            print_error(f"Not a directory: {target}")
            sys.exit(1)

    try:
        model = VibeModel(verbose=verbose)
    except (FileNotFoundError, RuntimeError) as e:
        print_error(str(e))
        sys.exit(1)

    model.confirm_tool = _confirm_tool

    print_welcome(_model_label())

    while True:
        try:
            text = get_input(os.getcwd())
        except KeyboardInterrupt:
            console.print("\n[bold red]Interrupted.[/]")
            break

        if text is None:
            break
        if not text:
            continue

        # ── Slash commands ───────────────────────────────────────────
        if text.startswith("/"):
            cmd = text.split()[0].lower()
            rest = text[len(cmd):].strip()

            if cmd == "/exit":
                console.print("[dim]Goodbye![/]")
                break
            elif cmd == "/help":
                print_help()
            elif cmd == "/trust":
                _gate_enabled = not _gate_enabled
                state = "on" if _gate_enabled else "off"
                print_info(f"tool confirmations {state}")
            elif cmd == "/reset":
                model.reset()
                print_info("Conversation cleared.")
            elif cmd == "/think":
                cfg.THINKING = True
                print_info("Thinking mode ON — chain-of-thought enabled.")
            elif cmd == "/nothink":
                cfg.THINKING = False
                print_info("Thinking mode OFF — faster responses.")
            elif cmd == "/model":
                console.print(f"  Backend: {cfg.BACKEND}")
                if cfg.BACKEND == "ollama":
                    console.print(f"  Model:   {cfg.OLLAMA_MODEL}")
                    console.print(f"  Host:    {cfg.OLLAMA_HOST}")
                    console.print(f"  Context: {cfg.OLLAMA_CTX:,}")
                    n = cfg.OLLAMA_NUM_GPU
                else:
                    console.print(f"  Model:   {cfg.MODEL_PATH.name}")
                    console.print(f"  Context: {cfg.N_CTX:,}")
                    n = cfg.N_GPU_LAYERS
                gpu_label = "all" if n == -1 else ("CPU only" if n == 0 else f"{n} layers")
                console.print(f"  GPU:     {gpu_label}")
                console.print(f"  Temp:    {cfg.TEMPERATURE}")
                console.print(f"  Tokens:  {cfg.MAX_TOKENS:,}")
                console.print(f"  Think:   {'on' if cfg.THINKING else 'off'}")
            elif cmd == "/tokens":
                _show_tokens(model)
            elif cmd == "/save":
                _save_memory(model)
            elif cmd == "/memory":
                _show_memory()
            elif cmd == "/sys":
                console.print(sys_info())
            elif cmd == "/gpu":
                console.print(gpu_info())
            elif cmd == "/net":
                console.print(net_info())
            elif cmd == "/ps":
                console.print(ps_list(rest or None))
            elif cmd == "/kill":
                if not rest:
                    print_error("Usage: /kill <pid|name>")
                else:
                    console.print(kill_process(rest))
            elif cmd == "/files":
                console.print(open_file_manager(rest or "."))
            elif cmd == "/service":
                parts = rest.split(None, 1)
                if not parts:
                    print_error("Usage: /service <name> [action]")
                else:
                    name = parts[0]
                    action = parts[1] if len(parts) > 1 else "status"
                    console.print(service_control(name, action))
            elif cmd == "/services":
                console.print(services_list(rest or None))
            elif cmd == "/offload":
                _handle_offload(rest, model)
            elif cmd == "/set":
                _handle_set(rest)
            else:
                print_error(f"Unknown command: {cmd}. Type /help for commands.")
            continue

        # ── Chat ─────────────────────────────────────────────────────
        response = stream_response(model.chat(text))

        # Context warning
        used = model.token_count()
        limit = cfg.OLLAMA_CTX if cfg.BACKEND == "ollama" else cfg.N_CTX
        if limit and used / limit > 0.85:
            console.print(
                f"\n[yellow]⚠ Context {used:,}/{limit:,} ({used/limit:.0%}) — "
                f"consider /save then /reset[/]"
            )


if __name__ == "__main__":
    main()
