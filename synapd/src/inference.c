/*
 * inference.c — llama.cpp bridge
 *
 * Manages GGUF model loading, context, and token generation.
 * All inference is synchronous per-call; the socket server
 * issues calls from a thread pool so parallelism is handled above.
 *
 * Supports:
 *   - CPU inference (always available)
 *   - GPU offload via Vulkan / CUDA / ROCm (auto-detected)
 *   - NPU offload via RKNN (Rockchip, ARM SBCs)
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>

#include "synapd.h"
#include "inference.h"
#include "log.h"

/* llama.cpp C API */
#include "llama.h"
#include "ggml-backend.h"
#include "gguf.h"

/* Offload every layer. llama clamps this down to the model's real layer count. */
#define GPU_LAYERS_ALL 999

/* ── Internal state ───────────────────────────────────────── */
struct synapd_inference {
    struct llama_model   *model;
    struct llama_context *ctx;
    struct llama_sampler *sampler;
    pthread_mutex_t       lock;       /* one inference at a time per ctx */
    char                  model_path[512];
    uint32_t              context_size;
    int                   n_threads;
    int                   n_gpu_layers;

    /* Stats */
    uint64_t total_tokens_in;
    uint64_t total_tokens_out;
    double   total_inference_ms;
};

/* ── GPU layer auto-detection ─────────────────────────────── */

/* Read the model's true layer count from its GGUF metadata ("<arch>.block_count").
 * Returns 0 if it can't be determined. Metadata only — no tensor data is read. */
static int gguf_block_count(const char *model_path) {
    struct gguf_init_params p = { .no_alloc = true, .ctx = NULL };
    struct gguf_context *gg = gguf_init_from_file(model_path, p);
    if (!gg) return 0;

    int n_layer = 0;
    int64_t arch_id = gguf_find_key(gg, "general.architecture");
    if (arch_id >= 0) {
        char key[128];
        snprintf(key, sizeof(key), "%s.block_count", gguf_get_val_str(gg, arch_id));
        int64_t kid = gguf_find_key(gg, key);
        if (kid >= 0) n_layer = (int)gguf_get_val_u32(gg, kid);
    }
    gguf_free(gg);
    return n_layer;
}

/*
 * Ask ggml what it can actually USE — not what is plugged into the PCI bus.
 *
 * This used to shell out to lspci and return a hardcoded 28 whenever it saw a
 * display controller. That is a lie in the one case that matters: when libllama
 * is built WITHOUT a CUDA backend the card is still on the bus, so synapd logged
 * "detected GPU ... gpu_layers=28" and then ran every layer on the CPU. It stayed
 * that way for a day, because the log never contradicted itself.
 *
 * ggml_backend_dev_* only reports backends that were compiled in and are usable,
 * so a CPU-only libllama now says so out loud instead of pretending.
 */
