/*
 * synapd-bench — how fast is this model, on this machine, from here?
 *
 * ⛔ THE NUMBERS COME FROM THE DAEMON, NOT FROM THIS PROCESS. A client can time
 * a round trip and divide by the characters it got back, and the answer will be
 * wrong in three ways at once: it counts the network, it counts the queue, and
 * it guesses at tokenisation (a "~4 chars per token" rule is off by 40% on code
 * and on any language that is not English). synapd reports what it actually
 * did — tokens in, tokens out, and the time each half took — on SYN_MSG_STATUS,
 * and this tool reads those.
 *
 * ⚠ TWO SPEEDS, REPORTED SEPARATELY, because they have different causes:
 *
 *   prefill   the prompt, decoded in batches, parallel, compute-bound.
 *             Slow prefill = not enough of the model is on the GPU.
 *   decode    the answer, one token at a time, memory-bandwidth-bound.
 *             Slow decode with fast prefill = the weights are in the wrong
 *             kind of memory (spilled to RAM, or a CPU build).
 *
 * A single "tokens per second" over the whole turn averages those two together
 * and hides whichever one is broken.
 *
 * ⚠ AND THE WIRE IS THE THIRD NUMBER. wall − (prefill + decode) is everything
 * that is not inference: the LAN round trip, systemd-socket-proxyd, the queue
 * behind another client. On a unix socket it is milliseconds. Over the bridge
 * from a laptop it is the number that decides whether remote inference feels
 * immediate, and it is invisible to any benchmark run on the daemon's own box.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
/* No _GNU_SOURCE here: meson.build adds it project-wide, and defining it again
 * is a warning on every build of this file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "synapd.h"

#define BENCH_DEFAULT_PORT    11435
#define BENCH_DEFAULT_TOKENS  128
#define BENCH_DEFAULT_RUNS    3
#define BENCH_TIMEOUT_SEC     600   /* a cold CPU-only load answers slowly, once */

/* Long enough to be a real prefill rather than a rounding error, fixed so two
 * runs are comparable, and deliberately a question the model cannot answer from
 * one word — a prompt that stops at the first token measures nothing. */
#define BENCH_PROMPT \
    "Explain, in a single paragraph and in plain language, what a compositor " \
    "does in a Wayland desktop: who owns the pixels, who decides where a " \
    "window goes, and what the client is responsible for drawing."

struct sample {
    unsigned prefill_tok, gen_tok;
    double   prefill_ms, gen_ms, wall_ms;
    int      contended;          /* somebody else's request landed in the middle */
};

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

/*
 * Find `key=` as a WHOLE key: preceded by a space or the start of the line, and
 * followed by '='.
 *
 * ⛔ NOT bare strstr(). The status line carries quoted free text —
 * model_name="Mistral Nemo Instruct 2407", and switch_err= holds a whole llama
 * error message — so a key name appearing inside somebody's model name would be
 * read as the field. The same trap synapd's own JSON reader was bitten by: the
 * first hit that is not a key means keep looking, not "absent".
 */
static const char *field(const char *s, const char *key)
{
    size_t klen = strlen(key);
    const char *p = s;

    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;

        const char *k  = p;
        const char *eq = strchr(k, '=');
        const char *sp = strchr(k, ' ');

        /* A token with no '=' before the next space is not a key at all — the
         * line opens with a bare "synapd:" and this is what walks past it. */
        if (!eq || (sp && eq > sp)) { p = sp ? sp + 1 : k + strlen(k); continue; }

        const char *v = eq + 1;
        const char *end;
        if (*v == '"') {
            /* ⛔ THE WHOLE QUOTED VALUE IS STEPPED OVER, and this is the part
             * that a "preceded by a space" rule gets wrong. model_name= is
             * free text out of the GGUF, so it can contain spaces AND an
             * equals sign: a model called `gen_tok=9999 prefill_ms=0.1` puts a
             * perfectly well-formed fake key inside a value, and every
             * position test that looks only at neighbouring characters accepts
             * it. tests/bench_test.c caught exactly that, in this function.
             *
             * An unterminated quote runs to the end of the line rather than
             * looping: a truncated status is a broken status, and reading the
             * remainder as keys would invent fields out of half a sentence. */
            const char *close = strchr(v + 1, '"');
            end = close ? close + 1 : v + strlen(v);
        } else {
            end = v + strcspn(v, " ");
        }

        if ((size_t)(eq - k) == klen && memcmp(k, key, klen) == 0)
            return v;
        p = end;
    }
    return NULL;
}

