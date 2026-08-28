import json
import os
import re
import urllib.error
import urllib.request
import time
from pathlib import Path
from typing import Iterator

import vibe.config as cfg
from vibe import cloud
from . import synapd_client
from .tools import TOOL_SCHEMAS, execute_tool


SYSTEM_PROMPT = """\
You are Vibe, a local AI coding assistant. You help with software engineering tasks: \
writing code, debugging, refactoring, explaining code, and running commands.

You have tools to read/write/edit files, run shell commands, search code, and list directories. \
Use them freely and proactively.

## Core rules
- Always read a file before editing it.
- Use write_file or edit_file to create/modify files — NEVER use bash echo/cat to write file content.
- Prefer edit_file over write_file for targeted changes. If edit_file returns "old_string not found", STOP and use write_file to rewrite the whole file — never retry edit_file with the same or similar string.
- Run tests after making changes when tests exist.
- Be concise — lead with action, not explanation.
- NEVER narrate what you are about to do before doing it. Do not say "I'll write the file now" or "Let me create..." — just call the tool immediately. Text before the first tool call is wasted tokens.
- If a task requires writing code, call write_file first. Explain afterward if needed.
- If write_file tool call is not available or not working, output the file content as a fenced code block with a filename comment on the first line: ```python\n# file: name.py\n# ...your complete code here...\n``` — the system will save it automatically. Write the real code in place of that comment; never emit a placeholder or a tag literally.

## Complex tasks (games, full apps, multi-file projects)
When asked to create something complex (a game, a full application, etc.):
1. Write the COMPLETE file in ONE write_file call. Do not write a skeleton and then edit it — write the full working code from the start.
2. After writing, ALWAYS read_file to verify the output is correct and complete.
3. If anything is wrong or incomplete, use write_file to rewrite the ENTIRE file with fixes — do not try to patch with edit_file.
4. Write WORKING code — every function must have a real implementation, not placeholders or stubs.
5. For games: include all game logic (collision detection, scoring, input handling, rendering). A game must be playable, not a skeleton.
6. Think step by step about what the program needs before writing: data structures, game loop, rendering, input handling, state management.
7. Do NOT use features you are unsure about. Stick to well-known standard library functions.

## Environment
- The current working directory is: {cwd}
- OS: Arch Linux. Package manager is pacman (or yay for AUR). Do NOT use apt/apt-get/brew.
- Python packages: install with pip inside the active virtualenv, not system pip.

## Bash tool constraints
- The bash tool runs in a subprocess with NO TTY. Do NOT run interactive or terminal-UI programs (curses, pygame, ncurses, etc.) through bash — they will always fail with errors like "cbreak() returned ERR". Write the code and tell the user to run it themselves.
- Games, TUIs, and any script with a game_loop/event_loop/main_loop CANNOT be tested via bash. They will always time out. When writing such programs: verify correctness by carefully reading the code, then tell the user to run it. NEVER attempt to run a game or interactive script through bash to "test" it.
- If bash returns a timeout error: STOP immediately. Do NOT retry, do NOT add timeout mechanisms to the script, do NOT keep editing and re-running. A timeout means the program needs a TTY. Tell the user to run it directly.
- GUI applications (file managers, text editors, browsers, etc.) CAN be launched through bash — the display environment (DISPLAY/WAYLAND_DISPLAY) is inherited. Launch them detached: `nohup thunar . &>/dev/null &`. Use xdg-open for generic file/URL opening. To open a file manager: try thunar, nautilus, dolphin, nemo, or pcmanfm in that order.

## Memory
- Project memory is stored in .vibe/memory.md — read it at the start of a session if it exists, \
and update it with important decisions, file layouts, and current status so future sessions \
don't need to rediscover everything.
{memory_section}"""

# ── Max tool-call loop iterations to prevent runaway agents ──────────────────
_MAX_TOOL_LOOPS = 25


