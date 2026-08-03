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

/*
 * Read a string array into fixed slots, walking whatever will not fit.
 *
 * Used for exactly two keys — general.tags and general.languages — and never
 * for tokenizer.ggml.tokens, which is the array this file goes out of its way
 * NOT to keep. The distinction is the caller's: this will happily try to hold
 * the first eight entries of a 131k-token vocabulary if asked, so it is only
 * ever asked about keys whose length is a handful.
 *
 * A value too long for a slot comes back empty from gg_str() and is dropped
 * rather than counted, so n_out is a count of things that are actually there.
 * The reader stays positioned correctly either way, which is the only property
 * that must hold: a desynchronised walk reads every field after this one out
 * of the middle of some other value.
 */
static int gg_str_array(gg_rd_t *r, uint32_t t, char *slots, size_t stride,
                        size_t slot_len, int max, int *n_out)
{
    *n_out = 0;
    if (t != GGUF_T_ARRAY) return gg_skip_value(r, t, 0);

    uint32_t et = gg_u32(r);
    uint64_t n  = gg_u64(r);
    if (r->bad || et >= GGUF_T__COUNT) { r->bad = 1; return 0; }

    if (et != GGUF_T_STRING) {
        /* The spec says these keys hold strings. A file that disagrees is not
         * one to guess about — walk the array and leave the field empty. */
        int fixed = gg_fixed_size[et];
        if (fixed <= 0) { r->bad = 1; return 0; }
        return gg_skip(r, n * (uint64_t)fixed);
    }

    for (uint64_t i = 0; i < n; i++) {
        if (*n_out < max) {
            char *dst = slots + (size_t)*n_out * stride;
            if (!gg_str(r, dst, slot_len)) return 0;
            if (dst[0]) (*n_out)++;
        } else if (!gg_str(r, NULL, 0)) {
            return 0;
        }
    }
    return 1;
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

/* ── Prose ──────────────────────────────────────────────────────────────
 *
 * Everything below turns the header into sentences. It is the answer to
 * velle's "there's info but nothing that helps decide to use that an end user
 * would understand": "Arch nomic-bert" is a true statement that tells a person
 * nothing, and "an embedding model — it cannot hold a conversation" is the
 * same fact in a form that stops them loading 261 MB for no reason.
 *
 * The rule throughout: say what the file says, and say nothing when it did
 * not. A model with no tags gets a shorter bio, never an invented one.
 */

/* Case-insensitive equality, for tags a producer may have capitalised. */
static int gg_ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
    }
    return *a == *b;
}

/*
 * A tag in the words a person would use, or NULL to drop it.
 *
 * Dropping is most of the job. The tags on a real file are a mix of what the
 * model DOES ("coding") and what it IS made of ("llama", "minicpm5",
 * "transformers", "text-generation") — and the second kind is either already
 * on screen as Arch or true of every model in the list, so printing it fills
 * the one line this has with words that cannot distinguish anything.
 */
const char *gguf_tag_english(const char *tag)
{
    if (!tag || !tag[0]) return NULL;

    /* Hugging Face qualifies some of its tags: "license:apache-2.0",
     * "base_model:mistralai/...", "region:us". None of them describe a skill,
     * and the licence already has a row of its own. */
    if (strchr(tag, ':')) return NULL;

    static const struct { const char *tag, *english; } map[] = {
        { "thinking",              "reasoning"              },
        { "reasoning",             "reasoning"              },
        { "chain-of-thought",      "reasoning"              },
        { "cot",                   "reasoning"              },
        { "code",                  "coding"                 },
        { "coding",                "coding"                 },
        { "code-generation",       "coding"                 },
        { "instruct",              "following instructions" },
        { "instruction-following", "following instructions" },
        { "instruction-tuned",     "following instructions" },
        { "chat",                  "conversation"           },
        { "conversational",        "conversation"           },
        { "math",                  "maths"                  },
        { "mathematics",           "maths"                  },
        { "roleplay",              "roleplay"               },
        { "role-play",             "roleplay"               },
        { "creative-writing",      "writing"                },
        { "writing",               "writing"                },
        { "storywriting",          "writing"                },
        { "summarization",         "summarising"            },
        { "translation",           "translation"            },
        { "multilingual",          "many languages"         },
        { "function-calling",      "tool use"               },
        { "tool-use",              "tool use"               },
        { "agent",                 "tool use"               },
        { "rag",                   "searching documents"    },
        { "retrieval",             "searching documents"    },
        { "sentence-similarity",   "searching documents"    },
        { "feature-extraction",    "searching documents"    },
        { "medical",               "medicine"               },
        { "biology",               "biology"                },
        { "finance",               "finance"                },
        { "legal",                 "law"                    },
        { "science",               "science"                },
        { "sql",                   "SQL"                    },
        { "vision",                "images"                 },
        { "multimodal",            "images"                 },
        { "uncensored",            "unfiltered answers"     },
        { "abliterated",           "unfiltered answers"     },
        { "distill",               "a distilled model"      },
        { "moe",                   "a mixture of experts"   },
        { "long-context",          "long documents"         },
    };

    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (gg_ieq(tag, map[i].tag)) return map[i].english;

    return NULL;
}

