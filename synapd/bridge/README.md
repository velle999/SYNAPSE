# synapd TCP bridge — site-specific, opt-in, NOT installed

These four files expose synapd's unix socket on `tcp/11435` so another machine
on the LAN can use the model this box has already loaded, instead of loading a
second copy of the same weights. Here that other machine is the Raspberry Pi
running chibi; without the bridge, ollama on the Pi loads its own Mistral 7B.

**The synapd package does not install any of this**, deliberately:

- It opens a listening port on `0.0.0.0`. That is a decision an administrator
  makes for one machine, not a default a distro ships to every install.
- `synapd-bridge.nft` hardcodes a peer address. Shipped as-is to someone else,
  that allowlist names an arbitrary host on *their* network.

They live in the repo because the alternative was worse: they existed only in
`/etc/systemd/system` and `/etc/nftables.d` on one machine, which meant the
control protecting a LAN-facing port was unversioned, unreviewed, and one
`rm` away from being gone with no record of what it had said.

## Files

| File | Installs to |
|---|---|
| `synapd-bridge.socket` | `/etc/systemd/system/` |
| `synapd-bridge.service` | `/etc/systemd/system/` |
| `synapd-bridge-guard.service` | `/etc/systemd/system/` |
| `synapd-bridge.nft` | `/etc/nftables.d/` |

## Install

```sh
sudo install -m644 synapd-bridge.socket synapd-bridge.service \
                   synapd-bridge-guard.service /etc/systemd/system/
sudo install -m644 synapd-bridge.nft /etc/nftables.d/
sudo $EDITOR /etc/nftables.d/synapd-bridge.nft     # set YOUR peer address
sudo systemctl daemon-reload
sudo systemctl enable --now synapd-bridge-guard.service synapd-bridge.socket
```

Verify it actually came up — see below for why that is not a formality:

```sh
systemctl is-active synapd-bridge.socket      # expect: active
ss -ltn | grep 11435                          # expect: a LISTEN line
sudo nft list table inet synapd_bridge_guard  # expect: the accept/drop rules
```

## The failure this directory exists to document

The guard is ordered `Before=synapd-bridge.socket` so the port can never be
open without the firewall rules in front of it, and the socket `Requires=` the
guard so it fails closed. Correct intent, and it silently disabled the whole
feature.

A normal service gets an implicit `After=basic.target`; `basic.target` is
ordered after `sockets.target`; `sockets.target` pulls in the socket unit,
which is ordered after the guard. systemd found the cycle and broke it by
deleting `synapd-bridge.socket/start`.

The result was a system where every check an administrator would think to run
passed. The guard was `active`. `nft list tables` showed the table. No unit was
failed, nothing was red. The port simply was never listening, for 16 boots. The
only evidence is one pair of lines at boot:

```
sockets.target: Found ordering cycle: synapd-bridge.socket/start after
  synapd-bridge-guard.service/start after basic.target/start after
  sockets.target/start - after synapd-bridge.socket
sockets.target: Job synapd-bridge.socket/start deleted to break ordering cycle
```

`DefaultDependencies=no` on the guard removes `basic.target` from the chain and
with it the cycle; the ordering it would have supplied is stated explicitly in
that unit instead. The reasoning is written out there in full.

Two general lessons, both already learned here the hard way once:

- **Never order a normal service `Before=` a `.socket` without
  `DefaultDependencies=no`.** The cycle is not obvious from either unit file;
  it only appears once systemd composes them with the implicit dependencies.
- **A fail-closed dependency can fail so closed the feature never runs.**
  `Requires=` guarantees the guard is up before the port opens. It does not
  guarantee the port ever opens. Those need separate verification, which is why
  the install steps above end with three commands that check the port, not the
  units.

## Threat model

`synapd-bridge.service` is a `systemd-socket-proxyd` that moves bytes between
the TCP socket and `/run/synapd/synapd.sock`. It runs `DynamicUser=yes` with an
empty `CapabilityBoundingSet` and `MemoryDenyWriteExecute`, holding no
privilege of its own — `/run/synapd/synapd.sock` is `srw-rw---- root:synapse`,
so `SupplementaryGroups=synapse` is all it needs.

Behind it, synapd's wire protocol has **no authentication**. The nftables
source allowlist is the entire access control; there is no second layer. That
is why the socket `Requires=` the guard rather than merely `Wants=` it.

synapd itself is not root — `User=synapd`, empty caps, `NoNewPrivileges`,
`RestrictAddressFamilies=AF_UNIX`, `SystemCallFilter=@system-service` — so a
parser compromise starts from a confined unprivileged account. It still reaches
the loaded model, the conversation context, and a socket every local session
trusts, so this is a lower ceiling rather than an acceptable outcome.

If you need this reachable by more than one known host, the honest answer is
not a wider allowlist — it is authentication in the protocol, which synapd does
not currently have.