class VibeModel:
    # Tools that change the system. These pass through confirm_tool (if set)
    # before running; read-only tools (read_file/glob/grep/list_dir) never do.
    # ⚠ THE LINE IS "DOES IT WRITE", not "is it dangerous". desktop_open is
    # here by its absence and that is the decision: opening a folder, a URL or
    # the control panel changes nothing, and a confirmation on it would train
    # the user to press Enter without reading — which is what makes the
    # confirmations that matter stop working.
    _CONFIRM_TOOLS = {"bash", "write_file", "edit_file",
                      "desktop_action", "desktop_setting", "move_file"}

    def __init__(self, verbose: bool = False):
        self._messages: list[dict] = []
        self._think_filter = _ThinkFilter()
        self._llm = None
        self._verbose = verbose
        # Optional gate: a callable (name, args) -> bool. The REPL sets it to an
        # interactive y/N prompt; left None (e.g. non-interactive/embedded use)
        # every tool runs unattended, preserving the old behaviour.
        self.confirm_tool = None

        if cfg.BACKEND == "ollama":
            self._init_ollama()
        elif cfg.BACKEND == "synapd":
            self._init_synapd()
        elif cfg.BACKEND in ("anthropic", "openai"):
            self._init_cloud()
        else:
            self._init_llama_cpp(verbose)

        self._reset_system()

    def _init_llama_cpp(self, verbose: bool):
        from llama_cpp import Llama
        if not cfg.MODEL_PATH.exists():
            raise FileNotFoundError(
                f"Model not found: {cfg.MODEL_PATH}\n"
                "Run ./setup.sh to download it."
            )
        self._llm = Llama(
            model_path=str(cfg.MODEL_PATH),
            n_gpu_layers=cfg.N_GPU_LAYERS,
            n_ctx=cfg.N_CTX,
            n_threads=cfg.N_THREADS,
            flash_attn=cfg.FLASH_ATTN,
            type_k=cfg.KV_CACHE_TYPE,
            type_v=cfg.KV_CACHE_TYPE,
            verbose=verbose,
        )

    def _init_ollama(self):
        try:
            urllib.request.urlopen(f"{cfg.OLLAMA_HOST}/api/tags", timeout=5)
        except Exception as e:
            raise RuntimeError(
                f"Ollama not reachable at {cfg.OLLAMA_HOST}: {e}\n"
                "Make sure ollama is running: ollama serve"
            )

    def _init_cloud(self):
        """A key, and nothing else.

        ⚠ DELIBERATELY NOT A REACHABILITY CHECK. The local backends ping because
        a dead synapd or a stopped ollama is a thing the user can fix before
        typing; a cloud provider is reachable or not at the moment of the
        request, and a probe at startup only buys a second request to be
        rate-limited by. What CAN be known now is whether there is a key, and
        that is the mistake worth catching before a window opens."""
        if not cfg.api_key(cfg.BACKEND):
            raise RuntimeError(
                f"no {cfg.BACKEND} key — write one with `vibe key {cfg.BACKEND}`, "
                f"or go back to the local model with `vibe provider synapd`"
            )

    def _gate(self, name: str, args: dict) -> bool:
        """True to run the tool. Destructive tools consult confirm_tool; a
        missing callback (non-interactive) always proceeds. A callback that
        raises must never wedge the agent, so treat an error as approval."""
        if name not in self._CONFIRM_TOOLS or self.confirm_tool is None:
            return True
        try:
            return bool(self.confirm_tool(name, args))
        except Exception:
            return True

    def _init_synapd(self):
        try:
            synapd_client.ping(
                socket_path=cfg.SYNAPD_SOCKET,
                host=cfg.SYNAPD_HOST,
                port=cfg.SYNAPD_PORT,
            )
        except synapd_client.SynapdError as e:
            raise RuntimeError(
                f"{e}\nMake sure synapd is running: sudo systemctl start synapd"
            )

    def _reset_system(self):
        cwd = os.getcwd()
        memory_path = Path(cwd) / ".vibe" / "memory.md"
        memory_section = ""
        if memory_path.exists():
            try:
                content = memory_path.read_text(encoding="utf-8").strip()
                if content:
                    memory_section = f"\n## Memory from previous sessions\n{content}\n"
            except Exception:
                pass
        system_content = SYSTEM_PROMPT.format(cwd=cwd, memory_section=memory_section)
        self._messages = [
            {"role": "system", "content": system_content}
        ]

    def reset(self):
        self._think_filter = _ThinkFilter()
        self._reset_system()

    def reload(self, verbose: bool | None = None):
        """Re-initialize the backend (e.g. after changing GPU layer count)."""
        if verbose is None:
            verbose = self._verbose
        if cfg.BACKEND == "ollama":
            self._init_ollama()
        elif cfg.BACKEND == "synapd":
            self._init_synapd()
        elif cfg.BACKEND in ("anthropic", "openai"):
            self._init_cloud()
        else:
            self._init_llama_cpp(verbose)
        self._reset_system()

    def token_count(self) -> int:
        text = " ".join(m.get("content", "") or "" for m in self._messages)
        if cfg.BACKEND == "llama_cpp" and self._llm:
            try:
                return len(self._llm.tokenize(text.encode())) if text else 0
            except Exception:
                pass
        # ollama / fallback: rough estimate (4 chars ≈ 1 token)
        return len(text) // 4

    @property
    def context_limit(self) -> int:
        if cfg.BACKEND == "ollama":
            return cfg.OLLAMA_CTX
        if cfg.BACKEND == "synapd":
            return cfg.SYNAPD_CTX
        if cfg.BACKEND == "anthropic":
            return cfg.ANTHROPIC_CTX
        if cfg.BACKEND == "openai":
            return cfg.OPENAI_CTX
        return cfg.N_CTX

    def _prune_messages(self):
        """
        Sliding-window pruning: keep the system prompt and the most recent
        messages that fit within ~75% of the context window.  Older messages
        are dropped in pairs (user+assistant) so the history stays coherent.
        """
        budget = int(self.context_limit * 0.75)
        if self.token_count() <= budget:
            return

        # Always keep the system prompt (index 0)
        system = self._messages[:1]
        rest = self._messages[1:]

        # Walk backwards, accumulating token cost, until we hit the budget
        kept: list[dict] = []
        chars = 0
        char_budget = budget * 4  # rough: 1 token ≈ 4 chars
        for msg in reversed(rest):
            msg_chars = len(msg.get("content") or "")
            if chars + msg_chars > char_budget and kept:
                break
            kept.append(msg)
            chars += msg_chars

        kept.reverse()

        # If we dropped anything, inject a continuity note
        dropped = len(rest) - len(kept)
        if dropped > 0:
            kept.insert(0, {
                "role": "system",
                "content": (
                    f"[{dropped} earlier messages were pruned to fit context. "
                    "The conversation continues below.]"
                ),
            })

        self._messages = system + kept

    def _user_content(self, text: str) -> str:
        """Prepend Qwen3 thinking-mode directive to user messages."""
        directive = "" if cfg.THINKING else "/no_think "
        return directive + text

    def summarize(self, user_text: str) -> Iterator[str]:
        """
        Tool-free summarization: sends a message without tool schemas so the
        model can't accidentally call tools during /save.
        """
        msgs = self._messages + [
            {"role": "user", "content": self._user_content(user_text)}
        ]

        if cfg.BACKEND == "ollama":
            # Non-streaming, no tools
            payload = json.dumps({
                "model": cfg.OLLAMA_MODEL,
                "messages": msgs,
                "stream": False,
                "options": {
                    "temperature": cfg.TEMPERATURE,
                    "num_predict": cfg.MAX_TOKENS,
                    "num_ctx": cfg.OLLAMA_CTX,
                    "num_gpu": cfg.OLLAMA_NUM_GPU,
                },
            }).encode()
            req = urllib.request.Request(
                f"{cfg.OLLAMA_HOST}/v1/chat/completions",
                data=payload,
                headers={"Content-Type": "application/json"},
            )
            try:
                with urllib.request.urlopen(req, timeout=cfg.OLLAMA_TIMEOUT) as resp:
                    result = json.loads(resp.read())
                content = result["choices"][0]["message"].get("content", "")
                yield content
            except Exception as e:
                yield f"[summarization failed: {e}]"
        elif cfg.BACKEND in ("anthropic", "openai"):
            # The same call the loop makes, with the tools left off — a
            # summary is text and a tool call in one would be noise.
            try:
                gen = (cloud.anthropic_stream(msgs, None)
                       if cfg.BACKEND == "anthropic"
                       else cloud.openai_stream(msgs, None))
                for chunk in gen:
                    part = chunk["choices"][0].get("delta", {}).get("content")
                    if part:
                        yield part
            except Exception as e:
                yield f"[summarization failed: {e}]"
        elif cfg.BACKEND == "synapd":
            # Single-shot, no tools described in the prompt.
            try:
                yield synapd_client.query(
                    synapd_client.flatten_messages(msgs, None),
                    max_tokens=cfg.MAX_TOKENS,
                    socket_path=cfg.SYNAPD_SOCKET,
                    host=cfg.SYNAPD_HOST,
                    port=cfg.SYNAPD_PORT,
                    timeout=cfg.SYNAPD_TIMEOUT,
                )
            except Exception as e:
                yield f"[summarization failed: {e}]"
        else:
            # llama-cpp: create completion without tools
            for chunk in self._llm.create_chat_completion(
                messages=msgs,
                temperature=cfg.TEMPERATURE,
                max_tokens=cfg.MAX_TOKENS,
                stream=True,
            ):
                delta = chunk["choices"][0].get("delta", {})
                if delta.get("content"):
                    yield delta["content"]

    def chat(self, user_text: str) -> Iterator[str]:
        """
        Send a user message and yield text tokens as they stream.
        Handles the full tool-call agentic loop internally.
        Yields special markers:
          - "\\x00TOOL_START\\x00{name}\\x00{json_args}\\x00"  — tool about to run
          - "\\x00TOOL_END\\x00{result}\\x00"                 — tool result
        """
        self._messages.append({
            "role": "user",
            "content": self._user_content(user_text),
        })

        # Prune before sending to avoid context overflow
        self._prune_messages()

        # Reset think filter at the start of each turn so interrupted
        # streams don't leave it in a bad state
        self._think_filter = _ThinkFilter()

        _autopush_remaining = 2  # max automatic nudges per user turn
        _force_tool = False      # force tool_choice="required" on next call
        _no_tools = False        # disable tools entirely (for code-block retries)
        _original_user_text = user_text  # saved for clean retry
        _loop_count = 0

        while True:
            _loop_count += 1
            if _loop_count > _MAX_TOOL_LOOPS:
                yield "\n[max tool iterations reached — stopping]\n"
                return

            # ── Call the model ────────────────────────────────────────────────
            try:
                stream = self._stream_completion(
                    force_tool=_force_tool,
                    no_tools=_no_tools,
                )
            except Exception as e:
                err_msg = f"Generation failed: {e}"
                yield f"\n[Error: {err_msg}]\n"
                self._messages.append({
                    "role": "assistant",
                    "content": f"[error: {err_msg}]",
                })
                return
            _force_tool = False
            _no_tools = False

            # ── Collect streamed response ─────────────────────────────────────
            assistant_text = ""
            tool_calls_acc: dict[int, dict] = {}

            # Rolling buffer: hold back enough chars to detect "<tool_call>"
            # before yielding, so we never stream that tag to the UI.
            _TAG = "<tool_call>"
            stream_buf = ""
            text_tool_call_started = False

            try:
                for chunk in stream:
                    delta = chunk["choices"][0].get("delta", {})

                    # Accumulate text tokens
                    if delta.get("content"):
                        token = delta["content"]
                        assistant_text += token

                        if not text_tool_call_started:
                            stream_buf += token
                            if _TAG in stream_buf:
                                # Yield everything before the tag, then stop
                                pre = stream_buf[:stream_buf.find(_TAG)]
                                if pre:
                                    yield from self._emit_text(pre)
                                text_tool_call_started = True
                                stream_buf = ""
                            elif len(stream_buf) > len(_TAG):
                                # Safe to yield all but last len(_TAG)-1 chars
                                safe_len = len(stream_buf) - len(_TAG) + 1
                                yield from self._emit_text(stream_buf[:safe_len])
                                stream_buf = stream_buf[safe_len:]
                        # else: text tool call in progress — accumulate only

                    # Accumulate structured tool calls
                    if delta.get("tool_calls"):
                        for tc in delta["tool_calls"]:
                            idx = tc.get("index", 0)
                            if idx not in tool_calls_acc:
                                tool_calls_acc[idx] = {
                                    "id": tc.get("id", f"tc_{idx}"),
                                    "type": "function",
                                    "function": {"name": "", "arguments": ""},
                                }
                            acc = tool_calls_acc[idx]
                            if tc.get("id"):
                                acc["id"] = tc["id"]
                            fn = tc.get("function", {})
                            if fn.get("name"):
                                acc["function"]["name"] += fn["name"]
                            args_val = fn.get("arguments")
                            if args_val is not None:
                                if isinstance(args_val, str):
                                    acc["function"]["arguments"] += args_val
                                else:
                                    # ollama sometimes returns dict instead of string
                                    acc["function"]["arguments"] = json.dumps(args_val)
            except KeyboardInterrupt:
                # Flush what we have and return
                if stream_buf:
                    yield from self._emit_text(stream_buf)
                if assistant_text:
                    self._messages.append({"role": "assistant", "content": assistant_text})
                return
            except Exception as e:
                yield f"\n[Stream error: {e}]\n"
                if assistant_text:
                    self._messages.append({"role": "assistant", "content": assistant_text})
                return

            # Flush any remaining buffered text (no tool_call detected)
            if stream_buf and not text_tool_call_started:
                yield from self._emit_text(stream_buf)

            tool_calls = [tool_calls_acc[i] for i in sorted(tool_calls_acc)]

            # Fallback: parse <tool_call> blocks from text when llama-cpp
            # streaming didn't populate delta["tool_calls"] (Qwen3 quirk)
            if not tool_calls and text_tool_call_started:
                tool_calls = _parse_text_tool_calls(assistant_text)

            # ── No tool calls → check for inline code blocks to auto-save ──────
            if not tool_calls:
                _visible = re.sub(r"<think>[\s\S]*?</think>", "", assistant_text).strip()

                # Auto-save: if response contains a fenced code block with a filename
                # comment, write it automatically (model can't do it via tool call)
                _saved = _auto_save_code_blocks(_visible)
                if _saved:
                    # If multiple blocks found, keep only the largest one
                    # (model often outputs explanation snippets alongside the main file)
                    if len(_saved) > 1:
                        _saved = [max(_saved, key=lambda x: len(x[1]))]

                    from .tools import write_file as _write_file
                    fake_tool_calls = []
                    tool_results = []
                    for i, (path, content) in enumerate(_saved):
                        tc_id = f"auto_{i}"
                        fake_tool_calls.append({
                            "id": tc_id,
                            "type": "function",
                            "function": {
                                "name": "write_file",
                                "arguments": json.dumps({"path": path, "content": "..."}),
                            },
                        })
                        yield f"\x00TOOL_START\x00write_file\x00{json.dumps({'path': path, 'content': content})}\x00"
                        if self._gate("write_file", {"path": path, "content": content}):
                            actual = _write_file(path, content)
                        else:
                            actual = f"[skipped: user declined to write {path}]"
                        yield f"\x00TOOL_END\x00{actual}\x00"
                        tool_results.append({
                            "role": "tool",
                            "tool_call_id": tc_id,
                            "content": actual,
                        })
                    # Record in history so the model knows the file exists
                    self._messages.append({
                        "role": "assistant",
                        "content": assistant_text or None,
                        "tool_calls": fake_tool_calls,
                    })
                    self._messages.extend(tool_results)
                    # Done — don't loop back for review on auto-saved files.
                    # The model can't reliably self-review via the fallback path.
                    return

                # Don't stall-detect if there's a substantial code block
                # (the model wrote code but auto-save couldn't find a filename)
                _has_code_block = bool(re.search(r"```\w*\s*\n.{100,}?```", _visible, re.DOTALL))

                _is_stall = (
                    not _has_code_block
                    and (
                        not _visible
                        or len(_visible) < 30
                        or _STALL_RE.search(_visible)
                    )
                )
                if _autopush_remaining > 0 and _is_stall:
                    _autopush_remaining -= 1
                    # Signal to UI that we're retrying
                    yield "\x00RETRY\x00"
                    self._reset_system()
                    self._messages.append({
                        "role": "user",
                        "content": (
                            f"/no_think {_original_user_text}\n\n"
                            "Output the COMPLETE code in a fenced code block. "
                            "Put a filename comment on the first line of the code:\n"
                            "```python\n# file: program.py\n"
                            "# full working code here\n```\n"
                            "NO explanation. NO narration. ONLY the code block."
                        ),
                    })
                    self._think_filter = _ThinkFilter()
                    _force_tool = False
                    _no_tools = True  # disable tools for retry
                    continue

                self._messages.append({
                    "role": "assistant",
                    "content": assistant_text,
                })
                return

            # ── Execute tool calls ────────────────────────────────────────────
            self._messages.append({
                "role": "assistant",
                "content": assistant_text or None,
                "tool_calls": tool_calls,
            })

            for tc in tool_calls:
                name = tc["function"]["name"]
                raw_args = tc["function"]["arguments"] or "{}"
                try:
                    args = json.loads(raw_args) if isinstance(raw_args, str) else raw_args
                except (json.JSONDecodeError, TypeError):
                    args = {}
                    yield f"\n[Warning: malformed tool arguments for {name}, using defaults]\n"
                if isinstance(args, dict):
                    # Also covers the native tool_calls path (ollama, llama_cpp)
                    args = _clean_tool_args(args)

                yield f"\x00TOOL_START\x00{name}\x00{json.dumps(args)}\x00"

                if not self._gate(name, args):
                    result = f"[skipped: user declined to run {name}]"
                else:
                    try:
                        result = execute_tool(name, args)
                    except Exception as e:
                        result = f"Error executing {name}: {e}"

                yield f"\x00TOOL_END\x00{result}\x00"

                self._messages.append({
                    "role": "tool",
                    "tool_call_id": tc.get("id", f"tc_{name}"),
                    "content": result,
                })

            # Prune after tool results to stay within context
            self._prune_messages()

            # Loop: send tool results back to model

    def _stream_completion(self, force_tool: bool = False, no_tools: bool = False):
        tool_choice = "required" if force_tool else "auto"
        if cfg.BACKEND == "ollama":
            return self._ollama_stream(
                tool_choice=tool_choice,
                no_tools=no_tools,
            )
        if cfg.BACKEND == "synapd":
            return self._synapd_stream(no_tools=no_tools)
        if cfg.BACKEND == "anthropic":
            return cloud.anthropic_stream(
                self._messages, None if no_tools else TOOL_SCHEMAS)
        if cfg.BACKEND == "openai":
            return cloud.openai_stream(
                self._messages, None if no_tools else TOOL_SCHEMAS,
                tool_choice=tool_choice)
        kwargs = dict(
            messages=self._messages,
            temperature=cfg.TEMPERATURE,
            top_p=cfg.TOP_P,
            top_k=cfg.TOP_K,
            min_p=cfg.MIN_P,
            repeat_penalty=cfg.REPEAT_PENALTY,
            max_tokens=cfg.MAX_TOKENS,
            stream=True,
        )
        if not no_tools:
            kwargs["tools"] = TOOL_SCHEMAS
            kwargs["tool_choice"] = tool_choice
        return self._llm.create_chat_completion(**kwargs)

    def _trim_messages_for_ollama(self) -> list[dict]:
        """Return messages with oversized assistant content truncated to avoid HTTP 500."""
        MAX_MSG_CHARS = 8000
        trimmed = []
        for msg in self._messages:
            content = msg.get("content") or ""
            if msg.get("role") == "assistant" and len(content) > MAX_MSG_CHARS:
                msg = {**msg, "content": content[:MAX_MSG_CHARS] + "\n...[truncated]"}
            trimmed.append(msg)
        return trimmed

    def _ollama_stream(self, tool_choice: str = "auto", no_tools: bool = False):
        """Non-streaming ollama call, faked as a stream of chunks."""
        body: dict = {
            "model": cfg.OLLAMA_MODEL,
            "messages": self._trim_messages_for_ollama(),
            "stream": False,
            "options": {
                "temperature": cfg.TEMPERATURE,
                "top_p": cfg.TOP_P,
                "top_k": cfg.TOP_K,
                "repeat_penalty": cfg.REPEAT_PENALTY,
                "num_predict": cfg.MAX_TOKENS,
                "num_ctx": cfg.OLLAMA_CTX,
                "num_gpu": cfg.OLLAMA_NUM_GPU,
            },
        }
        if not no_tools:
            body["tools"] = TOOL_SCHEMAS
            body["tool_choice"] = tool_choice
        payload = json.dumps(body).encode()
        req = urllib.request.Request(
            f"{cfg.OLLAMA_HOST}/v1/chat/completions",
            data=payload,
            headers={"Content-Type": "application/json"},
        )

        # Retry once on transient network errors
        for attempt in range(2):
            try:
                with urllib.request.urlopen(req, timeout=cfg.OLLAMA_TIMEOUT) as resp:
                    body = resp.read()
                break
            except urllib.error.HTTPError as e:
                if e.code >= 500 and attempt == 0:
                    # Server error — trim harder and retry
                    time.sleep(1)
                    continue
                raise RuntimeError(f"Ollama HTTP {e.code}: {e.read().decode()[:200]}")
            except urllib.error.URLError as e:
                if attempt == 0:
                    time.sleep(1)
                    continue
                raise RuntimeError(f"Ollama connection failed: {e.reason}")

        try:
            result = json.loads(body)
        except json.JSONDecodeError as e:
            raise RuntimeError(f"Ollama returned invalid JSON: {e}")

        if "choices" not in result or not result["choices"]:
            raise RuntimeError(f"Ollama returned unexpected response: {json.dumps(result)[:200]}")

        choice = result["choices"][0]
        message = choice.get("message", {})
        reasoning = (message.get("reasoning") or "").strip()
        content = (message.get("content") or "").strip()
        tool_calls = message.get("tool_calls") or []

        # Normalise tool_calls: ensure arguments is always a JSON string,
        # add index field, and ensure id exists (ollama can omit these)
        for i, tc in enumerate(tool_calls):
            tc.setdefault("index", i)
            tc.setdefault("id", f"ollama_{i}")
            fn = tc.get("function", {})
            args = fn.get("arguments", "{}")
            if not isinstance(args, str):
                fn["arguments"] = json.dumps(args)
            # Ensure name is present
            fn.setdefault("name", "unknown")

        # Emit reasoning as one think block (if thinking mode on)
        if reasoning and cfg.THINKING:
            yield {"choices": [{"delta": {"content": f"<think>{reasoning}</think>"}, "finish_reason": None}]}

        # Emit content in small chunks for a live feel
        if content:
            for i in range(0, len(content), 20):
                yield {"choices": [{"delta": {"content": content[i:i+20]}, "finish_reason": None}]}

        # Emit tool calls
        if tool_calls:
            yield {"choices": [{"delta": {"content": None, "tool_calls": tool_calls}, "finish_reason": "tool_calls"}]}

    def _synapd_stream(self, no_tools: bool = False):
        """Single-shot synapd query, faked as a stream of chunks.

        synapd has no structured tool-calling, so the tools are described in the
        prompt and the model emits <tool_call> blocks that Vibe's text parser
        picks up. There is no native tool_calls delta — content only.
        """
        prompt = synapd_client.flatten_messages(
            self._messages,
            None if no_tools else TOOL_SCHEMAS,
        )
        text = synapd_client.query(
            prompt,
            max_tokens=cfg.MAX_TOKENS,
            socket_path=cfg.SYNAPD_SOCKET,
            host=cfg.SYNAPD_HOST,
            port=cfg.SYNAPD_PORT,
            timeout=cfg.SYNAPD_TIMEOUT,
        )
        # Emit in small chunks so the UI animates like a stream.
        for i in range(0, len(text), 20):
            yield {"choices": [{"delta": {"content": text[i:i + 20]},
                                "finish_reason": None}]}

    def _emit_text(self, text: str) -> Iterator[str]:
        """Yield text through the think filter (or raw if thinking is on)."""
        if not cfg.THINKING:
            yield from self._think_filter.feed_iter(text)
        else:
            yield text


