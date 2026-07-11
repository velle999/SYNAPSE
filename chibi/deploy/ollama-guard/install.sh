#!/bin/sh
# install.sh — install and enable the ollama port guard. See README.md.
#
# Idempotent: safe to re-run after editing ollama.nft (e.g. the Pi's address
# changed). Loading the ruleset is what actually closes the port, so do it
# before enabling the unit rather than trusting the next reboot.
set -eu

[ "$(id -u)" -eq 0 ] || { echo "install.sh: must run as root" >&2; exit 1; }

command -v nft >/dev/null || { echo "install.sh: nftables not installed" >&2; exit 1; }

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

install -Dm644 "$DIR/ollama.nft"           /etc/nftables.d/ollama.nft
install -Dm644 "$DIR/ollama-guard.service" /etc/systemd/system/ollama-guard.service

# Validate before committing to it: a syntax error here would otherwise leave
# the port open with the unit "enabled" and nobody any the wiser.
nft -c -f /etc/nftables.d/ollama.nft

systemctl daemon-reload
systemctl enable --now ollama-guard.service

echo "ollama-guard: active. Rules now in force:"
nft list table inet ollama_guard
