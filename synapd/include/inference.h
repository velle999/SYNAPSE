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

/*
 * Append what synapd DETECTED about the loaded model to a status line:
 * identity, the prompt format it resolved, the sampling profile that matched,
 * and the values actually in force.
 *
 * This is the half a model picker cannot work out for itself. A filename says
 * nothing about whether the turn format was recognised or which profile won,
 * and those are exactly the two things that quietly go wrong.
 *
 * Writes nothing if no model is loaded. Takes the model read lock, so it is
 * safe against a concurrent SLEEP/WAKE.
 */
void inference_describe(synapd_state_t *s, char *buf, size_t len);
#endif
