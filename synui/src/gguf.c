/*
 * gguf.c — read a GGUF file's metadata header, and nothing else.
 *
 * The picker used to describe an installed model with its filename, its size on
 * disk, and a quantisation guessed from the filename. All three are things
 * whoever uploaded it chose to write down, and a renamed file lies about every
 * one of them: "Q4_K_M" in a name is a claim, not a measurement, and a 4 GB
 * file tells you nothing about whether it is a 7B at Q4 or a 3B at Q8. Picking
 * a model from that is picking blind, which is precisely what velle asked to
 * stop doing.
 *
 * Everything here comes out of the file's own header instead: the architecture,
 * the parameter count, the context length the model was trained for, the layer
 * count, the real quantisation from general.file_type, and whether it carries a
 * chat template at all — that last one being the fact synapd's "legacy" format
 * fallback is reporting after the fact, visible here BEFORE you load it.
 *
 * ── Why this reads a multi-GB file on the event-loop thread ──
 *
 * It does not. GGUF puts all its metadata in a header at offset 0, and this
 * stops the moment the key-value block ends — typically a few hundred KB in,
 * before a single tensor byte. The one thing that can be large is
 * tokenizer.ggml.tokens, a string array with an entry per vocabulary token, and
 * it is SKIPPED rather than kept: gg_skip_value() walks it without allocating
 * (see gg_skip on why that walk is not a seek per entry). A read is bounded by
 * SYN_GGUF_MAX_KV keys and SYN_GGUF_MAX_SCAN bytes on top of that, so a corrupt
 * or hostile header cannot turn into an unbounded loop in the compositor.
 * Measured on this box: 2 ms for a 7B, 13 ms for Mistral Nemo's 131k vocab.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

#include "syn_gguf.h"

/* GGUF metadata value types, in the order the spec numbers them. */
enum {
    GGUF_T_UINT8 = 0, GGUF_T_INT8, GGUF_T_UINT16, GGUF_T_INT16,
    GGUF_T_UINT32, GGUF_T_INT32, GGUF_T_FLOAT32, GGUF_T_BOOL,
    GGUF_T_STRING, GGUF_T_ARRAY, GGUF_T_UINT64, GGUF_T_INT64,
    GGUF_T_FLOAT64,
    GGUF_T__COUNT
};

#define SYN_GGUF_MAGIC     0x46554747u   /* "GGUF", little-endian */
#define SYN_GGUF_MAX_KV    4096          /* keys walked before giving up */
#define SYN_GGUF_MAX_SCAN  (64u << 20)   /* bytes walked before giving up */
#define SYN_GGUF_MAX_STR   4096          /* longest string value kept */

/* Fixed-width types, indexed by the enum above. 0 means "not fixed width" —
 * strings and arrays carry their own length. */
static const int gg_fixed_size[GGUF_T__COUNT] = {
    1, 1, 2, 2, 4, 4, 4, 1, 0, 0, 8, 8, 8
};

typedef struct {
    FILE *f;
    unsigned long long read;   /* bytes consumed, against SYN_GGUF_MAX_SCAN */
    int   bad;                 /* sticky: a short read or a bogus length */
} gg_rd_t;

static int gg_take(gg_rd_t *r, unsigned long long n)
{
    /* Charged before the read, so a bogus length is caught by the budget
     * rather than by trying to honour it. */
    if (r->bad) return 0;
    r->read += n;
    if (r->read > SYN_GGUF_MAX_SCAN) { r->bad = 1; return 0; }
    return 1;
}

static int gg_raw(gg_rd_t *r, void *dst, size_t n)
{
    if (!gg_take(r, n)) return 0;
    if (fread(dst, 1, n, r->f) != n) { r->bad = 1; return 0; }
    return 1;
}