const char *gguf_lang_english(const char *code)
{
    static const struct { const char *c, *name; } map[] = {
        { "en", "English"    }, { "zh", "Chinese"    }, { "es", "Spanish"    },
        { "fr", "French"     }, { "de", "German"     }, { "it", "Italian"    },
        { "pt", "Portuguese" }, { "ru", "Russian"    }, { "ja", "Japanese"   },
        { "ko", "Korean"     }, { "ar", "Arabic"     }, { "hi", "Hindi"      },
        { "nl", "Dutch"      }, { "pl", "Polish"     }, { "tr", "Turkish"    },
        { "vi", "Vietnamese" }, { "th", "Thai"       }, { "id", "Indonesian" },
        { "sv", "Swedish"    }, { "cs", "Czech"      }, { "uk", "Ukrainian"  },
        { "ro", "Romanian"   }, { "el", "Greek"      }, { "he", "Hebrew"     },
        { "fa", "Persian"    }, { "bn", "Bengali"    }, { "ur", "Urdu"       },
        { "ms", "Malay"      }, { "da", "Danish"     }, { "fi", "Finnish"    },
        { "no", "Norwegian"  }, { "hu", "Hungarian"  }, { "bg", "Bulgarian"  },
        { "hr", "Croatian"   }, { "sr", "Serbian"    }, { "sk", "Slovak"     },
        { "ca", "Catalan"    }, { "ta", "Tamil"      }, { "te", "Telugu"     },
        { "sw", "Swahili"    }, { "af", "Afrikaans"  }, { "et", "Estonian"   },
    };

    if (!code || !code[0]) return NULL;
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (gg_ieq(code, map[i].c)) return map[i].name;

    /* An unknown code is still worth showing — "speaks CY" is less use than
     * "speaks Welsh" but far more use than pretending it speaks nothing. */
    static char raw[16];
    size_t i = 0;
    for (; code[i] && i < sizeof(raw) - 1; i++)
        raw[i] = (code[i] >= 'a' && code[i] <= 'z') ? (char)(code[i] - 32)
                                                    : code[i];
    raw[i] = '\0';
    return raw;
}

void gguf_good_at(const syn_gguf_t *g, char *out, size_t len)
{
    if (out && len) out[0] = '\0';
    if (!g || !out || !len) return;

    size_t o = 0;
    for (int i = 0; i < g->n_tags; i++) {
        const char *e = gguf_tag_english(g->tags[i]);
        if (!e) continue;

        /* The map folds synonyms together — "thinking" and "reasoning" both
         * land on "reasoning" — so the same words arrive twice from files that
         * carry both tags. Checked against what has been WRITTEN rather than
         * against the tags, which is what makes the folding safe. */
        int seen = 0;
        for (size_t p = 0; p + strlen(e) <= o; p++)
            if (memcmp(out + p, e, strlen(e)) == 0) { seen = 1; break; }
        if (seen) continue;

        int n = snprintf(out + o, len - o, "%s%s",
                         o ? " \xc2\xb7 " : "", e);
        if (n < 0 || (size_t)n >= len - o) { out[o] = '\0'; break; }
        o += (size_t)n;
    }
}

