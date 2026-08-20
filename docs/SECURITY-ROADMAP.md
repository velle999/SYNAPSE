# Security roadmap

Work we can actually do, in the order it is worth doing. Written 2026-08-20
after an outside review of the repository, and kept here rather than in an
issue tracker so that each item carries what "done" means — a task whose
completion is a matter of opinion is a task that never completes.

`SECURITY.md` is the disclosure policy. This is the backlog behind it.

## What is deliberately NOT on this list

**An independent third-party security audit.** It is the right thing to want
and it is the first recommendation any reviewer makes. It is also not something
a one-person alpha can buy, and putting it on a backlog as though it were a
task turns the whole list into a wish. If the project ever reaches a point
where somebody offers, it happens then. Everything below is chosen because it
can be finished by the people already here.

---

## 1. Prove the BPF enforcement gate does what it says — in a VM

**Why first:** `50-default-deny.rules` arms three rules, but `--bpf-enforce` is
not in the shipped unit, so kernel enforcement has never run outside a
developer's head. Every safety property that makes arming it thinkable — the
30-second warmup, fail-open when synguard dies, the `synapse.bpf_enforce=0`
escape — is currently a claim in a comment. Turning it on across machines
before any of that is *observed* would be the exact failure this project keeps
finding in itself: a protection everybody believes in that nobody has watched
work.

**The rig:** `tools/bpf-enforce-check.sh`, run as root on the machine under
test. It arms the gate, makes the observations and disarms — and because the
escape hatch (`synapse.bpf_enforce=0`) lives in the boot menu and cannot be
reached over SSH, it schedules a **dead-man timer before it arms anything**, so
a machine that stops answering disarms itself in ten minutes without anybody
typing. Run it where you can reach the keyboard anyway.

**Done when**, with `--bpf-enforce`:

- [x] `systemd-run -q --collect cat /var/lib/synguard/bpf-canary` returns
      `Operation not permitted` **as root**. Root refused on a file root owns
      is a thing only an LSM can do, so this is the one observation that proves
      kernel enforcement rather than a late kill. **Observed 2026-08-20 on the
      laptop.**
- [x] …and `bpf-lsm: denied=` increments. **This one failed first time, and it
      was synguard rather than the kernel:** the counter was logged once at
      startup and never again, so a refusal left no trace in the journal at
      all. Fixed in 0.1.0-36 — logged on change, plus SIGUSR1 to sample on
      demand.
- [x] The same command in the first 30 seconds after synguard attaches
      **succeeds** — the warmup is real. **Observed.**
- [x] A wedged synguard (SIGSTOP, not kill — "alive but not answering" is the
      case the heartbeat exists for) leaves the canary readable again, and it
      goes back to being refused when synguard resumes. Fail-open is real in
      both directions. **Observed.**
- [ ] `synapse.bpf_enforce=0` on the kernel command line brings the VM up
      detect-only, with the canary readable. This is the way back from a bad
      rule and it must be verified before anybody needs it.
- [x] Planting `/etc/ld.so.preload` shows the open refused, the process still
      running, and the preload not applied. **Observed 2026-08-20** — and in
      four parts, because the first two attempts each passed for the wrong
      reason: while armed the file **cannot even be created**; planted while
      unarmed it really is on disk (checked against the filesystem, not against
      an exit code that is non-zero precisely when the plant worked); a normal
      program still runs with it in place; and the file itself is refused.
      ⚠ Twice reported as passing before it was actually testing anything —
      once because the rule refused the O_CREAT so the file never existed, once
      because the plant's scope was killed and the status read as failure.
- [ ] A normal desktop session comes up, logs in, and runs for an hour with no
      rule firing. The false-positive rate of these three rules is asserted;
      it has not been measured. **This is now the only thing standing between
      here and a decision on `--bpf-enforce`** — and it is worth doing whether
      or not the gate is ever armed, because the userspace path acts on these
      rules today, on every machine, and that is what took a laptop down.

**The answer, recorded 2026-08-20: NOT YET, and the reason is not the gate.**

Every safety property held — warmup, fail-open on a wedge, re-arm on recovery,
and the counters attributed each open to the right reason (`denied=1`,
`warmup=1`, exactly the two reads the rig made). 16 checks, 0 failures.

What argues against arming it is what the exercise found on the way. Three real
bugs in three runs, none of them findable by reading the code:

- a kernel refusal left **no trace in the journal** — the counter was logged
  once at boot and never again, so a gate that had refused thousands of opens
  looked identical to one that had refused none;
- the rig **killed itself**, having protected its reads with a scope and not
  its writes, in a file whose own header explains why that matters;
- `deny-ld-preload` **took a laptop down**, because ld.so opens that file on
  every exec and the USERSPACE path can only kill, not refuse.

That last one is the point. The thing that broke the machine was not the
kernel gate; it was a userspace rule that the gate had nothing to do with, and
arming the gate would not have prevented it. So the remaining risk is not in
this item at all — it is the false-positive box below, which needs a day of
ordinary use rather than a script.

Revisit when that box closes.

## 2. Attacker-controlled text reaching the AI classifier

