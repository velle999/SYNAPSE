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
 * SynapseOS Project — GPLv2
 * https://synapseos.dev
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
static int detect_gpu_layers(void) {
    /*
     * Probe available accelerators in priority order:
     *   CUDA → ROCm → Vulkan → RKNN (NPU) → CPU only
     *
     * Currently probes via lspci. Future: /sys/class/drm,
     * libvulkan availability, CUDA device enumeration.
     */
    FILE *f = popen("lspci 2>/dev/null | grep -iE 'VGA|3D|Display' | head -1", "r");
    if (!f) return 0;

    char buf[256] = {0};
    fgets(buf, sizeof(buf), f);
    pclose(f);

    if (strlen(buf) > 0) {
        syn_log(LOG_INFO, "inference: detected GPU: %.80s", buf);
        /*
         * Offload as many layers as we reasonably can.
         * VRAM estimation: 7B Q4_K_M ≈ 4.5GB → safe default for 6GB+ cards.
         * User can override with --gpu-layers.
         */
        return 28;  /* safe default for 7B model */
    }
    syn_log(LOG_INFO, "inference: no GPU detected, using CPU only");
    return 0;
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
                        ? detect_gpu_layers()
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
 *
 * Returns number of tokens generated, or -1 on error.
 */
int inference_run(synapd_state_t *s,
                  const char *system_ctx,
                  const char *prompt,
                  char *out_buf, size_t out_len,
                  int max_tokens)
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
    llama_kv_cache_clear(inf->ctx);

    struct llama_batch batch = llama_batch_get_one(tokens, n_prompt_tokens);
    if (llama_decode(inf->ctx, batch) != 0) {
        syn_log(LOG_ERR, "inference: llama_decode (prompt) failed");
        free(tokens);
        pthread_mutex_unlock(&inf->lock);
        return -1;
    }
    free(tokens);

    /* Generate */
    if (max_tokens <= 0) max_tokens = 512;
    int max_gen = max_tokens;
    if (n_prompt_tokens + max_gen > (int)inf->context_size)
        max_gen = inf->context_size - n_prompt_tokens - 8;

    size_t out_pos = 0;
    int n_generated = 0;
    const llama_vocab *vocab = llama_model_get_vocab(inf->model);

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

    return inference_run(s, sys, syscall_ctx, out_buf, out_len, 32);
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

    int r = inference_run(s, sys, proc_intent, buf, sizeof(buf), 8);
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
