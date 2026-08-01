#ifndef INFERENCE_H
#define INFERENCE_H
#include "synapd.h"

int  inference_init(synapd_state_t *s);
void inference_destroy(synapd_state_t *s);
int  inference_run(synapd_state_t *s,
                   const char *system_ctx,
                   const char *prompt,
                   char *out_buf, size_t out_len,
                   int max_tokens,
                   int raw);
int  inference_classify_syscall(synapd_state_t *s,
                                 const char *syscall_ctx,
                                 char *out_buf, size_t out_len);
/* Embed one string. Writes n_embd L2-normalised floats to out and returns the
 * dimension, or -1. Does NOT touch the chat model, so it keeps working while
 * that one is asleep. */
int  inference_embed(synapd_state_t *s,
                     const char *text,
                     float *out, int out_cap);

int  inference_sched_hint(synapd_state_t *s,
                           const char *proc_intent,
                           int *out_priority_delta);
#endif
