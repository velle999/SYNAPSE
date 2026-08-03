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

    char err[64];           /* why ok is 0, or a partial-read note when it is 1 */
} syn_gguf_t;

/* Read `path`'s metadata header. Returns 1 on success, 0 with out->err set
 * otherwise. Touches only the header — see gguf.c's note on why this is not a
 * multi-GB read. Never blocks on anything but local file I/O. */
int  gguf_read(const char *path, syn_gguf_t *out);

/* "7.24B" / "124M" / "—" for a parameter count. */
void gguf_params_str(long long n, char *buf, size_t len);

#endif /* SYNUI_SYN_GGUF_H */