/* Named in full up to this many, then counted. Mistral Nemo lists nine, which
 * is 567 pixels against a 464-pixel row — so the row was going to lose the tail
 * either way, and losing it to "+3 more" says that it happened. */
#define GG_LANGS_NAMED  6

void gguf_langs_str(const syn_gguf_t *g, char *out, size_t len)
{
    if (out && len) out[0] = '\0';
    if (!g || !out || !len) return;

    size_t o = 0;
    int named = 0;
    for (int i = 0; i < g->n_langs; i++) {
        const char *e = gguf_lang_english(g->langs[i]);
        if (!e) continue;

        if (named == GG_LANGS_NAMED) {
            snprintf(out + o, len - o, " +%d more", g->n_langs - i);
            return;
        }

        int n = snprintf(out + o, len - o, "%s%s", o ? ", " : "", e);
        if (n < 0 || (size_t)n >= len - o) {
            /* Out of buffer with more to say — same honesty, different limit. */
            snprintf(out + o, len - o, " +%d", g->n_langs - i);
            return;
        }
        o += (size_t)n;
        named++;
    }
}

void gguf_based_on(const syn_gguf_t *g, char *out, size_t len)
{
    if (out && len) out[0] = '\0';
    if (!g || !out || !len) return;

    if (g->base[0] && g->org[0])
        snprintf(out, len, "%s by %s", g->base, g->org);
    else if (g->base[0])
        snprintf(out, len, "%s", g->base);
    else if (g->org[0])
        snprintf(out, len, "%s", g->org);
}

/*
 * How many parameters this model has, in the least bad way available.
 *
 * general.parameter_count is the right answer and is absent from most real
 * files — all four on this box omit it — so general.size_label ("12B") is
 * parsed as the fallback. "8x7B" is read as the product, which is the total
 * weight count rather than the active one; for choosing a size class, which is
 * all this feeds, total is the figure that predicts the memory.
 */
static long long gg_param_est(const syn_gguf_t *g)
{
    if (g->params > 0) return g->params;
    if (!g->size_label[0]) return -1;

    const char *p = g->size_label;
    char *e = NULL;
    double v = strtod(p, &e);
    if (e == p || v <= 0) return -1;

    if (*e == 'x' || *e == 'X') {
        const char *q = e + 1;
        double v2 = strtod(q, &e);
        if (e == q || v2 <= 0) return -1;
        v *= v2;
    }

    if (*e == 'b' || *e == 'B') return (long long)(v * 1e9);
    if (*e == 'm' || *e == 'M') return (long long)(v * 1e6);
    return -1;
}

/*
 * Failing both of those, the model's SHAPE.
 *
 * A dense transformer's weights are about 12·layers·width², which is the
 * attention and feed-forward blocks; the embeddings add a few percent on top
 * and are ignored. Checked against the two files here that state a count:
 * Mistral 7B comes out at 6.4B against a real 7.24B, and MiniCPM5 1B at 0.85B
 * against 1B — wrong by ten to fifteen percent, and both land in the right
 * size class, which is the only thing this feeds.
 *
 * Deliberately NOT allowed anywhere a number is printed. "Params" stays a dash
 * for a file that did not say, because a dash is honest and "6.4B" would be
 * this arithmetic presented as something the file claimed. A mixture-of-experts
 * model is under-counted by this — it has more weights than its dense shape
 * implies — which is one more reason it only ever picks an adjective.
 */
static long long gg_param_shape(const syn_gguf_t *g)
{
    if (g->n_layers <= 0 || g->n_embd <= 0) return -1;
    return 12LL * g->n_layers * g->n_embd * g->n_embd;
}

/* The size class, in the terms the choice is actually made in: how good is it
 * going to be, and can this machine hold it. */
static const char *gg_size_class(long long params)
{
    if (params <= 0)         return NULL;
    if (params < 2500000000LL)  return "tiny";
    if (params < 6000000000LL)  return "small";
    if (params < 16000000000LL) return "mid-size";
    if (params < 35000000000LL) return "large";
    return "very large";
}

