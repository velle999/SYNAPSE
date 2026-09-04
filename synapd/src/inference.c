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
#include <math.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>

#include "synapd.h"
#include "inference.h"
#include "profile.h"
#include "log.h"

/* llama.cpp C API */
#include "llama.h"
#include "ggml-backend.h"
#include "gguf.h"

/* Offload every layer. llama clamps this down to the model's real layer count. */
#define GPU_LAYERS_ALL 999

/* ── Why the last load failed ─────────────────────────────────────────────
 *
 * llama.cpp knows exactly why it refused a file — "unknown pre-tokenizer type:
 * 'minicpm5'" is a complete diagnosis — and it says so to a log callback that
 * nothing was reading. The daemon then reported the switch as a bare failure,
 * and the picker reported it as nothing at all, so the only way to find out
 * was `journalctl -u synapd`. That is a fine answer for whoever wrote the
 * daemon and no answer at all for whoever is choosing a model.
 *
 * So the callback is installed, the last ERROR line is kept, and the switch
 * puts it where the picker can read it. Everything still goes to stderr
 * exactly as before — systemd captures that, and the full llama log is worth
 * far more than the one line held here.
 */
static pthread_mutex_t g_llama_err_lock = PTHREAD_MUTEX_INITIALIZER;
static char            g_llama_err[192];

