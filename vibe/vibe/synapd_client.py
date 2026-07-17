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
        "'Tool result:' and you continue. Available tools:",
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


def flatten_messages(messages: list[dict], tool_schemas: list[dict] | None) -> str:
    """Flatten an OpenAI-style message list into one prompt for synapd.

    Uses synapd's own <|system|>/<|user|>/<|assistant|> turn markers (synapd
    sends a SYN_QF_RAW prompt verbatim, so the client owns the whole template).
    Emitting the model's real turn tokens — rather than plain "User:/Assistant:"
    transcript lines — keeps the model from leaking template tokens and
    hallucinating extra turns, and lets it stop cleanly at end-of-turn.

    Assistant tool_calls are rendered back as <tool_call> blocks so the model
    sees the convention it must reuse. There is no tool role in this template,
    so a tool result is delivered as the next <|user|> turn. When tool_schemas
    is given, the text tool protocol is appended to the system block.
    """
    parts: list[str] = []
    for msg in messages:
        role = msg.get("role", "user")
        content = (msg.get("content") or "").strip()
        if role == "system":
            block = content
            if tool_schemas:
                block = f"{block}\n\n{tool_protocol_text(tool_schemas)}"
            parts.append(f"<|system|>\n{block}")
        elif role == "user":
            parts.append(f"<|user|>\n{content}")
        elif role == "assistant":
            rendered = content
            for tc in msg.get("tool_calls") or []:
                fn = tc.get("function", {})
                name = fn.get("name", "")
                arguments = fn.get("arguments", "{}")
                rendered += (
                    f'\n<tool_call>{{"name": "{name}", '
                    f'"arguments": {arguments}}}</tool_call>'
                )
            parts.append(f"<|assistant|>\n{rendered.strip()}")
        elif role == "tool":
            parts.append(f"<|user|>\nTool result: {content}")
    parts.append("<|assistant|>\n")
    return "\n".join(p for p in parts if p)