# ── Text tool-call parser ───────────────────────────────────────────────────────

_TOOL_CALL_RE = re.compile(r"<tool_call>\s*(.*?)\s*</tool_call>", re.DOTALL)

# Detects narration or post-hoc description instead of actually calling a tool
_STALL_RE = re.compile(
    r"(?i)"
    r"\b(i'?ll|let me|i will|i(?:'m| am) going to|i can|here(?:'s| is)(?: the| a)?)\b"
    r"[\s\S]{0,120}\b(write|create|build|implement|generate|code|make|develop|add)\b"
    r"|this implementation\b"
    r"|\bkey fix(es)?\b"
    r"|\bto run:\s"
    r"|\bwant me to add\b"
    r"|\bfeel free to\b"
    r"|\blet me know\b"
    r"|\b###\s+\w"           # markdown heading = describing, not acting
    r"|endoftext"            # leaked template token = context overflow
)


# Matches fenced code blocks (closed with ```)
_CODE_BLOCK_CLOSED_RE = re.compile(r"```(\w+)?\s*\n([\s\S]+?)```")
# Matches unclosed code blocks (model ran out of tokens before closing)
_CODE_BLOCK_OPEN_RE = re.compile(r"```(\w+)?\s*\n([\s\S]+)")
# Matches: # file: name.py, // file: name.js, -- file: name.sql, /* file: name.c */
_FILE_COMMENT_RE = re.compile(
    r'^(?:#|//|--|/\*)\s*(?:file|filename|path):\s*(\S+)', re.MULTILINE
)

