# Vibe Code

A local AI coding assistant. Runs entirely on your machine — no API keys, no cloud.

Supports two backends: **ollama** (recommended, easy model management) and **llama-cpp** (direct GGUF, full CUDA control).

## On SynapseOS

`vibe` is the desktop assistant as well as the coding one. The bar has a speech
bubble on it (Assistant); pressing it opens the chat window, and pressing it
again closes it. `vibe gui` opens the same window from anywhere, `vibe` on its
own is still the terminal REPL, and both are the same assistant — one
conversation loop, one tool set, one set of confirmations.

**It can act on the desktop.** Alongside reading and writing files and running
commands, it can open a URL, a folder — including your own by their plain
names, "downloads", "pictures", "home" — an application or one of the desktop's
own panels (`desktop_open`), report what this machine actually is
(`system_info`), run any of the compositor's actions
(`desktop_action`), and change a desktop setting and apply it live —
`bar_edge`, `dock_edge`, the theme, the wallpaper (`desktop_setting`). Anything
that WRITES asks first, and anything that only opens something does not.

**Everyday requests never reach a model.** "what time is it", "open youtube",
"is firefox installed" are answered by `synsh`, which already answers them from
a table in milliseconds. synsh is asked whether it claims a line — the list
lives there, not here, so an intent added to synsh reaches this with nothing
here edited.

**Neither do plain desktop requests.** "open downloads", "what's in my
documents", "lock the screen", "move the bar to the bottom" — the assistant
resolves the folder, the panel or the verb and does it. No model is loaded, no
tokens are generated, and the answer is the tool's own words. The header says
`auto · direct` when that is what happened.

```bash
vibe intents                       # what it answers directly
vibe intents "open my downloads"   # what a line would do — and does nothing
```

It covers opening (a folder by its plain name, an app, a panel, a path, a URL),
reading a folder, the compositor's verbs by the phrases people use for them,
the bar and dock settings, and what this machine is. Anything that writes still
asks first: the line is *does it write*, and it does not move because a model
was not involved.

**And it never guesses at the hardware.** "pc stats", "what are my specs",
"how much ram do i have" are read off this machine — /proc, the DMI tables,
`lspci`, `nvidia-smi` — and a field that cannot be read is left out rather than
filled in (`system_info`). It is the same rule as the folder listing: a
question this machine can answer and a model cannot is not a question to put
to a model.

⚠ **It claims whole lines and nothing else.** `open`, `run`, `list` and `lock`
are all real programs and real English, so "how do I open my downloads from a
script" is not "open downloads" and goes to the model, where it belongs. A verb
that matches but names nothing real on this machine is handed on too — nothing
is answered on a guess.

⚠ **Ask and Plan do not act.** In Ask you asked for an answer; in Plan you asked
what *would* be done. Auto and Agent carry a request out.

**Ask, Agent, Plan — picked for you, or chosen by hand.**

| mode | what it does |
|---|---|
| **Ask** | answers, and touches nothing — no tools at all |
| **Agent** | answers and does it, asking before anything is written |
| **Plan** | looks around with read-only tools and writes out the steps instead of taking them |
| **Auto** | the default: picks one of the three per message |

Auto is not a fourth behaviour — it resolves to one of the three and then acts
exactly as if you had chosen it. The header shows which (`auto · plan`), so a
routed turn is reviewable. The picker is a router over evidence the process
already has, not a second call to the model: asking the model which mode to use
costs a whole round trip before the first token, and it is wrong in exactly the
cases that matter.

**It is also a shell.** Type `ls -la` and it runs it — no model in the path, no
guess at what the output might have been. Whether a line is a command is
`synsh`'s judgement (`synsh --classify`), not a second opinion grown here, and
synsh's own prefixes work: `!` forces a command, `?` forces a question.

⛔ It always asks first. synsh calls "make me a sandwich" a command, because
`make` is a real program — no classifier gets that right from the words alone.
The answer is not a better classifier; it is showing you the command and
waiting.

**It speaks and it listens.** The 🔊 button reads answers aloud; the 🎤 button
takes one spoken line, shows you what it heard, and answers it. On the command
line that is `vibe voice say …`, `vibe voice listen`, `vibe voice status`.

⛔ Neither half is implemented here. chibi already ships a working voice stack —
piper for speech, faster-whisper for hearing, both with their models — and it
carries a year of things only learned by using it. vibe imports chibi's rather
than growing a second one, so there is one voice on the machine; `syn-speak`,
the desktop's screen reader, speaks through the same door. Without chibi,
speech falls back to espeak-ng and dictation says what is missing.

**It can answer to its name.** Turn on *wake* and say "Synapse, what's the
weather" — or "computer", which whisper transcribes correctly every time where a
product name is a coin toss.

⛔ It ships off, and it is the most serious switch here: armed, it leaves a
microphone open. Every utterance is transcribed **locally** by the model already
on disk, and a line that does not name the assistant is dropped — it reaches no
model, no log and no disk. Only a line that wakes it becomes a request, and then
it goes wherever your backend goes. On a cloud backend it says so out loud when
you arm it.

