/*
 * gguf_test — the metadata reader, against a file this test writes itself.
 *
 * Run with a path, it dumps what the parser made of a REAL model:
 *
 *     ./gguf_test /var/lib/synapd/models/synapse.gguf
 *
 * which is how the field list was checked against files llama.cpp actually
 * produces — a fixture only ever proves the parser agrees with whoever wrote
 * the fixture. Run with no arguments it builds a synthetic header and asserts
 * on it, which is the part `meson test` runs.
 *
 * The synthetic file deliberately includes the three shapes that broke earlier
 * drafts: a long string array (the tokenizer, which must be walked and never
 * read), a key longer than the parser's buffer (whose VALUE still has to be
 * skipped or every field after it is read at the wrong offset), and an
 * architecture-prefixed key arriving before general.architecture does.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>

#include "syn_gguf.h"

enum { T_UINT32 = 4, T_FLOAT32 = 6, T_STRING = 8, T_ARRAY = 9, T_UINT64 = 10 };

static void w_u32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void w_u64(FILE *f, uint64_t v) { fwrite(&v, 8, 1, f); }

static void w_str(FILE *f, const char *s)
{
    w_u64(f, (uint64_t)strlen(s));
    fwrite(s, 1, strlen(s), f);
}

static void kv_str(FILE *f, const char *k, const char *v)
{
    w_str(f, k); w_u32(f, T_STRING); w_str(f, v);
}

static void kv_u32(FILE *f, const char *k, uint32_t v)
{
    w_str(f, k); w_u32(f, T_UINT32); w_u32(f, v);
}

static void kv_u64(FILE *f, const char *k, uint64_t v)
{
    w_str(f, k); w_u32(f, T_UINT64); w_u64(f, v);
}

/* The tokenizer array: 40000 strings, ~300 KB, none of which may be kept. */
static void kv_token_array(FILE *f, const char *k, int n)
{
    w_str(f, k);
    w_u32(f, T_ARRAY);
    w_u32(f, T_STRING);
    w_u64(f, (uint64_t)n);
    for (int i = 0; i < n; i++) {
        char tok[24];
        snprintf(tok, sizeof(tok), "tok%d", i);
        w_str(f, tok);
    }
}

static void kv_f32_array(FILE *f, const char *k, int n)
{
    w_str(f, k);
    w_u32(f, T_ARRAY);
    w_u32(f, T_FLOAT32);
    w_u64(f, (uint64_t)n);
    for (int i = 0; i < n; i++) { float v = (float)i; fwrite(&v, 4, 1, f); }
}

static void dump(const syn_gguf_t *g, const char *path)
{
    char pbuf[24];
    gguf_params_str(g->params, pbuf, sizeof(pbuf));

    printf("%s\n", path);
    printf("  ok            %d%s%s\n", g->ok, g->err[0] ? "  err: " : "", g->err);
    printf("  version       %u\n", g->version);
    printf("  name          %s\n", g->name[0] ? g->name : "-");
    printf("  arch          %s\n", g->arch[0] ? g->arch : "-");
    printf("  quant         %s\n", g->quant[0] ? g->quant : "-");
    printf("  size_label    %s\n", g->size_label[0] ? g->size_label : "-");
    printf("  params        %lld (%s)\n", g->params, pbuf);
    printf("  context       %lld\n", g->ctx);
    printf("  layers        %d\n", g->n_layers);
    printf("  embedding     %d\n", g->n_embd);
    printf("  tensors       %lld\n", g->n_tensors);
    printf("  chat template %s\n", g->has_template ? "yes" : "NO (synapd must guess)");
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        syn_gguf_t g;
        gguf_read(argv[1], &g);
        dump(&g, argv[1]);
        return g.ok ? 0 : 1;
    }

    char path[] = "/tmp/synui-gguf-test-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *f = fdopen(fd, "wb");
    assert(f);

    /* A key longer than gguf.c's 256-byte key buffer. Its value is a string,
     * and if that string is not skipped, everything after it is read from the
     * middle of it — the failure this case exists to catch is not an error,
     * it is plausible-looking garbage in every later field. */
    char longkey[400];
    memset(longkey, 'k', sizeof(longkey) - 1);
    longkey[sizeof(longkey) - 1] = '\0';

    w_u32(f, 0x46554747u);   /* "GGUF" */
    w_u32(f, 3);             /* version */
    w_u64(f, 291);           /* tensor count */
    w_u64(f, 12);            /* kv count */

    /* block_count BEFORE general.architecture, on purpose: the parser matches
     * the suffix rather than building "llama.block_count" from an architecture
     * it has not read yet. */
    kv_u32(f, "llama.block_count", 32);
    kv_token_array(f, "tokenizer.ggml.tokens", 40000);
    kv_str(f, "general.architecture", "llama");
    kv_str(f, longkey, "this value must be skipped, not parsed");
    kv_str(f, "general.name", "Synapse Test 7B Instruct");
    kv_u32(f, "general.file_type", 15);          /* Q4_K_M */
    kv_u64(f, "general.parameter_count", 7241732096ULL);
    kv_str(f, "general.size_label", "7B");
    kv_u32(f, "llama.context_length", 32768);
    kv_u32(f, "llama.embedding_length", 4096);
    kv_f32_array(f, "llama.attention.scores", 512);
    kv_str(f, "tokenizer.chat_template", "{{ bos }}{% for m in messages %}…");

    fclose(f);

    syn_gguf_t g;
    int rc = gguf_read(path, &g);
    dump(&g, path);
    unlink(path);

    assert(rc == 1);
    assert(g.ok == 1);
    assert(g.err[0] == '\0');            /* a clean walk to the last key */
    assert(g.version == 3);
    assert(g.n_tensors == 291);
    assert(strcmp(g.arch, "llama") == 0);
    assert(strcmp(g.name, "Synapse Test 7B Instruct") == 0);
    assert(strcmp(g.quant, "Q4_K_M") == 0);
    assert(strcmp(g.size_label, "7B") == 0);
    assert(g.params == 7241732096LL);
    assert(g.ctx == 32768);
    assert(g.n_layers == 32);
    assert(g.n_embd == 4096);
    assert(g.has_template == 1);

    char pbuf[24];
    gguf_params_str(g.params, pbuf, sizeof(pbuf));
    assert(strcmp(pbuf, "7.24B") == 0);
    gguf_params_str(124000000LL, pbuf, sizeof(pbuf));
    assert(strcmp(pbuf, "124M") == 0);
    gguf_params_str(-1, pbuf, sizeof(pbuf));
    assert(strcmp(pbuf, "\xe2\x80\x94") == 0);

    /* Not a GGUF at all: a refusal, not a struct of zeroes presented as fact. */
    char junk[] = "/tmp/synui-gguf-junk-XXXXXX";
    int jfd = mkstemp(junk);
    assert(jfd >= 0);
    assert(write(jfd, "not a model, just some bytes", 28) == 28);
    close(jfd);
    syn_gguf_t bad;
    assert(gguf_read(junk, &bad) == 0);
    assert(bad.ok == 0 && bad.err[0]);
    printf("  reject junk   %s\n", bad.err);
    unlink(junk);

    assert(gguf_read("/nonexistent/model.gguf", &bad) == 0);
    assert(bad.ok == 0 && bad.err[0]);

    printf("gguf_test: OK\n");
    return 0;
}
