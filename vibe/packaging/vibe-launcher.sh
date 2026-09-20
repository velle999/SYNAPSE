#!/bin/bash
# Vibe — local AI coding assistant, launcher for SynapseOS (installed as
# /usr/bin/vibe). Distinct from the upstream vibe.sh dev launcher, which
# activates a .venv; the package uses system rich/prompt_toolkit instead.
#
# Defaults to the synapd backend so Vibe reuses the model already resident on
# the GPU (no second model, no extra VRAM). Env wins over vibe/config.py, so
# this is the single place SynapseOS picks the backend — upstream config.py
# keeps its neutral ollama default. Override with:
#   VIBE_BACKEND=ollama vibe              # or llama_cpp
#   VIBE_SYNAPD_HOST=10.0.0.153 vibe      # reach the TCP bridge on another host
#
# ⚠ THE USER'S CHOICE IS A FILE, AND IT DOES NOT OUTRANK THE ENVIRONMENT.
# `vibe provider <name>` and `vibe host <name>` write
# ~/.config/synui/vibe.env, because config.py
# lives under /usr/lib and is read-only at runtime — a setting that needs root
# to change is not a setting. But a VIBE_BACKEND already in the environment is
# somebody saying so for THIS run, and it stays on top: that is what makes
# `VIBE_BACKEND=ollama vibe` still mean what it says.
_env="${XDG_CONFIG_HOME:-$HOME/.config}/synui/vibe.env"

# Read rather than sourced: this file is written by `vibe provider` and `vibe
# host` and has no business being able to run shell. One reader for every key,
# each matched against a character class that cannot carry a command — adding a
# second hand-written sed is how the two spellings drift apart.
_envget() {   # _envget KEY CHARACTER-CLASS
    [ -r "$_env" ] || return 0
    sed -n "s/^$1=\\([$2]*\\)\$/\\1/p" "$_env" | tail -1
}

[ -n "${VIBE_BACKEND:-}" ] || VIBE_BACKEND=$(_envget VIBE_BACKEND 'A-Za-z_')
: "${VIBE_BACKEND:=synapd}"
export VIBE_BACKEND

# WHERE synapd IS, which is a separate question from WHICH BACKEND to use.
# Empty means /run/synapd/synapd.sock on this machine — right for any box that
# runs its own daemon. A hostname sends the same binary protocol to
# synapd-bridge.socket (tcp/11435) on another box, so a laptop with no GPU can
# answer from the desktop's resident model instead of its own CPU.
#
# ⛔ THIS IS WHAT THE DOCK BUTTON READS. A bar or .desktop launch starts vibe
# with no environment of its own, so a host that lives only in a shell export
# reaches the terminal and never the window — which reads as "the GUI ignores
# my setting", with the answers quietly coming from the local daemon.
[ -n "${VIBE_SYNAPD_HOST:-}" ] || VIBE_SYNAPD_HOST=$(_envget VIBE_SYNAPD_HOST 'A-Za-z0-9._-')
[ -n "${VIBE_SYNAPD_HOST:-}" ] && export VIBE_SYNAPD_HOST
[ -n "${VIBE_SYNAPD_PORT:-}" ] || VIBE_SYNAPD_PORT=$(_envget VIBE_SYNAPD_PORT '0-9')
[ -n "${VIBE_SYNAPD_PORT:-}" ] && export VIBE_SYNAPD_PORT

# The app tree is read-only under /usr/lib; put it on PYTHONPATH so `import
# vibe` resolves, and run main.py from there.
export PYTHONPATH="/usr/lib/vibe/app${PYTHONPATH:+:$PYTHONPATH}"
exec python3 /usr/lib/vibe/app/main.py "$@"