_SUPPORTED_EXT = r'(?:py|sh|js|ts|rb|go|rs|c|cpp|h|html|css|json|yaml|yml|toml|lua|java|kt)'

# Matches filenames in backticks/bold: `tetris.py`, *game.sh*
_FILENAME_HINT_STYLED_RE = re.compile(
    rf'[`*]{{1,2}}([\w./\-]+\.{_SUPPORTED_EXT})[`*]{{1,2}}'
)
# Matches bare filenames in surrounding prose: "updated tetris.py:" or "save as game.sh"
_FILENAME_HINT_BARE_RE = re.compile(
    rf'(?:^|[\s(])([\w./\-]+\.{_SUPPORTED_EXT})(?=[:\s),]|$)',
    re.MULTILINE,
)

# Map code-fence language tags to file extensions (for fallback naming)
_LANG_TO_EXT = {
    "python": "py", "python3": "py", "py": "py",
    "bash": "sh", "sh": "sh", "shell": "sh", "zsh": "sh",
    "javascript": "js", "js": "js", "typescript": "ts", "ts": "ts",
    "ruby": "rb", "go": "go", "rust": "rs",
    "c": "c", "cpp": "cpp", "java": "java", "kotlin": "kt",
    "lua": "lua", "html": "html", "css": "css",
    "json": "json", "yaml": "yaml", "toml": "toml",
}

