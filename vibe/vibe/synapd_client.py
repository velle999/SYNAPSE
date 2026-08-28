"""
synapd_client — talk to SynapseOS's synapd AI daemon.

synapd is SynapseOS's kernel-native inference daemon (a persistent llama.cpp
backend). It speaks a small binary framed protocol over a unix socket
(/run/synapd/synapd.sock), or over the TCP bridge (synapd-bridge.socket, port
11435) when reached from another host. There is NO token streaming and NO
structured tool-calling — one QUERY in, one full reply out. Vibe therefore
drives synapd through its *text* tool-call path (<tool_call>…</tool_call>),
with the tool protocol described in the prompt.

Wire format mirrors SYNAPSE/synapd/include/synapd.h. The query flags added in
synapd pkgrel 14 let an agentic client own the whole prompt and lift the
default 512-token cap:

    SYN_QF_RAW (0x8000)         skip the built-in Synapse persona + rolling OS
                               system-context injection + context store
    SYN_QF_TOKENS_MASK (0x7FFF) max output tokens (0 = daemon default 512)

Legacy clients send flags=0; this module always sets SYN_QF_RAW because vibe
supplies its own coding system prompt.
"""
import os
import socket
import struct

# header: magic, ver, msg_type, flags, payload_len, request_id, client_pid, ts
_HDR = struct.Struct("<IBBHIIIQ")
_MAGIC = 0x53594E41          # "SYNA"
_VER = 1
_MSG_QUERY = 0x01
_MSG_STATUS = 0x06
_MSG_RELOAD = 0x07
_MSG_ERROR = 0xFF

SYN_QF_RAW = 0x8000
SYN_QF_TOKENS_MASK = 0x7FFF

DEFAULT_SOCKET = "/run/synapd/synapd.sock"
DEFAULT_PORT = 11435


class SynapdError(RuntimeError):
    pass


def _connect(socket_path: str, host: str, port: int, timeout: float):
    """Unix socket locally, TCP bridge when a host is set. Same wire on both."""
    if host:
        s = socket.create_connection((host, port), timeout=timeout)
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        return s
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect(socket_path)
    return s


def _recv_exact(sock, n: int) -> bytes:
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise SynapdError("synapd: connection closed mid-message")
        buf.extend(chunk)
    return bytes(buf)


def ping(socket_path: str = DEFAULT_SOCKET, host: str = "",
         port: int = DEFAULT_PORT, timeout: float = 5.0) -> None:
    """Raise SynapdError if synapd is unreachable. A successful connect is enough."""
    try:
        _connect(socket_path, host, port, timeout).close()
    except OSError as e:
        target = f"{host}:{port}" if host else socket_path
        raise SynapdError(f"synapd unreachable at {target}: {e}") from e


def query(prompt: str, max_tokens: int = 2048,
          socket_path: str = DEFAULT_SOCKET, host: str = "",
          port: int = DEFAULT_PORT, timeout: float = 600.0) -> str:
    """Send one RAW QUERY and return the full reply text.

    max_tokens is clamped to the wire's 15-bit field; synapd further clamps to
    its context window. Raises SynapdError on transport failure or a daemon
    SYN_MSG_ERROR reply.
    """
    budget = max(0, min(int(max_tokens), SYN_QF_TOKENS_MASK))
    flags = SYN_QF_RAW | budget

    payload = prompt.encode("utf-8") + b"\x00"   # synapd reads a C string
    header = _HDR.pack(_MAGIC, _VER, _MSG_QUERY, flags,
                       len(payload), 1, os.getpid(), 0)

    sock = None
    try:
        sock = _connect(socket_path, host, port, timeout)
        sock.sendall(header + payload)

        rh = _recv_exact(sock, _HDR.size)
        magic, _ver, msg_type, _flags, plen, _rid, _pid, _ts = _HDR.unpack(rh)
        if magic != _MAGIC:
            raise SynapdError("synapd: bad response magic")
        body = _recv_exact(sock, plen) if plen else b""
        text = body.split(b"\x00", 1)[0].decode("utf-8", "replace")
        if msg_type == _MSG_ERROR:
            raise SynapdError(f"synapd error: {text}")
        return text
    except OSError as e:
        target = f"{host}:{port}" if host else socket_path
        raise SynapdError(f"synapd query failed at {target}: {e}") from e
    finally:
        if sock:
            try:
                sock.close()
            except OSError:
                pass


