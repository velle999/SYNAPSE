# synapse_kmod — SynapseOS Kernel Module

The kernel half of SynapseOS's security monitor and AI scheduler. It reports
security-relevant syscalls to `synguard` and applies scheduling hints from
`synapd`.

```
  every process
      │  execve · execveat · openat · socket · connect · ptrace ·
      │  init_module · finit_module · setuid(0)
      ▼
  ┌─────────────────────────────────────────────────┐
  │  synapse_kmod.ko                                │
  │  • kprobes on the syscalls above                │
  │  • an event ring (32768 events by default)      │
  │  • /dev/synapse-events, /sys/kernel/synapse/    │
  │  • scheduling hints, reverted when synapd dies  │
  └──────┬───────────────────────────┬──────────────┘
         │ /dev/synapse-events       │ ai_hints, status
         ▼                           ▲
  ┌──────────────┐            ┌──────────────┐
  │  synguard    │            │  synapd      │
  │  rules, AI   │            │  scheduling  │
  │  verdicts    │            │  hints       │
  └──────────────┘            └──────────────┘
```

## /dev/synapse-events

The event feed, `0440 root:root`. Each open has its own cursor, starting at the
oldest event still in the ring, so readers never take events from each other.
A reader that falls behind is told in the stream, ahead of the events that
follow the gap:

```
!dropped 84
```

One event per line, nine space-separated fields:

```
<timestamp_ns> <pid> <uid> <syscall_nr> <comm> <filename|-> <type:hex> <arg0> <ret|->
1758460000000000000 4242 1000 257 cat /etc/shadow 02 0 -13
```

- `pid` and `uid` are the global pid and the uid in the initial user namespace.
- `comm` and `filename` are escaped: every byte `<= 0x20`, `0x7f` and `\` is
  written as `\xHH`, so each is always one field.
- `filename` is at most 127 bytes of what the caller passed. For `connect` it
  is the destination, `A.B.C.D:port` or `[v6]:port`. For an `execveat` of a
  file descriptor it is `fd:<n>`.
- `type` is the event type (`01` exec, `02` open, `04` socket, `08` ptrace,
  `10` module, `40` setuid). `arg0` is the open flags, the ptrace request or
  the target uid.
- `ret` is the syscall's return value for `openat` (negative is `-errno`), and
  `-` for the probes that report at syscall entry.

## /sys/kernel/synapse/

| File | Mode | What it is |
|---|---|---|
| `status` | `0644` | synapd writes `ALIVE …`, `READY` or `SHUTDOWN`; reads back the last one |
| `ai_hints` | `0200` | synapd writes scheduling hints, one per line |
| `syscall_log` | `0440` | the newest 32 events, for a person with a shell; reading it consumes nothing |
| `stats` | `0444` | counters, including `openat_ret_missed` and `integrity_alerts` |
| `config` | `0644` | `events_enabled=0\|1` and `sched_enabled=0\|1` |
| `lockdown` | `0640` | `1` pins the module so `rmmod` is refused; `0` unpins |
| `version` | `0444` | module and kernel version |
| `sensitive_paths` | `0444` | the path prefixes whose opens are reported, one per line |

### ai_hints protocol (synapd → kmod)
```
HINT pid=1234 nice=-5 class=interactive
HINT pid=5678 nice=15 class=batch
HINT pid=9012 nice=-15 class=inference
```

Classes: `normal`, `interactive`, `batch`, `realtime`, `idle`, `inference`.
`realtime` is a nice bonus, never an RT policy. PID 1, kernel threads and the
core session daemons (`systemd`, `systemd-logind`, `synguard`, `synapd`,
`synui`, `synnet`, `seatd`, `greetd`) are never re-scheduled.

### status protocol (synapd → kmod)
```
ALIVE requests=42 active=2 model=1
READY
SHUTDOWN
```

## Build

```bash
# Build against running kernel
make

# Load
sudo make load

# Status
make status

# Unload
sudo make unload
```

## Module Parameters

Set at load time (`modprobe synapse_kmod synapse_ring_size=65536`); they are
read-only afterwards. The runtime switches are in `/sys/kernel/synapse/config`.

| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `synapse_events` | `true` | | Capture syscall events |
| `synapse_sched` | `true` | | Apply AI scheduling hints |
| `synapse_daemon_timeout` | `30` | 1–3600 | Seconds without a heartbeat before synapd counts as gone |
| `synapse_ring_size` | `32768` | 16–1048576 | Events the ring holds |

A value outside its range fails the load.

## Monitored Syscalls

- `execve`, `execveat` — process execution
- `openat` — opens of the paths in `sensitive_paths`, and writes to per-user
  autostart files (`~/.bashrc`, `~/.config/autostart/`, …)
- `socket`, `connect` — IP sockets and their destinations
- `ptrace` — `PTRACE_TRACEME`, `PTRACE_ATTACH`, `PTRACE_SEIZE`, `PTRACE_PEEKTEXT`
- `init_module`, `finit_module` — kernel module loading
- `setuid` — `setuid(0)` only

Not monitored: `open`, `openat2`, `creat`, io_uring, `setreuid`, `setresuid`,
`setgid`, `capset`, `mount`, `kill`.

### What the probes cannot see

The exec and open probes read the path the caller passed, at syscall entry.
So an open is reported only when that string starts with a watched prefix:
a relative path, `/etc//shadow`, or a symlink to a watched file is not
reported. A path in a page the process has not touched yet — for example a
string literal in a freshly forked child — cannot be read at all, and the
event is dropped (open) or has no filename (exec). The full list, and what
would close each one, is in `docs/SECURITY-ROADMAP.md` §5.

## Testing

```bash
tests/run-vm-tests.sh
```

Boots an installed kernel under qemu with the module and a test `/init`, and
exercises every entry point as root inside the VM: hostile paths, oversized
sysfs writes, a lapped ring, bad parameters, unload. Needs `qemu`, a kernel
with its `linux-headers`, and `cpio`; no root, and nothing on the host is
touched. `GAP` lines are the limits above, confirmed.

## Architecture Notes

**Daemon heartbeat:** If synapd stops sending `ALIVE` heartbeats for longer
than `synapse_daemon_timeout`, the module reverts every AI scheduling hint to
the process's original nice value and policy.

**Probe integrity:** every 5 seconds the module checks that none of its
probes has been disarmed and logs a `SECURITY:` line and counts
`integrity_alerts` if one has. The global `/sys/kernel/debug/kprobes/enabled`
switch is not visible to this check; synguard's canary covers it.

## License

GPLv2 — SynapseOS Project
