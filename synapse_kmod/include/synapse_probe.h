/*
 * synapse_probe.h
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

int     synapse_probe_init(int ring_size);
void    synapse_probe_exit(void);
/* Non-destructive peek at the newest events — the sysfs syscall_log view. */
ssize_t synapse_probe_read_log(char *buf, size_t buf_len);

/*
 * Cursor-based read for /dev/synapse-events. Advances only *cursor, never the
 * ring's tail, so concurrent readers cannot consume each other's events.
 * Adds to *dropped when the writer has lapped this reader.
 */
ssize_t synapse_probe_read_from(char *buf, size_t buf_len,
                                u32 *cursor, u64 *dropped);
u32     synapse_probe_ring_tail(void);
void    synapse_probe_set_enabled(bool enabled);
int     synapse_probe_integrity_check(void);   /* 0 = probes healthy */