# Shebang to extension
_SHEBANG_RE = re.compile(r'^#!\s*/(?:usr/(?:local/)?)?bin/(?:env\s+)?(\w+)')


def _infer_filename_from_code(code: str, lang_tag: str | None) -> str | None:
    """
    Last-resort: infer a reasonable filename from the code fence language
    tag and/or shebang line.  Returns e.g. 'program.py' or None.
    """
    ext = None
    # 1. Try language tag from the code fence
    if lang_tag:
        ext = _LANG_TO_EXT.get(lang_tag.lower())
    # 2. Try shebang
    if not ext:
        first_line = code.split("\n", 1)[0]
        m = _SHEBANG_RE.match(first_line)
        if m:
            interp = m.group(1).lower()
            # python3 → py, bash → sh, etc.
            ext = _LANG_TO_EXT.get(interp)
            if not ext and "python" in interp:
                ext = "py"
    if ext:
        return f"program.{ext}"
    return None


def _auto_save_code_blocks(text: str) -> list[tuple[str, str]]:
    """
    Find fenced code blocks and return (path, content) pairs.
    Tries closed blocks first, then falls back to unclosed blocks
    (model ran out of tokens before closing with ```).
    """
    # Try closed blocks first; if none found, try unclosed
    matches = list(_CODE_BLOCK_CLOSED_RE.finditer(text))
    if not matches:
        matches = list(_CODE_BLOCK_OPEN_RE.finditer(text))

    results = []
    for m in matches:
        lang_tag = m.group(1)  # e.g. "python", "bash", or None
        code = m.group(2).strip()
        if len(code) < 20:
            continue

        path = None

        # 1. Look for "# file: name" in the first 5 lines of the code
        first_lines = "\n".join(code.splitlines()[:5])
        fc = _FILE_COMMENT_RE.search(first_lines)
        if fc:
            path = fc.group(1)
            # Remove the comment line from the code
            code_lines = code.splitlines()
            for i, line in enumerate(code_lines[:5]):
                if _FILE_COMMENT_RE.match(line):
                    code_lines.pop(i)
                    break
            code = "\n".join(code_lines)
        else:
            # 2. Nearby context (1500 chars — enough for long explanations)
            before = text[max(0, m.start() - 1500):m.start()]
            after = text[m.end():m.end() + 1500]
            for regex in (_FILENAME_HINT_STYLED_RE, _FILENAME_HINT_BARE_RE):
                hit = regex.search(before) or regex.search(after)
                if hit:
                    path = hit.group(1)
                    break

            # 3. Search the ENTIRE text as last resort for styled hints
            if not path:
                hit = _FILENAME_HINT_STYLED_RE.search(text)
                if hit:
                    path = hit.group(1)

            # 4. Infer from language tag / shebang
            if not path:
                path = _infer_filename_from_code(code, lang_tag)

        if not path:
            continue
        results.append((path, code))
    return results


