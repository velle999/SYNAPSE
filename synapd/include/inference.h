#ifndef INFERENCE_H
#define INFERENCE_H
#include <stddef.h>
#include "synapd.h"

/* One turn of a conversation, as an OpenAI-shaped client sends them. */
typedef struct { const char *role; const char *content; } syn_chat_msg_t;

/* Render a conversation through the model's own chat template, ready to be run
 * with raw=1 — which is the flag that says the caller owns the whole template.
 * NULL when there is no template or the model is being reloaded; the caller
 * falls back to the plain system+prompt route. Caller frees. */
char *inference_render_chat(synapd_state_t *s, const syn_chat_msg_t *m, size_t n);

/* The chat model's size in MiB and its block count, straight from the file —
 * no model need be loaded, which is the state the offload policy reasons about. */
void inference_geometry(const char *model_path, size_t *mib, int *n_layer);
/* Free/total VRAM in MiB on the first GPU ggml can USE. 0/0 = no usable GPU,
 * which means "nothing to manage", NOT "no VRAM free". */
void inference_vram(size_t *free_mib, size_t *total_mib);

int  inference_init(synapd_state_t *s);
/*
 * Release the CHAT model only. Suspend, a model switch and an offload re-fit
 * all come through here, and none of them is about the retrieval embedder —
 * which is why the embedder is a separate object with its own lifetime. See
 * the note at the top of inference.c for the three comments that promised this
 * separation while the code did the opposite.
 */
void inference_destroy(synapd_state_t *s);
/* Chat model AND embedder. Process exit only. */
void inference_shutdown(synapd_state_t *s);
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

/*
 * Why the last load failed, in llama.cpp's own words.
 *
 * "unknown pre-tokenizer type: 'minicpm5'" is the whole diagnosis of a model
 * this build cannot run, and it used to exist only in the journal — the daemon
 * reported a bare failure and the picker showed nothing, so a switch that
 * could never work looked identical to one still in progress.
 *
 * Reset at the start of every load; empty when the last one succeeded or when
 * llama said nothing. inference_error_get() always NUL-terminates.
 */
void inference_error_reset(void);
void inference_error_get(char *buf, size_t len);
#endif
