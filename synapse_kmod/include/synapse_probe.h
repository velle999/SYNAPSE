/*
 * synapse_probe.h
 * SynapseOS Project — GPLv2
 */
#pragma once

int     synapse_probe_init(int ring_size);
void    synapse_probe_exit(void);
ssize_t synapse_probe_read_log(char *buf, size_t buf_len);
void    synapse_probe_set_enabled(bool enabled);
int     synapse_probe_integrity_check(void);   /* 0 = probes healthy */