It is a user service (`vibe wake on`, or **Control panel ▸ Sound ▸ Answer to
its name**) rather than something the chat window owns — a hands-free assistant
you have to be hands-on to start is not one.

While it is on **the bar says so**: the assistant button turns into a microphone
in the warning colour. That is deliberate — the chat window can be closed or on
another workspace, and a disclosure nobody is looking at is not one.

A follow-up inside 22 seconds needs no name, and the window closes after four
turns that never name it. That last part is the whole defence against a room
with a television in it: a person says the name again now and then; a broadcast
holding a conversation with itself never does. The design is chibi's.

**The backend is switchable, including to a paid one.**

```bash
vibe provider            # what it is now
vibe provider anthropic  # or synapd (local, the default), ollama, openai
vibe key anthropic       # prompts, and stores it 0600 in ~/.config/synui/ai/
```

The local backend is `synapd`, the model already resident on the GPU — no
second model and no extra VRAM. `anthropic` uses the official SDK
(`python-anthropic`, an optdepend) with adaptive thinking and the tool-use
blocks; `openai` speaks its chat-completions endpoint. A key goes into the **system
keyring** where a desktop has one, and into `~/.config/synui/ai/<provider>.key`
at 0600 where it does not. The environment (`ANTHROPIC_API_KEY`,
`OPENAI_API_KEY`) beats both, for a single run. `vibe key <provider> --forget`
removes it from everywhere.

⛔ `secret-tool` **exits 0 when there is no keyring running** — it prints its
complaint to stderr and returns success. Every write is therefore verified by
reading it back, and only a value that comes back byte for byte counts as
stored. Believing the exit status would mean telling somebody their key was
saved and having no key at all.

⛔ **A local model will describe a tool instead of using it.** "I'll use the
`desktop_open` tool", "please confirm if you'd like me to proceed", and — the
one that reads as a lie — a `Tool result:` it wrote itself and then answered
from. None of them run anything, and all three end the turn looking like
success. Measured on "open downloads", with the folder already resolving
correctly: **2 runs in 8** reached the file manager.

That is why the ordinary desktop requests no longer go through a model at all
(above). For the ones that still do, vibe treats a turn that talks about a tool
as a turn that has not happened yet: it re-asks once, saying that only the block
runs and that the confirmation is not the model's to ask for. The rules are in
the prompt too, and a `<tool_call>` still counts when the model wraps it in a
JSON code fence or never closes the tag.

⚠ `/no_think` goes to a **Qwen and nothing else**. It is one model family's
control token; every other model reads it as the first two words of the
question.

## The companion — tasks, habits, goals, focus and markets

Folded in from **velle.ai**, whose personalities and productivity suite live
here rather than in a second assistant. There is one assistant on this desktop
and this is it.

```
vibe persona [list|<name>|show]     the voice — 7 of them, each with its own temperature
vibe todo [list|today|overdue]      tasks
vibe todo add <text> [--project P] [--prio 1-4] [--due YYYY-MM-DD] [--tags a,b]
vibe todo done|start|cancel|rm <id>
vibe habit [list] | add <name> | check|uncheck|rm <id>
vibe goal [list [done]] | add <title> | progress <id> <pct> | milestone <id> <t> | done <mid>
vibe pom [status] | start [task] [--min N] | stop | stats
vibe quant <ticker> …               price and indicators (the one that uses the network)
```

Every one of them is also a slash command in the chat window (`/todo add …`),
and the model has read tools for all of them plus gated write tools — the same
line as everywhere else in vibe: **does it write** asks first, reading does not.
`market_quote` asks too, for a different reason: it is the only tool that
**leaves the machine**.

**A persona is a voice, not a system prompt.** velle.ai's profiles replace the
whole prompt; here they are appended to it, so the tool rules, this machine's
facts and the rule about when *not* to reach for a tool all survive the
costume. The temperature comes with the voice, and the default persona changes
nothing at all.

**The focus timer is on the bar.** The running session is a row in the
companion database plus a deadline published to
`~/.config/synui/pomodoro.state`; the bar, the chat window and the CLI are three
readers of that one fact. It survives the chat window being closed — which a
timer owned by `vibe serve` could not — and the first reader to notice the
deadline pass closes the session and sends the notification, so it announces
exactly once. Stopping early does **not** count as a completed pomodoro, so the
day's total is what finished rather than what was started.

Data lives in `~/.local/share/vibe/companion.db` — velle.ai's schemas, column
for column, so an existing `companion.db` opens here.

## Features

- Agentic tool-call loop: reads files, writes files, edits files, runs bash commands, globs, greps
- Reports what this machine actually is — CPU, memory, GPUs, drives — read off the machine, never guessed
- System control: GPU stats, process management, systemd services, network info
- Qwen3/Qwen3.5 thinking mode (chain-of-thought reasoning, toggleable at runtime)
- Streaming output with Rich UI
- Session memory via `/save` → `.vibe/memory.md`
- Context usage tracking with visual bar
- Opens GUI apps and file managers directly

## Requirements

