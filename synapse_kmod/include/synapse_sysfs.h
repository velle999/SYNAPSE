/*
 * synapse_sysfs.h
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once
#include <linux/kobject.h>

int  synapse_sysfs_init(struct kobject **kobj_out);
void synapse_sysfs_exit(struct kobject *kobj);