static double field_num(const char *s, const char *key, double missing)
{
    const char *v = field(s, key);
    return v ? strtod(v, NULL) : missing;
}

/* A quoted value, unquoted into `out`. Empty when the key is absent. */
static void field_str(const char *s, const char *key, char *out, size_t cap)
{
    out[0] = '\0';
    const char *v = field(s, key);
    if (!v) return;
    if (*v == '"') {
        v++;
        const char *end = strchr(v, '"');
        if (!end) return;
        size_t n = (size_t)(end - v);
        if (n >= cap) n = cap - 1;
        memcpy(out, v, n);
        out[n] = '\0';
    } else {
        snprintf(out, cap, "%.*s", (int)strcspn(v, " "), v);
    }
}

/* ── the connection ──────────────────────────────────────────────────────── */

static int dial(const char *sock_path, const char *host, int port, char *err, size_t errcap)
{
    int fd = -1;

    if (host && *host) {
        char portstr[16];
        snprintf(portstr, sizeof portstr, "%d", port);
        struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
        struct addrinfo *res = NULL;
        int rc = getaddrinfo(host, portstr, &hints, &res);
        if (rc != 0) {
            snprintf(err, errcap, "%s: %s", host, gai_strerror(rc));
            return -1;
        }
        for (struct addrinfo *a = res; a; a = a->ai_next) {
            fd = socket(a->ai_family, a->ai_socktype, 0);
            if (fd < 0) continue;
            if (connect(fd, a->ai_addr, a->ai_addrlen) == 0) break;
            close(fd);
            fd = -1;
        }
        freeaddrinfo(res);
        if (fd < 0) {
            /*
             * ⚠ SAY WHAT A TIMEOUT HERE USUALLY MEANS. The bridge is fronted by
             * an nftables allowlist that DROPS an unlisted source rather than
             * refusing it, so the honest error for "you are not on the list" is
             * a connect that never completes. Reporting only "Connection timed
             * out" sends people to look at the daemon, which is fine.
             */
            snprintf(err, errcap,
                     "cannot reach %s:%d: %s\n"
                     "       a timeout here is usually the far end's "
                     "synapd-bridge.nft allowlist — it drops, it does not refuse",
                     host, port, strerror(errno));
            return -1;
        }
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    } else {
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            snprintf(err, errcap, "socket: %s", strerror(errno));
            return -1;
        }
        struct sockaddr_un addr = { .sun_family = AF_UNIX };
        strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);
        if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
            snprintf(err, errcap, "cannot reach %s: %s%s", sock_path, strerror(errno),
                     errno == ENOENT || errno == ECONNREFUSED
                         ? "\n       synapd is not running — systemctl start synapd.socket"
                         : "");
            close(fd);
            return -1;
        }
    }

    struct timeval tv = { .tv_sec = BENCH_TIMEOUT_SEC, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    return fd;
}

static int read_exact(int fd, void *buf, size_t n)
{
    char *p = buf;
    while (n) {
        ssize_t got = read(fd, p, n);
        if (got <= 0) return -1;
        p += got;
        n -= (size_t)got;
    }
    return 0;
}

/* One request, one reply. `out` may be NULL; the payload is drained regardless
 * so the next message on this socket starts where the daemon thinks it does. */
static int ask(int fd, uint8_t type, uint16_t flags,
               const char *payload, char *out, size_t out_cap)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    uint32_t plen = payload ? (uint32_t)strlen(payload) + 1 : 0;

    syn_msg_header_t hdr = {
        .magic        = SYN_MAGIC,
        .version      = SYNAPD_PROTOCOL_VER,
        .msg_type     = type,
        .flags        = flags,
        .payload_len  = plen,
        .request_id   = (uint32_t)getpid(),
        .client_pid   = (uint32_t)getpid(),
        .timestamp_ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec,
    };

    if (write(fd, &hdr, sizeof hdr) != (ssize_t)sizeof hdr) return -1;
    if (plen && write(fd, payload, plen) != (ssize_t)plen)  return -1;

    syn_msg_header_t rep;
    if (read_exact(fd, &rep, sizeof rep) != 0) return -1;
    if (rep.magic != SYN_MAGIC)                return -1;

    if (out && out_cap) out[0] = '\0';
    if (rep.payload_len > 0 && rep.payload_len < SYN_MAX_PAYLOAD) {
        char *buf = malloc(rep.payload_len + 1);
        if (!buf) return -1;
        if (read_exact(fd, buf, rep.payload_len) != 0) { free(buf); return -1; }
        buf[rep.payload_len] = '\0';
        if (out && out_cap) snprintf(out, out_cap, "%s", buf);
        free(buf);
    }
    return (rep.msg_type == SYN_MSG_ERROR) ? -1 : 0;
}

