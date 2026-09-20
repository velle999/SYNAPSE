# synapd

A local inference daemon: one persistent llama.cpp process behind a Unix
socket, so several programs share one loaded model instead of each paying the
load time and the memory for their own.

It speaks a **llama.cpp-compatible HTTP API**, so anything that already talks
to `llama-server` can talk to this.

## Running it

```bash
synapd --model /path/to/model.gguf -f     # foreground
synapd -g 999                             # offload every layer it can
synapd --context 8192 --threads 8
systemctl start synapd                    # the packaged unit
```

The socket is `/run/synapd/synapd.sock` — a **socket, not a port**. Nothing is
listening on the network unless you opt in: an HTTP proxy unit
(`synapd-http-proxy.socket`, :8080) exists for front ends that can only speak
to a port, and it is off until enabled.

## How fast is it here?

```bash
synapd-bench                        # the loaded model, on this machine
synapd-bench --host desktop.lan     # another box's daemon, over the bridge
synapd-bench --last                 # what it answered last; asks it nothing
synapd-bench --json                 # one line, for scripts
```

**The numbers come from the daemon, not from the clock out here.** synapd
reports what it actually did — tokens in, tokens out, and the time each half
took — so nothing has to guess at tokenisation or count the network as if it
were the model.

Two speeds, always separately, because they fail for different reasons:

| | what it is | what slow means |
|---|---|---|
| **prefill** | reading the prompt, in batches | not enough layers are on the GPU |
| **decode** | writing the answer, one token at a time | the weights are in the wrong memory — spilled to RAM, or a CPU build |

A single tokens-per-second over the whole turn averages those together and
hides whichever one is broken.

The third number is **wire**: wall clock minus what the daemon accounted for.
On a Unix socket it is milliseconds. Over the LAN bridge it is what the network
and the proxy add, and it is the number that decides whether a laptop answering
from another machine feels immediate — invisible to any benchmark run on the
daemon's own box.

⚠ A run that overlaps another client's request is **discarded, not averaged
in**: the status line reports the last request the daemon answered, whoever
asked it.

## What it is for

`synsh`, `vibe` and the desktop's AI panel all connect to it. That is the
point of a daemon rather than a library: the model is loaded once, and a
second program asking a question does not wait for a 4 GB load.

## The llama.cpp it links

synapd links `libllama` directly, and its dependency is `synapse-llama`.

- On **SynapseOS** that name is the backend-specific build the ISO stages —
  CUDA or Vulkan, compiled with the right recipe for the machine.
- **Anywhere else**, install
  [`synapse-llama-system`](https://github.com/velle999/synapse-llama-system),
  which satisfies the same name using the distribution's own `llama-cpp` and
  `ggml` packages. Add `ggml-cuda` or `ggml-vulkan` for GPU offload; ggml
  loads whichever it finds at run time.

⚠ **Check that GPU offload actually happened.** A missing device node or a
driver mismatch does not fail — it falls back to the CPU and answers anyway,
correctly and slowly. `--debug` says how many layers were offloaded.
