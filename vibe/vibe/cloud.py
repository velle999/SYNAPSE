"""
cloud.py — the paid backends, behind the same stream the local ones speak.

⛔ THE REST OF VIBE MUST NOT LEARN THAT A BACKEND IS REMOTE. llm.py drives one
chunk shape — OpenAI's `{"choices": [{"delta": {...}}]}` — and every backend in
this tree fakes or forwards that shape, including synapd, which is a single-shot
unix socket with no streaming at all. So these two do the same: whatever the
provider's wire looks like, what comes back out of here is that chunk.

⚠ CLAUDE GOES THROUGH THE ANTHROPIC SDK, NOT THROUGH AN OPENAI-COMPATIBLE URL.
There are shims that will accept an Anthropic key at a /v1/chat/completions
path, and they drop exactly the things this assistant wants: adaptive thinking,
the tool-use content blocks, the refusal stop reason. The SDK is an optdepend
and the failure when it is missing is a sentence, not a traceback.

⚠ OpenAI needs no client of its own: its chat-completions endpoint IS the shape
llm.py already speaks to ollama, so it is the same code with a base URL and an
Authorization header. Reaching for a second SDK to send the same JSON would be
a dependency bought for nothing.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
"""

import json
import urllib.request
import urllib.error

import vibe.config as cfg


class CloudError(RuntimeError):
    pass


# ── Anthropic ───────────────────────────────────────────────────────────────

def _anthropic_client():
    """The SDK client, or a CloudError that says what to install.

    Imported HERE and not at module scope: vibe's default backend is local and
    a desktop that never touches the cloud must not need the package at all —
    the same rule the llama_cpp backend already follows."""
    try:
        import anthropic  # noqa: F401
    except ImportError:
        raise CloudError(
            "the anthropic package is not installed — "
            "synpkg install python-anthropic (AUR), or switch back with "
            "`vibe provider synapd`"
        )
    key = cfg.api_key("anthropic")
    if not key:
        raise CloudError(
            "no Anthropic key — write one with `vibe key anthropic` "
            f"(it is stored in {cfg.key_path('anthropic')}, mode 0600)"
        )
    import anthropic
    return anthropic.Anthropic(api_key=key, timeout=float(cfg.ANTHROPIC_TIMEOUT))


def anthropic_tools(schemas: list) -> list:
    """OpenAI tool schemas → Anthropic's shape.

    ⚠ The nesting differs and the key differs: OpenAI wraps a `function` object
    and calls the schema `parameters`; Anthropic is flat and calls it
    `input_schema`. One translation here rather than a second copy of the tool
    table, so a tool added to tools.py reaches both without being written twice.
    """
    out = []
    for s in schemas or []:
        fn = s.get("function") or {}
        if not fn.get("name"):
            continue
        out.append({
            "name": fn["name"],
            "description": fn.get("description", ""),
            "input_schema": fn.get("parameters") or {"type": "object", "properties": {}},
        })
    return out


def _anthropic_messages(messages: list) -> tuple:
    """(system, messages) in Anthropic's split.

    Anthropic takes the system prompt as its own parameter rather than as a
    message with role=system, and it rejects an empty content string — a tool
    result that came back blank has to become something."""
    system = ""
    out = []
    for m in messages:
        role = m.get("role")
        content = m.get("content")
        if role == "system":
            system = content or ""
            continue
        if role == "tool":
            out.append({
                "role": "user",
                "content": [{
                    "type": "tool_result",
                    "tool_use_id": m.get("tool_call_id") or "",
                    "content": (content or "(no output)"),
                }],
            })
            continue
        if role == "assistant" and m.get("tool_calls"):
            blocks = []
            if content:
                blocks.append({"type": "text", "text": content})
            for tc in m["tool_calls"]:
                fn = tc.get("function") or {}
                try:
                    args = json.loads(fn.get("arguments") or "{}")
                except json.JSONDecodeError:
                    args = {}
                blocks.append({
                    "type": "tool_use",
                    "id": tc.get("id") or "",
                    "name": fn.get("name") or "",
                    "input": args,
                })
            out.append({"role": "assistant", "content": blocks})
            continue
        out.append({"role": role or "user", "content": content or "(empty)"})
    return system, out