static const char *gg_size_note(long long params)
{
    if (params <= 0)         return NULL;
    if (params < 2500000000LL)  return "very fast and light on memory, but it "
                                       "gets things wrong more often";
    if (params < 6000000000LL)  return "quick, and good enough for most "
                                       "everyday questions";
    if (params < 16000000000LL) return "the usual sweet spot on a desktop";
    if (params < 35000000000LL) return "noticeably better answers, if the "
                                       "machine can hold it";
    return "the best answers here, and it needs the hardware to match";
}

/* What the quantisation cost. The code is already on screen as Quant; this is
 * what the code MEANS, which is the part nobody memorises. Public because the
 * download side of the picker asks you to choose one of these off a list, and
 * that is the choice it exists to make. */
const char *gguf_quant_english(const char *q)
{
    if (!q || !q[0]) return NULL;

    if (strncmp(q, "F32", 3) == 0 || strncmp(q, "F16", 3) == 0 ||
        strncmp(q, "BF16", 4) == 0)
        return "uncompressed, so full quality and the biggest file";
    if (strncmp(q, "Q8", 2) == 0)
        return "barely compressed, so effectively full quality";
    if (strncmp(q, "Q6", 2) == 0 || strncmp(q, "Q5", 2) == 0)
        return "lightly compressed, very close to full quality";
    if (strncmp(q, "Q4", 2) == 0 || strncmp(q, "IQ4", 3) == 0 ||
        strncmp(q, "MXFP4", 5) == 0)
        return "compressed — the usual balance of quality against size";
    if (strncmp(q, "Q3", 2) == 0 || strncmp(q, "IQ3", 3) == 0)
        return "heavily compressed, so the answers are rougher";
    if (strncmp(q, "Q2", 2) == 0 || strncmp(q, "IQ2", 3) == 0 ||
        strncmp(q, "IQ1", 3) == 0 || strncmp(q, "TQ", 2) == 0)
        return "crushed to the smallest it goes, and it shows in the answers";
    return NULL;
}

/* Tokens are not a unit anybody thinks in. Roughly three quarters of a word
 * each, which is close enough for "can I paste this document into it". */
static void gg_ctx_words(long long ctx, char *buf, size_t len)
{
    buf[0] = '\0';
    if (ctx <= 0) return;

    long long words = ctx * 3 / 4;
    if (words >= 1000000LL)
        snprintf(buf, len, "%.1f million words", (double)words / 1e6);
    else if (words >= 1000LL)
        snprintf(buf, len, "%lld thousand words", words / 1000);
    else
        snprintf(buf, len, "%lld words", words);
}

