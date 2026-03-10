/*
 * synapse_sched.h
 * SynapseOS Project — GPLv2
 */
#pragma once
#include "synapse_kmod.h"

int  synapse_sched_init(void);
void synapse_sched_exit(void);
void synapse_sched_apply_hint(pid_t pid, int nice_delta, ai_sched_class_t cls);
void synapse_sched_daemon_ready(void);
void synapse_sched_daemon_lost(void);
void synapse_sched_set_enabled(bool enabled);
