/*
 * synapse_evchr.h — /dev/synapse-events
 *
 * The event feed consumers should use. Each open() gets its own cursor, so
 * two readers cannot consume each other's events; see synapse_evchr.c for the
 * single-consumer bug this replaced.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

int  synapse_evchr_init(void);
void synapse_evchr_exit(void);