void gguf_bio(const syn_gguf_t *g, long long bytes, char *out, size_t len)
{
    if (out && len) out[0] = '\0';
    if (!g || !out || !len) return;

    if (!g->ok) {
        snprintf(out, len, "This file's header could not be read, so there is "
                           "nothing here to go on.");
        return;
    }

    /* The model's own words win. general.description is rare — none of the
     * files on this box carry it — but a producer who bothered to write one
     * has said something more specific than anything assembled from tags. */
    if (g->description[0]) {
        snprintf(out, len, "%s", g->description);
        return;
    }

    size_t o = 0;
    #define GG_SAY(...) do {                                        \
        int n_ = snprintf(out + o, len - o, __VA_ARGS__);            \
        if (n_ < 0 || (size_t)n_ >= len - o) { out[o] = '\0'; return; } \
        o += (size_t)n_;                                             \
    } while (0)

    /* ── What it is ── */
    if (g->is_embedding) {
        /* Said first and said bluntly. Everything else about an embedding
         * model is irrelevant to somebody looking for something to talk to. */
        GG_SAY("An embedding model: it turns text into numbers so other "
               "programs can search it. It cannot hold a conversation, and "
               "loading it here will not give you one.");
        return;
    }

    /* The size label is only quoted when the FILE stated a size. The shape
     * estimate is good enough to choose the adjective and not good enough to
     * print, so a file that named neither gets "A mid-size model" and no
     * number — which is exactly as much as is actually known. */
    long long stated = gg_param_est(g);
    long long params = stated > 0 ? stated : gg_param_shape(g);
    const char *cls  = gg_size_class(params);
    const char *note = gg_size_note(params);

    if (cls && g->size_label[0])
        GG_SAY("A %s %s model", cls, g->size_label);
    else if (cls)
        GG_SAY("A %s model", cls);
    else
        GG_SAY("A model");

    if (g->org[0]) GG_SAY(" from %s", g->org);

    /* "Instruct", "Thinking" — the fine-tune is the difference between two
     * files with the same base and the same size, and it is free text, so it
     * is quoted into the sentence rather than read as a phrase. */
    if (g->finetune[0]) GG_SAY(", the %s fine-tune", g->finetune);

    if (note) GG_SAY(" — %s.", note);
    else      GG_SAY(".");

    /* ── What it costs ── */
    const char *qn = gguf_quant_english(g->quant);
    if (qn) GG_SAY(" %s: %s.", g->quant, qn);

    if (bytes > 0) {
        /* The weights plus the working memory around them. The multiplier is
         * deliberately a rough one and the sentence says "about" — a precise
         * figure here would be a false one, since the real cost moves with the
         * context synapd actually opens.
         *
         * Below a gigabyte it is quoted in MB: "about 0.0 GB" is what a
         * rounded figure does to a small model, and a zero is worse than no
         * sentence at all. */
        double mb = (double)bytes * 1.15 / (1024.0 * 1024.0);
        if (mb >= 1024.0)
            GG_SAY(" Expect it to use about %.1f GB of memory while it runs.",
                   mb / 1024.0);
        else
            GG_SAY(" Expect it to use about %.0f MB of memory while it runs.",
                   mb);
    }

    /* ── What it can take in ── */
    char words[48];
    gg_ctx_words(g->ctx, words, sizeof(words));
    if (words[0])
        GG_SAY(" It can keep about %s in view at once.", words);

    #undef GG_SAY
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
        } else if (strcmp(key, "general.description") == 0 && t == GGUF_T_STRING) {
            gg_str(&r, out->description, sizeof(out->description));
        } else if (strcmp(key, "general.license") == 0 && t == GGUF_T_STRING) {
            gg_str(&r, out->license, sizeof(out->license));
        } else if (strcmp(key, "general.basename") == 0 && t == GGUF_T_STRING) {
            gg_str(&r, out->basename, sizeof(out->basename));
        } else if (strcmp(key, "general.finetune") == 0 && t == GGUF_T_STRING) {
            gg_str(&r, out->finetune, sizeof(out->finetune));
        } else if (strcmp(key, "general.organization") == 0 && t == GGUF_T_STRING) {
            gg_str(&r, out->org, sizeof(out->org));
        } else if (strcmp(key, "general.base_model.0.name") == 0 &&
                   t == GGUF_T_STRING) {
            gg_str(&r, out->base, sizeof(out->base));
        } else if (strcmp(key, "general.base_model.0.organization") == 0 &&
                   t == GGUF_T_STRING) {
            /* Only as a fallback. general.organization is who made THIS file;
             * the base model's is who made what it was built from, and the two
             * differ on every fine-tune. Order in the header is not fixed, so
             * the weaker source refuses to overwrite the stronger one rather
             * than relying on arriving first. */
            char tmp[sizeof(out->org)];
            gg_str(&r, tmp, sizeof(tmp));
            if (!out->org[0]) snprintf(out->org, sizeof(out->org), "%s", tmp);
        } else if (strcmp(key, "general.tags") == 0) {
            gg_str_array(&r, t, out->tags[0], sizeof(out->tags[0]),
                         sizeof(out->tags[0]), SYN_GGUF_TAGS, &out->n_tags);
        } else if (strcmp(key, "general.languages") == 0) {
            gg_str_array(&r, t, out->langs[0], sizeof(out->langs[0]),
                         sizeof(out->langs[0]), SYN_GGUF_LANGS, &out->n_langs);
        } else if (dot && strcmp(suf, "pooling_type") == 0) {
            /* Presence is the signal, not the value: only an embedding model
             * pools its token vectors into one, and nothing else writes this. */
            out->is_embedding = 1;
            gg_skip_value(&r, t, 0);
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
