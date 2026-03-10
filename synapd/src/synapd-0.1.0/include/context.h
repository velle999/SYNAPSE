#ifndef CONTEXT_H
#define CONTEXT_H
#include "synapd.h"
#include <stdint.h>
#include <sys/types.h>

int      context_init(synapd_state_t *s);
void     context_push(synapd_state_t *s, ctx_event_type_t type, pid_t pid, const char *data);
void     context_get_summary(synapd_state_t *s, char *out, size_t out_len);
uint32_t context_used_tokens(synapd_state_t *s);
void     context_flush(synapd_state_t *s);
void     context_destroy(synapd_state_t *s);
#endif
