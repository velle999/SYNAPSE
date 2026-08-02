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
sudo $EDITOR synapd-bridge.nft   # set YOUR peer address
./deploy.sh
sudo systemctl enable synapd-bridge-guard.service synapd-bridge.socket
```

`deploy.sh` installs every file, syntax-checks both rulesets with `nft -c`
before anything is reloaded, and then checks the **ports and tables** rather
than the unit states — see below for why that distinction is the whole point.

## Keeping /etc and this directory in sync

```sh
./deploy.sh --check     # 0 = in sync, 1 = drifted, prints the diff
```

Nothing here is packaged, so pacman never updates these files and nothing
notices when the repo moves ahead of `/etc`. That is not hypothetical. The
`DefaultDependencies=no` fix described below was committed to this directory
and never applied to the machine, so it kept booting the old unit and kept
losing `synapd-bridge.socket` to the very cycle that fix removes. The repo was
correct, the system was broken, and every check on either side in isolation
looked fine. Only the diff between them showed it.

**A control that lives only in `/etc` is unreviewed; a control that lives only
in git is not running.** `--check` is the cheap way to notice which one you
have.

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
  `deploy.sh` ends by checking the ports and the tables, not the units.

## The same bug, the other way round: ollama (removed)

A matching `ollama-guard.service`, `ollama.nft` and `ollama.service.d`
override used to live here, fencing `0.0.0.0:11434` for the same peer. They
were removed once synapd learned to serve embeddings (`SYN_MSG_EMBED`) and
chibi's Thoth stopped needing a second model server. Nothing on this box
consumes ollama now, and an unused guard for an uninstalled service is a
control nobody tests.

The bug it caught is kept, because it is about `Wants=` and not about ollama:

`ollama.service.d/override.conf` had `Wants=ollama-guard.service` where this
socket has `Requires=`. `Wants=` orders but does not gate, so a guard that
failed for any reason — a bad ruleset, a renamed file, an nft upgrade — left
ollama starting normally and binding `0.0.0.0:11434` with no rules in front of
it. Its own comment said "never open the port before the rules that fence it
off are loaded", which is what `After=` does; it is not what the file was
relied on to do.

Demonstrated rather than argued, by making the guard's `nft -f` fail:

```
Wants=      guard: failed   ollama: active     11434: LISTEN 0.0.0.0   ← exposed
Requires=   guard: failed   ollama: inactive   11434: closed
```

Two ports, the same threat, the same author, one word apart. When the same
control is expressed in more than one place, the copies drift — so when you
fix one, go and read the others.

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