/* ── one measured turn ───────────────────────────────────────────────────── */

static int one_run(int fd, const char *prompt, int max_tokens, struct sample *s)
{
    char status[4096];

    /* What the daemon has answered so far. Anything other than +1 by the time
     * this run finishes means another client's request is what the timings
     * below describe — see the note in report(). */
    if (ask(fd, SYN_MSG_STATUS, 0, NULL, status, sizeof status) != 0) return -1;
    double before = field_num(status, "requests", -1);

    uint16_t flags = (uint16_t)(SYN_QF_RAW |
                                ((unsigned)max_tokens & SYN_QF_TOKENS_MASK));

    uint64_t t0 = now_us();
    if (ask(fd, SYN_MSG_QUERY, flags, prompt, NULL, 0) != 0) return -1;
    s->wall_ms = (double)(now_us() - t0) / 1000.0;

    if (ask(fd, SYN_MSG_STATUS, 0, NULL, status, sizeof status) != 0) return -1;
    double after = field_num(status, "requests", -1);

    s->prefill_tok = (unsigned)field_num(status, "prefill_tok", 0);
    s->gen_tok     = (unsigned)field_num(status, "gen_tok", 0);
    s->prefill_ms  = field_num(status, "prefill_ms", 0);
    s->gen_ms      = field_num(status, "gen_ms", 0);
    s->contended   = (before >= 0 && after >= 0 && (after - before) != 1);

    if (s->gen_tok == 0 && s->prefill_tok == 0) {
        fprintf(stderr,
                "synapd-bench: this synapd does not report per-request timings\n"
                "       the prefill_tok/gen_tok keys arrive with synapd 0.1.0-55;\n"
                "       upgrade the machine running the daemon\n");
        return -1;
    }
    return 0;
}

static double rate(double tok, double ms) { return ms > 0 ? tok * 1000.0 / ms : 0.0; }

/* ── output ──────────────────────────────────────────────────────────────── */

static void report(const struct sample *runs, int n, const char *status,
                   const char *where, int json)
{
    double p_tok = 0, p_ms = 0, g_tok = 0, g_ms = 0, wall = 0;
    int used = 0, contended = 0;

    for (int i = 0; i < n; i++) {
        if (runs[i].contended) { contended++; continue; }
        p_tok += runs[i].prefill_tok; p_ms += runs[i].prefill_ms;
        g_tok += runs[i].gen_tok;     g_ms += runs[i].gen_ms;
        wall  += runs[i].wall_ms;
        used++;
    }
    if (!used) {
        fprintf(stderr, "synapd-bench: every run overlapped another client's "
                        "request — nothing measured\n");
        return;
    }

    /* The wire is what the wall clock saw that the daemon did not account for.
     * Clamped at zero: the two clocks are different machines' when --host is
     * used, and a benchmark reporting -0.2s of network teaches nobody
     * anything. */
    double infer = p_ms + g_ms;
    double wire  = wall - infer;
    if (wire < 0) wire = 0;

    char model_file[128], model_name[160];
    field_str(status, "model_file", model_file, sizeof model_file);
    field_str(status, "model_name", model_name, sizeof model_name);
    int layers = (int)field_num(status, "gpu_layers", -1);
    int ctx    = (int)field_num(status, "ctx_window", 0);

    if (json) {
        printf("{\"where\":\"%s\",\"model_file\":\"%s\",\"model_name\":\"%s\","
               "\"gpu_layers\":%d,\"context\":%d,\"runs\":%d,\"discarded\":%d,"
               "\"prefill_tokens\":%.0f,\"prefill_ms\":%.1f,\"prefill_tps\":%.1f,"
               "\"decode_tokens\":%.0f,\"decode_ms\":%.1f,\"decode_tps\":%.2f,"
               "\"wall_ms\":%.1f,\"wire_ms\":%.1f}\n",
               where, model_file, model_name, layers, ctx, used, contended,
               p_tok, p_ms / used, rate(p_tok, p_ms),
               g_tok, g_ms / used, rate(g_tok, g_ms),
               wall / used, wire / used);
        return;
    }

    printf("\n");
    printf("  model     %s%s%s%s\n",
           model_file[0] ? model_file : "(none)",
           model_name[0] ? " \xc2\xb7 " : "", model_name,
           layers < 0 ? "" : "");
    if (layers >= 0 || ctx > 0) {
        printf("  device    ");
        if (layers < 0)       printf("gpu layers unknown");
        else if (layers == 0) printf("CPU only");
        else                  printf("%d layers offloaded", layers);
        if (ctx > 0) printf(" \xc2\xb7 context %d", ctx);
        printf("\n");
    }
    printf("  from      %s\n", where);
    printf("\n");
    /*
     * ⚠ PER RUN, NOT TOTALS. The rates are the same either way — both halves
     * of the division scale together — but printing the summed tokens beside
     * the summed seconds and calling it "averaged over 2 runs" reads as one
     * run that generated twice as much. The rate is what is being measured;
     * the counts are there to show what it was measured over.
     */
    printf("  prefill   %5.0f tok  %6.2fs  %8.1f tok/s\n",
           p_tok / used, p_ms / used / 1000.0, rate(p_tok, p_ms));
    printf("  decode    %5.0f tok  %6.2fs  %8.2f tok/s\n",
           g_tok / used, g_ms / used / 1000.0, rate(g_tok, g_ms));
    printf("  wall      %18.2fs\n", wall / used / 1000.0);
    printf("  wire      %18.2fs  %s\n", wire / used / 1000.0,
           wire * 4 > infer ? "\xe2\x86\x90 more than a fifth of the turn"
                            : "(everything that is not inference)");
    if (used > 1) printf("\n  per run, averaged over %d runs\n", used);
    if (contended)
        printf("  %d run(s) discarded: another client was answered mid-run\n",
               contended);
    printf("\n");
}

