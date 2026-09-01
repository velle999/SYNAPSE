# synsh

A shell that takes plain language as well as commands. Type a command and it
runs it; type a sentence and it works out what you meant, shows you the
command it would run, and asks before running it.

Without an AI backend it is an ordinary interactive shell — the natural
language half switches off and everything else stays.

It understands and answers in fourteen languages: English, Deutsch, Français,
Español, Português, Italiano, Nederlands, Polski, Русский, 日本語, 中文, 한국어,
हिन्दी and العربية.

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

## Language

synsh follows the session — `LC_ALL`, then `LC_MESSAGES`, then `LANG` — which
on SynapseOS is what you picked when the image booted, on the same screen that
put the installer into that language. Nothing to configure.

```bash
syn lang            # what it is speaking, and what it could speak
syn lang de         # for this session
synsh --lang de     # for one run
```

`set language de` in `~/.synshrc` makes it durable, and `SYNSH_LANG=de` in the
environment sets it for one shell without moving the rest of the session's
locale.

Two things it changes and one it does not. It changes what synsh **says** — its
messages, its help, and the language the AI writes its one-line explanations
in. It does not change what synsh **runs**: `ls` is `ls` in every language, and
so is every exit code.

Requests are understood in all fourteen whatever the setting says, so somebody
working in German who types `list files` out of habit still gets it — and
accents are optional, in both directions:

```
wie spät ist es      wie spaet ist es      WIE SPÄT IST ES
¿qué hora es?        que hora es
今何時ですか          现在几点               кто́рый час
```

## Being a shell

The everyday shell things work, and each of these is asserted by
`tests/shell_test.sh`:

- pipelines of any length, at any volume — every stage runs at once;
- `$VAR`, `${VAR}`, `$?`, `$$`, `$(command)` and backticks, with the
  unquoted results split into separate arguments;
- globs — `*`, `?`, `[...]` — left alone when nothing matches;
- redirection, attached or spaced: `>`, `>>`, `<`, `2>`, `2>&1`, `&>`;
- `;`, `&&`, `||` and `&`, with `jobs` listing what is in the background;
- `NAME=value`, on its own or in front of a command — where it applies to that
  command only, so `LANG=C ls` leaves the shell where it was;
- quoting, `~`, and aliases that may expand into built-ins.

What it deliberately does not have: arithmetic `$(( ))`, `${x:-y}`, arrays,
process substitution, and `for`/`while`. synsh is a natural-language front end
that also has to be a competent everyday shell, not a second bash — and
`bash -c` is an arm's length away.

## Being driven by something else

The classification is available on its own, so another program can ask what a
line is before deciding what to do with it — none of these run anything:

```bash
synsh --classify 'delete the logs'    # shell, builtin, ai or hybrid
synsh --intent-check 'open downloads' # exit 0 if synsh answers it itself
synsh --toolinfo                      # the tools resolved from $PATH
```

`--classify` and `--intent-check` answer the same for a given line whatever
language the caller's environment is in: the phrase tables hold every language
at once and matching never depends on which one synsh was told to speak.

## The AI backend

Translation goes to `synapd`, the SynapseOS AI daemon, over a local socket —
no request leaves the machine and no key is needed. Without `synapd`
installed and running, synsh says so once and carries on as a shell.