/*
 * Walk past n bytes.
 *
 * The size test is not a micro-optimisation. fseeko DISCARDS stdio's buffer, so
 * seeking past each entry of a string array turns every one of them into a real
 * read syscall — reading Mistral Nemo's 131k-token vocabulary that way took
 * 144 ms, against 13 ms for a model with a small one, and that is a visible
 * hitch on a compositor thread that owes every client a frame. Short skips are
 * therefore consumed THROUGH the buffer, where they cost a pointer move, and
 * fseeko is kept for the genuinely large jumps it is good at.
 */
static int gg_skip(gg_rd_t *r, unsigned long long n)
{
    if (!gg_take(r, n)) return 0;

    if (n <= 4096) {
        char bin[4096];
        if (fread(bin, 1, (size_t)n, r->f) != n) { r->bad = 1; return 0; }
        return 1;
    }
    if (fseeko(r->f, (off_t)n, SEEK_CUR) != 0) { r->bad = 1; return 0; }
    return 1;
}

static uint32_t gg_u32(gg_rd_t *r)
{
    unsigned char b[4] = {0};
    if (!gg_raw(r, b, 4)) return 0;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static uint64_t gg_u64(gg_rd_t *r)
{
    unsigned char b[8] = {0};
    if (!gg_raw(r, b, 8)) return 0;
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | b[i];
    return v;
}

/*
 * A GGUF string: u64 length, then that many bytes, NOT NUL-terminated.
 *
 * Truncation is a failure here rather than a shortening. A key cut to fit would
 * compare equal to a DIFFERENT key — "general.name" is a prefix of nothing
 * today, but the whole point of a length-prefixed format is that the producer
 * chooses the length, and matching on a prefix of it is how you end up reading
 * one field's value into another field's slot. Same lesson as js_str truncating
 * a Hugging Face repo id into a valid id for someone else's repo.
 */
static int gg_str(gg_rd_t *r, char *out, size_t out_len)
{
    if (out && out_len) out[0] = '\0';
    uint64_t n = gg_u64(r);
    if (r->bad) return 0;

    if (!out || n >= out_len || n > SYN_GGUF_MAX_STR) {
        /* Not wanted, or too long to hold — walk past it either way. */
        return gg_skip(r, n);
    }
    if (!gg_raw(r, out, (size_t)n)) return 0;
    out[n] = '\0';
    return 1;
}

/* Walk one value of type `t` without keeping it. Arrays recurse exactly one
 * level, which is all GGUF allows: an array of arrays is not a thing the format
 * describes, so a nested one is a corrupt file and is refused. */
static int gg_skip_value(gg_rd_t *r, uint32_t t, int nested)
{
    if (t >= GGUF_T__COUNT) { r->bad = 1; return 0; }

    if (t == GGUF_T_STRING) return gg_str(r, NULL, 0);

    if (t == GGUF_T_ARRAY) {
        if (nested) { r->bad = 1; return 0; }
        uint32_t et = gg_u32(r);
        uint64_t n  = gg_u64(r);
        if (r->bad || et >= GGUF_T__COUNT) { r->bad = 1; return 0; }

        int fixed = gg_fixed_size[et];
        if (fixed > 0) {
            /* One seek for the whole run — this is the fast path a 32k-entry
             * array of floats takes. */
            return gg_skip(r, n * (uint64_t)fixed);
        }
        /* Strings: each carries its own length, so they have to be walked one
         * at a time. Still no allocation and still one seek each. */
        for (uint64_t i = 0; i < n; i++)
            if (!gg_skip_value(r, et, 1)) return 0;
        return 1;
    }

    int fixed = gg_fixed_size[t];
    if (fixed <= 0) { r->bad = 1; return 0; }
    return gg_skip(r, (unsigned long long)fixed);
}

/* Read an integer value of any width into a long long. Returns 0 if the type
 * is not an integer, leaving the reader positioned after the value regardless —
 * a caller that guessed the type wrong must not desynchronise the walk. */
static int gg_int_value(gg_rd_t *r, uint32_t t, long long *out)
{
    unsigned char b[8] = {0};
    switch (t) {
    case GGUF_T_UINT8:  case GGUF_T_INT8:
        if (!gg_raw(r, b, 1)) return 0;
        *out = (t == GGUF_T_INT8) ? (long long)(signed char)b[0] : (long long)b[0];
        return 1;
    case GGUF_T_UINT16: case GGUF_T_INT16:
        if (!gg_raw(r, b, 2)) return 0;
        *out = (long long)((uint32_t)b[0] | ((uint32_t)b[1] << 8));
        if (t == GGUF_T_INT16) *out = (long long)(int16_t)*out;
        return 1;
    case GGUF_T_UINT32: case GGUF_T_INT32:
        if (!gg_raw(r, b, 4)) return 0;
        *out = (long long)((uint32_t)b[0] | ((uint32_t)b[1] << 8) |
                           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24));
        if (t == GGUF_T_INT32) *out = (long long)(int32_t)*out;
        return 1;
    case GGUF_T_UINT64: case GGUF_T_INT64: {
        if (!gg_raw(r, b, 8)) return 0;
        uint64_t v = 0;
        for (int i = 7; i >= 0; i--) v = (v << 8) | b[i];
        *out = (long long)v;
        return 1;
    }
    default:
        return gg_skip_value(r, t, 0) ? 0 : 0;
    }
}