/*
 * What the daemon answered LAST, whoever asked it — read, never measured.
 *
 * ⚠ THIS GENERATES NOTHING. It is one STATUS round trip, which is why a
 * settings pane can call it every time it draws: running a real benchmark to
 * fill in a row would burn seconds of GPU and a few hundred tokens each time
 * somebody opened a window.
 *
 * ⛔ AND IT IS NOT THIS PROCESS'S MEASUREMENT. The numbers belong to whatever
 * request the daemon handled most recently — a vibe turn, chibi, the command
 * bar. That makes it a fair picture of how the machine is behaving and a bad
 * one for comparing two models, because nothing here controls the prompt. The
 * caller says "last answer", not "benchmark".
 *
 * Output is key=value on one line: stable, greppable, and free of prose that
 * would ship English inside every other language when a GUI drew it.
 */
static int report_last(const char *status, int json)
{
    if (!field(status, "gen_tok")) {
        fprintf(stderr, "synapd-bench: this synapd does not report per-request "
                        "timings (needs 0.1.0-55)\n");
        return 1;
    }
    double p_tok = field_num(status, "prefill_tok", 0);
    double p_ms  = field_num(status, "prefill_ms", 0);
    double g_tok = field_num(status, "gen_tok", 0);
    double g_ms  = field_num(status, "gen_ms", 0);

    char model_file[128];
    field_str(status, "model_file", model_file, sizeof model_file);

    if (json) {
        printf("{\"decode_tps\":%.2f,\"prefill_tps\":%.1f,\"decode_tokens\":%.0f,"
               "\"prefill_tokens\":%.0f,\"model_file\":\"%s\",\"measured\":%s}\n",
               rate(g_tok, g_ms), rate(p_tok, p_ms), g_tok, p_tok, model_file,
               g_tok > 0 ? "true" : "false");
    } else {
        printf("decode_tps=%.2f prefill_tps=%.1f gen_tok=%.0f prefill_tok=%.0f "
               "model_file=\"%s\"\n",
               rate(g_tok, g_ms), rate(p_tok, p_ms), g_tok, p_tok, model_file);
    }
    return 0;
}

static void usage(void)
{
    /* ⚠ STAYS ENGLISH. Every other string a user reads on this desktop is
     * translated; a --help for an operator tool is the documented exception,
     * and putting it in the catalog would mean translating flag names. */
    fprintf(stderr,
        "usage: synapd-bench [options]\n"
        "\n"
        "  How fast the loaded model answers, measured by the daemon itself.\n"
        "\n"
        "  --host HOST      benchmark another machine's synapd over the LAN\n"
        "                   bridge instead of this one's unix socket\n"
        "  --port PORT      bridge port (default %d)\n"
        "  --socket PATH    unix socket (default %s)\n"
        "  --tokens N       answer budget per run (default %d)\n"
        "  --runs N         measured runs, averaged (default %d)\n"
        "  --prompt TEXT    use this prompt instead of the built-in one\n"
        "  --no-warmup      do not discard the first run\n"
        "  --last           print what the daemon answered LAST and exit,\n"
        "                   without asking it anything (one status round trip)\n"
        "  --json           one line of JSON, for scripts\n"
        "  --help\n"
        "\n"
        "  The first run is discarded by default: it pays for loading the\n"
        "  model and for a cold cache, which is a real cost and not this\n"
        "  model's speed.\n",
        BENCH_DEFAULT_PORT, SYNAPD_SOCKET_PATH,
        BENCH_DEFAULT_TOKENS, BENCH_DEFAULT_RUNS);
}

