/*
 * synapse_sched.h
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once
#include "synapse_kmod.h"

int  synapse_sched_init(void);
void synapse_sched_exit(void);
void synapse_sched_apply_hint(pid_t pid, int nice_delta, ai_sched_class_t cls);
void synapse_sched_daemon_ready(void);
void synapse_sched_daemon_lost(void);

/* Drop hints whose process has exited or whose pid now belongs to someone
 * else. Safe from atomic context; called from the daemon watchdog timer.
 * Without it the per-pid hint table never shrinks and a recycled pid inherits
 * the previous process's saved nice/policy. */
void synapse_sched_sweep(void);
void synapse_sched_set_enabled(bool enabled);