def tool_protocol_text(tool_schemas: list[dict]) -> str:
    """Render OpenAI-style tool schemas as a text protocol the model can follow.

    synapd has no structured tool-calling, so the tools and the emit convention
    have to live in the prompt. Vibe's text parser (_TOOL_CALL_RE) reads back the
    <tool_call>{"name":..,"arguments":{..}}</tool_call> blocks this describes.
    """
    lines = [
        "## Tools",
        "You cannot call tools through an API. To use a tool, emit EXACTLY:",
        '<tool_call>{"name": "<tool>", "arguments": {<args>}}</tool_call>',
        "Emit the block and nothing after it; the result comes back as "
        "'Tool result:' and you continue.",
        "",
        # ⛔ THE THREE WAYS A SMALL MODEL AVOIDS ACTING, said in advance. Every
        # one of them was produced by the shipped local model for "open
        # downloads", and every one of them ran nothing while reading as
        # success: it announced the tool it was about to use, it asked to be
        # allowed to use it, and it wrote its own 'Tool result:' and answered
        # from the fiction. The rule they all break is the same one.
        "⚠ THE BLOCK IS THE ACTION, AND TEXT IS NOT. Naming a tool, saying you "
        "will use one, or describing what it would return does nothing at all.",
        "- Do not ask permission. The tools that need it are stopped and "
        "confirmed for you; asking merely ends your turn without acting.",
        "- Never write 'Tool result:' yourself. Only the real result is true, "
        "and it arrives after your block, never inside it.",
        "- To OPEN a folder for the user, that is desktop_open. list_dir reads "
        "the names and opens nothing.",
        "",
        "Available tools:",
        "",
    ]
    for schema in tool_schemas:
        fn = schema.get("function", schema)
        name = fn.get("name", "?")
        desc = (fn.get("description", "") or "").strip().splitlines()
        desc1 = desc[0] if desc else ""
        params = fn.get("parameters", {}).get("properties", {})
        required = set(fn.get("parameters", {}).get("required", []))
        args = ", ".join(
            f"{p}{'*' if p in required else ''}: {spec.get('type', 'any')}"
            for p, spec in params.items()
        )
        lines.append(f"- {name}({args}) — {desc1}")
    lines.append("")
    lines.append("(* = required argument)")
    return "\n".join(lines)


def status(socket_path: str = DEFAULT_SOCKET, host: str = "",
           port: int = DEFAULT_PORT, timeout: float = 10.0) -> dict:
    """What synapd is running right now, as a dict.

    ⛔ THIS IS HOW THE PROMPT LEARNS WHICH TEMPLATE TO USE, and not knowing it
    is the bug this function exists for. The status line carries

        model_name="mistralai_mistral-7b-instruct-v0.2" format="[INST]"

    and `format` is not a guess: inference.c derives it by RENDERING the
    model's own chat template around a marker and keeping whatever came out in
    front (template_probe). Ignoring it and emitting `<|user|>` at a Mistral —
    which is what this client did — feeds the model turn markers it has never
    seen. It does not error. It free-associates a transcript and invents its
    own markers, and what reaches the window is `◁user▷` and `◁assistant▷`
    around answers to questions nobody asked.
    """
    s = _connect(socket_path, host, port, timeout)
    try:
        hdr = _HDR.pack(_MAGIC, _VER, _MSG_STATUS, 0, 0, 1, os.getpid(), 0)
        s.sendall(hdr)
        head = _recv_exact(s, _HDR.size)
        _, _, _, _, plen, _, _, _ = _HDR.unpack(head)
        body = _recv_exact(s, plen).decode("utf-8", "replace") if plen else ""
    finally:
        s.close()

    out = {}
    # `key=value` and `key="value with spaces"`, which is what the line mixes.
    import re
    for m in re.finditer(r'(\w+)=("([^"]*)"|\S+)', body):
        out[m.group(1)] = m.group(3) if m.group(3) is not None else m.group(2)
    return out


def reload_model(name: str, socket_path: str = DEFAULT_SOCKET, host: str = "",
                 port: int = DEFAULT_PORT, timeout: float = 30.0) -> str:
    """Ask synapd to load a different model, by bare filename.

    ⚠ NO PRIVILEGE IS NEEDED AND NONE IS TAKEN. synapd resolves the name inside
    its own model directory and remembers the choice itself, in
    /var/lib/synapd/model.selected, only after the load succeeds — which is why
    switching a model is a socket message and not a sudoers rule. A name that
    escapes the directory is refused by the daemon, not by this.
    """
    payload = name.encode() + b"\0"
    s = _connect(socket_path, host, port, timeout)
    try:
        s.sendall(_HDR.pack(_MAGIC, _VER, _MSG_RELOAD, 0, len(payload), 1,
                            os.getpid(), 0) + payload)
        head = _recv_exact(s, _HDR.size)
        _, _, mt, _, plen, _, _, _ = _HDR.unpack(head)
        body = _recv_exact(s, plen).decode("utf-8", "replace") if plen else ""
    finally:
        s.close()
    if mt == _MSG_ERROR:
        raise SynapdError(f"synapd refused the switch: {body}")
    return body


