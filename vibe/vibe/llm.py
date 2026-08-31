import json
import os
import re
import urllib.error
import urllib.request
import time
from pathlib import Path
from typing import Iterator

import vibe.config as cfg
from vibe import cloud, modes
from vibe import chibi_bridge
from vibe import personas
from vibe import companion_tools
from . import synapd_client
from .tools import TOOL_MAP, TOOL_SCHEMAS, execute_tool


SYSTEM_PROMPT = """\
You are Synapse, the assistant built into SynapseOS. You are the whole desktop's \
assistant, not only a coding one: you answer questions, you write and explain \
things, you drive this machine, and you write code.

## When to use a tool, and when not to

⚠ THIS IS THE RULE THAT MATTERS MOST. Tools are for THIS MACHINE — its files, its \
settings, its windows, its packages. A question about the world is answered from \
what you know, immediately, with no tool at all.

- "what is the speed of light" — answer it. Do not grep for it.
- "who wrote Dune", "explain TCP slow start", "draft an email declining a meeting" \
  — answer, or write it. No tools.
- "what is in my Downloads folder", "open the control panel", "move the bar to the \
  bottom", "fix the bug in main.py" — those are this machine. Use tools.
- If you are unsure which it is, answer first and offer to look.

Never search a file for a fact that is not about this computer. A grep that finds \
nothing is not an answer to a question about physics.

## This desktop

SynapseOS is Arch-based, and it has its own programs. Prefer them:

- `synpkg` installs and removes packages (a pacman front end). Not apt, not brew.
- `synfiles` is the file manager, `syn-edit` the editor, `syntty` the terminal, \
  `cliamp` the music player, `synstudio` the video editor, `synsh` the shell.
- `synctl` drives the compositor (synui); `syn-settings` is the settings CLI; \
  Control panel ▸ is the graphical one.
- `syn-update` updates the system. `synguard` is the security daemon, `synapd` \
  the local AI daemon — you may well be running on it.

Use desktop_open to open a URL, a folder, an app or a panel. Use desktop_action \
for a compositor verb. Use desktop_setting to change a desktop setting — that is \
how the bar and the dock are moved (bar_edge, dock_edge).

## Doing things on the machine

- Read a file before editing it.
- write_file or edit_file to create or change a file — never bash echo/cat for \
  file content.
- Prefer edit_file for a targeted change. If edit_file says "old_string not \
  found", STOP and use write_file to rewrite the whole file — never retry \
  edit_file with a similar string.
- Anything that writes asks the user first. Say what you are about to do in one \
  short line when a confirmation is coming, so the question is not a surprise.
- READ WHAT THE TOOL ANSWERED. A result starting with "Error:" means IT DID NOT \
HAPPEN. Say so, quote the error, and either try a different target or ask — \
never report a folder as opened, a file as written or a setting as changed \
merely because you asked for it. A wrong answer the user can see is \
recoverable; a failure reported as a success is not.
- Be concise. Lead with the answer or the action.

## Writing code

- Write the COMPLETE file in ONE write_file call. Do not write a skeleton and \
  then edit it.
- After writing, read_file to check it, and rewrite the whole file if anything \
  is wrong. Do not patch with edit_file.
- Every function gets a real implementation. No placeholders, no stubs. A game \
  must be playable: collision, scoring, input, rendering.
- Stick to standard library calls you are sure of.
- Run tests after a change when tests exist.

## The bash tool

- No TTY. Curses, pygame and other terminal-UI programs ALWAYS fail there \
  ("cbreak() returned ERR"). Write the code and tell the user to run it.
- A game loop or event loop cannot be tested through bash — it will time out. \
  Read it carefully instead.
- On a timeout: STOP. Do not retry, do not add timeouts to the script. It needs \
  a TTY.
- GUI apps CAN be launched (the display is inherited), but prefer desktop_open — \
  it knows this desktop's own applications.

## Environment
- Working directory: {cwd}
- Arch Linux. `synpkg` or pacman; never apt/apt-get/brew.
- Python packages: pip inside the active virtualenv, not system pip.

## Memory
- Project memory is .vibe/memory.md — read it at the start of a session if it \
exists, and update it with decisions, file layouts and current status.
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
    # ⚠ The companion's writing tools are in here by the same line: a task the
    # model inferred from a sentence and added to somebody's list is a write to
    # their records, and listing them is not.
    _CONFIRM_TOOLS = ({"bash", "write_file", "edit_file",
                       "desktop_action", "desktop_setting", "move_file"}
                      | companion_tools.WRITE_TOOLS)

    def __init__(self, verbose: bool = False):
        self._messages: list[dict] = []
        self._think_filter = _ThinkFilter()
        self._llm = None
        self._verbose = verbose
        self._synapd_fmt = ""
        self._synapd_model = ""
        # Ask / Agent / Plan, or AUTO — see modes.py. Per-TURN: `_turn_tools`
        # is what this turn may reach for, and AUTO resolves to a real mode
        # before it is set.
        self.mode = modes.AUTO
        self._turn_tools = TOOL_SCHEMAS
        self.active_mode = modes.AGENT
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
        # ⛔ WHICH TEMPLATE THE LOADED MODEL WANTS, asked once rather than
        # assumed forever. See synapd_client.status(): getting this wrong does
        # not error, it produces an assistant that answers questions nobody
        # asked with `◁user▷` written around them.
        try:
            st = synapd_client.status(
                socket_path=cfg.SYNAPD_SOCKET,
                host=cfg.SYNAPD_HOST,
                port=cfg.SYNAPD_PORT,
            )
            self._synapd_fmt = st.get("format", "")
            self._synapd_model = st.get("model_name", "")
        except Exception:
            # A daemon too old to answer STATUS still answers QUERY, and the
            # plain transcript is readable by anything. Not worth failing over.
            self._synapd_fmt = ""
            self._synapd_model = ""

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
        # ⛔ APPENDED, NEVER SUBSTITUTED. The persona is a voice; everything
        # above it — the tools, this desktop's facts, and the rule about when
        # NOT to reach for a tool — is what makes this an assistant rather than
        # a chatbot, and a costume must not cost any of it. Empty string for
        # the default persona, so nothing changes for anyone who never picks
        # one. See vibe/personas.py.
        # What the avatar on this desktop already knows about this person.
        # ⛔ APPENDED AS CONTEXT, NEVER AS INSTRUCTIONS — it is a record of a
        # user written by another program, and a memory that can issue orders
        # is an injection surface. Empty on a machine without chibi, which is
        # every plain-Arch install. See vibe/chibi_bridge.py.
        system_content += chibi_bridge.memory_section()
        system_content += personas.voice_section()
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
        """Prepend Qwen3's thinking-mode directive — TO A QWEN, AND NOTHING ELSE.

        ⚠ `/no_think` IS ONE MODEL FAMILY'S SYNTAX, NOT A SETTING. Qwen3 reads
        it as a control token and drops its chain of thought; every other model
        reads it as the first two words the user said. SynapseOS ships a
        Mistral, so for a year the model was asked to `/no_think open
        downloads` — a line that begins with a command it has never heard, on
        the one turn the instruction has to be understood."""
        if cfg.THINKING or not self._is_qwen():
            return text
        return "/no_think " + text

    def _is_qwen(self) -> bool:
        """Whether the model actually answering is a Qwen3.

        The name comes from whichever backend is live: synapd reports the
        loaded model in status(), and the local backends are configured by
        name. An unknown backend answers no, which is the safe way round — a
        missing directive costs a little thinking, an unwanted one is noise in
        the prompt."""
        if cfg.BACKEND == "synapd":
            name = self._synapd_model
        elif cfg.BACKEND == "ollama":
            name = cfg.OLLAMA_MODEL
        elif cfg.BACKEND == "llama_cpp":
            name = str(cfg.MODEL_PATH)
        else:
            name = ""
        return "qwen" in name.lower()

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
                    "temperature": personas.temperature(),
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
                    synapd_client.flatten_messages(msgs, None, self._synapd_fmt),
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
                temperature=personas.temperature(),
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
        # ── Which mode this turn runs in ────────────────────────────────
        #
        # ⚠ RESOLVED PER TURN, not per session. AUTO is the default and it
        # routes each line on its own: the same conversation can answer a
        # question with no tools and then act on the next line. A mode chosen
        # by hand is honoured verbatim and routes nothing.
        self.active_mode = modes.resolve(self.mode, user_text)
        self._turn_tools = modes.tools_for(self.active_mode, TOOL_SCHEMAS)

        # The mode's instruction rides on the USER turn rather than the system
        # block, for the reason the tool reminder does: on a Mistral the system
        # block is folded into the FIRST user turn, so by message ten it is
        # thousands of tokens behind the question and a 7B has stopped
        # following it.
        text_for_model = user_text + modes.PROMPT.get(self.active_mode, "")
        # ⚠ ONLY WHEN THE LINE IS ABOUT THIS MACHINE. An ASK turn with no tools
        # asked about a real folder invented its contents; see modes._ASK_NO_INVENT.
        if self.active_mode == modes.ASK:
            text_for_model += modes.ask_addendum(user_text)
        # ⚠ AND SO DOES THE PERSONA'S, for exactly the same reason: a character
        # described once in a system block Mistral folded into turn one has
        # faded by message ten, and a persona that quietly stops being itself
        # halfway through a conversation is the whole feature not working.
        text_for_model += personas.reminder()

        self._messages.append({
            "role": "user",
            "content": self._user_content(text_for_model),
        })

        # Prune before sending to avoid context overflow
        self._prune_messages()

        # Reset think filter at the start of each turn so interrupted
        # streams don't leave it in a bad state
        self._think_filter = _ThinkFilter()

        _autopush_remaining = 2  # max automatic nudges per user turn
        # A separate budget from _autopush_remaining on purpose: a turn that
        # narrated a tool and a turn that narrated code are different failures,
        # and one must not spend the other's retry.
        _toolpush_remaining = 1  # one re-ask for a turn that talked about a tool
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

                # ── The turn that talked about a tool instead of calling one ──
                #
                # ⛔ THE RETRY BELOW IS CODE-SHAPED AND THIS ONE IS NOT. That
                # one asks for a fenced code block WITH THE TOOLS TURNED OFF,
                # which is right for "write me tetris" and is the worst
                # possible answer to "open downloads": the one thing the turn
                # needed was a tool call, and the retry removes the tools.
                #
                # A small local model fails this turn in three ways, and all
                # three are the same failure — text where an action belonged:
                # it announces the tool ("I'll use the `desktop_open` tool"),
                # it asks to be allowed to use it, or it WRITES THE TOOL RESULT
                # ITSELF and answers from the fiction. The last one is the
                # dangerous one: a folder listing that was never read, reported
                # as fact. None of them ran anything, so the honest reading of
                # all three is that the turn has not happened yet.
                if (_toolpush_remaining > 0 and self._turn_tools
                        and self.active_mode == modes.AGENT
                        and not _has_code_block and _TOOL_STALL_RE.search(_visible)):
                    _toolpush_remaining -= 1
                    yield "\x00RETRY\x00"
                    self._messages.append({
                        "role": "user",
                        "content": self._user_content(
                            f"{_original_user_text}\n\n"
                            "You have not done that yet — you described it. "
                            "Text does nothing on this machine: only a "
                            "<tool_call> block runs, and its result comes back "
                            "to you afterwards. Do not ask permission (the "
                            "confirmation is not yours to ask for) and do not "
                            "write a tool result yourself. Emit the block now, "
                            "as your entire reply:\n"
                            '<tool_call>{"name": "<tool>", "arguments": '
                            "{<args>}}</tool_call>"
                        ),
                    })
                    self._think_filter = _ThinkFilter()
                    _force_tool = True
                    _no_tools = False
                    continue

                _is_stall = (
                    not _has_code_block
                    and (
                        not _visible
                        or len(_visible) < 30
                        or _STALL_RE.search(_visible)
                    )
                )
                # ⛔ THIS RETRY IS CODE-SHAPED, SO IT BELONGS TO AGENT ALONE.
                # It answers a stall by demanding a complete fenced program
                # with the tools off, which is right for "write me tetris" and
                # is nonsense in the other two modes. ASK exists to answer and
                # touch nothing; PLAN exists to write steps. Worse, `_is_stall`
                # fires on any reply under 30 characters, so the correct ASK
                # answer to a question about this machine — "I need to look."
                # — was read as a stall and retried into a Python file the user
                # never asked for. Measured 2 of 3 turns.
                if (_autopush_remaining > 0 and _is_stall
                        and self.active_mode == modes.AGENT):
                    _autopush_remaining -= 1
                    # Signal to UI that we're retrying
                    yield "\x00RETRY\x00"
                    self._reset_system()
                    self._messages.append({
                        "role": "user",
                        "content": self._user_content(
                            f"{_original_user_text}\n\n"
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
                self._messages, None if no_tools else self._turn_tools)
        if cfg.BACKEND == "openai":
            return cloud.openai_stream(
                self._messages, None if no_tools else self._turn_tools,
                tool_choice=tool_choice)
        kwargs = dict(
            messages=self._messages,
            temperature=personas.temperature(),
            top_p=cfg.TOP_P,
            top_k=cfg.TOP_K,
            min_p=cfg.MIN_P,
            repeat_penalty=cfg.REPEAT_PENALTY,
            max_tokens=cfg.MAX_TOKENS,
            stream=True,
        )
        if not no_tools and self._turn_tools:
            kwargs["tools"] = self._turn_tools
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
                "temperature": personas.temperature(),
                "top_p": cfg.TOP_P,
                "top_k": cfg.TOP_K,
                "repeat_penalty": cfg.REPEAT_PENALTY,
                "num_predict": cfg.MAX_TOKENS,
                "num_ctx": cfg.OLLAMA_CTX,
                "num_gpu": cfg.OLLAMA_NUM_GPU,
            },
        }
        if not no_tools and self._turn_tools:
            body["tools"] = self._turn_tools
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
            None if no_tools else self._turn_tools,
            self._synapd_fmt,
        )
        text = synapd_client.query(
            prompt,
            max_tokens=cfg.MAX_TOKENS,
            socket_path=cfg.SYNAPD_SOCKET,
            host=cfg.SYNAPD_HOST,
            port=cfg.SYNAPD_PORT,
            timeout=cfg.SYNAPD_TIMEOUT,
        )
        # ⚠ AND CUT IT WHERE IT STARTS WRITING THE NEXT TURN. Right template or
        # not, a 7B under a long tool transcript will occasionally open a user
        # turn of its own and answer it. That is unexplainable in a chat window
        # and one line to remove here.
        text = synapd_client.trim_hallucinated_turn(text)
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


# A turn that TALKED ABOUT a tool instead of emitting one. Narration, a
# request for permission, or — the one that reads as the assistant lying — a
# "Tool result:" the model wrote itself and then answered from.
#
# ⚠ THE PHRASES ARE THE MODEL'S OWN, taken from what the shipped local model
# actually produced for "open downloads": "I'll use the `desktop_open` tool",
# "Please confirm if you'd like me to proceed", "**Tool result:** [...]".
# ⚠ A BACKTICKED TOOL NAME IS THE STRONGEST SIGNAL, and the names come from
# TOOL_MAP rather than from a list written out here — a second copy would stop
# matching the day a tool is added. Backticks are what keep it from firing on
# prose about somebody's bash history.
_TOOL_STALL_RE = re.compile(
    r"(?i)"
    r"\*{0,2}tool result\*{0,2}\s*:"                    # it forged the result
    r"|\b(?:i(?:'ll| will| am going to| can)|let me|shall i|would you like me"
    r"|please confirm|say the word)\b[\s\S]{0,80}\b(?:tool|call|use|open|run)\b"
    r"|`(?:" + "|".join(TOOL_MAP) + r")`"
    r"|\btool_call\b"                                    # described the block
    # ⛔ AND THE ONE WITH NO TELL AT ALL: an answer that reports what is on
    # this machine, written without reading it. A turn that really listed a
    # folder has a tool call and never reaches this test, so a listing here is
    # invented by construction.
    r"|\b(?:here (?:are|is)|these are)\b[\s\S]{0,40}"
    r"\b(?:files|folders|contents|directory|folder)\b"
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
    # ⛔ A TOOL CALL IS NOT CODE, however it was fenced. Models wrap the block
    # in ```json, and a truncated one parses as neither — which used to land in
    # the user's working directory as `program.json`, a file nobody asked for
    # whose contents are a failed attempt to open their Downloads folder.
    if "<tool_call>" in text:
        return []

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


def _json_object_at(text: str, start: int) -> str | None:
    """The one complete JSON object beginning at or after `start`, or None.

    ⚠ BRACE COUNTING, NOT A REGEX, and it stops at the first balanced object —
    everything the model wrote afterwards (a closing fence, a paragraph about
    what it just did, a second invented turn) is not part of the call."""
    i = text.find("{", start)
    if i < 0:
        return None
    depth, in_str, esc = 0, False, False
    for j in range(i, len(text)):
        c = text[j]
        if in_str:
            if esc:
                esc = False
            elif c == "\\":
                esc = True
            elif c == '"':
                in_str = False
            continue
        if c == '"':
            in_str = True
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return text[i:j + 1]
    return None


def _unclosed_tool_calls(text: str) -> list[str]:
    """The JSON of every <tool_call> that was never closed.

    ⛔ A MISSING `</tool_call>` IS THE COMMON CASE ON A SMALL LOCAL MODEL, not
    a corner one. It opens the tag inside a ```json fence and closes the fence
    instead, or the token budget runs out mid-block. The call it wrote is
    complete and correct; only the punctuation after it is missing, and
    throwing the whole turn away for that is how "open downloads" answered with
    a paragraph about opening downloads.
    """
    out = []
    for m in re.finditer(r"<tool_call>", text):
        rest = text[m.end():]
        if "</tool_call>" in rest:
            continue                      # closed — the strict pass has it
        obj = _json_object_at(rest, 0)
        if obj:
            out.append(obj)
    return out


def _parse_text_tool_calls(text: str) -> list[dict]:
    """Extract Qwen3-style <tool_call>...</tool_call> blocks from assistant text."""
    calls = []
    blocks = [m.group(1) for m in _TOOL_CALL_RE.finditer(text)]
    blocks += _unclosed_tool_calls(text)
    for i, block in enumerate(blocks):
        # A fenced block is still a block: models wrap the JSON in ```json.
        block = re.sub(r"^```[a-zA-Z]*\s*|\s*```$", "", block.strip())
        try:
            data = json.loads(block)
        except json.JSONDecodeError:
            obj = _json_object_at(block, 0)
            if obj is None:
                continue
            try:
                data = json.loads(obj)
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