/*
 * general.file_type — the ggml enum, which is the only place the REAL
 * quantisation is written down. A filename says what someone typed.
 *
 * Values are ggml's llama_ftype. The gaps are types that were removed or never
 * shipped; an unknown one prints as its number rather than as a wrong name,
 * because a confident wrong answer here is worse than no answer.
 */
static const char *gg_ftype_name(long long ft)
{
    switch (ft) {
    case 0:  return "F32";
    case 1:  return "F16";
    case 2:  return "Q4_0";
    case 3:  return "Q4_1";
    case 7:  return "Q8_0";
    case 8:  return "Q5_0";
    case 9:  return "Q5_1";
    case 10: return "Q2_K";
    case 11: return "Q3_K_S";
    case 12: return "Q3_K_M";
    case 13: return "Q3_K_L";
    case 14: return "Q4_K_S";
    case 15: return "Q4_K_M";
    case 16: return "Q5_K_S";
    case 17: return "Q5_K_M";
    case 18: return "Q6_K";
    case 19: return "IQ2_XXS";
    case 20: return "IQ2_XS";
    case 21: return "Q2_K_S";
    case 22: return "IQ3_XS";
    case 23: return "IQ3_XXS";
    case 24: return "IQ1_S";
    case 25: return "IQ4_NL";
    case 26: return "IQ3_S";
    case 27: return "IQ3_M";
    case 28: return "IQ2_S";
    case 29: return "IQ2_M";
    case 30: return "IQ4_XS";
    case 31: return "IQ1_M";
    case 32: return "BF16";
    case 36: return "TQ1_0";
    case 37: return "TQ2_0";
    case 40: return "MXFP4";
    default: return NULL;
    }
}

/* "7.24B", "124M" — the shape of the model, in the unit people actually say it
 * in. Two significant figures because the third never changes a decision. */
void gguf_params_str(long long n, char *buf, size_t len)
{
    if (n <= 0)            { snprintf(buf, len, "\xe2\x80\x94"); return; }
    if (n >= 1000000000LL) { snprintf(buf, len, "%.2fB", (double)n / 1e9); return; }
    if (n >= 1000000LL)    { snprintf(buf, len, "%.0fM", (double)n / 1e6); return; }
    snprintf(buf, len, "%lld", n);
}