- Arch Linux (setup script uses pacman)
- NVIDIA GPU with CUDA support
- Python 3.12+
- [ollama](https://ollama.com) (recommended) or llama-cpp-python with CUDA

## Setup

### Ollama (recommended)

```bash
./setup.sh            # installs ollama, pulls qwen3:14b, creates venv
./vibe.sh
```

Other models you can pull:
```bash
ollama pull qwen3.5:9b         # smaller, faster
ollama pull qwen3:30b-a3b      # larger MoE, needs /offload for <16GB VRAM
ollama pull qwen2.5-coder:14b  # code-specialized
```

Switch models in `vibe/config.py`:
```python
OLLAMA_MODEL = "qwen3:14b"     # change to any pulled model
```

### llama-cpp (direct GGUF)

```bash
./setup.sh llama_cpp  # installs CUDA, builds llama-cpp-python, downloads GGUF
./vibe.sh
```

Set in `vibe/config.py`:
```python
BACKEND = "llama_cpp"
MODEL_PATH = ROOT_DIR / "models" / "Qwen3-8B-Q8_0.gguf"
```

### GPU offload

If a model doesn't fit entirely in VRAM, use `/offload` at runtime to split layers between GPU and CPU/RAM:

```
/offload 35    # 35 layers on GPU, rest on CPU/RAM
/offload 0     # CPU only (no VRAM used)
/offload -1    # all layers on GPU (default)
```

## Usage

```bash
# Launch
./vibe.sh

# Point at a project directory
./vibe.sh ~/my-project

# Verbose mode (shows tracebacks)
./vibe.sh --verbose
```

## Slash Commands

**Conversation**

| Command    | Description                                       |
|------------|---------------------------------------------------|
| `/reset`   | Clear conversation history                        |
| `/think`   | Enable chain-of-thought reasoning                 |
| `/nothink` | Disable chain-of-thought (faster)                 |
| `/tokens`  | Show context usage with a visual bar              |
| `/model`   | Show current backend and model info               |
| `/save`    | Summarize session to `.vibe/memory.md`            |
| `/memory`  | Print current `.vibe/memory.md`                   |
| `/exit`    | Quit                                              |
| `/help`    | Show all commands                                 |

**System Info**

| Command           | Description                                     |
|-------------------|-------------------------------------------------|
| `/sys`            | CPU, RAM, disk usage, uptime                    |
| `/gpu`            | GPU utilization, VRAM usage, temperature        |
| `/net`            | Network interfaces and listening ports          |
| `/ps [filter]`    | Top processes by CPU (optional name filter)     |
| `/files [path]`   | Open file manager (default: cwd)                |

**Process & Service Control**

| Command                        | Description                                    |
|--------------------------------|------------------------------------------------|
| `/kill <pid\|name>`            | Send SIGTERM to a PID or matching processes    |
| `/service <name> [action]`     | systemctl control (default: status). Actions: `start` `stop` `restart` `reload` `enable` `disable` |
| `/services [filter]`           | List running services                          |

**Runtime Config**

| Command                    | Description                              |
|----------------------------|------------------------------------------|
| `/offload <n>`             | GPU layers (-1=all, 0=CPU only, N=partial) |
| `/set temp <0.0-2.0>`      | Generation temperature                   |
| `/set tokens <n>`          | Max output tokens                        |
| `/set top_p <0.0-1.0>`     | Nucleus sampling probability             |
| `/set top_k <n>`           | Top-k sampling                           |
| `/set repeat_penalty <n>`  | Repetition penalty                       |

## Configuration

`vibe/config.py`:

```python
# Backend
BACKEND      = "ollama"      # "ollama" or "llama_cpp"

# Ollama
OLLAMA_HOST  = "http://localhost:11434"
OLLAMA_MODEL = "qwen3:14b"
OLLAMA_NUM_GPU = -1          # GPU layers (-1 = all, 0 = CPU only)

# llama-cpp
MODEL_PATH   = ROOT_DIR / "models" / "Qwen3-8B-Q8_0.gguf"
N_CTX        = 32768         # context window (tokens)
N_GPU_LAYERS = -1            # GPU layers (-1 = all, 0 = CPU only)

# Generation (both backends)
TEMPERATURE  = 0.6
MAX_TOKENS   = 16384
THINKING     = False         # /think to enable
```

## Project Structure

```
vibe-code/
├── main.py          # REPL entry point, slash command handling
├── vibe/
│   ├── config.py    # Backend selection, model paths, generation params
│   ├── intents.py   # Desktop requests answered directly — no model in the path
│   ├── modes.py     # Ask / Agent / Plan, and the routing between them
│   ├── desktop.py   # desktop_open / desktop_action / desktop_setting
│   ├── llm.py       # VibeModel — agentic loop, ollama + llama-cpp backends
│   ├── tools.py     # Tool schemas + implementations (read, write, edit, bash, glob, grep, ls)
│   ├── system.py    # What this machine is — CPU, memory, GPUs, drives, uptime
│   └── ui.py        # Rich console UI, prompt_toolkit session, streaming renderer
├── setup.sh         # llama-cpp setup script
├── vibe.sh          # Launch wrapper
└── requirements.txt
```
