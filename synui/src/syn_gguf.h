/*
 * gguf.h — what a GGUF file says about itself.
 *
 * Deliberately free of synui.h: the parser is a file reader with no compositor
 * state in it, which is what lets tests/gguf_test.c link it on its own and run
 * against a real multi-GB model without a Wayland display.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNUI_SYN_GGUF_H
#define SYNUI_SYN_GGUF_H

#include <stddef.h>

/* Held per listed model, so these are deliberately small. A model with more
 * than eight useful tags has not told you more than one with eight. */
#define SYN_GGUF_TAGS   8
#define SYN_GGUF_LANGS  12

typedef struct {
    int  ok;                /* 0 = nothing below is meaningful; see err */
    unsigned version;

    char name[96];          /* general.name — the model's own name */
    char arch[32];          /* general.architecture: llama, qwen2, gemma3 … */
    char quant[24];         /* from general.file_type, NOT from the filename */
    char size_label[16];    /* general.size_label: "7B" when the file says so */

    long long params;       /* general.parameter_count, -1 = not stated */
    long long ctx;          /* <arch>.context_length, -1 = not stated */
    long long n_tensors;
    int  n_layers;          /* <arch>.block_count, -1 = not stated */
    int  n_embd;            /* <arch>.embedding_length, -1 = not stated */

    /* Whether the file carries tokenizer.chat_template. This is the fact behind
     * synapd reporting format="legacy": no template means the daemon has to
     * guess how to frame a turn, and a wrongly-framed prompt still answers
     * fluently, so there is nothing else that would ever tell you. */
    int  has_template;

    /* ── What the model is FOR ──────────────────────────────────────────
     *
     * Everything above is a measurement. None of it answers the question
     * somebody standing in front of the picker is actually asking, which is
     * "should I use this one?". These are the fields that do, and they are
     * already sitting in the same header — general.tags on the file velle was
     * looking at reads [thinking, coding, instruction-following], which is the
     * model describing its own job, and the picker was discarding it. */
    char description[192];  /* general.description — rare, but when it is there
                             * it is the model's own words and outranks ours */
    char license[32];       /* general.license: "apache-2.0" */
    char basename[48];      /* general.basename: "Mistral-Nemo" */
    char finetune[48];      /* general.finetune: "Instruct" */
    char org[48];           /* general.organization, else the base model's */
    char base[64];          /* general.base_model.0.name — what it grew from */

    char tags[SYN_GGUF_TAGS][24];    /* general.tags, minus the noise */
    int  n_tags;
    char langs[SYN_GGUF_LANGS][12];  /* general.languages: "en", "zh" … */
    int  n_langs;

    /* <arch>.pooling_type is present only on embedding models, and it is the
     * one signal that separates "this cannot hold a conversation" from "this
     * merely ships without a chat template". Worth its own flag because
     * loading an embedding model into a chat daemon is the single most
     * expensive mistake this panel can let you make: it costs a multi-GB load
     * and answers with nothing a person can read. */
    int  is_embedding;

    char err[64];           /* why ok is 0, or a partial-read note when it is 1 */
} syn_gguf_t;

/* Read `path`'s metadata header. Returns 1 on success, 0 with out->err set
 * otherwise. Touches only the header — see gguf.c's note on why this is not a
 * multi-GB read. Never blocks on anything but local file I/O. */
int  gguf_read(const char *path, syn_gguf_t *out);

/* "7.24B" / "124M" / "—" for a parameter count. */
void gguf_params_str(long long n, char *buf, size_t len);

/*
 * ── The prose ───────────────────────────────────────────────────────────
 *
 * These turn the fields above into something an end user can decide on. They
 * live here rather than in the renderer because they are the part worth
 * testing — what they say is a judgement, and a judgement that only exists
 * inside a cairo call cannot be checked. gguf_test prints all four.
 *
 * Every one of them writes a NUL-terminated string and writes "" rather than
 * inventing a claim when the header did not say.
 */

/* Two or three sentences: what this model is, what the compression cost it,
 * how much memory it wants, and how much it can read at once. `bytes` is the
 * file's size on disk (-1 if unknown) — the header does not carry it and the
 * memory figure is the fact people ask for first. */
void gguf_bio(const syn_gguf_t *g, long long bytes, char *out, size_t len);

/* "reasoning · coding · following instructions" — general.tags with the
 * family and format noise dropped and the jargon spelt out. */
void gguf_good_at(const syn_gguf_t *g, char *out, size_t len);

/* "English, Chinese" — general.languages, named rather than coded. */
void gguf_langs_str(const syn_gguf_t *g, char *out, size_t len);

/* "Mistral Nemo Base 2407 by Mistralai" — where this one came from. */
void gguf_based_on(const syn_gguf_t *g, char *out, size_t len);

/* What a quantisation code costs you, in words: "Q4_K_M" → "compressed — the
 * usual balance of quality against size". NULL for a code this does not know.
 * Public because the download side asks you to pick one off a list, and the
 * code alone is not a thing anybody can pick on. */
const char *gguf_quant_english(const char *q);

/* One tag in plain English, or NULL when it says nothing worth a line. Public
 * so the Hugging Face side of the picker can describe a repo it has not
 * downloaded yet with the same words as the file it will become. */
const char *gguf_tag_english(const char *tag);

/* A language code as its name in English ("en" → "English"), or the code
 * itself uppercased when it is not one this knows. That fallback is returned
 * from a static buffer, so it is good until the next call — copy it if you
 * need two at once. Single-threaded like the rest of the picker. */
const char *gguf_lang_english(const char *code);

#endif /* SYNUI_SYN_GGUF_H */
