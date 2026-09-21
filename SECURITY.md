# Security policy

SynapseOS is an Arch-based Linux distribution with an AI assistant wired into
the desktop. That combination means it carries a security daemon, a network
policy daemon with a firewall, a sandbox, and a local model that other programs
talk to — so there is real attack surface here beyond "it is a package
repository", and reports about it are welcome.

## Reporting a vulnerability

**Please do not open a public issue for a security problem.**

Use GitHub's private reporting on this repository:
**Security → Advisories → Report a vulnerability**
(<https://github.com/velle999/SYNAPSE/security/advisories/new>). It is enabled,
it is the preferred route, and it keeps the report private until there is
something to install.

If that is not available to you, email **brncomputerhelp@gmail.com** with
`SECURITY` in the subject.

**What to expect.** This project is maintained by one person; treat these as
honest intentions rather than a contractual SLA:

| | |
|---|---|
| First reply | within 7 days |
| Assessment, with a yes/no on whether it is a vulnerability | within 14 days |
| Fix for something remotely reachable or privilege-escalating | as fast as it can be built and released |
| Public disclosure | after a fix ships, or 90 days, whichever comes first — earlier if you prefer, and sooner if it is already being exploited |

Credit is given in the release notes unless you ask otherwise.

## What is in scope

The code in this repository. The components most worth your attention, roughly
in order of how much damage a flaw in them would do:

- **`synguard`** — the security monitor. Applies policy through
  `synapse_kmod`'s action interface and a BPF LSM. Runs as root.
- **`synapse_kmod`** — an out-of-tree kernel module. Anything here is a kernel
  bug on a user's machine.
- **`synnet`** — network policy and the system firewall (`nftables`). A flaw
  that unfilters a machine, or that lets an untrusted value reach `nft`, counts.
- **`synapd`** — the AI daemon, and the socket every other component queries.
  Prompt content crossing a privilege boundary is in scope; see below.
- **`synui`** — the Wayland compositor. It brokers privileged Wayland globals
  (screen capture, virtual input, gamma), and which clients may hold them is a
  security question.
- **`syn-confine`** — the Landlock sandbox. A confinement that does not confine
  is a vulnerability, not a bug.
- **`syn-install`** — the installer. It partitions disks, writes bootloaders and
  configures LUKS.
- **`synpkg`**, **`syn-update`** — they install packages and build from source
  as root. Anything that lets an attacker choose what gets installed is serious.
- The **ISO build** (`archiso/`) and anything that affects what ships on it.

### Things that are in scope and easy to miss

- **The AI is a privilege boundary.** `synapd` answers queries from components
  running at different privilege levels. A prompt-injection path that turns
  model output into a privileged action — a command run, a rule loaded, a file
  written — is a vulnerability here, not a curiosity. Model output is treated
  as untrusted input everywhere it crosses into an action, and a place where it
  is not is a bug worth reporting.
- **Anything that makes a security feature silently inert.** A firewall that
  does not load, a sandbox that fails open, a rule file that is parsed by
  nobody, a check that reports success without running. These are treated as
  vulnerabilities even when nothing is exploitable yet, because the machine and
  its owner both believe a protection is in place that is not. This project has
  shipped that failure and it is the shape we most want reports about.
- **Escalation from the desktop user to root**, by any of the helpers that
  self-elevate (`pkexec`, `sudo -n`, polkit) or the pacman hooks.

## What is out of scope

- Bugs in Arch Linux packages, the AUR, or upstream projects SynapseOS merely
  ships. Report those upstream; if SynapseOS's *use* of them is what is unsafe,
  that is ours.
- The default trust model of the firewall, which accepts unsolicited traffic
  from RFC1918 and IPv6 ULA/link-local sources so that LAN services stay
  reachable. This means a hostile device on the same network is trusted, and on
  public Wi-Fi handing out `192.168.x` addresses, so is that network. It is a
  deliberate tradeoff, documented in `synnet/src/monitor.c`, and it can be
  narrowed by the operator. A better default is a design discussion — open an
  issue — rather than a vulnerability report.
- `synguard` being killable by root. It is a userspace daemon by design, stated
  in `synguard/src/synguard_main.c`: the threat model is a workstation and an
  AI assistant, not adversarial kernel hardening.
- Anything requiring physical access to an unlocked machine, or that begins
  "as root, …".
- Findings from an automated scanner with no demonstrated impact. A CVE in a
  dependency that the code does not reach is not a report on its own.

## Supported versions

Releases are ISOs, and there is one line of them.

| Version | Supported |
|---|---|
| The current release (see [Releases](https://github.com/velle999/SYNAPSE/releases)) | Yes |
| Anything older | No — fixes land in the next release |

An installed system does **not** update by reinstalling: `syn-update` rebuilds
changed components from this repository, so a security fix pushed here reaches
existing machines without a new ISO. Two exceptions worth knowing when judging
whether a fix has actually landed:

- components in `syn-update`'s `UNSUPPORTED` map are **not** delivered that way
  and move only with an ISO;
- `synapse_kmod` is a kernel module, so a machine has to reboot into the
  rebuilt one.

## Hardening this is built on

Not a guarantee — a list of what exists, so a report can say which part it
defeats:

- default-drop inbound firewall (`synnet`), applied at boot and re-checked
  every minute
- process/file policy with a BPF LSM (`synguard`), with rules in
  `/etc/synguard/rules.d`
- Landlock sandboxing for AI-adjacent processes (`syn-confine`)
- systemd hardening on every shipped unit
- Secure Boot support, and LUKS from the installer
- the AI backend can be turned off, and off survives a reboot

## Work in progress

`docs/SECURITY-ROADMAP.md` is the backlog behind this policy — what is known to
be missing, in the order it is being done, with what "done" means for each. It
is kept in the repository rather than in an issue tracker so that the honest
answer to "is X covered yet" is one file away.

**Kernel-level enforcement is on by default** (synguard 0.1.0-44 and later):
the deny rules that lower to `synguard`'s BPF-LSM gate are refused in the
kernel. Settings ▸ Security turns it off, by writing `off` to
`/etc/synguard/bpf-enforce`; `synapse.bpf_enforce=0` on the kernel command line
keeps the gate from loading for one boot.

## Verifying a release

Releases from 0.2.9.5 onward are signed with the SynapseOS release key:

```
6548 9EF5 C20D 0BD9 4211  472B ED33 6DB7 952B 609E
SynapseOS Release Signing <releases@soslinux.org>   ed25519, expires 2029-08-28
```

The public key is at <https://soslinux.org/synapseos-release-key.asc>, and the
README's *Check who built it* section has the commands.

SynapseOS's own packages are not signed one by one. On the ISO they are covered
by the ISO's signature; an installed system's `syn-update` builds them from
source fetched from GitHub over HTTPS.

## Please do not

- Run tests against machines that are not yours.
- Open a public issue, a pull request, or a discussion describing an unfixed
  vulnerability.
- Ask for a bounty. There is no budget for one; this is a personal project.

Thank you for looking.