def anthropic_stream(messages: list, tools: list | None):
    """Yield llm.py's chunk shape from an Anthropic streamed response.

    Streaming is not a nicety here: `max_tokens` is in the thousands and a
    non-streamed request that large is a request that can hit an HTTP timeout
    with the whole answer still on the wire.
    """
    client = _anthropic_client()
    system, msgs = _anthropic_messages(messages)

    kwargs = dict(
        model=cfg.ANTHROPIC_MODEL,
        max_tokens=cfg.ANTHROPIC_MAX_TOKENS,
        messages=msgs,
        # ⚠ Adaptive, and SUMMARISED. The default on this model family is
        # `omitted`, which streams thinking blocks with empty text — on a chat
        # window that reads as a long pause before anything appears.
        thinking={"type": "adaptive", "display": "summarized"},
        output_config={"effort": cfg.ANTHROPIC_EFFORT},
    )
    if system:
        kwargs["system"] = system
    if tools:
        kwargs["tools"] = anthropic_tools(tools)

    idx = 0
    with client.messages.stream(**kwargs) as stream:
        for event in stream:
            if event.type == "text":
                yield {"choices": [{"delta": {"content": event.text},
                                    "finish_reason": None}]}
        final = stream.get_final_message()

    # ⚠ A REFUSAL IS AN HTTP 200. `stop_reason` is the only thing that says so,
    # and reading `content` without checking it hands the user an empty answer
    # with no explanation.
    if getattr(final, "stop_reason", "") == "refusal":
        why = ""
        det = getattr(final, "stop_details", None)
        if det is not None:
            why = f" ({getattr(det, 'category', '') or 'no category'})"
        yield {"choices": [{"delta": {"content":
                f"\n[the model declined this request{why}]\n"},
                "finish_reason": "stop"}]}
        return

    # The tool calls, once, complete. The provider streams them as partial JSON
    # and llm.py is happy to take them whole — it accumulates by index either
    # way, and a single complete delta cannot be a half-parsed argument.
    for block in getattr(final, "content", []) or []:
        if getattr(block, "type", "") != "tool_use":
            continue
        yield {"choices": [{"delta": {"tool_calls": [{
            "index": idx,
            "id": block.id,
            "type": "function",
            "function": {"name": block.name,
                         "arguments": json.dumps(block.input or {})},
        }]}, "finish_reason": None}]}
        idx += 1
    if idx:
        yield {"choices": [{"delta": {}, "finish_reason": "tool_calls"}]}


# ── OpenAI ──────────────────────────────────────────────────────────────────

def openai_stream(messages: list, tools: list | None, tool_choice: str = "auto"):
    """The same chat-completions call the ollama backend makes, with a key.

    Not streamed, and faked as one chunk, for the reason the ollama path is not:
    the tool-call loop wants whole arguments, and a chat window that renders a
    paragraph at a time is not what makes this feel slow — the model is.
    """
    key = cfg.api_key("openai")
    if not key:
        raise CloudError(
            "no OpenAI key — write one with `vibe key openai` "
            f"(it is stored in {cfg.key_path('openai')}, mode 0600)"
        )
    body = {
        "model": cfg.OPENAI_MODEL,
        "messages": [m for m in messages],
        "stream": False,
        "temperature": cfg.TEMPERATURE,
        "max_tokens": cfg.MAX_TOKENS,
    }
    if tools:
        body["tools"] = tools
        body["tool_choice"] = tool_choice
    req = urllib.request.Request(
        f"{cfg.OPENAI_HOST}/v1/chat/completions",
        data=json.dumps(body).encode(),
        headers={"Content-Type": "application/json",
                 "Authorization": f"Bearer {key}"},
    )
    try:
        with urllib.request.urlopen(req, timeout=cfg.OPENAI_TIMEOUT) as resp:
            result = json.loads(resp.read())
    except urllib.error.HTTPError as e:
        detail = ""
        try:
            detail = json.loads(e.read()).get("error", {}).get("message", "")
        except Exception:
            pass
        raise CloudError(f"OpenAI refused the request ({e.code}): {detail or e.reason}")
    except Exception as e:
        raise CloudError(f"OpenAI is not reachable: {e}")

    choice = (result.get("choices") or [{}])[0]
    msg = choice.get("message") or {}
    delta = {}
    if msg.get("content"):
        delta["content"] = msg["content"]
    if msg.get("tool_calls"):
        calls = []
        for i, tc in enumerate(msg["tool_calls"]):
            fn = tc.get("function") or {}
            calls.append({"index": i, "id": tc.get("id") or f"call_{i}",
                          "type": "function",
                          "function": {"name": fn.get("name") or "",
                                       "arguments": fn.get("arguments") or "{}"}})
        delta["tool_calls"] = calls
    yield {"choices": [{"delta": delta,
                        "finish_reason": choice.get("finish_reason")}]}