/*
 * tests/bench_test.c includes this file with SYNAPD_BENCH_TEST defined so it
 * can call the parsing helpers directly. They are static, and splitting three
 * functions into their own translation unit to reach them would be a worse
 * answer than this one line: what the test must pin down is that the SHIPPED
 * parser refuses a key that appears inside somebody's model name, and the only
 * way to be sure of that is to test the shipped parser.
 */
#ifndef SYNAPD_BENCH_TEST
int main(int argc, char *argv[])
{
    const char *host = NULL, *sock = SYNAPD_SOCKET_PATH, *prompt = BENCH_PROMPT;
    int port = BENCH_DEFAULT_PORT, tokens = BENCH_DEFAULT_TOKENS;
    int runs = BENCH_DEFAULT_RUNS, warmup = 1, json = 0, last_only = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        int has_next = (i + 1 < argc);
        if      (!strcmp(a, "--host")   && has_next) host   = argv[++i];
        else if (!strcmp(a, "--port")   && has_next) port   = atoi(argv[++i]);
        else if (!strcmp(a, "--socket") && has_next) sock   = argv[++i];
        else if (!strcmp(a, "--tokens") && has_next) tokens = atoi(argv[++i]);
        else if (!strcmp(a, "--runs")   && has_next) runs   = atoi(argv[++i]);
        else if (!strcmp(a, "--prompt") && has_next) prompt = argv[++i];
        else if (!strcmp(a, "--no-warmup"))          warmup = 0;
        else if (!strcmp(a, "--last"))               last_only = 1;
        else if (!strcmp(a, "--json"))               json   = 1;
        else if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
        else { fprintf(stderr, "synapd-bench: unknown option %s\n", a); usage(); return 2; }
    }

    if (runs < 1)   runs = 1;
    if (tokens < 1) tokens = BENCH_DEFAULT_TOKENS;
    if ((unsigned)tokens > SYN_QF_TOKENS_MASK) tokens = SYN_QF_TOKENS_MASK;

    char where[256];
    if (host) snprintf(where, sizeof where, "%s:%d", host, port);
    else      snprintf(where, sizeof where, "%s", sock);

    char err[512] = "";
    int fd = dial(sock, host, port, err, sizeof err);
    if (fd < 0) {
        fprintf(stderr, "synapd-bench: %s\n", err);
        return 1;
    }

    if (last_only) {
        char status[4096] = "";
        int rc = ask(fd, SYN_MSG_STATUS, 0, NULL, status, sizeof status);
        close(fd);
        if (rc != 0) {
            fprintf(stderr, "synapd-bench: %s did not answer a status request\n", where);
            return 1;
        }
        return report_last(status, json);
    }

    struct sample *samples = calloc((size_t)runs, sizeof *samples);
    if (!samples) { close(fd); return 1; }

    if (warmup) {
        struct sample discard;
        if (!json) {
            fprintf(stderr, "  warming up (first answer pays for the load)...\r");
            fflush(stderr);
        }
        if (one_run(fd, prompt, tokens, &discard) != 0) {
            fprintf(stderr, "synapd-bench: the warm-up query failed — "
                            "is a model loaded?\n");
            free(samples);
            close(fd);
            return 1;
        }
    }

    for (int i = 0; i < runs; i++) {
        if (!json) {
            fprintf(stderr, "  run %d of %d...                              \r",
                    i + 1, runs);
            fflush(stderr);
        }
        if (one_run(fd, prompt, tokens, &samples[i]) != 0) {
            fprintf(stderr, "synapd-bench: run %d failed\n", i + 1);
            free(samples);
            close(fd);
            return 1;
        }
    }
    if (!json) fprintf(stderr, "                                            \r");

    char status[4096] = "";
    ask(fd, SYN_MSG_STATUS, 0, NULL, status, sizeof status);
    report(samples, runs, status, where, json);

    free(samples);
    close(fd);
    return 0;
}
#endif /* SYNAPD_BENCH_TEST */
