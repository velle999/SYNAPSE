# synapse-llama-system

A package with no files. It satisfies the `synapse-llama` dependency that
`synapd` declares, using the llama.cpp your distribution already ships.

```bash
git clone https://github.com/velle999/synapse-llama-system
cd synapse-llama-system && makepkg -si
```

That pulls in `llama-cpp` and `ggml`, and claims the name synapd asks for.

## Why it exists

On SynapseOS, `synapse-llama` is a llama.cpp tree built by the ISO build with a
particular backend recipe compiled in. There is no such tree on a machine that
is not being built into an ISO, so that package cannot be produced anywhere
else — which left synapd installable only on SynapseOS.

Arch ships the same libraries: `libllama.so` and `llama.h` from `llama-cpp`,
`libggml` from `ggml`, and the GPU backends as separate packages that ggml
loads out of `/usr/lib/ggml` at run time. synapd builds and links against them
unmodified. So the only thing missing was the name, and this is the name.

## GPU offload

CPU by default. For anything faster, install a backend beside it:

| package | for |
|---|---|
| `ggml-cuda` | NVIDIA |
| `ggml-vulkan` | AMD and Intel, and NVIDIA without CUDA |
| `ggml-hip` | AMD through ROCm |
| `ggml-blas` | faster prompt processing, still on the CPU |

Nothing needs reconfiguring — ggml discovers what is installed.

## ⛔ Not for SynapseOS

It **conflicts** with the real `synapse-llama`, deliberately: both would own
`/usr/lib/libllama.so`. On a SynapseOS machine the backend-specific package is
the right one and this is the wrong one, which is why it is in no build list,
on no ISO, and in no installer.