static int detect_gpu_layers(const char *model_path, off_t model_bytes) {
    ggml_backend_dev_t gpu = NULL;

    for (size_t i = 0, n = ggml_backend_dev_count(); i < n; i++) {
        ggml_backend_dev_t dev = ggml_backend_dev_get(i);
        if (ggml_backend_dev_type(dev) == GGML_BACKEND_DEVICE_TYPE_GPU) {
            gpu = dev;
            break;
        }
    }

    if (!gpu) {
        syn_log(LOG_WARNING, "inference: libllama has no usable GPU backend — "
                          "running on CPU. Install synapse-llama-cuda for GPU offload.");
        return 0;
    }

    size_t vram_free = 0, vram_total = 0;
    ggml_backend_dev_memory(gpu, &vram_free, &vram_total);
    syn_log(LOG_INFO, "inference: GPU backend %s (%s), VRAM %zu/%zu MiB free",
            ggml_backend_dev_name(gpu), ggml_backend_dev_description(gpu),
            vram_free / (1024 * 1024), vram_total / (1024 * 1024));

    int n_layer = gguf_block_count(model_path);
    if (n_layer <= 0 || model_bytes <= 0) {
        /* Can't size it — llama clamps an over-large count to the real one. */
        syn_log(LOG_INFO, "inference: model geometry unknown, offloading all layers");
        return GPU_LAYERS_ALL;
    }

    /* The weights are what the file size accounts for; the KV cache and compute
     * buffers are not. At 4096 ctx a 7B needs roughly 0.7 GiB of them, so keep
     * a 1 GiB reserve before deciding everything fits. */
    const size_t reserve = 1024ull * 1024 * 1024;

    if (vram_free > (size_t)model_bytes + reserve) {
        syn_log(LOG_INFO, "inference: all %d layers fit in VRAM", n_layer);
        return GPU_LAYERS_ALL;
    }

    /* Tight fit: offload what the free VRAM can actually hold. Layers are close
     * enough to equal-sized to divide the weights by the block count. */
    size_t usable    = vram_free > reserve ? vram_free - reserve : 0;
    size_t per_layer = (size_t)model_bytes / (size_t)n_layer;
    int    layers    = per_layer ? (int)(usable / per_layer) : 0;
    if (layers > n_layer) layers = n_layer;
    if (layers < 0)       layers = 0;

    syn_log(LOG_WARNING, "inference: only %zu MiB VRAM free for a %zu MiB model — "
                      "offloading %d of %d layers, the rest run on CPU",
            vram_free / (1024 * 1024), (size_t)model_bytes / (1024 * 1024),
            layers, n_layer);
    return layers;
}

/* ── Init ─────────────────────────────────────────────────── */
int inference_init(synapd_state_t *s) {
    struct stat st;
    if (stat(s->config.model_path, &st) < 0) {
        syn_log(LOG_ERR, "inference: model not found at %s: %s",
                 s->config.model_path, strerror(errno));
        return -1;
    }

    synapd_inference_t *inf = calloc(1, sizeof(*inf));
    if (!inf) return -1;

    pthread_mutex_init(&inf->lock, NULL);
    strncpy(inf->model_path, s->config.model_path, sizeof(inf->model_path) - 1);
    inf->context_size = s->config.context_window;
    inf->n_threads    = s->config.n_threads;
    inf->n_gpu_layers = s->config.n_gpu_layers < 0
                        ? detect_gpu_layers(s->config.model_path, st.st_size)
                        : s->config.n_gpu_layers;

    syn_log(LOG_INFO, "inference: loading model %s (ctx=%u threads=%d gpu_layers=%d)",
             inf->model_path, inf->context_size, inf->n_threads, inf->n_gpu_layers);

    /* llama.cpp model params */
    struct llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = inf->n_gpu_layers;

    inf->model = llama_load_model_from_file(inf->model_path, mparams);
    if (!inf->model) {
        syn_log(LOG_ERR, "inference: llama_load_model_from_file failed");
        free(inf);
        return -1;
    }

    /* llama.cpp context params */
    struct llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx     = inf->context_size;
    cparams.n_threads = inf->n_threads;

    inf->ctx = llama_new_context_with_model(inf->model, cparams);
    if (!inf->ctx) {
        syn_log(LOG_ERR, "inference: llama_new_context_with_model failed");
        llama_free_model(inf->model);
        free(inf);
        return -1;
    }

    /* Greedy sampler (temperature/top-p added per request in future) */
    inf->sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(inf->sampler, llama_sampler_init_greedy());

    s->inference   = inf;
    s->model_loaded = 1;

    syn_log(LOG_INFO, "inference: model loaded OK — %lld parameters",
             (long long)llama_model_n_params(inf->model));

    return 0;
}

/* ── Core inference call ──────────────────────────────────── */
/*
 * inference_run — run a prompt through the model
 *
 * @s         : daemon state
 * @system_ctx: optional system prompt prefix (may be NULL)
 * @prompt    : user prompt
 * @out_buf   : caller-allocated output buffer
 * @out_len   : size of out_buf
 * @max_tokens: max tokens to generate (0 = use context default)
 * @raw       : if non-zero, suppress the built-in Synapse persona so the caller
 *              controls the whole prompt (agentic clients). system_ctx is still
 *              honoured if supplied; NULL/empty means no system block at all.
 *
 * Returns number of tokens generated, or -1 on error.
 */
