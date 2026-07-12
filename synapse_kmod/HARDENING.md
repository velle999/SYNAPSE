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
  `kptr_restrict=2`, `dmesg_restrict=1`, `kexec_load_disabled=1`.

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

## Not enabled here: module signature enforcement

`module.sig_enforce=1` / kernel `lockdown=integrity` are the real wall against
loading an unsigned/untrusted module — but they require the module-signing key
to be **trusted by the kernel**, which it is not on this machine:

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
