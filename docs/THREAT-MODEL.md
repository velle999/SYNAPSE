# Threat model

What SynapseOS's security machinery defends against, what it does not, and
where each protection actually lives. Written 2026-09-21 as item 6 of
[`SECURITY-ROADMAP.md`](SECURITY-ROADMAP.md), after items 1, 2 and 5 had been
done — so every row below is either **measured** (with the date and how) or
**read from the code** (with the file). Nothing here is an intention.

`SECURITY.md` is the disclosure policy and the short version of this for
people installing the system.

## Who this is about

| attacker | what they have | the usual way in |
|---|---|---|
| **Content** | text that reaches the machine: a web page (and its title), a file and its name, a process name, a network peer | nothing more than being looked at, downloaded, or connected to |
| **A local process** | code running as the desktop user | anything the user installed or ran |
| **Root** | everything | a local escalation, or a compromised root process |

The AI is not an attacker, but everything it reads may have been written by
one, so this document treats every model answer as attacker-influenced.

---

## 1. What the AI can and cannot influence

A local llama.cpp model behind `synapd` (`/run/synapd/synapd.sock`,
`root:synapse 0660`). Any process of a user in the `synapse` group can query it,
so the model is a service, not a privilege boundary.

| component | what reaches the model that an attacker chose | what the answer can do | what bounds it |
|---|---|---|---|
| **synguard** | process name (`comm`) and path of the event | set the threat level and the reason shown with an alert; raise an `escalate` rule to **deny** under `--ai-enforce` | ALERT is the floor: an answer can never make a rule quieter. `--ai-enforce` is **off**. Fields are quoted and escaped. Measured 2026-09-21: an injected answer frame in a filename was copied back 2/2 before; with the floor it cannot lower anything. (`synguard/src/event_processor.c` `sg_ai_bound_verdict`) |
| **synui command bar** | what you type; with **Super+Backspace**, the focused window's app id and **title** | run a shell command (`CMD:`), focus a window, switch workspace | Only an answer that **starts** with `CMD:` is a command, and it runs inside **syn-confine**: your files read-only, `/tmp` writable, network allowed. A bare installed GUI app name launches as itself. Measured 2026-09-21: a hostile page title got its command emitted in 4 of 12 runs; before synui 623 all four would have run **unconfined**. (`synui/src/cmdplan.c`) |
| **synsh** | what you type; the current directory's path | propose one shell command | **Asks before running** by default (`ai_confirm`); `syn safe off` turns that off, and then a hostile directory name can reach a shell. (`synsh/src/exec.c`) |
| **vibe** | files and command output its tools read; your messages | read files; with confirmation, run `bash` (inside syn-confine), write/edit/move files, change desktop settings; **without** confirmation, open an app, folder or URL | Writing tools ask first; since vibe 36 a confirmation that **fails** is a no (it used to be a yes). `bash` refuses to run without syn-confine. (`vibe/vibe/llm.py`, `vibe/vibe/tools.py`) |
| **synnet** | the destination IP and port of an outbound connection — numbers only | **block** that destination | Can only add a block, never open anything; asked only about a fixed list of known-bad ports; no answer means no block. (`synnet/src/monitor.c`) |
| **chibi** | synguard's alerts (process name, and a reason that may quote the model) | what chibi **says** | No tools that act. A steered model can make chibi reassure or alarm you, not do anything. |
| **the scheduler** | — | nothing today | synapd's `scheduler_write_hint()` has no callers; nothing writes a model's answer into `/sys/kernel/synapse/ai_hints`. If that path is revived, the kmod still refuses PID 1, kernel threads and the core daemons, and never grants a real-time policy. |

**What no answer can do:** kill a process without `--ai-enforce`; change a
firewall rule other than adding an egress block; write outside `/tmp` through
the command bar or vibe's `bash`; reach the kernel's scheduler.

---

## 2. What survives synguard being killed

synguard runs as root with `Restart=always` (3 s). Measured on the laptop
2026-08-20 (`tools/bpf-enforce-check.sh`) unless marked.

