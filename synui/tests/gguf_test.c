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
#include <sys/stat.h>

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

/* A short string array, the shape general.tags and general.languages take. */
static void kv_str_array(FILE *f, const char *k, const char *const *v, int n)
{
    w_str(f, k);
    w_u32(f, T_ARRAY);
    w_u32(f, T_STRING);
    w_u64(f, (uint64_t)n);
    for (int i = 0; i < n; i++) w_str(f, v[i]);
}

/* The bio quotes a memory figure off the file's size on disk, which the header
 * does not carry — so the dump has to go and look. */
static long long bytes_of(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (long long)st.st_size : -1;
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
    printf("  embedding     %s\n", g->is_embedding ? "YES (cannot chat)" : "no");
    printf("  license       %s\n", g->license[0] ? g->license : "-");
    printf("  finetune      %s\n", g->finetune[0] ? g->finetune : "-");

    /* The prose, printed in full: this is what the pane will say, and reading
     * it against a real file is the only way to tell whether it reads like
     * something a person would write. */
    char buf[512];
    gguf_good_at(g, buf, sizeof(buf));
    printf("  good at       %s\n", buf[0] ? buf : "-");
    gguf_langs_str(g, buf, sizeof(buf));
    printf("  speaks        %s\n", buf[0] ? buf : "-");
    gguf_based_on(g, buf, sizeof(buf));
    printf("  based on      %s\n", buf[0] ? buf : "-");
    printf("  bio           ");
    gguf_bio(g, bytes_of(path), buf, sizeof(buf));
    printf("%s\n", buf);
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

    /* "thinking" and "reasoning" both fold to "reasoning" — the pair is here to
     * pin that the folded word is printed ONCE. "llama" and "text-generation"
     * are here because they must be dropped: one is already on screen as Arch
     * and the other is true of everything in the list, so neither can tell two
     * models apart. */
    static const char *const tags[] = {
        "thinking", "reasoning", "coding", "llama", "text-generation",
        "instruction-following",
    };
    static const char *const langs[] = { "en", "fr", "cy" };

    w_u32(f, 0x46554747u);   /* "GGUF" */
    w_u32(f, 3);             /* version */
    w_u64(f, 291);           /* tensor count */
    w_u64(f, 18);            /* kv count */

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
    kv_str_array(f, "general.tags", tags, 6);
    kv_str_array(f, "general.languages", langs, 3);
    kv_str(f, "general.license", "apache-2.0");
    kv_str(f, "general.finetune", "Instruct");
    /* The base model's organisation arrives BEFORE general.organization, which
     * is the order that catches a fallback written as "last writer wins". */
    kv_str(f, "general.base_model.0.organization", "Someone Else");
    kv_str(f, "general.organization", "Synapse");

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

    assert(g.is_embedding == 0);
    assert(strcmp(g.license, "apache-2.0") == 0);
    assert(strcmp(g.finetune, "Instruct") == 0);
    /* general.organization outranks the base model's, whichever order the two
     * arrive in. */
    assert(strcmp(g.org, "Synapse") == 0);

    /* ── The prose ── */
    char pr[512];

    /* The noise tags are gone, and the two that fold together are printed
     * once. Exact string, because the whole point of this line is that it
     * reads like a sentence rather than a tag dump. */
    gguf_good_at(&g, pr, sizeof(pr));
    assert(strcmp(pr, "reasoning \xc2\xb7 coding \xc2\xb7 "
                      "following instructions") == 0);

    /* An unknown code is shown rather than dropped: "Welsh" would be better
     * than "CY", but "CY" is much better than silently claiming it speaks
     * only English and French. */
    gguf_langs_str(&g, pr, sizeof(pr));
    assert(strcmp(pr, "English, French, CY") == 0);

    /* Mistral Nemo lists nine languages, which is wider than the row they are
     * drawn in. Named to a fixed count and then counted, so the tail is lost
     * to a "+3 more" that admits it rather than to a pixel clip that does
     * not. Built by hand: nine languages in the synthetic file would be nine
     * more keys pinning nothing else. */
    syn_gguf_t many = { .ok = 1, .n_langs = 9 };
    static const char *const nine[] = { "en", "fr", "de", "es", "it",
                                        "pt", "ru", "zh", "ja" };
    for (int i = 0; i < 9; i++)
        snprintf(many.langs[i], sizeof(many.langs[i]), "%s", nine[i]);
    gguf_langs_str(&many, pr, sizeof(pr));
    assert(strcmp(pr, "English, French, German, Spanish, Italian, "
                      "Portuguese +3 more") == 0);

    gguf_based_on(&g, pr, sizeof(pr));
    assert(strcmp(pr, "Synapse") == 0);   /* no base model named in this file */

    /* 7.24B is mid-size; Q4_K_M is the balanced quantisation; 32768 tokens is
     * about 24 thousand words. All three have to survive into the sentence, or
     * the pane is back to printing numbers nobody can act on. */
    gguf_bio(&g, 4368439584LL, pr, sizeof(pr));
    printf("  bio           %s\n", pr);
    assert(strstr(pr, "mid-size 7B model"));
    assert(strstr(pr, "from Synapse"));
    assert(strstr(pr, "the Instruct fine-tune"));
    assert(strstr(pr, "Q4_K_M"));
    assert(strstr(pr, "24 thousand words"));
    assert(strstr(pr, "GB of memory"));
    /* Never a memory claim when the size is not known. */
    gguf_bio(&g, -1, pr, sizeof(pr));
    assert(!strstr(pr, "GB of memory"));

    /* A buffer far too small must truncate at a boundary it wrote, not run
     * off the end — the bio is assembled by repeated appends and this is the
     * case where one of them does not fit. */
    char tiny[24];
    gguf_bio(&g, 4368439584LL, tiny, sizeof(tiny));
    assert(strlen(tiny) < sizeof(tiny));

    char pbuf[24];
    gguf_params_str(g.params, pbuf, sizeof(pbuf));
    assert(strcmp(pbuf, "7.24B") == 0);
    gguf_params_str(124000000LL, pbuf, sizeof(pbuf));
    assert(strcmp(pbuf, "124M") == 0);
    gguf_params_str(-1, pbuf, sizeof(pbuf));
    assert(strcmp(pbuf, "\xe2\x80\x94") == 0);

    /*
     * An embedding model, shaped like the nomic-embed-text file that sits in
     * velle's model directory: no chat template, and a pooling_type nothing
     * else writes. This is the case the panel used to describe as
     * "Arch nomic-bert", which is true and tells a person nothing — loading it
     * costs a multi-GB read and answers with vectors.
     */
    char epath[] = "/tmp/synui-gguf-embed-XXXXXX";
    int efd = mkstemp(epath);
    assert(efd >= 0);
    FILE *ef = fdopen(efd, "wb");
    assert(ef);

    w_u32(ef, 0x46554747u);
    w_u32(ef, 3);
    w_u64(ef, 112);
    w_u64(ef, 5);
    kv_str(ef, "general.architecture", "nomic-bert");
    kv_str(ef, "general.name", "nomic-embed-text-v1.5");
    kv_u32(ef, "nomic-bert.context_length", 2048);
    kv_u32(ef, "nomic-bert.pooling_type", 1);
    kv_u32(ef, "general.file_type", 1);
    fclose(ef);

    syn_gguf_t emb;
    assert(gguf_read(epath, &emb) == 1);
    dump(&emb, epath);
    unlink(epath);

    assert(emb.is_embedding == 1);
    assert(emb.has_template == 0);
    gguf_bio(&emb, 274290560LL, pr, sizeof(pr));
    assert(strstr(pr, "cannot hold a conversation"));
    /* And it says so INSTEAD of the size-and-quality patter, which would be
     * advice about a choice that should not be made at all. */
    assert(!strstr(pr, "GB of memory"));

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