int inference_run(synapd_state_t *s,
                  const char *system_ctx,
                  const char *prompt,
                  char *out_buf, size_t out_len,
                  int max_tokens,
                  int raw)
{
    synapd_inference_t *inf = s->inference;
    if (!inf || !inf->model || !inf->ctx) return -1;
    if (!prompt || !out_buf || out_len == 0) return -1;

    pthread_mutex_lock(&inf->lock);

    /* Build full prompt with optional system context */
    char *full_prompt = NULL;
    int plen;

    if (system_ctx && *system_ctx) {
        plen = asprintf(&full_prompt,
            "<|system|>\n%s\n<|user|>\n%s\n<|assistant|>\n",
            system_ctx, prompt);
    } else if (raw) {
        /* Raw agentic client with no system_ctx: send the prompt VERBATIM so the
         * client owns the ENTIRE chat template — turn markers, tool results, and
         * the trailing <|assistant|> generation cue. Wrapping it in one more
         * <|user|> turn made the model leak template tokens and hallucinate extra
         * turns, because it saw a half-applied template it tried to complete. */
        plen = asprintf(&full_prompt, "%s", prompt);
    } else {
        plen = asprintf(&full_prompt,
            "<|system|>\nYou are Synapse, the AI core of SynapseOS. "
            "You assist with system administration, code, and OS-level tasks. "
            "Be concise and precise. Reference system context when relevant.\n"
            "<|user|>\n%s\n<|assistant|>\n",
            prompt);
    }

    if (plen < 0 || !full_prompt) {
        pthread_mutex_unlock(&inf->lock);
        return -1;
    }

    /* Tokenize */
    int n_prompt_tokens = -llama_tokenize(
        llama_model_get_vocab(inf->model),
        full_prompt, strlen(full_prompt),
        NULL, 0,
        true,   /* add_special */
        true    /* parse_special */
    );

    llama_token *tokens = malloc(n_prompt_tokens * sizeof(llama_token));
    if (!tokens) {
        free(full_prompt);
        pthread_mutex_unlock(&inf->lock);
        return -1;
    }

    llama_tokenize(
        llama_model_get_vocab(inf->model),
        full_prompt, strlen(full_prompt),
        tokens, n_prompt_tokens,
        true, true
    );

    free(full_prompt);

    /* Evaluate prompt */
    llama_memory_clear(llama_get_memory(inf->ctx), true);

    /* A prompt longer than the context can never fit — reject cleanly rather
     * than feed a doomed batch. (The caller, e.g. vibe, prunes to fit.) */
    if (n_prompt_tokens >= (int)inf->context_size) {
        syn_log(LOG_WARNING,
                "inference: prompt %d tokens >= context window %u — rejecting",
                n_prompt_tokens, inf->context_size);
        free(tokens);
        pthread_mutex_unlock(&inf->lock);
        return -1;
    }

    /* Feed the prompt in n_batch-sized chunks. llama_decode aborts the whole
     * daemon with GGML_ASSERT(n_tokens_all <= n_batch) if a single batch
     * exceeds n_batch (default 512 here, while n_ctx is much larger). Short
     * OS-assistant queries never hit this; agentic clients that send tool
     * schemas + file contents + history routinely do. Positions continue
     * across chunks from the KV state (no clear between chunks). */
    int n_batch = (int)llama_n_batch(inf->ctx);
    if (n_batch < 1) n_batch = 512;
    for (int off = 0; off < n_prompt_tokens; off += n_batch) {
        int chunk = n_prompt_tokens - off;
        if (chunk > n_batch) chunk = n_batch;
        struct llama_batch batch = llama_batch_get_one(tokens + off, chunk);
        if (llama_decode(inf->ctx, batch) != 0) {
            syn_log(LOG_ERR, "inference: llama_decode (prompt chunk @%d, %d tok) failed",
                    off, chunk);
            free(tokens);
            pthread_mutex_unlock(&inf->lock);
            return -1;
        }
    }
    free(tokens);

    /* Generate */
    if (max_tokens <= 0) max_tokens = 512;
    int max_gen = max_tokens;
    if (n_prompt_tokens + max_gen > (int)inf->context_size)
        max_gen = inf->context_size - n_prompt_tokens - 8;

    size_t out_pos = 0;
    int n_generated = 0;
    const struct llama_vocab *vocab = llama_model_get_vocab(inf->model);

    for (int i = 0; i < max_gen && out_pos < out_len - 1; i++) {
        llama_token tok = llama_sampler_sample(inf->sampler, inf->ctx, -1);

        if (llama_vocab_is_eog(vocab, tok))
            break;

        /* Token → piece */
        char piece[128] = {0};
        int piece_len = llama_token_to_piece(vocab, tok, piece, sizeof(piece) - 1, 0, true);
        if (piece_len < 0) break;

        /* Copy to output buffer */
        size_t copy = piece_len;
        if (out_pos + copy >= out_len - 1)
            copy = out_len - 1 - out_pos;
        memcpy(out_buf + out_pos, piece, copy);
        out_pos += copy;

        /* Continue decoding */
        struct llama_batch next = llama_batch_get_one(&tok, 1);
        if (llama_decode(inf->ctx, next) != 0) break;

        n_generated++;
        llama_sampler_accept(inf->sampler, tok);
    }
    out_buf[out_pos] = '\0';

    /* Update stats */
    inf->total_tokens_in  += n_prompt_tokens;
    inf->total_tokens_out += n_generated;
    atomic_fetch_add(&s->requests_total, 1);

    pthread_mutex_unlock(&inf->lock);
    return n_generated;
}

