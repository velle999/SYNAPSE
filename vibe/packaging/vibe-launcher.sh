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
# `vibe provider <name>` writes ~/.config/synui/vibe.env, because config.py
# lives under /usr/lib and is read-only at runtime — a setting that needs root
# to change is not a setting. But a VIBE_BACKEND already in the environment is
# somebody saying so for THIS run, and it stays on top: that is what makes
# `VIBE_BACKEND=ollama vibe` still mean what it says.
_env="${XDG_CONFIG_HOME:-$HOME/.config}/synui/vibe.env"
if [ -z "${VIBE_BACKEND:-}" ] && [ -r "$_env" ]; then
    # Read rather than sourced: this file is written by `vibe provider` and has
    # no business being able to run shell.
    _pick=$(sed -n 's/^VIBE_BACKEND=\([A-Za-z_]*\)$/\1/p' "$_env" | tail -1)
    [ -n "$_pick" ] && VIBE_BACKEND=$_pick
fi
: "${VIBE_BACKEND:=synapd}"
export VIBE_BACKEND

# The app tree is read-only under /usr/lib; put it on PYTHONPATH so `import
# vibe` resolves, and run main.py from there.
export PYTHONPATH="/usr/lib/vibe/app${PYTHONPATH:+:$PYTHONPATH}"
exec python3 /usr/lib/vibe/app/main.py "$@"
