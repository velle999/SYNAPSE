# synguard

A security monitor that watches process, file and network behaviour through
eBPF and classifies what it sees against a set of rules — optionally with a
local model as a second opinion.

```bash
synguard --mode audit        # watch and log, change nothing (the default)
synguard --mode enforce
synguard --mode learning
synguard --mode lockdown
synguard -d                  # foreground, verbose
```

## What it will and will not do on its own

**Run by hand, it starts in `audit`:** nothing is denied until you say so.
**The packaged service runs `--mode enforce --bpf-enforce`** and acts on the
three rules in `50-default-deny.rules`: a write to `/etc/ld.so.preload` and a
read of synguard's canary file are refused in the kernel, and a program run
from `/dev` is stopped. Every other rule alerts or logs.

Two switches gate the parts that can act:

- `--ai-enforce` lets the classifier's verdicts deny or quarantine. Without
  it the classifier is **advisory** and its verdicts are clamped to alerts —
  only rule verdicts can kill. In either mode an `escalate` rule is at least
  an alert: the classifier adds a threat level and a reason, and cannot make
  the event quieter.
- `--bpf-enforce` arms the BPF-LSM gate, so an enforceable deny is refused
  in-kernel rather than the process being killed after the fact. The packaged
  unit passes it. `/etc/synguard/bpf-enforce` containing `off` leaves the gate
  loaded but unarmed; `synapse.bpf_enforce=0` on the kernel command line keeps
  it from loading at all for that boot.

## Rules

Rules live in `/etc/synguard/rules.d/` (`--rules DIR` to point elsewhere).
A rule that is not armed is not enforced, and a rule that names an operation
the kernel gate does not cover cannot be enforced in-kernel however it is
written — `--mode audit` with the audit log is the way to see which of your
rules are actually doing anything before switching modes.

## Requires

`libbpf` and a kernel with BPF-LSM available for `--bpf-enforce`; without it
the monitor still runs and still classifies, and the enforcement half is
simply unavailable rather than silently inert.
