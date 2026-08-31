# synsh

A shell that takes plain English as well as commands. Type a command and it
runs it; type a sentence and it works out what you meant, shows you the
command it would run, and asks before running it.

Without an AI backend it is an ordinary interactive shell — the natural
language half switches off and everything else stays.

## Using it

```bash
synsh                        # interactive
synsh -c 'ls -la'            # one command
synsh -c 'how big is my home folder' --intent
```

Inside a session:

- a line that looks like a command runs as one;
- a line that looks like English is translated, shown, and confirmed;
- `?` at the start of a line always asks the AI;
- `!` at the start always forces shell.

`--no-ai` turns translation off for a run, and `--no-confirm` runs a
suggestion without the prompt.

## Being driven by something else

The classification is available on its own, so another program can ask what a
line is before deciding what to do with it — none of these run anything:

```bash
synsh --classify 'delete the logs'    # shell, builtin, ai or hybrid
synsh --intent-check 'open downloads' # exit 0 if synsh answers it itself
synsh --toolinfo                      # the tools resolved from $PATH
```

## The AI backend

Translation goes to `synapd`, the SynapseOS AI daemon, over a local socket —
no request leaves the machine and no key is needed. Without `synapd`
installed and running, synsh says so once and carries on as a shell.