static void llama_log_capture(enum ggml_log_level level, const char *text,
                              void *user) {
    (void)user;

    /*
     * ⛔ DEBUG DOES NOT REACH stderr, AND THAT IS THE WHOLE OF THIS BLOCK.
     *
     * This used to forward every level, on the reasoning that systemd captures
     * stderr and the full llama log is worth more than the one line kept
     * below. That is true of the LOAD, which is what it was written against.
     * It is not true of generation: ggml's CUDA backend logs
     * "CUDA Graph id N reused" at DEBUG **per token**, so a single answer
     * writes thousands of identical lines through journald — measured here at
     * 7162 lines in ten minutes, against a journal already 4 GB on disk. The
     * model's own throughput pays for every one of them.
     *
     * Everything INFO and above still goes through untouched, so the model
     * card, the tensor counts, the offload summary and every warning read
     * exactly as before. Only the per-token trace is dropped.
     *
     * ⚠ CONT IS A CONTINUATION OF WHATEVER CAME BEFORE IT, not a level of its
     * own — llama emits progress as a line at INFO followed by dots at CONT.
     * Dropping a DEBUG line while letting its dots through would leave
     * fragments attached to an unrelated line above, so a CONT inherits the
     * decision made for the line it continues.
     */
    static enum ggml_log_level last_real = GGML_LOG_LEVEL_INFO;
    if (level != GGML_LOG_LEVEL_CONT) last_real = level;
    if (text && last_real != GGML_LOG_LEVEL_DEBUG) fputs(text, stderr);

    if (level != GGML_LOG_LEVEL_ERROR || !text) return;

    /* Keep the FIRST error of a failed load, not the last. llama follows a
     * real diagnosis with generic wrappers — "failed to load model" — and the
     * wrapper would overwrite the sentence that actually says why. Cleared at
     * the start of each load, so "first since then" is what this means. */
    pthread_mutex_lock(&g_llama_err_lock);
    if (!g_llama_err[0]) {
        size_t n = strlen(text);
        while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r')) n--;

        /*
         * llama prefixes its errors with the function that raised them:
         * "llama_model_load: error loading model: unknown pre-tokenizer …".
         * Nobody choosing a model needs the C symbol, so leading
         * "<identifier>: " runs are stripped.
         *
         * Only an IDENTIFIER, and only from the front. Splitting on the last
         * ": " would read better on this particular message and would eat the
         * path out of the next one — "failed to open: /var/lib/…: No such
         * file" would arrive as "No such file". A prefix that is a bare C
         * symbol is never the content; anything with a space in it might be.
         */
        const char *msg = text;
        for (;;) {
            const char *p = msg;
            while (p < text + n &&
                   (*p == '_' || (*p >= 'a' && *p <= 'z') ||
                    (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9')))
                p++;
            if (p == msg || p + 2 > text + n || p[0] != ':' || p[1] != ' ')
                break;
            msg = p + 2;
        }

        n -= (size_t)(msg - text);
        if (n >= sizeof(g_llama_err)) n = sizeof(g_llama_err) - 1;
        memcpy(g_llama_err, msg, n);
        g_llama_err[n] = '\0';
    }
    pthread_mutex_unlock(&g_llama_err_lock);
}

void inference_error_reset(void) {
    pthread_mutex_lock(&g_llama_err_lock);
    g_llama_err[0] = '\0';
    pthread_mutex_unlock(&g_llama_err_lock);
}

void inference_error_get(char *buf, size_t len) {
    if (!buf || !len) return;
    pthread_mutex_lock(&g_llama_err_lock);
    snprintf(buf, len, "%s", g_llama_err);
    pthread_mutex_unlock(&g_llama_err_lock);
}

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
    float                 temperature;
    float                 top_p;
    int                   top_k;

    /* The model's OWN chat template, straight out of its GGUF metadata
     * (tokenizer.chat_template). Owned by the model — do not free.
     * NULL means the GGUF declares none, which selects the legacy fallback
     * format in build_prompt(). */
    const char           *chat_tmpl;
    int                   tmpl_warned;  /* fallback warning is once, not per query */

    /* Model identity, read once at load. Kept for SYN_MSG_STATUS: a client
     * picking a model needs to see what synapd actually detected, not guess
     * from the filename. */
    char                  model_name[128];
    char                  prof_name[PROFILE_MATCH_MAX];  /* "" = no profile matched */
    char                  tmpl_probe[40];  /* rendered opening marker, e.g. "[INST]" */

    /* Stats */
    uint64_t total_tokens_in;
    uint64_t total_tokens_out;
    double   total_inference_ms;
};

/*
 * ── Retrieval embeddings ──
 *
 * Deliberately a second model+context rather than a mode of the first:
 * llama.cpp fixes cparams.embeddings at context creation, and pooled embedding
 * decode wants every token flagged for output, which is the opposite of what
 * generation does.
 *
 * ⛔ AND DELIBERATELY NOT INSIDE synapd_inference. It used to be, with three
 * comments in two files each promising that SYN_MSG_SLEEP "never touches
 * these" and that RAG "keeps answering through a gaming session". It did not:
 * SLEEP calls inference_destroy(), inference_destroy() freed this struct's
 * embed_model along with everything else, and so every suspend — and every
 * model switch — took chibi's memory down with the chat model, to reclaim
 * 274 MB that was never the problem. Three comments describing an intention
 * the code did not implement.
 *
 * Moving it OUT is the fix, rather than a fourth comment or a flag on the
 * destroy: the separation is now structural. This object has its own
 * lifetime, created once and freed only by inference_shutdown(), so there is
 * no longer a code path that could take it away by accident. Its own lock too,
 * so an embed and a chat turn do not block each other.
 */
struct synapd_embed {
    struct llama_model   *model;
    struct llama_context *ctx;
    pthread_mutex_t       lock;
    int                   dim;
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
        /*
         * ⛔ TWO DIFFERENT FAULTS END UP HERE AND THEY HAVE OPPOSITE FIXES.
         * This used to answer both with "Install synapse-llama-cuda", which on
         * 2026-08-28 was printed on a box where synapse-llama-cuda WAS
         * installed and CUDA had said, three lines earlier, "no CUDA-capable
         * device is detected". The message sent the reader to the package
         * manager for a problem that was two missing device nodes.
         *
         * A GPU backend that was compiled in registers itself even when it
         * enumerates nothing, so the registration is what tells the two apart:
         * no registration means the wrong libllama, a registration with zero
         * devices means the library is fine and the driver did not answer.
         */
        const char *gpu_reg = NULL;
        for (size_t i = 0, n = ggml_backend_reg_count(); i < n; i++) {
            ggml_backend_reg_t reg = ggml_backend_reg_get(i);
            const char *name = ggml_backend_reg_name(reg);
            if (name && strcmp(name, "CPU") != 0) {
                gpu_reg = name;
                break;
            }
        }

        if (gpu_reg) {
            syn_log(LOG_WARNING, "inference: %s backend is present but reports NO "
                    "DEVICE — running on CPU. The library is fine; the driver did "
                    "not answer. Check that /dev/nvidiactl and /dev/nvidia0 exist "
                    "(synapd-setup.service makes them) and that this process may "
                    "open them.", gpu_reg);
        } else {
            syn_log(LOG_WARNING, "inference: libllama has no GPU backend compiled in "
                    "— running on CPU. Install synapse-llama-cuda for GPU offload.");
        }
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

/*
 * What the offload policy needs to know about the model, read from the FILE.
 *
 * ⚠ FROM THE FILE, NOT FROM A LOADED MODEL, and deliberately: the policy has
 * to reason about a model that is currently unloaded — that is the whole state
 * it is trying to get out of. Asking the loaded model would answer nothing
 * exactly when the answer matters.
 */
void inference_geometry(const char *model_path, size_t *mib, int *n_layer)
{
    struct stat st;
    if (mib)     *mib     = 0;
    if (n_layer) *n_layer = 0;
    if (!model_path) return;

    if (stat(model_path, &st) == 0 && mib)
        *mib = (size_t)(st.st_size / (1024 * 1024));
    if (n_layer)
        *n_layer = gguf_block_count(model_path);
}

/*
 * Free and total VRAM on the first GPU ggml can actually USE, in MiB.
 *
 * ⚠ ggml_backend_dev_*, NOT nvidia-smi and NOT lspci. detect_gpu_layers()
 * learned this the hard way — a card on the PCI bus says nothing about whether
 * this libllama has a backend for it, and the version that shelled out to
 * lspci reported "detected GPU" while running every layer on the CPU. Same
 * source as the load-time decision, so the two cannot disagree about the card.
 *
 * 0/0 means no usable GPU, which the caller must read as "nothing to manage"
 * rather than as "no VRAM free" — the difference between doing nothing and
 * shedding a model that was never on the card.
 */
void inference_vram(size_t *free_mib, size_t *total_mib)
{
    if (free_mib)  *free_mib  = 0;
    if (total_mib) *total_mib = 0;

    for (size_t i = 0, n = ggml_backend_dev_count(); i < n; i++) {
        ggml_backend_dev_t dev = ggml_backend_dev_get(i);
        if (ggml_backend_dev_type(dev) != GGML_BACKEND_DEVICE_TYPE_GPU)
            continue;
        size_t f = 0, t = 0;
        ggml_backend_dev_memory(dev, &f, &t);
        if (free_mib)  *free_mib  = f / (1024 * 1024);
        if (total_mib) *total_mib = t / (1024 * 1024);
        return;
    }
}

/* Read one GGUF metadata string from a loaded model. Leaves out[] empty (not
 * unterminated) when the key is absent, so callers can test out[0]. */
static void meta_str(struct llama_model *m, const char *key,
                     char *out, size_t out_len)
{
    out[0] = '\0';
    if (llama_model_meta_val_str(m, key, out, out_len) <= 0)
        out[0] = '\0';
}

/*
 * Work out how a prompt will actually open, by rendering the template around a
 * marker and keeping whatever it puts in front.
 *
 * llama.cpp has no "which template did you match" call, so this is the honest
 * way to answer it: apply the thing and look at what comes out. The result is
 * derived from the template itself — "[INST]" for Mistral, "<|im_start|>user"
 * for ChatML — never guessed from the model's name.
 */
static void template_probe(synapd_inference_t *inf) {
    /* Control characters, so it cannot collide with real template markup. */
    static const char MARK[] = "\x01SYNAPD\x01";

    if (!inf->chat_tmpl) {
        snprintf(inf->tmpl_probe, sizeof(inf->tmpl_probe), "legacy");
        return;
    }

    struct llama_chat_message m[1] = { { "user", MARK } };
    char buf[1024];
    int32_t r = llama_chat_apply_template(inf->chat_tmpl, m, 1, true,
                                          buf, sizeof(buf));
    char *at = NULL;
    if (r > 0 && r < (int32_t)sizeof(buf)) {
        buf[r] = '\0';
        at = strstr(buf, MARK);
    }
    if (!at) {
        snprintf(inf->tmpl_probe, sizeof(inf->tmpl_probe), "unknown");
        return;
    }
    *at = '\0';

    /* One line, no padding — this goes in a status field. */
    for (char *p = buf; *p; p++)
        if (*p == '\n' || *p == '\r' || *p == '\t') *p = ' ';
    char *start = buf;
    while (*start == ' ') start++;
    char *end = start + strlen(start);
    while (end > start && end[-1] == ' ') end--;
    *end = '\0';

    /* Bounded explicitly: a template is free to open with something far longer
     * than this field, and truncation here is cosmetic — it is a display hint,
     * not the format itself. */
    snprintf(inf->tmpl_probe, sizeof(inf->tmpl_probe), "%.*s",
             (int)sizeof(inf->tmpl_probe) - 1, start);
}

/* ── Init ─────────────────────────────────────────────────── */
/*
 * Bring the embedder up, once.
 *
 * Absent or unreadable is NOT fatal: synapd's job is the chat model, and a box
 * with no embedder should still answer queries. Embed requests then fail with
 * a clear message instead of the daemon refusing to start.
 *
 * ⚠ IDEMPOTENT, AND THAT IS THE POINT. inference_init() runs again on every
 * suspend/resume, every model switch and every offload re-fit; this returns
 * immediately on all of them, so a 274 MB model is loaded once per daemon
 * lifetime instead of once per chat reload.
 */
static void embed_init(synapd_state_t *s)
{
    struct stat st;

    if (s->embed) return;
    if (!s->config.embed_model_path) return;

    synapd_embed_t *e = calloc(1, sizeof(*e));
    if (!e) return;
    pthread_mutex_init(&e->lock, NULL);

    if (stat(s->config.embed_model_path, &st) == 0) {

        struct llama_model_params eparams = llama_model_default_params();
        eparams.n_gpu_layers = 99;   /* 274 MB -- always worth offloading whole */

        e->model = llama_model_load_from_file(s->config.embed_model_path,
                                                      eparams);
        if (!e->model) {
            syn_log(LOG_WARNING, "inference: embedding model failed to load (%s); "
                    "embeddings disabled", s->config.embed_model_path);
        } else {
            struct llama_context_params ecp = llama_context_default_params();
            ecp.embeddings = true;
            ecp.n_ctx      = 2048;
            /* Pooled embedding decode submits the whole sequence at once, so
             * both batch sizes must cover it -- leave n_ubatch at the default
             * 512 and a 600-token passage silently fails to decode. */
            ecp.n_batch    = 2048;
            ecp.n_ubatch   = 2048;
            ecp.n_threads  = s->config.n_threads;
            /* pooling_type is left UNSPECIFIED on purpose so llama.cpp uses the
             * value baked into the GGUF (nomic-bert.pooling_type = 1, MEAN).
             * Hardcoding MEAN would silently produce wrong vectors for any
             * other embedder someone points this at. */

            e->ctx = llama_init_from_model(e->model, ecp);
            if (!e->ctx) {
                syn_log(LOG_WARNING, "inference: embedding context failed; "
                        "embeddings disabled");
                llama_model_free(e->model);
                e->model = NULL;
            } else {
                e->dim = llama_model_n_embd(e->model);
                syn_log(LOG_INFO, "inference: embeddings ready (%s, dim=%d)",
                        s->config.embed_model_path, e->dim);
            }
        }
    } else {
        syn_log(LOG_INFO, "inference: no embedding model at %s; embeddings disabled",
                s->config.embed_model_path);
    }

    /* Kept even with no model in it: `e->model == NULL` is the "embeddings
     * unavailable" answer inference_embed() gives, and allocating the holder
     * once means that answer never depends on a second allocation later. */
    s->embed = e;
}

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
    inf->temperature  = s->config.temperature;
    inf->top_p        = s->config.top_p;
    inf->top_k        = s->config.top_k;
    inf->n_gpu_layers = s->config.n_gpu_layers < 0
                        ? detect_gpu_layers(s->config.model_path, st.st_size)
                        : s->config.n_gpu_layers;

    /*
     * ⛔ THE CAP IS APPLIED HERE BECAUSE THIS IS THE ONLY PLACE IT CAN BE.
     * llama.cpp fixes n_gpu_layers when the model is created and has no API to
     * move a layer between VRAM and RAM afterwards, so the offload policy
     * cannot "shift" anything — it sets a cap and asks for a reload, and this
     * is where the cap turns into a model. offload.c has the policy;
     * pressure.h has the reasoning.
     *
     * ⚠ IT ONLY EVER LOWERS. A cap above what fits would be the policy
     * overruling detect_gpu_layers()' measurement of the actual card, which is
     * the one number here that was taken from the hardware.
     */
    int cap = atomic_load(&s->offload_cap);
    if (cap >= 0 && inf->n_gpu_layers > cap) {
        syn_log(LOG_INFO, "inference: offload capped at %d layers (was %d) — "
                "something else on this machine needs the VRAM",
                cap, inf->n_gpu_layers);
        inf->n_gpu_layers = cap;
    }

    /*
     * What we are ABOUT to hold, recorded before the load so a failure leaves
     * a truthful figure rather than the previous model's. GPU_LAYERS_ALL is a
     * "clamp me" sentinel, not a count: llama reduces it to the real block
     * count, so publishing 999 would have the policy believing it holds 999
     * layers and never shedding enough.
     */
    {
        int blocks   = gguf_block_count(s->config.model_path);
        int resident = inf->n_gpu_layers;
        if (blocks > 0 && resident > blocks) resident = blocks;
        if (resident < 0) resident = 0;
        atomic_store(&s->offload_resident, resident);
    }

    syn_log(LOG_INFO, "inference: loading model %s (ctx=%u threads=%d gpu_layers=%d)",
             inf->model_path, inf->context_size, inf->n_threads, inf->n_gpu_layers);

    /* Installed here rather than once at startup because this is the only
     * place that cares, and setting it every load is idempotent. The reset
     * immediately after is what makes "the first error" mean "the first error
     * of THIS load" — a previous failure's reason must never be reported
     * against a later file. */
    llama_log_set(llama_log_capture, NULL);
    inference_error_reset();

    /* llama.cpp model params */
    struct llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = inf->n_gpu_layers;

    inf->model = llama_model_load_from_file(inf->model_path, mparams);
    if (!inf->model) {
        syn_log(LOG_ERR, "inference: llama_model_load_from_file failed");
        free(inf);
        return -1;
    }

    /* llama.cpp context params */
    struct llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx     = inf->context_size;
    cparams.n_threads = inf->n_threads;

    inf->ctx = llama_init_from_model(inf->model, cparams);
    if (!inf->ctx) {
        syn_log(LOG_ERR, "inference: llama_init_from_model failed");
        llama_model_free(inf->model);
        free(inf);
        return -1;
    }

    /* ── Model identity ──────────────────────────────────────────────────
     * Read once, here, because everything below depends on knowing WHICH
     * model this is: the chat template, the sampling profile, and what
     * SYN_MSG_STATUS reports to a UI. */
    char m_basename[64] = {0}, m_arch[64] = {0};
    meta_str(inf->model, "general.name",         inf->model_name, sizeof(inf->model_name));
    meta_str(inf->model, "general.basename",     m_basename,      sizeof(m_basename));
    meta_str(inf->model, "general.architecture", m_arch,          sizeof(m_arch));
    if (!inf->model_name[0])
        snprintf(inf->model_name, sizeof(inf->model_name), "(unnamed)");

    /* ── Chat template ───────────────────────────────────────────────────
     * Ask the model what turn format it was trained on instead of assuming.
     * synapd hardcoded a Zephyr-style <|system|>/<|user|>/<|assistant|>
     * prompt for every model it ever loaded, while the models it actually
     * ships (Mistral 7B Instruct, Mistral Nemo) want [INST] ... [/INST].
     * Mistral answers anyway, which is exactly why it went unnoticed —
     * the format was wrong on every query and nothing ever said so.
     *
     * NULL here is not an error: a GGUF with no tokenizer.chat_template
     * falls back to the legacy format in inference_run(). */
    inf->chat_tmpl = llama_model_chat_template(inf->model, NULL);
    template_probe(inf);
    if (inf->chat_tmpl)
        syn_log(LOG_INFO, "inference: chat template from GGUF — model \"%s\", "
                "prompts open with %s", inf->model_name, inf->tmpl_probe);
    else
        syn_log(LOG_WARNING, "inference: model \"%s\" declares NO chat template; "
                "using legacy <|system|>/<|user|> format", inf->model_name);

    /* ── Sampling profile ────────────────────────────────────────────────
     * The template comes out of the GGUF; the sampler settings cannot,
     * because no such key exists. Match the model against the shipped
     * profiles instead, and let an explicit command-line flag win — a
     * flag is someone saying what they want, a profile is only a default. */
    synapd_profile_t prof;
    if (profile_resolve(m_basename, inf->model_name, m_arch, &prof)) {
        int pinned = 0;
        if (prof.have_temperature) {
            if (s->config.temp_set)  pinned = 1;
            else                     inf->temperature = prof.temperature;
        }
        if (prof.have_top_p) {
            if (s->config.top_p_set) pinned = 1;
            else                     inf->top_p = prof.top_p;
        }
        if (prof.have_top_k) {
            if (s->config.top_k_set) pinned = 1;
            else                     inf->top_k = prof.top_k;
        }

        snprintf(inf->prof_name, sizeof(inf->prof_name), "%s", prof.matched);
        syn_log(LOG_INFO, "inference: sampling profile [%s] from %s%s",
                prof.matched, prof.source,
                pinned ? " (some values pinned by command line)" : "");
    } else {
        syn_log(LOG_INFO, "inference: no sampling profile matched \"%s\" — "
                "using built-in defaults", inf->model_name);
    }

    /*
     * Sampler chain.
     *
     * This was hardcoded greedy, which makes an identical prompt return a
     * byte-identical reply forever. That is correct for a one-shot answer and
     * wrong for anything a user can ask twice: Chiron's faction leaders greeted
     * you with the same sentence, word for word, every single visit.
     *
     * Order matters -- narrow the candidates, then flatten the distribution,
     * then draw. temperature 0 keeps the old deterministic behaviour, so a
     * caller that depends on reproducibility can still ask for it.
     */
    inf->sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
    if (inf->temperature <= 0.0f) {
        llama_sampler_chain_add(inf->sampler, llama_sampler_init_greedy());
        syn_log(LOG_INFO, "inference: sampler greedy (deterministic)");
    } else {
        if (inf->top_k > 0)
            llama_sampler_chain_add(inf->sampler, llama_sampler_init_top_k(inf->top_k));
        if (inf->top_p > 0.0f && inf->top_p < 1.0f)
            llama_sampler_chain_add(inf->sampler, llama_sampler_init_top_p(inf->top_p, 1));
        llama_sampler_chain_add(inf->sampler, llama_sampler_init_temp(inf->temperature));
        llama_sampler_chain_add(inf->sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
        syn_log(LOG_INFO, "inference: sampler temp=%.2f top_p=%.2f top_k=%d",
                (double)inf->temperature, (double)inf->top_p, inf->top_k);
    }



    /* Idempotent: only the first load pays for it. Deliberately AFTER the chat
     * model is up, so a box whose embedder is missing or broken still gets its
     * assistant, and a slow embedder never delays the thing people are waiting
     * for. */
    embed_init(s);

    s->inference   = inf;
    s->model_loaded = 1;

    syn_log(LOG_INFO, "inference: model loaded OK — %lld parameters",
             (long long)llama_model_n_params(inf->model));

    return 0;
}

/* ── Prompt construction ──────────────────────────────────── */

/* The built-in persona, used when no caller supplied a system context. */
#define SYNAPSE_PERSONA \
    "You are Synapse, the AI core of SynapseOS. " \
    "You assist with system administration, code, and OS-level tasks. " \
    "Be concise and precise. Reference system context when relevant."

/*
 * Format one system+user exchange with the model's own chat template.
 *
 * Returns a malloc'd prompt, or NULL to mean "no usable template" — the
 * caller then falls back to the legacy format rather than sending the model
 * something half-applied.
 *
 * Note llama_chat_apply_template() is not a Jinja parser; it substring-matches
 * a fixed list of known templates. Verified against both shipped models:
 * Mistral Nemo's elaborate Jinja and Mistral 7B v0.2's both resolve to the
 * [INST] family, and both fold the system role into the first user turn —
 * which matters because v0.2's template rejects a system role outright.
 */
/* Any number of turns, through the model's own template.
 *
 * ⚠ THE WHOLE CONVERSATION, NOT THE LAST TURN. An OpenAI-shaped client sends
 * the history every time, and flattening it into one user message loses the
 * turn markers the model was trained on — it then answers as if the previous
 * exchange were a quotation somebody pasted. The template is the only thing
 * that knows how this model separates turns, so it does the separating. */
static char *apply_chat_messages(synapd_inference_t *inf,
                                 const struct llama_chat_message *msg, size_t n)
{
    if (!inf->chat_tmpl || n == 0) return NULL;

    size_t body = 0;
    for (size_t i = 0; i < n; i++)
        body += strlen(msg[i].content ? msg[i].content : "") + 16;

    /* llama.cpp's own recommendation is 2x the message bytes; the markers it
     * adds are small next to the content. A short prompt still gets headroom. */
    int32_t cap = (int32_t)(body * 2 + 256);
    char *buf = malloc(cap);
    if (!buf) return NULL;

    int32_t r = llama_chat_apply_template(inf->chat_tmpl, msg, n, true, buf, cap);

    /* Undersized: the return value is the exact size needed, so this retries
     * once and cannot loop. Keep one spare byte for the NUL. */
    if (r >= cap) {
        char *nb = realloc(buf, (size_t)r + 1);
        if (!nb) { free(buf); return NULL; }
        buf = nb;
        cap = r + 1;
        r = llama_chat_apply_template(inf->chat_tmpl, msg, n, true, buf, cap);
    }

    if (r < 0 || r >= cap) { free(buf); return NULL; }
    buf[r] = '\0';
    return buf;
}

static char *apply_chat_template(synapd_inference_t *inf,
                                 const char *system_ctx,
                                 const char *user)
{
    struct llama_chat_message msg[2];
    size_t n = 0;
    if (system_ctx && *system_ctx) {
        msg[n].role = "system"; msg[n].content = system_ctx; n++;
    }
    msg[n].role = "user"; msg[n].content = user; n++;
    return apply_chat_messages(inf, msg, n);
}

/* Render a whole conversation the way this model expects to see one.
 *
 * ⚠ TRY the read lock, like inference_describe: WAKE holds the write lock for
 * the length of a multi-gigabyte reload, and an HTTP request that blocked on it
 * would hold its connection open for the duration rather than saying the model
 * is busy.
 *
 * Returns NULL when there is no template or the model is in flux — the caller
 * has a legacy path for the first and an error for the second, and cannot tell
 * them apart, which is why it must treat NULL as "use the plain route". */
char *inference_render_chat(synapd_state_t *s, const syn_chat_msg_t *m, size_t n)
{
    if (!s || !m || n == 0) return NULL;
    if (pthread_rwlock_tryrdlock(&s->model_rw) != 0) return NULL;

    char *out = NULL;
    synapd_inference_t *inf = s->inference;
    if (inf && inf->model && inf->chat_tmpl) {
        struct llama_chat_message *msg = calloc(n, sizeof *msg);
        if (msg) {
            for (size_t i = 0; i < n; i++) {
                msg[i].role = m[i].role ? m[i].role : "user";
                msg[i].content = m[i].content ? m[i].content : "";
            }
            out = apply_chat_messages(inf, msg, n);
            free(msg);
        }
    }
    pthread_rwlock_unlock(&s->model_rw);
    return out;
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
    int plen = 0;

    if (raw && !(system_ctx && *system_ctx)) {
        /* Raw agentic client with no system_ctx: send the prompt VERBATIM so the
         * client owns the ENTIRE chat template — turn markers, tool results, and
         * the trailing generation cue. Wrapping it in one more user turn made the
         * model leak template tokens and hallucinate extra turns, because it saw
         * a half-applied template it tried to complete.
         *
         * This stays byte-exact now that synapd applies templates itself: the
         * whole point of the flag is that synapd must not second-guess a client
         * that already formatted its own conversation. */
        plen = asprintf(&full_prompt, "%s", prompt);
    } else {
        const char *sys = (system_ctx && *system_ctx) ? system_ctx : SYNAPSE_PERSONA;

        full_prompt = apply_chat_template(inf, sys, prompt);
        if (full_prompt) {
            plen = (int)strlen(full_prompt);
        } else {
            /* No usable template — legacy format, exactly as before, so a
             * template-less GGUF behaves the way it always did. */
            if (!inf->tmpl_warned) {
                inf->tmpl_warned = 1;   /* under inf->lock */
                syn_log(LOG_WARNING, "inference: no usable chat template; "
                        "falling back to legacy <|system|>/<|user|> format");
            }
            plen = asprintf(&full_prompt,
                "<|system|>\n%s\n<|user|>\n%s\n<|assistant|>\n", sys, prompt);
        }
    }

    if (plen < 0 || !full_prompt) {
        pthread_mutex_unlock(&inf->lock);
        return -1;
    }

    /* The opening bytes are the turn markers, which is the one thing you cannot
     * infer from a reply: a mis-formatted prompt still produces fluent text.
     * Truncated hard, and DEBUG-only, so a normal log never carries prompt text. */
    syn_log(LOG_DEBUG, "inference: prompt %d bytes, opens: %.80s", plen, full_prompt);

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
/*
 * Embed one string into an L2-normalised vector.
 *
 * Retrieval compares vectors by cosine similarity, and the store chibi already
 * has on disk was written by this same GGUF through ollama. Two things
 * therefore have to match that pipeline exactly or every stored vector becomes
 * noise:
 *
 *   - The text is embedded VERBATIM. nomic's "search_query: " / "search_document: "
 *     task prefixes are the caller's job -- chibi already adds them in
 *     thoth_rag.py before the call, exactly as it did when ollama was serving.
 *     Adding them here too would double them.
 *   - The result is L2-normalised, which is what llama.cpp's own server does.
 *
 * Returns the dimension, or -1.
 */
int inference_embed(synapd_state_t *s, const char *text, float *out, int out_cap) {
    /*
     * ⚠ NOT GUARDED ON s->inference. The embedder is its own object with its
     * own lifetime now, and the whole point of that is that RAG keeps
     * answering while the CHAT model is away — asleep for a suspend, being
     * switched, or shed onto the CPU by the offload policy. Requiring the chat
     * model here would put back exactly the coupling that made chibi's memory
     * go dark every time a game started.
     */
    if (!s || !s->embed || !text || !out) return -1;
    synapd_embed_t *e = s->embed;

    if (!e->ctx || !e->model) return -1;
    if (out_cap < e->dim) return -1;

    pthread_mutex_lock(&e->lock);

    const struct llama_vocab *vocab = llama_model_get_vocab(e->model);

    int n_tok = -llama_tokenize(vocab, text, (int32_t)strlen(text),
                                NULL, 0, true /* add_special */, false);
    if (n_tok <= 0) {
        pthread_mutex_unlock(&e->lock);
        return -1;
    }

    /* Truncate rather than fail: a passage longer than the context is a caller
     * chunking badly, and a short vector beats no vector at retrieval time. */
    int n_ctx_max = (int)llama_n_ctx(e->ctx);
    if (n_tok > n_ctx_max) n_tok = n_ctx_max;

    llama_token *toks = malloc((size_t)n_tok * sizeof(llama_token));
    if (!toks) {
        pthread_mutex_unlock(&e->lock);
        return -1;
    }
    llama_tokenize(vocab, text, (int32_t)strlen(text), toks, n_tok, true, false);

    /* Built by hand rather than llama_batch_get_one(): pooled embeddings need
     * EVERY token flagged for output, where generation flags only the last.
     * Get this wrong and llama_get_embeddings_seq() hands back NULL. */
    struct llama_batch batch = llama_batch_init(n_tok, 0, 1);
    for (int i = 0; i < n_tok; i++) {
        batch.token[i]      = toks[i];
        batch.pos[i]        = i;
        batch.n_seq_id[i]   = 1;
        batch.seq_id[i][0]  = 0;
        batch.logits[i]     = 1;
    }
    batch.n_tokens = n_tok;

    llama_memory_clear(llama_get_memory(e->ctx), true);

    int rc = llama_decode(e->ctx, batch);
    llama_batch_free(batch);
    free(toks);

    if (rc != 0) {
        syn_log(LOG_WARNING, "inference: embed decode failed (rc=%d)", rc);
        pthread_mutex_unlock(&e->lock);
        return -1;
    }

    const float *emb = llama_get_embeddings_seq(e->ctx, 0);
    if (!emb) {
        syn_log(LOG_WARNING, "inference: no pooled embedding returned");
        pthread_mutex_unlock(&e->lock);
        return -1;
    }

    int dim = e->dim;
    double sum = 0.0;
    for (int i = 0; i < dim; i++) sum += (double)emb[i] * (double)emb[i];
    double norm = sqrt(sum);
    if (norm <= 0.0) norm = 1.0;   /* degenerate input; pass through unscaled */
    for (int i = 0; i < dim; i++) out[i] = (float)((double)emb[i] / norm);

    pthread_mutex_unlock(&e->lock);
    return dim;
}

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

/* ── Status detail ────────────────────────────────────────── */
void inference_describe(synapd_state_t *s, char *buf, size_t len) {
    if (!s || !buf || len == 0) return;
    buf[0] = '\0';

    /*
     * TRY the read lock rather than taking it.
     *
     * STATUS has always been answerable during a load — that is why it reports
     * "loading" from an atomic instead of blocking. WAKE holds the WRITE lock
     * for the entire reload of a 7 GB model, so a blocking read here would hang
     * every status poll for the duration, and synui polls status on a timer.
     * A busy lock means the model is in flux, and the model= field already says
     * so; the detail is simply omitted until it settles.
     */
    if (pthread_rwlock_tryrdlock(&s->model_rw) != 0) return;

    synapd_inference_t *inf = s->inference;
    if (inf && inf->model) {
        /* The FILENAME as well as the GGUF's internal name. They are unrelated
         * on purpose — "synapse.gguf" holds "Mistral Nemo Instruct 2407" — so a
         * picker listing a directory has nothing to match its rows against
         * without this, and can only mark a model it switched to itself. */
        const char *slash = strrchr(inf->model_path, '/');
        const char *file  = slash ? slash + 1 : inf->model_path;

        snprintf(buf, len,
                 " model_name=\"%s\" model_file=\"%s\" format=\"%s\" profile=%s "
                 "temp=%.2f top_p=%.2f top_k=%d",
                 inf->model_name, file,
                 inf->tmpl_probe,
                 inf->prof_name[0] ? inf->prof_name : "none",
                 (double)inf->temperature, (double)inf->top_p, inf->top_k);
    }

    pthread_rwlock_unlock(&s->model_rw);
}

/* ── Destroy ──────────────────────────────────────────────── */
/*
 * Release the CHAT model, and only the chat model.
 *
 * ⛔ THIS IS THE ONE EVERY RUNTIME PATH CALLS — suspend, a model switch, an
 * offload re-fit — and none of them is about the embedder. It used to free the
 * embedder too, because the embedder lived in this struct, which is how every
 * suspend and every model switch quietly took chibi's memory down with it for
 * the sake of 274 MB. Three separate comments promised that could not happen.
 * The embedder is a separate object now (see the note at the top of this file),
 * so the promise is kept by the structure rather than by a comment.
 *
 * inference_shutdown() is the one that takes everything.
 */
void inference_destroy(synapd_state_t *s) {
    if (!s->inference) return;
    synapd_inference_t *inf = s->inference;

    pthread_mutex_lock(&inf->lock);
    if (inf->sampler) llama_sampler_free(inf->sampler);
    if (inf->ctx)     llama_free(inf->ctx);
    if (inf->model)   llama_model_free(inf->model);
    pthread_mutex_unlock(&inf->lock);
    pthread_mutex_destroy(&inf->lock);

    syn_log(LOG_INFO,
        "inference: chat model released — total in=%llu out=%llu tokens",
        (unsigned long long)inf->total_tokens_in,
        (unsigned long long)inf->total_tokens_out);

    free(inf);
    s->inference   = NULL;
    s->model_loaded = 0;
    atomic_store(&s->offload_resident, 0);
}

/*
 * Everything, for process exit only.
 *
 * ⚠ The embedder is freed HERE and nowhere else. If a second caller ever
 * appears, the question to answer first is what happens to RAG while that
 * caller's operation is in flight — which is the question the old single
 * destroy never asked.
 */
void inference_shutdown(synapd_state_t *s) {
    inference_destroy(s);

    if (!s->embed) return;
    synapd_embed_t *e = s->embed;

    pthread_mutex_lock(&e->lock);
    if (e->ctx)   llama_free(e->ctx);
    if (e->model) llama_model_free(e->model);
    pthread_mutex_unlock(&e->lock);
    pthread_mutex_destroy(&e->lock);

    free(e);
    s->embed = NULL;
}
