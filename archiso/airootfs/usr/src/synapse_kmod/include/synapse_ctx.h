/*
 * synapse_ctx.h
 * SynapseOS Project — GPLv2
 */
#pragma once
#include <linux/types.h>

int  synapse_ctx_init(void);
void synapse_ctx_exit(void);
void synapse_ctx_complete_query(u32 request_id, const char *response);