# ── The turn markers, per family ────────────────────────────────────────────
#
# ⚠ THE KEY IS synapd's `format` STRING VERBATIM, so this table is indexed by
# the thing the daemon actually says rather than by a model name this would
# have to keep in step. An unknown format falls through to the plain
# transcript, which no model is trained on but every model can read.
_TEMPLATES = {
    # Mistral. ⚠ THERE IS NO SYSTEM ROLE — Mistral 7B v0.2's template REJECTS
    # one outright, and synapd's own apply_chat_template folds it into the
    # first user turn for exactly that reason. So does this.
    "[INST]": {
        "system_into_first_user": True,
        "user":      ("[INST] ", " [/INST]"),
        "assistant": ("", "</s>"),
    },
    # ChatML — Qwen, and most things that are not Mistral or Llama 3.
    "<|im_start|>user": {
        "system_into_first_user": False,
        "system":    ("<|im_start|>system\n", "<|im_end|>\n"),
        "user":      ("<|im_start|>user\n", "<|im_end|>\n"),
        "assistant": ("<|im_start|>assistant\n", "<|im_end|>\n"),
        "open":      "<|im_start|>assistant\n",
    },
    "<|im_start|>": {
        "system_into_first_user": False,
        "system":    ("<|im_start|>system\n", "<|im_end|>\n"),
        "user":      ("<|im_start|>user\n", "<|im_end|>\n"),
        "assistant": ("<|im_start|>assistant\n", "<|im_end|>\n"),
        "open":      "<|im_start|>assistant\n",
    },
    # Zephyr/TinyLlama — the one this client used to emit at everything.
    "<|user|>": {
        "system_into_first_user": False,
        "system":    ("<|system|>\n", "</s>\n"),
        "user":      ("<|user|>\n", "</s>\n"),
        "assistant": ("<|assistant|>\n", "</s>\n"),
        "open":      "<|assistant|>\n",
    },
}

# Every turn marker any of the families above can produce, plus the ones models
# hallucinate when they have been fed somebody else's. Used to CUT a reply that
# has started writing the next turn itself.
#
# ⛔ THIS GUARD IS NOT REDUNDANT WITH GETTING THE TEMPLATE RIGHT. A small model
# under a long tool transcript will still occasionally open a turn of its own,
# and the visible result is the model answering a question it invented for the
# user. Cheap to cut, impossible to explain if left in.
_TURN_MARKERS = (
    "<|im_start|>", "<|im_end|>", "<|user|>", "<|system|>", "<|assistant|>",
    "[INST]", "[/INST]", "</s>", "<|eot_id|>", "<|start_header_id|>",
    "\u25c1user\u25b7", "\u25c1assistant\u25b7", "\u25c1system\u25b7",
    "\nUser:", "\nHuman:", "\nAssistant:",
)


def trim_hallucinated_turn(text: str) -> str:
    """Cut a reply at the point it starts writing somebody else's turn."""
    cut = len(text)
    for m in _TURN_MARKERS:
        i = text.find(m)
        if 0 <= i < cut:
            cut = i
    return text[:cut].rstrip()


def _render_tool_calls(content: str, msg: dict) -> str:
    """An assistant turn, with its tool calls written the way it must write them."""
    out = content
    for tc in msg.get("tool_calls") or []:
        fn = tc.get("function", {})
        out += (f'\n<tool_call>{{"name": "{fn.get("name", "")}", '
                f'"arguments": {fn.get("arguments", "{}")}}}</tool_call>')
    return out.strip()


