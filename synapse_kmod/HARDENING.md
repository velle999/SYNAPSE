# synapse_kmod — anti-hijack hardening

This module is the kernel side of SynapseOS's intrusion monitor. These are the
controls that defend it (and the system) against being subverted or blinded,
and how to turn on the ones that are opt-in.

## Always on

- **Protected-PID guard** — the `ai_hints` scheduler path refuses to touch
  PID 0/1, the global init, kernel threads, and the core SynapseOS/session
  daemons. A local DoS like `echo "HINT pid=1 class=idle" > ai_hints` is
  rejected and logged.
- **Probe self-integrity** — every 5 s the module checks its own syscall
  kprobes are still armed. A disarm is logged `pr_crit` and counted in
  `/sys/kernel/synapse/stats` (`integrity_alerts`).
- **Loud blinding** — disabling event capture via `config` logs a warning; it
  can no longer happen silently.
- **Tightened sysfs perms** — `ai_hints` 0200, `status`/`config` 0644,
  `lockdown` 0640 (root-write only).
- **sysctls** (`/usr/lib/sysctl.d/90-synapse-hardening.conf`) —
  `kptr_restrict=2`, `dmesg_restrict=1`, `kexec_load_disabled=1`, plus the
  2026-08-10 batch: `suid_dumpable=0`, `protected_fifos/regular=2`,
  `ldisc_autoload=0`, no ICMP redirects in or out, loose `rp_filter`,
  `bpf_jit_harden=1`.
- **module blacklist** (`/usr/lib/modprobe.d/90-synapse-blacklist.conf`) —
  dccp/sctp/rds/tipc and the FireWire stack refuse to load.

## Auditing this with Lynis

`lynis audit system` scored **66** on 2026-08-10 with zero warnings. Two things
to know before chasing the number:

1. `--pentest` does nothing when you run as root — you get the identical
   privileged scan. Run it **unprivileged** (`lynis audit system --pentest` as
   your normal user) if you want the local-attacker view; that is a different
   report, not a stricter one.
2. Several remaining suggestions are ones we decline on purpose. The reasons
   are written next to each setting in `config/90-synapse-hardening.conf` and
   `config/90-synapse-blacklist.conf` rather than here, so they are read at the
   point of change. In short: `modules_disabled` (breaks DKMS/NVIDIA — use the
   opt-in unit below instead), `sysrq=0` (we keep 16, sync-only, for recovery),
   `unprivileged_bpf_disabled` (we are already stricter than the test), strict
   `rp_filter` (breaks VPN/policy routing), `usb-storage` (we build install
   media), compilers-root-only (this is a dev distro), and `auditd`
   (synguard's kmod already collects syscalls; two collectors would fight).

To stop those from masking real drift, put them in `/etc/lynis/custom.prf` as
`skip-test=` lines so the index tracks things we would actually act on.

## Opt-in: module self-pin (anti-rmmod)

```
echo 1 > /sys/kernel/synapse/lockdown   # pin: rmmod now fails with EBUSY
echo 0 > /sys/kernel/synapse/lockdown   # unpin (required before a DKMS upgrade)
```

A tripwire/speed-bump: a determined root can still write `lockdown=0`, but not
silently. Default is unpinned so kernel-upgrade rebuilds can unload the module.

## Opt-in: seal module loading (strong anti-rootkit)

`synapse-lock-modules.service` sets `kernel.modules_disabled=1` late in boot,
after which **no** module can load or unload until reboot — an attacker with
root can neither `insmod` a rootkit nor `rmmod` the monitor. It is
**disabled by default** because it is signing-independent and absolute:

```
systemctl enable --now synapse-lock-modules.service
```

Trade-off: hot-plugged hardware whose driver is not already resident will not
work until reboot. Confirm everything you need loads at boot first
(`lsmod` after a normal session), then enable.

## Module signature enforcement — automatic, "assume SB on, fall back"

`syn-secureboot` (run each boot by `synapse-secureboot.service`) manages
`module.sig_enforce=1` for you:

- **Secure Boot on + all modules kernel-trusted** → it adds
  `module.sig_enforce=1` to the kernel cmdline. The kernel then refuses any
  module without a trusted signature.
- **Otherwise** → it makes sure enforcement is off, so the machine still boots
  and loads nvidia/synapse_kmod. Modules are still signed; you're just not
  enforcing yet.

Check posture any time: `syn-secureboot status`. Guided enrollment for real
hardware: `syn-secureboot enroll`.

`module.sig_enforce=1` / kernel `lockdown=integrity` are the real wall against
loading an unsigned/untrusted module — but they require the module-signing key
to be **trusted by the kernel**. On a machine with Secure Boot **off** (as
shipped), it is not:

- Secure Boot is **off**, so no shim/MOK keyring is loaded.
- The DKMS key (`/var/lib/dkms/mok.pub`) signs `synapse_kmod` *and* the nvidia
  modules, but the running kernel does not trust it (`/proc/sys/kernel/tainted`
  has the unsigned-module bit set).

Enabling enforcement as-is would make the kernel **reject nvidia and
synapse_kmod at load → no GPU, likely an unbootable system.** Do NOT set
`sig_enforce` before completing enrollment:

1. Enable Secure Boot in firmware and install shim (`shim-signed`) + `mokutil`.
2. `mokutil --import /var/lib/dkms/mok.pub`, set a one-time password.
3. Reboot; enroll the key in the MOK manager (blue screen) with that password.
4. Confirm `mokutil --test-key /var/lib/dkms/mok.pub` reports it enrolled and
   `bootctl status` / `dmesg | grep -i 'Loading .* MOK'` shows it loaded.
5. Only then add `module.sig_enforce=1` to the kernel cmdline. Verify a reboot
   still brings up the GPU before considering it done.