# Wrapper keys a model may use instead of "arguments"
_ARG_KEYS = ("arguments", "parameters", "args", "input")

# Args that carry literal file content, so a stray wrapper tag becomes a syntax
# error in the written file. Only these are unwrapped — a bash command or an
# old_string must survive byte-for-byte.
_CONTENT_ARGS = ("content", "new_string")

# A <code>…</code> wrapper around the WHOLE value. Balanced-only: an unmatched
# tag is left alone, so a file whose content legitimately opens with markup is
# never truncated.
_CODE_TAG_RE = re.compile(r"\A\s*<code>\s*\n(.*)\n\s*</code>\s*\Z", re.DOTALL)


def _unwrap_code_tag(value: str) -> str:
    """Strip a <code>…</code> wrapper the model copied from the prompt."""
    m = _CODE_TAG_RE.match(value)
    return m.group(1) if m else value


def _extract_tool_args(data: dict) -> dict:
    """Pull the argument mapping out of one parsed <tool_call> block.

    Models emit the args either wrapped ({"name":.., "arguments":{..}}) or
    flattened onto the top level ({"name":.., "path":.., "content":..}).
    synapd's model does the latter, so accept both.
    """
    for key in _ARG_KEYS:
        if key in data:
            val = data[key]
            if isinstance(val, str):
                # Some models double-encode the wrapper as a JSON string
                try:
                    val = json.loads(val)
                except json.JSONDecodeError:
                    return {}
            return val if isinstance(val, dict) else {}
    # Flattened form: everything that isn't call metadata is an argument
    return {k: v for k, v in data.items() if k not in ("name", "type", "id")}