/* ── Syscall event classifier ─────────────────────────────── */
/*
 * Lightweight call for the kernel module's syscall event stream.
 * Returns a one-line classification tag:
 *   "NORMAL", "SUSPICIOUS:<reason>", or "BLOCK:<reason>"
 */
int inference_classify_syscall(synapd_state_t *s,
                                const char *syscall_ctx,
                                char *out_buf, size_t out_len)
{
    const char *sys =
        "You are a Linux kernel security analyzer inside SynapseOS. "
        "Classify the following syscall sequence as exactly ONE of: "
        "NORMAL, SUSPICIOUS:<brief reason>, or BLOCK:<brief reason>. "
        "Reply with only the classification tag, nothing else.";

    return inference_run(s, sys, syscall_ctx, out_buf, out_len, 32, 0);
}

/* ── Scheduling hint generator ────────────────────────────── */
/*
 * Given process intent strings from AI_CTX syscalls,
 * return a scheduling priority adjustment: -20..+19
 * encoded as a signed int in the output buffer string.
 */
int inference_sched_hint(synapd_state_t *s,
                          const char *proc_intent,
                          int *out_priority_delta)
{
    char buf[64] = {0};
    const char *sys =
        "You are a Linux scheduler advisor inside SynapseOS. "
        "Given a process intent description, reply with ONLY a single "
        "integer from -20 (lower priority) to +19 (higher priority). "
        "No explanation.";

    int r = inference_run(s, sys, proc_intent, buf, sizeof(buf), 8, 0);
    if (r < 0) return -1;

    *out_priority_delta = atoi(buf);
    if (*out_priority_delta < -20) *out_priority_delta = -20;
    if (*out_priority_delta >  19) *out_priority_delta =  19;
    return 0;
}

/* ── Destroy ──────────────────────────────────────────────── */
void inference_destroy(synapd_state_t *s) {
    if (!s->inference) return;
    synapd_inference_t *inf = s->inference;

    pthread_mutex_lock(&inf->lock);
    if (inf->sampler) llama_sampler_free(inf->sampler);
    if (inf->ctx)     llama_free(inf->ctx);
    if (inf->model)   llama_free_model(inf->model);
    pthread_mutex_unlock(&inf->lock);
    pthread_mutex_destroy(&inf->lock);

    syn_log(LOG_INFO,
        "inference: shutdown — total in=%llu out=%llu tokens",
        (unsigned long long)inf->total_tokens_in,
        (unsigned long long)inf->total_tokens_out);

    free(inf);
    s->inference   = NULL;
    s->model_loaded = 0;
}
