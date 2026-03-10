# synapse_kmod — SynapseOS Kernel Module

The kernel half of the SynapseOS AI stack.

```
  User Process
      │
      │  AI_CTX_SET("I'm rendering a video")
      │  AI_CTX_QUERY("what's using my disk?")
      ▼
  ┌─────────────────────────────────────┐
  │  synapse_kmod.ko                    │
  │                                     │
  │  • kprobes on execve, open, socket  │
  │  • AI_CTX syscall handlers          │
  │  • /sys/kernel/synapse/ sysfs       │
  │  • Scheduling hint application      │
  └─────────┬─────────────────┬─────────┘
            │ sysfs            │ sysfs
            │ syscall_log      │ ai_hints
            ▼                  ▼
  ┌─────────────────────────────────────┐
  │  synapd (AI Daemon)                 │
  │  • llama.cpp inference              │
  │  • Security classification          │
  │  • Scheduling recommendations       │
  └─────────────────────────────────────┘
```

## Sysfs Interface

```
/sys/kernel/synapse/
  ├── version      (r)   module version + kernel version
  ├── status       (rw)  synapd writes heartbeat; kmod reads health
  ├── ai_hints     (w)   synapd writes scheduling hints
  ├── syscall_log  (r)   captured syscall events (ring buffer drain)
  ├── stats        (r)   event/hint counters
  └── config       (rw)  enable/disable events and scheduling
```

### ai_hints protocol (synapd → kmod)
```
HINT pid=1234 nice=-5 class=interactive
HINT pid=5678 nice=15 class=batch
HINT pid=9012 nice=-15 class=inference
```

### status protocol (synapd → kmod)
```
ALIVE requests=42 active=2 model=1
READY
SHUTDOWN
```

### syscall_log format (kmod → synapd)
```
<timestamp_ns> <pid> <uid> <syscall_nr> <comm> <filename>
1710000000000000000 1234 1000 59 bash /usr/bin/vim
```

## AI_CTX Syscalls (Linux 7.0+)

```c
// Declare process intent to the AI scheduler
struct ai_ctx_set_args args = {
    .flags  = AI_CTX_FLAG_COMPUTE | AI_CTX_FLAG_LATENCY,
    .intent = "compiling a 200k line C++ project",
};
syscall(NR_AI_CTX_SET, &args);

// Read AI-assigned scheduling class
struct ai_ctx_get_result result;
syscall(NR_AI_CTX_GET, &result);
// result.sched_class == AI_SCHED_BATCH
// result.nice_value  == 5
// result.reason      == "compilation batch — deprioritized during active use"

// Ask AI a question directly from your process
struct ai_ctx_query_args q = {
    .prompt     = "what's causing high memory usage?",
    .max_tokens = 256,
    .timeout_ms = 5000,
};
syscall(NR_AI_CTX_QUERY, &q);
// q.response == "Process kswapd0 is actively swapping..."
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

| Parameter | Default | Description |
|-----------|---------|-------------|
| `synapse_events` | `true` | Capture syscall events |
| `synapse_sched` | `true` | Apply AI scheduling hints |
| `synapse_daemon_timeout` | `30` | Seconds before daemon considered dead |
| `synapse_ring_size` | `4096` | Syscall event ring buffer entries |

## Monitored Syscalls

- `execve` / `execveat` — process execution
- `openat` — opens of sensitive paths (`/etc/shadow`, `/boot/`, etc.)
- `socket` / `connect` — new network sockets
- `ptrace` — process inspection (PTRACE_ATTACH)
- `init_module` / `finit_module` — kernel module loading
- `setuid` → root escalation attempts
- `capset` — capability changes

## Architecture Notes

**On Linux < 7.0:** AI_CTX syscalls are implemented via a kprobe on `do_syscall_64`. The shim intercepts syscall numbers 451-453 and dispatches them to the module handlers. Less efficient but works on any 6.8+ kernel.

**On Linux 7.0 (SynapseOS):** Native `ai_ctx_register_handlers()` API is used. Zero overhead on non-AI_CTX syscalls.

**Daemon heartbeat:** If synapd stops sending `ALIVE` heartbeats for >30 seconds, the module automatically reverts all AI scheduling hints back to stock Linux CFS values for every affected process.

**Safety:** The module never blocks user processes. Hint application is best-effort. If the AI is wrong, the scheduler just uses its hint — the process continues running normally.

## License

GPLv2 — SynapseOS Project