def _clean_tool_args(args: dict) -> dict:
    """Unwrap prompt-placeholder tags from content-bearing args."""
    for key in _CONTENT_ARGS:
        if isinstance(args.get(key), str):
            args[key] = _unwrap_code_tag(args[key])
    return args


def _parse_text_tool_calls(text: str) -> list[dict]:
    """Extract Qwen3-style <tool_call>...</tool_call> blocks from assistant text."""
    calls = []
    for i, m in enumerate(_TOOL_CALL_RE.finditer(text)):
        try:
            data = json.loads(m.group(1))
        except json.JSONDecodeError:
            continue
        if not isinstance(data, dict):
            continue
        calls.append({
            "id": f"call_{i}",
            "type": "function",
            "function": {
                "name": data.get("name", ""),
                "arguments": json.dumps(_clean_tool_args(_extract_tool_args(data))),
            },
        })
    return calls


# ── Thinking-block stream filter ───────────────────────────────────────────────

class _ThinkFilter:
    """Stateful filter that strips <think>...</think> from a token stream."""
    def __init__(self):
        self._in_think = False
        self._buf = ""

    def feed_iter(self, token: str) -> Iterator[str]:
        self._buf += token
        while True:
            if self._in_think:
                end = self._buf.find("</think>")
                if end == -1:
                    self._buf = self._buf[-len("</think>"):]  # keep tail for partial match
                    return
                self._buf = self._buf[end + len("</think>"):]
                self._in_think = False
            else:
                start = self._buf.find("<think>")
                if start == -1:
                    out = self._buf
                    self._buf = ""
                    if out:
                        yield out
                    return
                out = self._buf[:start]
                self._buf = self._buf[start + len("<think>"):]
                self._in_think = True
                if out:
                    yield out
