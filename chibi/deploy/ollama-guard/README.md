# ollama-guard

Fences the ollama API (tcp/11434) so only loopback and chibi's Raspberry Pi can
reach it.

## Why this exists

The desktop serves `ollama-cuda` with `OLLAMA_HOST=0.0.0.0` so chibi, running on
`raspberrypi.lan`, can use the GPU. **ollama has no authentication** — anything
that can open the port can run inference, load models, and read whatever the
model is asked about. Bound to `0.0.0.0` on a LAN, that is every host on the
subnet.

This host also runs no general firewall (`/etc/nftables.conf` exists but
`nftables.service` is disabled), so there was nothing else standing in front of
the port.

These two files used to live **only on the desktop** — nothing in the repo would
have recreated them. A rebuild or a fresh install brought ollama back up wide
open, with no sign anything was missing. That is the failure this directory
prevents; the config is the same one that has been running, now versioned.

## Install

```sh
sudo ./install.sh
```

Then confirm the rules are loaded and the service survives a reboot:

```sh
sudo nft list table inet ollama_guard
systemctl is-enabled ollama-guard   # -> enabled
```

## Site-specific bit

`ollama.nft` hard-codes the client address:

```
tcp dport 11434 ip saddr 192.168.40.248 counter accept   # chibi @ raspberrypi.lan
```

If the Pi's address changes (DHCP lease, new hardware), edit that line and
re-run `install.sh` — otherwise chibi silently loses its LLM and falls back to
whatever `llm_client.py` is configured to do next.

It lives in its own nftables table so it cannot disturb synguard's `inet synnet`
table or `/etc/nftables.conf`. The chain policy is `accept` and every rule is
qualified by `tcp dport 11434`, so no other traffic can be affected.
