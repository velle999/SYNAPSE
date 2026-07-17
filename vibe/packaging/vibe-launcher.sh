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
: "${VIBE_BACKEND:=synapd}"
export VIBE_BACKEND

# The app tree is read-only under /usr/lib; put it on PYTHONPATH so `import
# vibe` resolves, and run main.py from there.
export PYTHONPATH="/usr/lib/vibe/app${PYTHONPATH:+:$PYTHONPATH}"
exec python3 /usr/lib/vibe/app/main.py "$@"
