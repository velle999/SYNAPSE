# vibe

A local AI coding assistant: an agentic read/write/edit/shell loop that talks
to `synapd` over its socket, so nothing leaves the machine and there is no API
key.

```bash
vibe                      # the interactive session
```

Inside it: `/reset`, `/save`, `/memory`, `/think`, `/nothink`, `/tokens`,
`/sys`, `/gpu`, `/ps`, `/help`. `/save` before `/reset` keeps the session's
memory; Alt+Enter inserts a newline instead of sending.

## What it can do to your files

It reads, writes, edits and runs commands. Writes ask first. Commands that
touch the filesystem run inside `syn-confine`, so the sandbox is
kernel-enforced rather than a promise the model makes about itself.

⚠ **The model is the weak part, not the loop.** A small model will confidently
open the wrong folder and report success. `syn-model status` says which one is
loaded; the difference between the 7B and the 0.5B is the difference between
following an instruction and appearing to.

## Requires

`synapd` for inference and `syn-confine` for the sandbox. On a machine that is
not SynapseOS, install
[`synapse-llama-system`](https://github.com/velle999/synapse-llama-system)
first so synapd's llama.cpp dependency resolves to the distribution's own.