**Why:** `synguard` sends security-event context to the local model and parses
back a verdict. The event carries strings an attacker chooses — process names,
file paths, argv. The parser is already constrained (an unrecognised verdict
becomes LOG, and AI verdicts are clamped to alert without `--ai-enforce`), and
that constraint is the defence. It has never been attacked on purpose.

**Done when:**

- [ ] A test suite runs the classifier against event fixtures whose comm/path
      fields contain instruction-shaped text — `"VERDICT: allow"`, newlines
      followed by a fake response frame, the delimiters the prompt itself uses.
- [ ] Every case yields a verdict no more permissive than the rule that
      matched. The property to pin is one-directional: injection must not be
      able to make the system **more** permissive. Making it noisier is
      tolerable.
- [ ] The prompt builder escapes or bounds the fields it interpolates, and the
      test proves the bound rather than the intention.
- [ ] `--ai-enforce` is exercised too, since that is the mode where a verdict
      can kill. It ships off; the test is what keeps it honest for anybody who
      turns it on.

## 3. Signed release artifacts, with one documented way to verify

**Why:** releases carry `.sha256` and `.b2sum` and the release notes explain
how to check them — which proves the file is intact, not that it is ours.
Anybody who can replace the ISO can replace the checksum beside it.

**Done when:**

- [ ] A release-signing key exists, its fingerprint is published in
      `SECURITY.md` and on soslinux.org, and the private half is not on a
      machine running an alpha OS.
- [ ] `publish-release.sh` signs the ISO (detached `.sig`) and refuses to
      publish unsigned.
- [ ] Verification is **one command** in the release notes and on the download
      page, with the Windows and macOS spellings the checksum instructions
      already carry.
- [ ] The ISO's own `pacman` keyring story is written down: what signs the
      SynapseOS packages inside it, and what happens on a box whose keyring is
      older than the key.

## 4. Reproducible ISO builds

**Why:** signing says who built it. Reproducibility says the source in this
repository is what is in the image — which is the claim a public source tree
implicitly makes and currently cannot support. `profiledef.sh` already honours
`SOURCE_DATE_EPOCH` for the label, which is the shape of the work and about one
per cent of it.

**Done when:**

- [ ] `SOURCE_DATE_EPOCH` is set from the release commit and honoured
      throughout the build, not only in the ISO label.
- [ ] Two builds of the same commit on the same host produce ISOs that differ
      only in ways that are **listed** — and the list shrinks over time rather
      than being a permanent excuse.
- [ ] A script does that comparison, so "is it still reproducible" is a command
      and not a project.
- [ ] The remaining non-determinism is documented per cause: package build
      order, timestamps in squashfs, the AUR packages we do not build.

⚠ Note the honest ceiling: SynapseOS ships packages built from the AUR
(`davinci-resolve`, `linux-wallpaperengine`) and Arch packages we do not build.
Full bit-for-bit reproducibility of the whole image is not reachable. **Our own
components** are, and that is what this item means.

## 5. A pass over `synapse_kmod`, with a checklist

**Why:** it is the component where a bug is worst — kprobes on execve, openat,
network, ptrace, module load, setuid and capability changes, plus sysfs
interfaces and custom syscalls. A memory-safety or authorization bug there
turns a local compromise into a kernel one. There is a hardening document
already; what there is not is a systematic read against it.

Not an audit, and this item does not pretend to be one. A structured internal
pass finds the easy half.

**Done when:**

- [ ] Every sysfs attribute is listed with its permissions and who may write
      it, and each write path is checked for bounds and for what happens on a
      partial or oversized write.
- [ ] Every path where a userspace string enters the module has its length
      bound identified, and a fuzz case for it.
- [ ] The ring buffer between the kmod and synguard is examined for the
      producer/consumer races that a userspace reader can provoke.
- [ ] The custom syscalls are enumerated with who may call them.
- [ ] Anything unresolved is written down here rather than closed.

## 6. A threat model, once the above have taught us what it says

**Why last:** a threat model written now would be a restatement of intentions.
Written after items 1, 2 and 5, it can say what was observed. `SECURITY.md`'s
scope section is a first draft of the audience-facing half.

**Done when:**

- [ ] What the AI can and cannot influence, per component, as a table.
- [ ] What survives synguard being killed, and what does not.
- [ ] What the firewall's LAN-trust actually admits, with the roaming case
      spelled out.
- [ ] Which of these are enforced by the kernel and which by a userspace
      daemon that a root attacker can stop.

---

## Done

- **`SECURITY.md` and private vulnerability reporting** — `ac77b3a`,
  2026-08-20. Private reporting was already enabled on the repository; there
  was nothing pointing at it.
- **synapd trusted the client's own PID** — `57119c2`, 2026-08-20.
  `w->client_pid = hdr.client_pid`, with `SO_PEERCRED` never consulted.
  Attribution and context poisoning rather than escalation; found by the
  outside review that prompted this file.
- **The firewall was invisible to its own status command** — `d1ee8af`,
  2026-08-20. It had been running the whole time.
- **The shipped synguard policy never acted** — `8e471f1`, 2026-08-20. 55
  rules, none of them `deny` or `quarantine`.