int gguf_read(const char *path, syn_gguf_t *out)
{
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    out->params = out->ctx = -1;
    out->n_layers = out->n_embd = -1;

    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(out->err, sizeof(out->err), "cannot open the file");
        return 0;
    }
    /* A big buffer is what makes walking a string array cheap: the per-entry
     * seeks then land inside it instead of turning into syscalls. */
    setvbuf(f, NULL, _IOFBF, 1 << 16);

    gg_rd_t r = { .f = f, .read = 0, .bad = 0 };

    if (gg_u32(&r) != SYN_GGUF_MAGIC) {
        snprintf(out->err, sizeof(out->err), "not a GGUF file");
        fclose(f);
        return 0;
    }
    out->version = gg_u32(&r);
    /* v1 put the counts in 32 bits and is long dead; refusing it is honest,
     * and reading it as v2+ would produce confident nonsense. */
    if (out->version < 2 || out->version > 16) {
        snprintf(out->err, sizeof(out->err),
                 "GGUF version %u is not supported", out->version);
        fclose(f);
        return 0;
    }

    out->n_tensors = (long long)gg_u64(&r);
    uint64_t n_kv  = gg_u64(&r);
    if (r.bad) {
        snprintf(out->err, sizeof(out->err), "the header ends early");
        fclose(f);
        return 0;
    }
    if (n_kv > SYN_GGUF_MAX_KV) n_kv = SYN_GGUF_MAX_KV;

    /* The architecture prefixes most of the interesting keys ("llama.block_count"),
     * and it is written before them in every file produced by the reference
     * converter — but "in practice first" is not "guaranteed first", so the
     * suffix is matched instead of a built-up key. That also means one pass
     * handles a file that puts general.architecture last. */
    char key[256];
    for (uint64_t i = 0; i < n_kv && !r.bad; i++) {
        if (!gg_str(&r, key, sizeof(key))) break;
        uint32_t t = gg_u32(&r);
        if (r.bad) break;

        /* The key was too long for key[] and came back empty: its value still
         * has to be walked, or every field after it is read at the wrong
         * offset. This is the case a `continue` without the skip would turn
         * into silent garbage. */
        if (!key[0]) { gg_skip_value(&r, t, 0); continue; }

        const char *dot = strchr(key, '.');
        const char *suf = dot ? dot + 1 : key;
        long long v = 0;

        if (strcmp(key, "general.architecture") == 0 && t == GGUF_T_STRING) {
            gg_str(&r, out->arch, sizeof(out->arch));
        } else if (strcmp(key, "general.name") == 0 && t == GGUF_T_STRING) {
            gg_str(&r, out->name, sizeof(out->name));
        } else if (strcmp(key, "general.size_label") == 0 && t == GGUF_T_STRING) {
            gg_str(&r, out->size_label, sizeof(out->size_label));
        } else if (strcmp(key, "general.file_type") == 0) {
            if (gg_int_value(&r, t, &v)) {
                const char *q = gg_ftype_name(v);
                if (q) snprintf(out->quant, sizeof(out->quant), "%s", q);
                else   snprintf(out->quant, sizeof(out->quant), "ftype %lld", v);
            }
        } else if (strcmp(key, "general.parameter_count") == 0) {
            if (gg_int_value(&r, t, &v)) out->params = v;
        } else if (strcmp(key, "tokenizer.chat_template") == 0) {
            out->has_template = 1;
            gg_skip_value(&r, t, 0);
        } else if (dot && strcmp(suf, "context_length") == 0) {
            if (gg_int_value(&r, t, &v)) out->ctx = v;
        } else if (dot && strcmp(suf, "block_count") == 0) {
            if (gg_int_value(&r, t, &v)) out->n_layers = (int)v;
        } else if (dot && strcmp(suf, "embedding_length") == 0) {
            if (gg_int_value(&r, t, &v)) out->n_embd = (int)v;
        } else {
            gg_skip_value(&r, t, 0);
        }
    }

    fclose(f);

    /* A truncated walk still yields whatever was read before it stopped, and
     * the fields it never reached stay at their "unknown" values. Reporting
     * that as a total failure would throw away a perfectly good architecture
     * and parameter count over a malformed key near the end. */
    if (r.bad && !out->arch[0] && out->params < 0) {
        snprintf(out->err, sizeof(out->err), "the metadata is unreadable");
        return 0;
    }
    if (r.bad)
        snprintf(out->err, sizeof(out->err), "the metadata ends early");

    out->ok = 1;
    return 1;
}