# ⚠ THE LAST USER TURN GETS A ONE-LINE REMINDER, and it is not padding. The
# tool protocol lands in the SYSTEM block — which Mistral folds into the first
# user turn — so by the time a conversation has a few tool results in it, the
# rules are a couple of thousand tokens behind the question. A 7B loses them
# there: it stops emitting <tool_call> and starts describing what it would do,
# or it greps for the speed of light. One line at the point of asking fixes
# both, and it states BOTH branches deliberately — a reminder that only pushed
# toward tools is how "answer the question" becomes a grep.
# ⚠ THIS RIDES ON THE LAST USER TURN, which is why it is worth its tokens: on
# a 7B the system block is thousands of tokens behind by message ten, and this
# is the sentence directly before the answer. It says what to do and, because
# announcing a tool is the failure that costs a whole turn, what not to.
_TOOL_NUDGE = ("\n\n(If this needs a file, a command, or something on this "
               "machine, emit the <tool_call> block now, as your whole reply "
               "— do not announce it and do not ask first. If it is general "
               "knowledge or writing, just answer it.)")


def flatten_messages(messages: list[dict], tool_schemas: list[dict] | None,
                     fmt: str = "") -> str:
    """Flatten an OpenAI-style message list into one prompt for synapd.

    ⛔ IN THE TEMPLATE THE LOADED MODEL ACTUALLY USES. synapd sends a
    SYN_QF_RAW prompt verbatim, so the client owns the whole template — and for
    a year this client owned it by emitting `<|system|>/<|user|>/<|assistant|>`
    at every model regardless. At a Mistral, which is what SynapseOS ships,
    those tokens mean nothing: the model reads the lot as prose, keeps writing,
    and invents turn markers of its own. `◁user▷` and `◁assistant▷` appearing
    in the chat window is that, and so is the assistant answering a question
    the user never typed.

    `fmt` is synapd's own `format` field from status() — "[INST]",
    "<|im_start|>user", "<|user|>" — derived by rendering the model's real
    template, never guessed from its name. An empty or unrecognised value falls
    through to a plain transcript: no model is trained on it, but every model
    can read it, and it contains no tokens to be misread as somebody else's.

    Assistant tool_calls are rendered back as <tool_call> blocks so the model
    sees the convention it must reuse. No template here has a tool role, so a
    tool result is delivered as the next user turn.
    """
    t = _TEMPLATES.get(fmt or "")

    last_user = -1
    for i, m in enumerate(messages):
        if m.get("role") in ("user", "tool"):
            last_user = i

    # ── The plain transcript, for a template nobody here knows ──────────
    if t is None:
        parts = []
        for i, msg in enumerate(messages):
            role = msg.get("role", "user")
            content = (msg.get("content") or "").strip()
            if role == "system":
                block = content
                if tool_schemas:
                    block = f"{block}\n\n{tool_protocol_text(tool_schemas)}"
                parts.append(block)
            elif role in ("user", "tool"):
                body = f"Tool result: {content}" if role == "tool" else content
                if tool_schemas and i == last_user:
                    body += _TOOL_NUDGE
                parts.append(f"User: {body}")
            elif role == "assistant":
                parts.append(f"Assistant: {_render_tool_calls(content, msg)}")
        parts.append("Assistant:")
        return "\n\n".join(p for p in parts if p)

    # ── A real template ─────────────────────────────────────────────────
    system = ""
    for msg in messages:
        if msg.get("role") == "system":
            system = (msg.get("content") or "").strip()
            break
    if tool_schemas:
        system = (system + "\n\n" + tool_protocol_text(tool_schemas)).strip()

    parts = []
    pending_system = system

    if not t["system_into_first_user"] and system:
        o, c = t["system"]
        parts.append(f"{o}{system}{c}")
        pending_system = ""

    for i, msg in enumerate(messages):
        role = msg.get("role", "user")
        content = (msg.get("content") or "").strip()
        if role == "system":
            continue
        if role in ("user", "tool"):
            body = f"Tool result: {content}" if role == "tool" else content
            if tool_schemas and i == last_user:
                body += _TOOL_NUDGE
            # ⚠ Mistral has no system role at all, so the system prompt rides
            # into the FIRST user turn — which is what synapd's own
            # apply_chat_template does, and what v0.2's template demands.
            if pending_system:
                body = f"{pending_system}\n\n{body}"
                pending_system = ""
            o, c = t["user"]
            parts.append(f"{o}{body}{c}")
        elif role == "assistant":
            o, c = t["assistant"]
            parts.append(f"{o}{_render_tool_calls(content, msg)}{c}")

    # A conversation that is nothing but a system prompt still has to ask.
    if pending_system:
        o, c = t["user"]
        parts.append(f"{o}{pending_system}{c}")

    # …and the opening of the turn the model is to write. Mistral needs none —
    # `[/INST]` already ends the user turn and the reply follows it.
    parts.append(t.get("open", ""))
    return "".join(p for p in parts if p)