| protection | synguard dead | synguard wedged (SIGSTOP) |
|---|---|---|
| **BPF-LSM refusals** (`deny-ld-preload` writes, `deny-bpf-canary`) | **gone** — the BPF link is held by synguard's fd, so the hooks detach (fail-open, by design) | **gone** within the heartbeat — the gate stops refusing when the beat goes stale; observed, and it re-arms on resume |
| **userspace kills and quarantines** | gone | gone |
| **alerts, audit log, secfeed** (window borders, chibi) | gone; chibi says the monitor is down | gone |
| **kmod event capture** | **continues** into the ring; nothing reads it. A restarted synguard starts at the oldest event still there, so a short outage is caught up — a long one laps the ring and is reported as dropped | same |
| **kmod probe-integrity watchdog** | continues (kernel log) | continues |
| **the firewall** (synnet's nftables chain) | **unaffected** — the rules live in the kernel, and synnet does not remove them when it exits | unaffected |
| **syn-confine sandboxes** | **unaffected** — Landlock is in the kernel and cannot be dropped by the confined process | unaffected |
| **the canary** (catches kprobes being disarmed globally) | gone — it is synguard's | gone |

So **killing synguard costs every refusal and every kill, and costs nothing
on the network side**. A root attacker who stops it has 3 seconds before
systemd restarts it — or forever, if they mask the unit. The kmod's `lockdown`
pin makes `rmmod synapse_kmod` fail until someone writes `lockdown=0`; it is a
tripwire, not a wall, and it is off by default.

---

## 3. What the firewall's LAN trust admits

synnet's input chain (`synnet/src/monitor.c`, `synnet_nft_ensure_firewall`) is
**policy drop**, and accepts:

- loopback, and established/related connections;
- all ICMP and ICMPv6;
- **every port, from any RFC1918 source** — `10.0.0.0/8`, `172.16.0.0/12`,
  `192.168.0.0/16` — and from IPv6 ULA `fc00::/7` and link-local `fe80::/10`;
- DHCP client replies (`udp 68`, `546`);
- the gateway services on container/VM bridges it is told to trust.

Everything else inbound is dropped. Outbound is not filtered, except
destinations synnet (or its model) has blocked.

**At home** that means any device on the home network can reach any service
that listens on all interfaces. **Roaming**, it means the same thing on
somebody else's network: café and hotel Wi-Fi hand out RFC1918 addresses, so
**every other guest is "LAN"**, on every port, and on IPv6 so is everyone on
the same link. The firewall does not know which network it is on.

What that exposes depends on what listens. A fresh install enables avahi
(mDNS: it announces the machine's name and services to the network), CUPS
(bound to localhost by default) and Syncthing (port 22000, which authenticates
peers by device ID). Anything the owner turns on — SSH, VNC, Samba, a media
server, game streaming — joins that list, reachable by the café the same as by
the house.

`synnet --open <port>` opens a port to everyone; `--allow <ip>` only removes a
block and opens nothing; `--trust-if` covers DHCP and DNS on a bridge, not
every port. Tailscale's `100.64.0.0/10` is **not** in the trusted ranges, so
a tunnel comes up and its inbound packets are dropped.

---

## 4. Kernel-enforced, or a daemon root can stop?

| protection | enforced by | a root attacker can |
|---|---|---|
| file-open refusals (lowered deny rules) | the kernel (BPF LSM) — **armed by default since synguard 44** | stop synguard (the hooks detach), write `off` to `/etc/synguard/bpf-enforce`, or boot with `synapse.bpf_enforce=0` |
| kills, quarantines, alerts | synguard, userspace | stop it |
| AI verdicts | synguard + synapd, userspace | stop either; with `--ai-enforce` off they only ever alert |
| syscall event capture | the kernel (kprobes, synapse_kmod) | `events_enabled=0` (synguard 43 alerts on it), `rmmod` (unless pinned), or disarm kprobes globally (the canary catches that — while synguard runs) |
| the input firewall | the kernel (nftables) | `nft flush ruleset`, or `synnet --firewall off` |
| AI egress blocks | the kernel (nftables set) | same |
| command sandboxes (vibe `bash`, the command bar) | the kernel (Landlock) — cannot be undone from inside | nothing to stop: it binds only the confined process. Root outside it is unaffected |
| which Wayland clients can capture the screen, read keystrokes through an input method, or watch the clipboard | synui, userspace — withheld only from clients that carry a **security context** (Flatpak and other sandboxes) | not needed: an ordinary process running as the user already gets them |
| what an update installs | the update key's signature, checked by `syn-update` (commits) and by makepkg (published source tarballs) | add a key to `/usr/share/syn-update/keys`, or run with `SYN_UPDATE_ALLOW_UNSIGNED=1` |

**Updates** are the one path from outside into every installed machine.
`syn-update` fetches `github.com/velle999/SYNAPSE` and builds only commits
signed with the SynapseOS update key. The key it checks against is the one in
the installed syn-update package, never one in the fetched tree. It checks
every commit since the last one it verified, not only the newest, and stops
before the first that is not signed: `syn-update check` names what it left
out. Pushing to GitHub, or answering for it, is not enough to reach an
installed machine. (syn-update 0.1.0-63 brought the check; the apply that
installs it is the last one taken unchecked.)

The components published on their own (for plain Arch) are signed the same
way: each release carries the tarball's `.sig`, and the package repository's
PKGBUILD names the key in `validpgpkeys`, so makepkg refuses a tarball the key
did not sign. The PKGBUILD itself comes from the package repository, whose
commits carry the same signature.

The update key is kept on the build machine **without a passphrase**, so that
every commit and release is signed without a prompt. Whoever controls that
machine controls updates; `SECURITY.md` has the fingerprint.

---

## What this does not cover

- **What the probes cannot see.** synapse_kmod reports an open or an exec from
  the path the caller passed, at syscall entry, so a relative path, a symlink,
  or a path in a page the process has not touched yet is not reported; several
  syscalls have no probe at all. Measured and listed in
  [`SECURITY-ROADMAP.md` §5](SECURITY-ROADMAP.md).
- **Anything inside a file.** synguard watches activity; malware scanning is
  `syn-scan`'s, on a schedule.
- **A root attacker who is patient.** Every userspace protection here can be
  stopped by root, and the kernel ones can be switched off by root. What they
  buy is that doing so is loud — a journal line, an alert, or a missed canary —
  as long as synguard is the thing still watching.
