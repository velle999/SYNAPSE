"""Can the loaded model actually drive the assistant?

⛔ THE QUESTION THIS ANSWERS IS NOT "IS THE MODEL GOOD". It is narrower and it
is the one that decides whether the assistant works at all: does this model
reach for a tool when the request is about THIS MACHINE, and does it keep its
hands off one when the request is a fact or a piece of writing? A model that
fails the first half describes what you could do instead of doing it. A model
that fails the second greps for the speed of light. Both were seen on the
model SynapseOS shipped, and neither is fixable from the prompt.

Run it, switch the model (`model_bench.py --switch <file.gguf>`), run it again.
The numbers are per-model and only comparable to each other.

Usage:
  model_bench.py                    bench whatever synapd has loaded
  model_bench.py --switch NAME      switch synapd to NAME first, then bench
"""
import re
import sys
import time

sys.path.insert(0, __file__.rsplit("/", 2)[0])

from vibe import synapd_client as sc
import vibe.config as cfg
cfg.BACKEND = "synapd"

# (prompt, wants_tool). `wants_tool` is what a working assistant must DO, not
# what would be nice — every TOOL row is about this machine and cannot be
# answered from knowledge, and every NONE row can be answered without touching
# anything and has no business reading a file.
CASES = [
    ("list the files in /etc/synapd",              True),
    ("what is in the file /etc/hostname",          True),
    ("how much disk space is free on this machine", True),
    ("open the control panel",                     True),
    ("move the bar to the bottom of the screen",   True),
    ("what is the speed of light",                 False),
    ("who wrote the novel Dune",                   False),
    ("write a two-line haiku about a cat",         False),
]

TOOL_RE = re.compile(r"<tool_call>\s*\{", re.I)


def main() -> int:
    argv = sys.argv[1:]
    if argv and argv[0] == "--switch":
        if len(argv) < 2:
            print("--switch needs a model filename"); return 2
        print(f"switching synapd to {argv[1]} …")
        sc.reload_model(argv[1])
        for _ in range(120):
            st = sc.status()
            if st.get("model") == "loaded" and argv[1].startswith(
                    st.get("model_file", "\0")[:4]):
                break
            time.sleep(1)

    st = sc.status()
    print(f"model  : {st.get('model_name')}")
    print(f"file   : {st.get('model_file')}")
    print(f"format : {st.get('format')}   ctx={st.get('ctx_window')}")
    print()

    from vibe.llm import SYSTEM_PROMPT
    from vibe.tools import TOOL_SCHEMAS
    system = SYSTEM_PROMPT.format(cwd="/home/velle", memory_section="")

    right = 0
    total_s = 0.0
    for prompt, wants in CASES:
        msgs = [{"role": "system", "content": system},
                {"role": "user", "content": prompt}]
        flat = sc.flatten_messages(msgs, TOOL_SCHEMAS, st.get("format", ""))
        t0 = time.time()
        try:
            out = sc.query(flat, max_tokens=384, timeout=180)
        except Exception as e:
            out = f"[error: {e}]"
        dt = time.time() - t0
        total_s += dt
        out = sc.trim_hallucinated_turn(out)
        got = bool(TOOL_RE.search(out))
        ok = got == wants
        right += ok
        name = ""
        m = re.search(r'"name"\s*:\s*"([a-z_]+)"', out)
        if m:
            name = m.group(1)
        flag = "ok  " if ok else "MISS"
        want = "tool" if wants else "text"
        print(f"  {flag} want={want:4s} got={'tool' if got else 'text'}"
              f"{'/' + name if name else '':<16} {dt:5.1f}s  {prompt[:44]}")
        if not ok:
            print(f"        → {out.strip()[:150].replace(chr(10), ' ')}")

    print()
    print(f"  {right}/{len(CASES)} correct   {total_s:.0f}s total "
          f"({total_s / len(CASES):.1f}s a turn)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
