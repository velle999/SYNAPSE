/*
 * aimodel.c — the AI model picker (control panel ▸ System ▸ AI model)
 *
 * Two jobs, and the first is the one that matters:
 *
 *   - Report what synapd DETECTED about the model it is running: the name out
 *     of the GGUF, the prompt format it resolved, the sampling profile that
 *     matched, and the values in force. None of that is inferable from a
 *     filename, and all of it fails silently. A model fed the wrong turn
 *     format still answers fluently, which is exactly how synapd spent its
 *     whole life framing Mistral prompts as Zephyr with nothing to show for
 *     it. The panel reads these from SYN_MSG_STATUS rather than working them
 *     out again here — otherwise it would report what synui PREDICTS, and the
 *     one bug it exists to expose is the two disagreeing.
 *
 *   - Switch models, over SYN_MSG_RELOAD. synapd confines that to its own
 *     models directory and refuses anything with a '/' in it, so this panel
 *     sends a bare filename and never a path.
 *
 * The switch is not instant and is not pretended to be: synapd acknowledges
 * at once and loads several GB on its own thread. The row shows "loading…"
 * until a status poll says a model is up again, so the panel is following the
 * daemon rather than counting down its own guess.
 *
 * Keys follow filters.c/power.c (Up/Down select, Enter/Space activate, Esc
 * close) because a panel that worked its own way would be its own bug.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <curl/curl.h>
#include <wayland-server-core.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "synui.h"

/* Must match synapd's SYNAPD_MODEL_DIR. synapd is the one that enforces it —
 * this is only where the list is read from, so a mismatch shows an empty panel
 * rather than letting anything out of the directory.
 *
 * Overridable only so the test can point it at a directory of stub files, the
 * way lid_test does with SYNUI_POWER_SUPPLY_DIR. Nothing in the build sets it. */
#ifndef AIMODEL_DIR
#define AIMODEL_DIR  "/var/lib/synapd/models"
#endif

/* ── Model list ──────────────────────────────────────────── */

static int aimodel_cmp(const void *a, const void *b)
{
    const syn_aimodel_entry_t *x = a, *y = b;
    return strcasecmp(x->name, y->name);
}

/*
 * Read the models directory.
 *
 * Every .gguf is listed, including the embedding model, because hiding files
 * on a guess about their purpose would be synui deciding what synapd may load.
 * A GGUF's role is not in its filename, and a picker that silently omitted the
 * one you were looking for would be worse than one that lists something odd.
 */
static void aimodel_scan(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;
    am->count = 0;
    am->scan_err = 0;

    DIR *d = opendir(AIMODEL_DIR);
    if (!d) {
        am->scan_err = errno;
        snprintf(am->status, sizeof(am->status),
                 "cannot read %s: %s", AIMODEL_DIR, strerror(errno));
        return;
    }

    struct dirent *e;
    while ((e = readdir(d)) && am->count < AIMODEL_MAX) {
        const char *n = e->d_name;
        size_t len = strlen(n);
        if (len < 6 || strcmp(n + len - 5, ".gguf") != 0) continue;
        if (n[0] == '.') continue;

        /* Skipped rather than truncated: a shortened name is a name synapd
         * would refuse, so listing it would offer a row that cannot load. */
        if (len >= sizeof(am->models[0].name)) {
            wlr_log(WLR_INFO, "synui: model name too long to list: %s", n);
            continue;
        }

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", AIMODEL_DIR, n);
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;

        syn_aimodel_entry_t *m = &am->models[am->count++];
        /* Cleared before it is filled. The list is re-sorted on every scan, so
         * slot i is a DIFFERENT model between one scan and the next — keeping
         * the previous occupant's probed header here would describe a freshly
         * downloaded file with the facts of whatever used to sort into its
         * place, which is worse than showing nothing at all. */
        memset(m, 0, sizeof(*m));
        /* Bounded explicitly. The length guard above already rules truncation
         * out; the precision is what lets the compiler see that. */
        snprintf(m->name, sizeof(m->name), "%.*s", (int)sizeof(m->name) - 1, n);
        m->bytes = (long long)st.st_size;
    }
    closedir(d);

    qsort(am->models, am->count, sizeof(am->models[0]), aimodel_cmp);

    if (am->count == 0)
        snprintf(am->status, sizeof(am->status),
                 "no .gguf models in %s", AIMODEL_DIR);
}

/*
 * Work out which listed model is the loaded one.
 *
 * Matched on the FILENAME synapd reports, not the name inside the GGUF — those
 * are unrelated by design ("synapse.gguf" holds "Mistral Nemo Instruct 2407"),
 * so a name comparison would be a guess dressed as a fact.
 *
 * -1 when nothing matches, which is correct and not a failure: synapd may be
 * running a model from outside this directory because an ExecStart flag named
 * one, and marking a row anyway would be a lie about which file is live.
 */
/*
 * What model `idx` says about itself, read once and kept.
 *
 * Deliberately probed HERE rather than at scan time or in each of the handlers
 * that can move the cursor. Scan time would read every header in the directory
 * before the panel could open, for models the cursor may never reach; spreading
 * it across the handlers means a new one can forget, and a row drawn from an
 * unprobed entry is a row that quietly says "—" about a file that would have
 * answered. One accessor, called by whoever needs the facts, cannot drift.
 *
 * The cost is a first read on the drawing path: 2 ms for a 7B, 13 ms for a
 * 131k-vocab model, once per file per session. Everything after is a struct
 * already in hand. Returns NULL only for an index that is not a model.
 */
const syn_gguf_t *aimodel_info(syn_server_t *s, int idx)
{
    syn_aimodel_t *am = &s->aimodel;
    if (idx < 0 || idx >= am->count) return NULL;

    syn_aimodel_entry_t *m = &am->models[idx];
    if (!m->probed) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", AIMODEL_DIR, m->name);
        /* Marked probed either way: a file that cannot be read will not start
         * being readable between two frames, and retrying per render would put
         * a failing open in the compositor's draw path forever. */
        m->probed = 1;
        if (!gguf_read(path, &m->info))
            wlr_log(WLR_INFO, "synui: %s: %s", m->name, m->info.err);
    }
    return &m->info;
}

static void aimodel_mark_loaded(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;
    const char *file = s->overlay.model_file;

    /* A switch in flight owns the marker: synapd is still reporting the OLD
     * file until the new one is resident, and letting that win would bounce
     * the "loading …" tag back to the previous row mid-load. */
    if (am->switching) return;

    am->loaded_idx = -1;
    if (!file || !*file) return;

    for (int i = 0; i < am->count; i++) {
        if (strcmp(am->models[i].name, file) == 0) {
            am->loaded_idx = i;
            return;
        }
    }
}

/* ══ The download catalogue ═══════════════════════════════════════════════
 *
 * Everything below this line is about models that are NOT on the disk yet.
 *
 * The list comes from Hugging Face, live, which means every string in it is
 * attacker-controlled as far as this file is concerned: a repo name ends up in
 * a URL, and a filename inside a repo ends up as a filename ROOT WRITES into
 * synapd's models directory. So the parsers below copy nothing they have not
 * bounded, and three validators — aimodel_url_ok, aimodel_name_ok,
 * aimodel_token_of — sit between what the network said and what leaves this
 * process. syn-model re-checks all of it on the privileged side; this half is
 * what stops a bad request being sent at all, and the tests pin both.
 */

/* ── Minimal JSON scanning ───────────────────────────────────
 *
 * Hand-rolled, as news.c's XML is, and for the same reasons: the two endpoints
 * need four fields between them, and a JSON library is a dependency the
 * compositor does not otherwise want. The one rule that matters is that
 * strings are skipped as strings — a repo description containing a brace must
 * not move the depth counter, or a nested field would read as a top-level one.
 */

static const char *js_skip_string(const char *p, const char *end)
{
    if (p >= end || *p != '"') return NULL;
    for (p++; p < end; p++) {
        if (*p == '\\') { p++; continue; }
        if (*p == '"')  return p + 1;
    }
    return NULL;
}

/* The value after `"key":` at depth 0 of the object at [obj,end). Nested
 * objects are skipped whole, so "size" inside "lfs" is not this "size". */
static const char *js_value_of(const char *obj, const char *end,
                               const char *key)
{
    size_t klen = strlen(key);
    int depth = 0;

    for (const char *p = obj; p < end; ) {
        if (*p == '"') {
            const char *after = js_skip_string(p, end);
            if (!after) return NULL;
            if (depth == 1 && (size_t)(after - p) == klen + 2 &&
                strncmp(p + 1, key, klen) == 0) {
                while (after < end && (*after == ' ' || *after == ':')) after++;
                return after < end ? after : NULL;
            }
            p = after;
            continue;
        }
        if (*p == '{' || *p == '[') depth++;
        else if (*p == '}' || *p == ']') depth--;
        p++;
    }
    return NULL;
}

/*
 * A string field, and 0 if it did not fit.
 *
 * Failing on overflow rather than truncating is the point. A repo id is
 * checked for shape AFTER it is copied, so a 600-character id cut to fit the
 * field would arrive as a well-formed id for a DIFFERENT repository — one that
 * passes every validator and downloads something nobody chose. A value that
 * does not fit is not this value, so the caller gets nothing and drops the
 * entry.
 */
static int js_str(const char *obj, const char *end, const char *key,
                  char *out, size_t n)
{
    out[0] = '\0';
    const char *v = js_value_of(obj, end, key);
    if (!v || v >= end || *v != '"') return 0;

    size_t o = 0;
    for (const char *p = v + 1; p < end; p++) {
        if (*p == '"') { out[o] = '\0'; return out[0] ? 1 : 0; }
        if (o + 1 >= n) { out[0] = '\0'; return 0; }    /* would not fit */
        if (*p == '\\') {
            p++;
            if (p >= end) break;
            /* Only the escapes these endpoints actually emit. A \u sequence is
             * dropped rather than half-decoded: it can only appear in prose
             * here, and a mangled byte in a repo id would be worse than a
             * missing character. */
            switch (*p) {
            case 'n': case 't': case 'r': out[o++] = ' '; break;
            case 'u': p += 4; break;
            default:  out[o++] = *p; break;
            }
            continue;
        }
        out[o++] = *p;
    }
    out[0] = '\0';                                      /* never closed */
    return 0;
}

/*
 * An array of strings ("tags": ["gguf", "coding", …]) into a fixed table.
 *
 * Stops at the closing bracket, so a truncated body yields the tags it did
 * receive rather than nothing. Individual entries that do not fit `elem` are
 * SKIPPED, not truncated: every consumer of these matches them against a table
 * of exact words, and a tag cut in half is either no match (harmless) or the
 * wrong match (not) — dropping it is the only outcome that cannot mislead.
 * Unlike js_str's repo ids, one missing tag costs a word in a description.
 */
static int js_str_array(const char *obj, const char *end, const char *key,
                        char (*out)[AIMODEL_TAG_LEN], int max)
{
    const char *v = js_value_of(obj, end, key);
    if (!v || v >= end || *v != '[') return 0;

    int n = 0;
    for (const char *p = v + 1; p < end && n < max; ) {
        if (*p == ']') break;
        if (*p != '"') { p++; continue; }

        const char *after = js_skip_string(p, end);
        if (!after) break;

        size_t raw = (size_t)(after - p) - 2;           /* inside the quotes */
        if (raw > 0 && raw < AIMODEL_TAG_LEN) {
            memcpy(out[n], p + 1, raw);
            out[n][raw] = '\0';
            n++;
        }
        p = after;
    }
    return n;
}

static long long js_num(const char *obj, const char *end, const char *key,
                        long long def)
{
    const char *v = js_value_of(obj, end, key);
    if (!v || v >= end) return def;
    if (!isdigit((unsigned char)*v) && *v != '-') return def;
    return strtoll(v, NULL, 10);
}

/*
 * The largest `"key":N` anywhere in the object, at any depth.
 *
 * Only for the file tree's size. An LFS entry carries both the real size and
 * the pointer's 135 bytes, and which one is top-level has changed under us
 * before — taking the larger is right in both shapes, where reading one
 * position would silently list a 4 GB model as 135 bytes.
 */
static long long js_num_max(const char *obj, const char *end, const char *key)
{
    size_t klen = strlen(key);
    long long best = -1;

    for (const char *p = obj; p < end; ) {
        if (*p == '"') {
            const char *after = js_skip_string(p, end);
            if (!after) break;
            if ((size_t)(after - p) == klen + 2 &&
                strncmp(p + 1, key, klen) == 0) {
                const char *v = after;
                while (v < end && (*v == ' ' || *v == ':')) v++;
                if (v < end && isdigit((unsigned char)*v)) {
                    long long n = strtoll(v, NULL, 10);
                    if (n > best) best = n;
                }
            }
            p = after;
            continue;
        }
        p++;
    }
    return best;
}

/* The next `{...}` element of a JSON array, as a [start,end) pair. */
static const char *js_next_object(const char *p, const char *end,
                                  const char **obj_end)
{
    while (p < end && *p != '{') {
        if (*p == '"') { p = js_skip_string(p, end); if (!p) return NULL; continue; }
        p++;
    }
    if (p >= end) return NULL;

    int depth = 0;
    for (const char *q = p; q < end; ) {
        if (*q == '"') { q = js_skip_string(q, end); if (!q) return NULL; continue; }
        if (*q == '{') depth++;
        else if (*q == '}') {
            if (--depth == 0) { *obj_end = q + 1; return p; }
        }
        q++;
    }
    return NULL;
}

/* ── Reading a name ──────────────────────────────────────── */

/*
 * The quantisation, out of the filename.
 *
 * There is nowhere else to get it: a GGUF's quant is not in the API's metadata,
 * only in what the uploader called the file. Q4_K_M, IQ3_XS, F16 and friends
 * all start with an optional I, a Q or an F, then a digit — anchored at a token
 * boundary so the "q4" in a repo called "seq4" is not one.
 */
void aimodel_quant_of(const char *filename, char *out, size_t n)
{
    out[0] = '\0';
    if (!filename || n < 2) return;

    for (const char *p = filename; *p; p++) {
        if (p != filename) {
            char prev = p[-1];
            if (prev != '.' && prev != '-' && prev != '_') continue;
        }
        const char *q = p;
        if (*q == 'I' || *q == 'i') q++;                 /* IQ3_XS */
        if (*q == 'B' || *q == 'b') q++;                 /* BF16 */
        if (*q != 'Q' && *q != 'q' && *q != 'F' && *q != 'f') continue;
        if (!isdigit((unsigned char)q[1])) continue;

        size_t o = 0;
        for (const char *r = p; *r && o + 1 < n; r++) {
            if (!isalnum((unsigned char)*r) && *r != '_') break;
            out[o++] = (char)toupper((unsigned char)*r);
        }
        out[o] = '\0';
        return;
    }
}

/*
 * The parameter count, out of the repo name: 7B, 0.5B, 8x7B.
 *
 * Bounded at 999 because the other thing that looks like this in a model name
 * is a context length or a date, and "4096B" is not a parameter count.
 *
 * A mixture of experts is reported whole — "8x7B", not "7B" and not "56B".
 * Eight experts of seven billion is neither of those numbers in memory or in
 * quality, and picking one of them to print would be inventing a fact about
 * how big the download is going to be.
 */
void aimodel_params_of(const char *name, char *out, size_t n)
{
    out[0] = '\0';
    if (!name || n < 2) return;

    for (const char *p = name; *p; p++) {
        if (!isdigit((unsigned char)*p)) continue;
        if (p != name && (isalnum((unsigned char)p[-1]) || p[-1] == '.')) continue;

        /* An "8x" prefix belongs to the count that follows it. */
        const char *start = p;
        const char *q = p;
        while (isdigit((unsigned char)*q)) q++;
        if ((*q == 'x' || *q == 'X') && isdigit((unsigned char)q[1])) {
            q++;
            while (isdigit((unsigned char)*q)) q++;
        }
        if (*q == '.') { q++; while (isdigit((unsigned char)*q)) q++; }
        if (*q != 'B' && *q != 'b') continue;
        if (isalnum((unsigned char)q[1])) continue;      /* "7Bit" is not 7B */

        /* The value checked is the one after any "x": 8x7B is a 7B expert. */
        const char *num = strpbrk(start, "xX");
        double v = strtod(num && num < q ? num + 1 : start, NULL);
        if (v <= 0 || v > 999) continue;

        size_t len = (size_t)(q - start) + 1;
        if (len + 1 > n) return;
        memcpy(out, start, len);
        out[len - 1] = 'B';
        out[len] = '\0';
        return;
    }
}

/* ── What may leave this process ─────────────────────────── */

/*
 * A repo id, as it came off the network and before it is put in a URL.
 *
 * Two path segments of an ordinary charset. Anything else — a space, a query
 * separator, a second slash, a dot segment — is refused rather than escaped,
 * because a repo id that needs escaping is not one HF issued.
 */
static int aimodel_repo_id_ok(const char *id)
{
    int slashes = 0;
    size_t len = 0;

    for (const char *p = id; *p; p++, len++) {
        if (*p == '/') {
            if (++slashes > 1) return 0;
            if (p == id || p[1] == '\0' || p[1] == '/') return 0;
            continue;
        }
        if (!isalnum((unsigned char)*p) &&
            *p != '.' && *p != '_' && *p != '-') return 0;
    }
    if (slashes != 1 || len == 0 || len >= 128) return 0;
    if (strstr(id, "..")) return 0;
    return 1;
}

/* A path inside a repo: the same charset, plus the slashes of a subdirectory. */
static int aimodel_repo_path_ok(const char *path)
{
    size_t len = strlen(path);
    if (len == 0 || len >= 128) return 0;
    if (path[0] == '/' || path[0] == '.') return 0;
    if (strstr(path, "..")) return 0;

    for (const char *p = path; *p; p++)
        if (!isalnum((unsigned char)*p) &&
            *p != '.' && *p != '_' && *p != '-' && *p != '/') return 0;
    return 1;
}

int aimodel_name_ok(const char *file)
{
    if (!file) return 0;
    size_t len = strlen(file);
    if (len < 6 || len >= 121) return 0;              /* x.gguf … the field */
    if (strcasecmp(file + len - 5, ".gguf") != 0) return 0;
    if (!isalnum((unsigned char)file[0])) return 0;   /* no dot-leading, no dash */

    for (const char *p = file; *p; p++)
        if (!isalnum((unsigned char)*p) &&
            *p != '.' && *p != '_' && *p != '-') return 0;
    return 1;
}

int aimodel_url_ok(const char *url)
{
    static const char *prefix = "https://huggingface.co/";
    if (!url) return 0;
    if (strncmp(url, prefix, strlen(prefix)) != 0) return 0;
    if (strlen(url) >= 512) return 0;

    /* No whitespace and no control characters: this becomes one argv element
     * for curl, and a space in it would become a second argument. */
    for (const char *p = url; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c <= 0x20 || c == 0x7f || c == '"' || c == '\'' || c == '\\')
            return 0;
    }
    return 1;
}

int aimodel_token_of(const char *file, char *out, size_t n)
{
    if (!file || n < 2) return 0;
    out[0] = '\0';

    size_t o = 0;
    for (const char *p = file; *p && o + 1 < n && o < 63; p++) {
        if (*p == '.') break;                          /* the stem only */
        if (isalnum((unsigned char)*p) || *p == '_' || *p == '-')
            out[o++] = *p;
    }
    out[o] = '\0';

    /* A leading dash or dot would be an option to something downstream, and a
     * token that reduced to nothing cannot name a file. */
    if (o == 0 || !isalnum((unsigned char)out[0])) { out[0] = '\0'; return 0; }
    return 1;
}

/* ── The two responses ───────────────────────────────────── */

int aimodel_parse_search(const char *body, size_t len,
                         syn_aimodel_cat_t *out, int max)
{
    const char *end = body + len;
    const char *p = body, *obj, *obj_end;
    int n = 0;

    while (n < max && (obj = js_next_object(p, end, &obj_end))) {
        p = obj_end;

        syn_aimodel_cat_t *c = &out[n];
        memset(c, 0, sizeof(*c));

        if (!js_str(obj, obj_end, "id", c->id, sizeof(c->id))) continue;
        if (!aimodel_repo_id_ok(c->id)) {
            wlr_log(WLR_INFO, "synui: aimodel: skipping odd repo id");
            continue;
        }

        const char *slash = strchr(c->id, '/');
        snprintf(c->author, sizeof(c->author), "%.*s",
                 (int)(slash - c->id), c->id);
        snprintf(c->name, sizeof(c->name), "%s", slash + 1);

        c->downloads = js_num(obj, obj_end, "downloads", 0);
        c->likes     = js_num(obj, obj_end, "likes", 0);

        /* The license is a tag, not a field: "license:apache-2.0". */
        const char *lic = memmem(obj, (size_t)(obj_end - obj),
                                 "\"license:", 9);
        if (lic) {
            lic += 9;
            size_t o = 0;
            while (lic < obj_end && *lic != '"' && o + 1 < sizeof(c->license))
                c->license[o++] = *lic++;
            c->license[o] = '\0';
        }

        /* The tags are what let the AVAILABLE side say what a model is FOR
         * before it has been downloaded. Read whole rather than scanned for,
         * the way the licence above still is: the licence is one known prefix,
         * while a description needs every tag to run past gguf_tag_english(). */
        c->n_tags = js_str_array(obj, obj_end, "tags", c->tags, AIMODEL_TAG_MAX);

        /* "base_model:Qwen/Qwen3-Coder-30B" — what this was built from.
         * Hugging Face also emits "base_model:quantized:Owner/Name", which
         * names the repo this was quantised from and is very often this same
         * model; the plain form is preferred and the qualified one is only a
         * fallback so a repo carrying ONLY the qualified tag still says
         * something. Skipped entirely when it just restates this repo. */
        for (int t = 0; t < c->n_tags; t++) {
            const char *tg = c->tags[t];
            if (strncmp(tg, "base_model:", 11) != 0) continue;
            const char *val = tg + 11;
            int qualified = (strncmp(val, "quantized:", 10) == 0);
            if (qualified) val += 10;
            if (!strchr(val, '/')) continue;
            if (strcmp(val, c->id) == 0) continue;      /* itself; says nothing */
            if (!c->base_model[0] || !qualified)
                snprintf(c->base_model, sizeof(c->base_model), "%s", val);
        }

        aimodel_params_of(c->name, c->params, sizeof(c->params));
        c->detail   = AIMODEL_DETAIL_NONE;
        c->sel_file = 0;
        n++;
    }
    return n;
}

/* ══ Describing a model that is NOT downloaded yet ═══════════════════════
 *
 * The INSTALLED side reads the GGUF header and says what the file is. That is
 * the wrong side of the decision: by the time the header exists, several GB
 * have already been fetched. The facts that change a mind belong on the
 * AVAILABLE side, where the choice is actually made.
 *
 * There is no header to read there, so these assemble the same description out
 * of what the repo listing carries — its tags, its name and the size and
 * quantisation of the file you have selected. Everything goes through the SAME
 * vocabulary the installed side uses (gguf_tag_english, gguf_quant_english),
 * which is why gguf.h exports them. A model must not appear to change its
 * description merely by being downloaded.
 *
 * The rule is gguf.c's rule: say what the repo said, and say nothing when it
 * did not. Nothing here is inferred from a filename beyond the parameter count
 * the list column already shows.
 */

void aimodel_cat_good_at(const syn_aimodel_cat_t *c, char *out, size_t len)
{
    if (out && len) out[0] = '\0';
    if (!c || !out || !len) return;

    size_t o = 0;
    for (int i = 0; i < c->n_tags; i++) {
        const char *e = gguf_tag_english(c->tags[i]);
        if (!e) continue;

        /* Same de-duplication as gguf_good_at(), and for the same reason: the
         * map folds synonyms together, so "chat" and "conversational" both
         * arrive as "conversation". Checked against what has been WRITTEN,
         * not against the tags. */
        int seen = 0;
        for (size_t p = 0; p + strlen(e) <= o; p++)
            if (memcmp(out + p, e, strlen(e)) == 0) { seen = 1; break; }
        if (seen) continue;

        int n = snprintf(out + o, len - o, "%s%s", o ? " \xc2\xb7 " : "", e);
        if (n < 0 || (size_t)n >= len - o) { out[o] = '\0'; break; }
        o += (size_t)n;
    }
}

/* "Qwen3 Coder 30B A3B Instruct by Qwen" — the base_model tag in the same
 * shape gguf_based_on() writes, so the two sides read alike. */
void aimodel_cat_based_on(const syn_aimodel_cat_t *c, char *out, size_t len)
{
    if (out && len) out[0] = '\0';
    if (!c || !out || !len || !c->base_model[0]) return;

    const char *slash = strchr(c->base_model, '/');
    if (!slash) return;

    char owner[64], name[96];
    snprintf(owner, sizeof(owner), "%.*s",
             (int)(slash - c->base_model), c->base_model);
    snprintf(name, sizeof(name), "%s", slash + 1);

    /* Separators to spaces. A repo name is written for a URL, not for a
     * sentence, and "Qwen3-Coder-30B" mid-prose reads as a part number. */
    for (char *p = name; *p; p++) if (*p == '-' || *p == '_') *p = ' ';

    /* "GGUF" is on every repo in this list by construction (the search filters
     * on it), so it distinguishes nothing and only costs width. */
    size_t nl = strlen(name);
    if (nl > 5 && strcasecmp(name + nl - 5, " GGUF") == 0) name[nl - 5] = '\0';

    if (owner[0] >= 'a' && owner[0] <= 'z') owner[0] = (char)(owner[0] - 32);

    snprintf(out, len, "%s by %s", name, owner);
}

void aimodel_cat_bio(const syn_aimodel_cat_t *c, const syn_aimodel_file_t *f,
                     char *out, size_t len)
{
    if (out && len) out[0] = '\0';
    if (!c || !out || !len) return;

    size_t o = 0;
    #define AM_SAY(...) do {                                            \
        int n_ = snprintf(out + o, len - o, __VA_ARGS__);               \
        if (n_ < 0 || (size_t)n_ >= len - o) { out[o] = '\0'; return; } \
        o += (size_t)n_;                                                \
    } while (0)

    /* ── What it is ── */
    char owner[64];
    snprintf(owner, sizeof(owner), "%s", c->author);
    if (owner[0] >= 'a' && owner[0] <= 'z') owner[0] = (char)(owner[0] - 32);

    if (c->params[0]) AM_SAY("A %s model", c->params);
    else              AM_SAY("A model");

    if (owner[0]) AM_SAY(" from %s", owner);
    AM_SAY(".");

    /* ── What it costs ── */
    /* Both of these depend on WHICH quantisation is selected, which is the
     * choice this pane exists to make — so the description moves as the
     * selection does, rather than describing the repo in the abstract. */
    if (f) {
        const char *qn = gguf_quant_english(f->quant);
        if (qn && f->quant[0]) AM_SAY(" %s: %s.", f->quant, qn);

        if (f->bytes > 0) {
            /* Same 1.15 multiplier and the same "about" hedge as gguf_bio() —
             * this is the identical estimate, made before the download rather
             * than after it, and it must not disagree with itself. */
            double mb = (double)f->bytes * 1.15 / (1024.0 * 1024.0);
            if (mb >= 1024.0)
                AM_SAY(" Expect it to use about %.1f GB of memory while it runs.",
                       mb / 1024.0);
            else
                AM_SAY(" Expect it to use about %.0f MB of memory while it runs.",
                       mb);
        }
    } else if (c->detail == AIMODEL_DETAIL_BUSY ||
               c->detail == AIMODEL_DETAIL_WANT) {
        AM_SAY(" Reading the repository for its sizes \xe2\x80\xa6");
    }

    /* Deliberately no context-window sentence: the listing does not carry one,
     * and the installed side's "keeps about N words in view" comes from the
     * header. Guessing it here would be the one invented fact in the pane. */

    #undef AM_SAY
}

int aimodel_parse_tree(const char *body, size_t len,
                       syn_aimodel_file_t *out, int max)
{
    const char *end = body + len;
    const char *p = body, *obj, *obj_end;
    int n = 0;

    while (n < max && (obj = js_next_object(p, end, &obj_end))) {
        p = obj_end;

        char type[16], path[192];
        js_str(obj, obj_end, "type", type, sizeof(type));
        if (type[0] && strcmp(type, "file") != 0) continue;
        if (!js_str(obj, obj_end, "path", path, sizeof(path))) continue;

        size_t plen = strlen(path);
        if (plen < 6 || strcasecmp(path + plen - 5, ".gguf") != 0) continue;
        if (!aimodel_repo_path_ok(path)) continue;

        /* Skipped rather than truncated, for the reason aimodel_scan() skips
         * an over-long filename: a shortened path is a path the repo does not
         * have, so listing it would offer a download that 404s. */
        if (plen >= sizeof(out[0].file)) continue;

        /* A multi-part GGUF is not something this panel can install: synapd
         * loads one file, and fetching part 1 of 3 would leave a model that
         * cannot load. Listing them would be offering a download that breaks. */
        if (strstr(path, "-00001-of-") || strstr(path, "-split-")) continue;

        syn_aimodel_file_t *f = &out[n];
        memset(f, 0, sizeof(*f));
        /* Bounded explicitly. The length guard above already rules truncation
         * out; the precision is what lets the compiler see that. */
        snprintf(f->file, sizeof(f->file), "%.*s",
                 (int)sizeof(f->file) - 1, path);
        f->bytes = js_num_max(obj, obj_end, "size");
        aimodel_quant_of(path, f->quant, sizeof(f->quant));
        n++;
    }
    return n;
}

/* ── The fetch thread ────────────────────────────────────── */
/* news.c's shape exactly: park on a condvar, do the network with the lock
 * dropped, hand the result over under the lock, and write one byte to a pipe
 * the event loop is watching. Only the main thread ever touches wlroots. */

#define AIMODEL_UA           "synui/0.1 (SynapseOS model picker)"
#define AIMODEL_CONNECT_SEC  8
#define AIMODEL_XFER_SEC     25
#define AIMODEL_MAX_BODY     (4u * 1024 * 1024)

typedef struct { char *buf; size_t len, cap; } dlbuf_t;

static size_t cat_write(void *ptr, size_t sz, size_t nm, void *ud)
{
    dlbuf_t *d = ud;
    size_t add = sz * nm;

    if (d->len + add + 1 > AIMODEL_MAX_BODY) return 0;   /* aborts the transfer */
    if (d->len + add + 1 > d->cap) {
        size_t cap = d->cap ? d->cap * 2 : 65536;
        while (cap < d->len + add + 1) cap *= 2;
        char *nb = realloc(d->buf, cap);
        if (!nb) return 0;
        d->buf = nb;
        d->cap = cap;
    }
    memcpy(d->buf + d->len, ptr, add);
    d->len += add;
    d->buf[d->len] = '\0';
    return add;
}

/* The stop flag as libcurl sees it, so logout does not wait out a connect
 * timeout on a slow host — the AI thread's 15s hang, not repeated. */
static int cat_progress(void *ud, curl_off_t dt, curl_off_t dn,
                        curl_off_t ut, curl_off_t un)
{
    (void)dt; (void)dn; (void)ut; (void)un;
    syn_aimodel_t *am = ud;
    return atomic_load(&am->stop) ? 1 : 0;
}

static int cat_get(syn_aimodel_t *am, CURL *curl, const char *url, dlbuf_t *d)
{
    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cat_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, d);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, AIMODEL_UA);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 4L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)AIMODEL_CONNECT_SEC);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)AIMODEL_XFER_SEC);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, cat_progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, am);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    CURLcode rc = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

    if (rc != CURLE_OK) {
        if (!atomic_load(&am->stop))
            wlr_log(WLR_INFO, "synui: aimodel: %s", curl_easy_strerror(rc));
        return -1;
    }
    if (code != 200) {
        wlr_log(WLR_INFO, "synui: aimodel: HTTP %ld", code);
        return -1;
    }
    return 0;
}

/* Only what a search box may put in a URL. Everything else is dropped rather
 * than percent-encoded: this is a model search, not a general query language,
 * and dropping keeps the URL provably free of separators. */
static void url_escape_query(const char *in, char *out, size_t n)
{
    size_t o = 0;
    for (const char *p = in; *p && o + 4 < n; p++) {
        if (isalnum((unsigned char)*p) || *p == '.' || *p == '_' || *p == '-') {
            out[o++] = *p;
        } else if (*p == ' ') {
            memcpy(out + o, "%20", 3);
            o += 3;
        }
    }
    out[o] = '\0';
}

static void *aimodel_thread_fn(void *arg)
{
    syn_server_t *s = arg;
    syn_aimodel_t *am = &s->aimodel;

    CURL *curl = curl_easy_init();
    if (!curl) {
        wlr_log(WLR_ERROR, "synui: aimodel: curl init failed");
        return NULL;
    }

    syn_aimodel_cat_t *got = calloc(AIMODEL_CAT_MAX, sizeof(*got));
    syn_aimodel_file_t *gotf = calloc(AIMODEL_FILE_MAX, sizeof(*gotf));
    if (!got || !gotf) {
        free(got); free(gotf);
        curl_easy_cleanup(curl);
        wlr_log(WLR_ERROR, "synui: aimodel: out of memory");
        return NULL;
    }

    while (!atomic_load(&am->stop)) {
        pthread_mutex_lock(&am->lock);
        while (!atomic_load(&am->stop) && !atomic_load(&am->want))
            pthread_cond_wait(&am->cv, &am->lock);
        if (atomic_load(&am->stop)) { pthread_mutex_unlock(&am->lock); break; }
        atomic_store(&am->want, 0);

        char query[AIMODEL_QUERY_MAX], detail[128];
        snprintf(query, sizeof(query), "%s", am->req_query);
        snprintf(detail, sizeof(detail), "%s", am->req_detail);
        int do_search = am->req_search;
        am->req_search    = 0;         /* consumed: taken once, not once per wake */
        am->req_detail[0] = '\0';
        pthread_mutex_unlock(&am->lock);

        /* A search first, so a query typed while a detail was in flight lands
         * on the list it was typed against. */
        if (do_search) {
            char url[512], esc[AIMODEL_QUERY_MAX * 3];
            url_escape_query(query, esc, sizeof(esc));
            /* pipeline_tag matters as much as the gguf filter: without it the
             * most-downloaded GGUF repos are embedding, speech and OCR models,
             * which synapd cannot hold a conversation with. Offering those as
             * downloads would be offering several GB that cannot answer. */
            snprintf(url, sizeof(url),
                     "https://huggingface.co/api/models"
                     "?filter=gguf&pipeline_tag=text-generation"
                     "&sort=downloads&direction=-1&limit=%d%s%s",
                     AIMODEL_CAT_MAX, esc[0] ? "&search=" : "", esc);

            dlbuf_t d = {0};
            int rc = cat_get(am, curl, url, &d);
            int n = (rc == 0 && d.len) ?
                    aimodel_parse_search(d.buf, d.len, got, AIMODEL_CAT_MAX) : 0;
            free(d.buf);

            if (atomic_load(&am->stop)) break;

            pthread_mutex_lock(&am->lock);
            memcpy(am->fetched, got, sizeof(*got) * (size_t)n);
            am->n_fetched   = n;
            am->search_rc   = rc;
            am->have_search = 1;
            pthread_mutex_unlock(&am->lock);
        }

        if (detail[0] && aimodel_repo_id_ok(detail) && !atomic_load(&am->stop)) {
            char url[512];
            snprintf(url, sizeof(url),
                     "https://huggingface.co/api/models/%s/tree/main?recursive=1",
                     detail);

            dlbuf_t d = {0};
            int rc = cat_get(am, curl, url, &d);
            int n = (rc == 0 && d.len) ?
                    aimodel_parse_tree(d.buf, d.len, gotf, AIMODEL_FILE_MAX) : 0;
            free(d.buf);

            if (atomic_load(&am->stop)) break;

            pthread_mutex_lock(&am->lock);
            snprintf(am->det_id, sizeof(am->det_id), "%s", detail);
            memcpy(am->det_files, gotf, sizeof(*gotf) * (size_t)n);
            am->n_det  = n;
            am->det_rc = rc;
            pthread_mutex_unlock(&am->lock);
        }

        char byte = 1;
        if (write(am->pipe[1], &byte, 1) < 0 && errno != EAGAIN)
            wlr_log(WLR_ERROR, "synui: aimodel: pipe write failed");
    }

    free(got);
    free(gotf);
    curl_easy_cleanup(curl);
    return NULL;
}

/* Ask for the file list of whatever the cursor has settled on. */
static void aimodel_request_detail(syn_server_t *s, const char *id)
{
    syn_aimodel_t *am = &s->aimodel;
    if (!am->running || !id || !*id) return;

    pthread_mutex_lock(&am->lock);
    snprintf(am->req_detail, sizeof(am->req_detail), "%s", id);
    atomic_store(&am->want, 1);
    pthread_cond_signal(&am->cv);
    pthread_mutex_unlock(&am->lock);
}

static void aimodel_request_search(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;
    if (!am->running) return;

    pthread_mutex_lock(&am->lock);
    snprintf(am->req_query, sizeof(am->req_query), "%s", am->query);
    am->req_search = 1;
    am->searching  = 1;
    atomic_store(&am->want, 1);
    pthread_cond_signal(&am->cv);
    pthread_mutex_unlock(&am->lock);

    snprintf(am->search_msg, sizeof(am->search_msg), "searching \xe2\x80\xa6");
}

/* A fetch landed. */
static int aimodel_readable(int fd, uint32_t mask, void *data)
{
    (void)mask;
    syn_server_t *s = data;
    syn_aimodel_t *am = &s->aimodel;

    char drain[64];
    while (read(fd, drain, sizeof(drain)) > 0)
        ;   /* the pipe is only a wake-up; the results came via the lock */

    pthread_mutex_lock(&am->lock);
    /* A search that matched nothing is an ANSWER, and the old list must go —
     * keying off "did anything come back" instead would leave the previous
     * results on screen under a query they do not match. */
    int got_search = am->have_search;
    int n = am->n_fetched, rc = am->search_rc;
    am->have_search = 0;
    if (got_search) am->searching = 0;

    if (got_search) {
        /* The detail already fetched for a repo still in the new list is kept:
         * re-searching the same text must not throw away file lists that are
         * still true, and re-fetching them would be a request per repo. Taken
         * BEFORE the new list overwrites it. */
        syn_aimodel_cat_t prev[AIMODEL_CAT_MAX];
        int n_prev = am->n_cat;
        memcpy(prev, am->cat, sizeof(prev));

        memcpy(am->cat, am->fetched, sizeof(*am->cat) * (size_t)n);
        am->n_cat = n;

        for (int i = 0; i < n; i++)
            for (int j = 0; j < n_prev; j++)
                if (prev[j].detail == AIMODEL_DETAIL_OK &&
                    strcmp(prev[j].id, am->cat[i].id) == 0) {
                    memcpy(am->cat[i].files, prev[j].files, sizeof(prev[j].files));
                    am->cat[i].n_files  = prev[j].n_files;
                    am->cat[i].sel_file = prev[j].sel_file;
                    am->cat[i].detail   = AIMODEL_DETAIL_OK;
                    break;
                }
    }

    /* The file list, folded into whichever row asked for it. */
    if (am->det_id[0]) {
        for (int i = 0; i < am->n_cat; i++) {
            if (strcmp(am->cat[i].id, am->det_id) != 0) continue;
            if (am->det_rc == 0 && am->n_det > 0) {
                memcpy(am->cat[i].files, am->det_files,
                       sizeof(*am->det_files) * (size_t)am->n_det);
                am->cat[i].n_files  = am->n_det;
                am->cat[i].sel_file = 0;
                am->cat[i].detail   = AIMODEL_DETAIL_OK;
            } else {
                am->cat[i].n_files = 0;
                am->cat[i].detail  = AIMODEL_DETAIL_FAIL;
            }
            break;
        }
        am->det_id[0] = '\0';
    }
    pthread_mutex_unlock(&am->lock);

    /* Only a search answers for the search line. A file listing landing is not
     * evidence about whether the query matched anything. */
    if (got_search) {
        if (rc != 0)
            snprintf(am->search_msg, sizeof(am->search_msg),
                     "cannot reach huggingface.co");
        else if (n == 0)
            snprintf(am->search_msg, sizeof(am->search_msg), "no models match");
        else
            am->search_msg[0] = '\0';
    }

    /* The list may have shrunk under the cursor. -1 puts it back in INSTALLED
     * rather than on a row that is no longer there. */
    if (am->cat_sel >= am->n_cat) am->cat_sel = am->n_cat ? am->n_cat - 1 : -1;
    if (am->scroll > aimodel_slots(am) - AIMODEL_ROWS)
        am->scroll = aimodel_slots(am) - AIMODEL_ROWS;
    if (am->scroll < 0) am->scroll = 0;

    if (am->visible) synui_render_aimodel(s);
    return 0;
}

/* ── Starting a download ─────────────────────────────────── */

#define AIMODEL_RUN_DIR  "/run/syn-model"

static double aimodel_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

/* The repo entry the cursor is on, or NULL. */
static syn_aimodel_cat_t *cat_current(syn_aimodel_t *am)
{
    if (am->cat_sel < 0 || am->cat_sel >= am->n_cat) return NULL;
    return &am->cat[am->cat_sel];
}

static syn_aimodel_file_t *cat_current_file(syn_aimodel_t *am)
{
    syn_aimodel_cat_t *c = cat_current(am);
    if (!c || c->n_files <= 0) return NULL;
    if (c->sel_file < 0 || c->sel_file >= c->n_files) c->sel_file = 0;
    return &c->files[c->sel_file];
}

/* The local filename for a file inside a repo: its basename, which is what
 * synapd will be asked to load later. */
static void aimodel_local_name(const char *repo_path, char *out, size_t n)
{
    const char *base = strrchr(repo_path, '/');
    snprintf(out, n, "%s", base ? base + 1 : repo_path);
}

static int aimodel_already_have(syn_aimodel_t *am, const char *file)
{
    for (int i = 0; i < am->count; i++)
        if (strcmp(am->models[i].name, file) == 0) return 1;
    return 0;
}

/*
 * Queue the request and start the unit that does the work.
 *
 * synui does not download anything itself: it runs as the user and cannot
 * write to synapd's models directory, and a compositor that fetched several GB
 * would be doing it on the thread that draws. The request goes into a
 * group-writable spool and syn-model-download@TOKEN.service — root, network,
 * and nothing else — picks it up. A polkit rule allows the start; without the
 * rule the start is refused and the panel says so rather than appearing to
 * work.
 */
static int aimodel_start_download(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;

    syn_aimodel_cat_t  *c = cat_current(am);
    syn_aimodel_file_t *f = cat_current_file(am);
    if (!c || !f) {
        snprintf(am->status, sizeof(am->status),
                 "no file to download \xc2\xb7 still reading the repo");
        return 0;
    }
    if (am->dl.state == AIMODEL_DL_STARTING ||
        am->dl.state == AIMODEL_DL_RUNNING) {
        snprintf(am->status, sizeof(am->status),
                 "a download is already running");
        return 0;
    }

    char local[128];
    aimodel_local_name(f->file, local, sizeof(local));

    if (!aimodel_name_ok(local)) {
        snprintf(am->status, sizeof(am->status),
                 "refused: the repo names that file oddly");
        wlr_log(WLR_ERROR, "synui: aimodel: refusing filename from %s", c->id);
        return 0;
    }
    if (aimodel_already_have(am, local)) {
        snprintf(am->status, sizeof(am->status), "already installed");
        return 0;
    }

    char url[512];
    snprintf(url, sizeof(url), "https://huggingface.co/%s/resolve/main/%s",
             c->id, f->file);
    if (!aimodel_url_ok(url)) {
        snprintf(am->status, sizeof(am->status), "refused: bad download URL");
        wlr_log(WLR_ERROR, "synui: aimodel: refusing URL for %s", c->id);
        return 0;
    }

    char token[72];
    if (!aimodel_token_of(local, token, sizeof(token))) {
        snprintf(am->status, sizeof(am->status), "refused: unusable filename");
        return 0;
    }

    /* Written and closed before the unit is started, so the request is always
     * there by the time root looks for it. O_EXCL: a token already queued is a
     * download already asked for. */
    char req[256];
    snprintf(req, sizeof(req), "%s/req/%s.request", AIMODEL_RUN_DIR, token);

    int fd = open(req, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0640);
    if (fd < 0 && errno == EEXIST) {
        /* A request left over from a start that never happened — the usual
         * cause is polkit refusing, which leaves the spool file with nothing
         * coming to consume it. Since this process is not running a download,
         * the leftover is stale by definition and retrying must not be
         * impossible until the next reboot. */
        unlink(req);
        fd = open(req, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0640);
    }
    if (fd < 0) {
        if (errno == ENOENT)
            snprintf(am->status, sizeof(am->status),
                     "no download spool \xc2\xb7 is syn-model installed?");
        else
            snprintf(am->status, sizeof(am->status),
                     "cannot queue a download: %s", strerror(errno));
        wlr_log(WLR_ERROR, "synui: aimodel: open %s: %s", req, strerror(errno));
        return 0;
    }

    char body[768];
    int len = snprintf(body, sizeof(body), "url=%s\nfile=%s\n", url, local);
    ssize_t wrote = write(fd, body, (size_t)len);
    close(fd);
    if (wrote != len) {
        unlink(req);
        snprintf(am->status, sizeof(am->status), "cannot queue a download");
        return 0;
    }

    char unit[128];
    snprintf(unit, sizeof(unit), "syn-model-download@%s.service", token);

    pid_t pid = fork();
    if (pid < 0) {
        unlink(req);
        snprintf(am->status, sizeof(am->status), "cannot start the downloader");
        return 0;
    }
    if (pid == 0) {
        setsid();
        synui_child_reset_signals();
        /* No shell. The unit name is built from a token of [A-Za-z0-9._-]
         * only, and it is one argv element regardless. */
        execlp("systemctl", "systemctl", "start", "--no-block", unit,
               (char *)NULL);
        _exit(127);
    }

    memset(&am->dl, 0, sizeof(am->dl));
    am->dl.state = AIMODEL_DL_STARTING;
    snprintf(am->dl.token, sizeof(am->dl.token), "%s", token);
    snprintf(am->dl.file, sizeof(am->dl.file), "%s", local);
    snprintf(am->dl.msg, sizeof(am->dl.msg), "starting \xe2\x80\xa6");
    am->dl.got = am->dl.total = 0;
    am->dl.pct = 0;
    am->dl.started_at = aimodel_now();

    am->status[0] = '\0';
    wlr_log(WLR_INFO, "synui: aimodel: queued %s from %s", local, c->id);

    if (am->dl_timer) wl_event_source_timer_update(am->dl_timer, 400);
    return 1;
}

/*
 * ── Deleting an installed model ──────────────────────────────────────────
 *
 * The mirror of the download: synui can read /var/lib/synapd/models and cannot
 * write it (0750 synapd:synapse), so root does the unlink via
 * syn-model-delete@TOKEN.service and this side only asks. Same spool, same
 * token rules, same "the request file is untrusted" contract on the far end —
 * syn-model re-validates the filename before it removes anything.
 *
 * Two presses, never one. aimodel_delete_arm() only marks the row; this runs
 * on the confirming press. A single-key delete of a file that took an hour to
 * fetch is not an affordance, it is a trap.
 */
int aimodel_delete_arm(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;

    if (am->cat_sel >= 0) {
        snprintf(am->status, sizeof(am->status),
                 "that one is not downloaded \xc2\xb7 nothing to delete");
        return 0;
    }
    if (am->selected < 0 || am->selected >= am->count) return 0;

    if (am->del_token[0]) {
        snprintf(am->status, sizeof(am->status), "a delete is already running");
        return 0;
    }
    /* Refused here as well as being survivable: synapd holds the weights in
     * memory, so deleting the running model does not break the session — but
     * it is almost never what was meant, and the picker knows which one it is.
     * The privileged half deliberately does NOT enforce this; it cannot know
     * what is loaded, and it clears the remembered pick instead. */
    if (am->selected == am->loaded_idx) {
        snprintf(am->status, sizeof(am->status),
                 "synapd is running this one \xc2\xb7 switch first");
        return 0;
    }

    am->del_armed = am->selected;
    snprintf(am->status, sizeof(am->status),
             "delete %s? \xc2\xb7 Delete again confirms \xc2\xb7 Esc cancels",
             am->models[am->selected].name);
    return 1;
}

void aimodel_delete_cancel(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;
    if (am->del_armed < 0) return;
    am->del_armed = -1;
    am->status[0] = '\0';
}

int aimodel_delete_confirm(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;

    int idx = am->del_armed;
    am->del_armed = -1;
    if (idx < 0 || idx >= am->count) return 0;

    /* Re-checked against the ROW ARMED, not the cursor. A rescan between the
     * two presses can re-sort the list, and confirming by index alone would
     * remove whatever sorted into that slot instead. */
    const char *file = am->models[idx].name;
    if (!aimodel_name_ok(file)) {
        snprintf(am->status, sizeof(am->status), "refused: odd filename");
        return 0;
    }
    if (idx == am->loaded_idx) {
        snprintf(am->status, sizeof(am->status),
                 "synapd is running this one \xc2\xb7 switch first");
        return 0;
    }

    char token[72];
    if (!aimodel_token_of(file, token, sizeof(token))) {
        snprintf(am->status, sizeof(am->status), "refused: unusable filename");
        return 0;
    }

    char req[256];
    snprintf(req, sizeof(req), "%s/req/%s.delete", AIMODEL_RUN_DIR, token);

    int fd = open(req, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0640);
    if (fd < 0 && errno == EEXIST) {
        /* Same reasoning as the download spool: a leftover request means a
         * start that never happened (usually polkit), and it must not make
         * retrying impossible until the next reboot. */
        unlink(req);
        fd = open(req, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0640);
    }
    if (fd < 0) {
        if (errno == ENOENT)
            snprintf(am->status, sizeof(am->status),
                     "no spool \xc2\xb7 is syn-model installed?");
        else
            snprintf(am->status, sizeof(am->status),
                     "cannot queue a delete: %s", strerror(errno));
        wlr_log(WLR_ERROR, "synui: aimodel: open %s: %s", req, strerror(errno));
        return 0;
    }

    char body[256];
    int len = snprintf(body, sizeof(body), "file=%s\n", file);
    ssize_t wrote = write(fd, body, (size_t)len);
    close(fd);
    if (wrote != len) {
        unlink(req);
        snprintf(am->status, sizeof(am->status), "cannot queue a delete");
        return 0;
    }

    char unit[128];
    snprintf(unit, sizeof(unit), "syn-model-delete@%s.service", token);

    pid_t pid = fork();
    if (pid < 0) {
        unlink(req);
        snprintf(am->status, sizeof(am->status), "cannot start the deleter");
        return 0;
    }
    if (pid == 0) {
        setsid();
        synui_child_reset_signals();
        execlp("systemctl", "systemctl", "start", "--no-block", unit,
               (char *)NULL);
        _exit(127);
    }

    snprintf(am->del_token, sizeof(am->del_token), "%s", token);
    snprintf(am->del_file,  sizeof(am->del_file),  "%s", file);
    am->del_until = aimodel_now() + 10.0;
    snprintf(am->status, sizeof(am->status), "deleting %s \xe2\x80\xa6", file);

    wlr_log(WLR_INFO, "synui: aimodel: queued delete of %s", file);
    if (am->dl_timer) wl_event_source_timer_update(am->dl_timer, 300);
    return 1;
}

/*
 * Watch for the file to actually go.
 *
 * The directory is the source of truth, not the unit's exit status: synui does
 * not wait on the child, and `systemctl start --no-block` returns before the
 * unit runs. If the file is gone the delete happened, whoever did it.
 *
 * The deadline is what turns a silent polkit refusal — the failure this whole
 * spool arrangement is most prone to — into a sentence instead of a status
 * line stuck on "deleting …". The progress file carries the privileged half's
 * own reason when it got far enough to write one.
 */
static void aimodel_poll_delete(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;
    if (!am->del_token[0]) return;

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", AIMODEL_DIR, am->del_file);

    if (access(path, F_OK) != 0) {
        snprintf(am->status, sizeof(am->status), "deleted %s", am->del_file);
        am->del_token[0] = '\0';
        am->del_file[0]  = '\0';
        am->del_until    = 0.0;

        /* The list is rebuilt rather than patched: aimodel_scan re-sorts, and
         * the cursor has to be put back inside a list that just got shorter. */
        aimodel_scan(s);
        aimodel_mark_loaded(s);
        if (am->selected >= am->count) am->selected = am->count - 1;
        if (am->selected < 0) am->selected = 0;
        return;
    }

    if (aimodel_now() < am->del_until) return;

    /* Still there and out of time. Prefer the privileged half's own words. */
    char pp[256], buf[256] = {0};
    snprintf(pp, sizeof(pp), "%s/%s.progress", AIMODEL_RUN_DIR, am->del_token);
    int fd = open(pp, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        ssize_t r = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (r > 0) buf[r] = '\0';
    }
    const char *msg = strstr(buf, "msg=");
    if (msg) {
        msg += 4;
        size_t n = strcspn(msg, "\n");
        snprintf(am->status, sizeof(am->status), "delete failed: %.*s",
                 (int)n, msg);
    } else {
        snprintf(am->status, sizeof(am->status),
                 "delete did not happen \xc2\xb7 check: systemctl status "
                 "syn-model-delete@%s", am->del_token);
    }
    wlr_log(WLR_ERROR, "synui: aimodel: delete of %s did not complete",
            am->del_file);

    am->del_token[0] = '\0';
    am->del_file[0]  = '\0';
    am->del_until    = 0.0;
}

/*
 * Read the progress file the privileged half writes.
 *
 * Polled rather than watched: it is rewritten once a second by a mv, so an
 * inotify watch would be re-arming on a new inode every second for a file this
 * cheap to stat.
 */
static void aimodel_poll_download(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;
    if (!am->dl.token[0]) return;

    char path[256];
    snprintf(path, sizeof(path), "%s/%s.progress", AIMODEL_RUN_DIR, am->dl.token);

    FILE *fp = fopen(path, "re");
    if (!fp) {
        /* Not there yet is normal for the first poll or two: the unit has to be
         * activated before it can write anything. */
        if (am->dl.state == AIMODEL_DL_STARTING) return;
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        if (!strncmp(line, "state=", 6)) {
            const char *v = line + 6;
            if      (!strcmp(v, "running")) am->dl.state = AIMODEL_DL_RUNNING;
            else if (!strcmp(v, "done"))    am->dl.state = AIMODEL_DL_DONE;
            else if (!strcmp(v, "failed"))  am->dl.state = AIMODEL_DL_FAILED;
        } else if (!strncmp(line, "pct=", 4)) {
            am->dl.pct = atoi(line + 4);
        } else if (!strncmp(line, "got=", 4)) {
            am->dl.got = strtoll(line + 4, NULL, 10);
        } else if (!strncmp(line, "total=", 6)) {
            am->dl.total = strtoll(line + 6, NULL, 10);
        } else if (!strncmp(line, "msg=", 4)) {
            snprintf(am->dl.msg, sizeof(am->dl.msg), "%s", line + 4);
        }
    }
    fclose(fp);

    if (am->dl.state == AIMODEL_DL_DONE) {
        /* The file is on the disk now — rescan so it appears under INSTALLED,
         * where it can be loaded like any other. */
        aimodel_scan(s);
        aimodel_mark_loaded(s);
        snprintf(am->status, sizeof(am->status),
                 "downloaded %s \xc2\xb7 select it to load", am->dl.file);
    } else if (am->dl.state == AIMODEL_DL_FAILED) {
        snprintf(am->status, sizeof(am->status), "download failed: %s",
                 am->dl.msg[0] ? am->dl.msg : "see journalctl -u syn-model-download@");
    }
}

static int aimodel_dl_tick(void *data)
{
    syn_server_t *s = data;
    syn_aimodel_t *am = &s->aimodel;
    double t = aimodel_now();

    int live = (am->dl.state == AIMODEL_DL_STARTING ||
                am->dl.state == AIMODEL_DL_RUNNING);
    if (live) aimodel_poll_download(s);

    /*
     * Nothing at all after the unit was asked for.
     *
     * `systemctl start` is spawned and not waited on, so a polkit refusal —
     * the rule not installed, or the session not local and active — produces
     * no error anywhere synui can see. Without this the panel would sit on
     * "starting…" for the rest of the session and look like a slow download
     * rather than one that never began.
     */
    if (am->dl.state == AIMODEL_DL_STARTING &&
        t - am->dl.started_at > 15.0) {
        am->dl.state = AIMODEL_DL_FAILED;
        snprintf(am->dl.msg, sizeof(am->dl.msg), "the downloader never started");
        snprintf(am->status, sizeof(am->status),
                 "download did not start \xc2\xb7 check: systemctl status "
                 "syn-model-download@%s", am->dl.token);
        wlr_log(WLR_ERROR, "synui: aimodel: %s never reported progress",
                am->dl.token);
        live = 0;
    }

    /* The detail fetch is debounced here too, so arrowing through the list
     * fires one request when the cursor stops rather than one per keypress —
     * the same reason the control-panel row waits before loading a model. */
    if (am->detail_at > 0.0 && t >= am->detail_at) {
        am->detail_at = 0.0;
        syn_aimodel_cat_t *c = cat_current(am);
        if (c && c->detail == AIMODEL_DETAIL_WANT) {
            c->detail = AIMODEL_DETAIL_BUSY;
            aimodel_request_detail(s, c->id);
        }
    }

    /* A delete in flight is watched on the same tick — it is a stat of one
     * path, and giving it a timer of its own would be a second thing to arm,
     * disarm and tear down for work that finishes in milliseconds. */
    aimodel_poll_delete(s);

    if (am->visible) synui_render_aimodel(s);

    /* Re-armed only while there is something to watch: the panel is open (the
     * debounce needs the tick) or a download is running, which outlives the
     * panel because closing it must not abandon a 4 GB fetch. A finished
     * download re-arms nothing — its result is already in am->dl for whenever
     * the panel is opened again. A delete counts as live for the same reason a
     * download does: its outcome must still be reported if the panel closed. */
    if (am->visible || live || am->del_token[0])
        wl_event_source_timer_update(am->dl_timer,
                                     (live || am->del_token[0]) ? 400 : 200);
    return 0;
}

/* Put the cursor on a catalogue row and arm its detail fetch. */
static void aimodel_touch_detail(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;
    syn_aimodel_cat_t *c = cat_current(am);
    if (!c) return;
    if (c->detail == AIMODEL_DETAIL_OK || c->detail == AIMODEL_DETAIL_BUSY) return;

    c->detail = AIMODEL_DETAIL_WANT;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    am->detail_at = (double)now.tv_sec + now.tv_nsec / 1e9 + 0.35;
    if (am->dl_timer) wl_event_source_timer_update(am->dl_timer, 200);
}

/* ── Lifecycle ───────────────────────────────────────────── */

void aimodel_init(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;

    am->loaded_idx = -1;
    am->cat_sel    = -1;
    am->del_armed  = -1;
    am->pipe[0] = am->pipe[1] = -1;
    atomic_store(&am->stop, 0);
    atomic_store(&am->want, 0);

    /* news.c calls this too and libcurl counts its initialisations, so a second
     * call is a no-op rather than a conflict. */
    curl_global_init(CURL_GLOBAL_DEFAULT);

    if (pipe2(am->pipe, O_CLOEXEC) < 0) {
        wlr_log(WLR_ERROR, "synui: aimodel: pipe() failed");
        am->pipe[0] = am->pipe[1] = -1;
        return;
    }
    fcntl(am->pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(am->pipe[1], F_SETFL, O_NONBLOCK);

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    am->src      = wl_event_loop_add_fd(loop, am->pipe[0], WL_EVENT_READABLE,
                                        aimodel_readable, s);
    am->dl_timer = wl_event_loop_add_timer(loop, aimodel_dl_tick, s);

    pthread_mutex_init(&am->lock, NULL);
    pthread_cond_init(&am->cv, NULL);

    if (pthread_create(&am->thread, NULL, aimodel_thread_fn, s) != 0) {
        wlr_log(WLR_ERROR, "synui: aimodel: thread failed");
        if (am->src)      { wl_event_source_remove(am->src);      am->src = NULL; }
        if (am->dl_timer) { wl_event_source_remove(am->dl_timer); am->dl_timer = NULL; }
        close(am->pipe[0]); close(am->pipe[1]);
        am->pipe[0] = am->pipe[1] = -1;
        pthread_mutex_destroy(&am->lock);
        pthread_cond_destroy(&am->cv);
        return;
    }
    am->running = 1;
}

void aimodel_finish(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;

    if (am->running) {
        atomic_store(&am->stop, 1);
        pthread_mutex_lock(&am->lock);
        pthread_cond_signal(&am->cv);      /* out of the idle wait… */
        pthread_mutex_unlock(&am->lock);
        pthread_join(am->thread, NULL);    /* …and cat_progress aborts a transfer */
        am->running = 0;
        pthread_mutex_destroy(&am->lock);
        pthread_cond_destroy(&am->cv);
    }

    if (am->src)      { wl_event_source_remove(am->src);      am->src = NULL; }
    if (am->dl_timer) { wl_event_source_remove(am->dl_timer); am->dl_timer = NULL; }
    if (am->pipe[0] >= 0) { close(am->pipe[0]); close(am->pipe[1]); }
    am->pipe[0] = am->pipe[1] = -1;
}

/* ── Panel ───────────────────────────────────────────────── */

void aimodel_show(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;

    am->visible   = 1;
    am->switching = 0;
    am->status[0] = '\0';
    am->loaded_idx = -1;
    /* Never opens with a confirmation already on screen, whatever was armed
     * when it was last closed. */
    am->del_armed = -1;

    aimodel_scan(s);
    if (am->selected >= am->count) am->selected = 0;
    if (am->selected < 0)          am->selected = 0;
    aimodel_mark_loaded(s);

    /* The cursor opens in INSTALLED — the panel's first job is still switching
     * between the models that are here. */
    am->cat_sel = -1;
    am->typing  = 0;

    /* One search per session unless something asks for another: the list is
     * "the most downloaded GGUF repos", which does not change while a panel is
     * open, and a request every time the panel opened would be traffic nobody
     * asked for.
     *
     * An empty list IS asking for another, though — the usual reason for one is
     * that the last attempt could not reach the network, and re-opening the
     * panel after fixing that must not keep showing the failure. The in-flight
     * flag is what stops that becoming a request per keypress. */
    if (am->n_cat == 0 && !am->searching)
        aimodel_request_search(s);

    /* Drives the debounce and, if one is running, the download's progress. */
    if (am->dl_timer) wl_event_source_timer_update(am->dl_timer, 200);

    /* The detail lines come from the status poller, which only runs while
     * something wants it. Ask for it, or the panel opens on stale numbers and
     * fills in a second later for no reason the user can see. */
    synmon_want_refresh(s);

    synui_render_aimodel(s);
}

void aimodel_hide(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;
    am->visible = 0;
    am->typing  = 0;

    /* A pending detail fetch dies with the panel; a DOWNLOAD does not. Closing
     * the picker on a 4 GB fetch is ordinary, and the unit is not synui's child
     * anyway — the tick keeps following it so reopening shows where it got to. */
    am->detail_at = 0.0;
    if (am->dl_timer &&
        am->dl.state != AIMODEL_DL_STARTING && am->dl.state != AIMODEL_DL_RUNNING)
        wl_event_source_timer_update(am->dl_timer, 0);   /* disarm */

    /* Release the poller unless something else still wants it — the overlay, or
     * the control panel, whose AI-model row follows the daemon the same way.
     * Leaving it armed would keep a socket round-trip running once a second for
     * a panel nobody is looking at. */
    synmon_want_refresh(s);

    synui_render_aimodel(s);
    ctlpanel_child_closed(s, "aimodel");
}

void aimodel_toggle(syn_server_t *s)
{
    if (s->aimodel.visible) aimodel_hide(s);
    else                    aimodel_show(s);
}

/*
 * Fold the newest status poll into the picker's state.
 *
 * This is how a switch finishes. synapd reports model=loading while the swap
 * runs and model=loaded when it is done, so the switch clears its own
 * "switching" flag on the daemon's word rather than on a timer — a 12 GB model
 * on a cold cache takes as long as it takes.
 *
 * Shared by the panel and the control-panel row: a switch fired from the row
 * has to finish the same way, and a second copy of "is it done yet" is exactly
 * the kind of thing that would come to disagree.
 */
/*
 * How long a switch may sit unstarted before an unchanged daemon is read as a
 * failure rather than as one about to begin.
 *
 * Only used when the "loading" state was never observed — the poll runs about
 * once a second and a small model can load between two of them. Six seconds is
 * far longer than synapd takes to accept a request and spawn the thread, and
 * far shorter than any real load, so neither end of it is a close call.
 */
#define AIMODEL_SWITCH_GRACE 6.0

static void aimodel_settle(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;

    if (am->switching) {
        const char *state = s->overlay.model;
        const char *file  = s->overlay.model_file;

        /* Proof the daemon actually started. Once this is seen, a report of
         * anything other than the requested file is a finished attempt. */
        if (strcmp(state, "loading") == 0) am->switch_seen_loading = 1;

        int settled = (strcmp(state, "loaded") == 0);
        int arrived = settled && strcmp(file, am->switch_file) == 0;

        if (arrived) {
            /* Both conditions matter. "loaded" alone is still true for the OLD
             * model in the moment between the request being accepted and
             * synapd taking the write lock, so waiting on the filename too is
             * what stops the panel calling a switch done before it has begun.
             *
             * Matched against switch_file rather than the row under the
             * cursor: the cursor moves while several GB load, and a switch
             * that finished must settle against what was ASKED FOR. */
            am->switching = 0;
            snprintf(am->status, sizeof(am->status), "loaded");
        } else if ((settled || strcmp(state, "none") == 0) &&
                   (am->switch_seen_loading ||
                    aimodel_now() - am->switch_at > AIMODEL_SWITCH_GRACE)) {
            /*
             * It finished, and what came back is not what was asked for.
             *
             * synapd restores the previous model when a load fails, so the
             * daemon ends up looking EXACTLY as it did before the request —
             * which is why this branch did not exist and why the panel sat on
             * "loading …" forever, refusing every later pick with "still
             * loading · wait for it to finish". There was nothing to wait for.
             *
             * The reason comes from synapd, which now carries llama.cpp's own
             * words. Against an older daemon that field is empty and the panel
             * says only that it failed — still infinitely better than a
             * spinner that never stops.
             */
            const char *why = s->overlay.switch_err;
            int mine = (s->overlay.switch_file[0] == '\0') ||
                       (strcmp(s->overlay.switch_file, am->switch_file) == 0);

            am->switching = 0;
            if (why[0] && mine)
                snprintf(am->status, sizeof(am->status),
                         "could not load \xc2\xb7 %s", why);
            else
                snprintf(am->status, sizeof(am->status),
                         "could not load \xc2\xb7 synapd is still running %s",
                         file[0] ? file : "nothing");

            wlr_log(WLR_ERROR, "synui: switch to %s failed: %s",
                    am->switch_file, why[0] ? why : "(no reason reported)");
        }
    }

    aimodel_mark_loaded(s);
}

/*
 * A status poll landed.
 *
 * Two consumers now — the picker, and the control panel's AI-model row, which
 * shows the same fact in one line. Neither is up most of the time, and the poll
 * only runs while one of them is, so the early return is the common case.
 */
void aimodel_status_changed(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;
    if (!am->visible && !s->ctlpanel.visible) return;

    aimodel_settle(s);

    if (am->visible) {
        synui_render_aimodel(s);
        return;
    }

    /* Row only. Follow the daemon: if synapd is running something else — the
     * picker loaded it, a `synctl` did, or synapd restarted onto its default —
     * the row says so rather than sitting on a stale pick.
     *
     * Not while a pick is settling or a switch is in flight: those ARE the
     * user's cursor, and snapping it back to the model still resident would
     * undo the choice being made a keypress at a time. */
    if (!am->switching && s->ctlpanel.model_commit_at == 0.0 &&
        am->loaded_idx >= 0)
        am->selected = am->loaded_idx;

    ctlpanel_refresh(s);
}

/* ── Activation ──────────────────────────────────────────── */

/*
 * Ask synapd for whatever the cursor is on.
 *
 * Returns 1 if a request actually went out, 0 otherwise — refused, already
 * loaded, or nothing to load. Either way am->status carries the reason, which
 * is what both callers put on screen; neither renders here, because the panel
 * and the control-panel row draw different things.
 */
static int aimodel_load_selected(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;
    if (am->count == 0) return 0;
    if (am->selected < 0 || am->selected >= am->count) return 0;

    if (am->switching) {
        snprintf(am->status, sizeof(am->status),
                 "still loading \xc2\xb7 wait for it to finish");
        return 0;
    }

    const char *name = am->models[am->selected].name;

    /* Already running it: say so rather than making synapd unload and reload
     * several GB to arrive back where it started. */
    if (am->loaded_idx == am->selected) {
        snprintf(am->status, sizeof(am->status), "already loaded");
        return 0;
    }

    char reply[160] = {0};
    if (synmon_send_reload(name, reply, sizeof(reply)) != 0) {
        /* synapd's refusal names the rule that was broken — show it verbatim
         * instead of a generic failure that sends you to the logs. */
        snprintf(am->status, sizeof(am->status), "%s",
                 reply[0] ? reply : "synapd refused the switch");
        wlr_log(WLR_ERROR, "synui: model switch to %s failed: %s", name, reply);
        return 0;
    }

    am->switching  = 1;
    am->loaded_idx = am->selected;

    /* What was asked for, and when — not the row index. The cursor moves while
     * several GB load, so an index cannot say afterwards which file this was,
     * and settling is now a three-way question (arrived / failed / not yet)
     * rather than the one-way one it used to be. */
    snprintf(am->switch_file, sizeof(am->switch_file), "%s", name);
    am->switch_at           = aimodel_now();
    am->switch_seen_loading = 0;

    snprintf(am->status, sizeof(am->status), "loading \xe2\x80\xa6");
    wlr_log(WLR_INFO, "synui: switching synapd to %s", name);
    return 1;
}

/* ── The control-panel row ───────────────────────────────── */

void aimodel_row_sync(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;

    /* Not while the picker owns the state: it has its own cursor on screen and
     * the control panel is only stepping aside for it. */
    if (am->visible) return;

    aimodel_scan(s);
    aimodel_mark_loaded(s);

    /* switching is deliberately NOT cleared here. A switch fired from the row
     * outlives the panel — Esc while a 7 GB model loads is normal — and
     * clearing it would let the row show a pick as settled, and a second
     * request go out, while synapd is still holding the write lock. It clears
     * when the daemon says so (aimodel_settle), or when the picker is opened,
     * which is the escape hatch if synapd died mid-load and never will.
     *
     * Only the leftover text goes, since it describes a switch that has
     * finished; a live "loading …" is still true. */
    if (!am->switching) am->status[0] = '\0';

    /* Start on the loaded model. -1 means synapd has not said yet (the poll is
     * one round trip behind the panel opening) or is running something outside
     * the directory — leave the cursor where it was; the next poll to land
     * moves it, via aimodel_status_changed(). */
    if (am->loaded_idx >= 0)      am->selected = am->loaded_idx;
    else if (am->selected >= am->count) am->selected = 0;
}

void aimodel_row_value(syn_server_t *s, char *buf, size_t n)
{
    syn_aimodel_t *am = &s->aimodel;

    if (am->count == 0)                                  { snprintf(buf, n, "none"); return; }
    if (am->selected < 0 || am->selected >= am->count)   { snprintf(buf, n, "none"); return; }

    const char *name = am->models[am->selected].name;

    /* Every entry ends in .gguf, so the suffix distinguishes nothing and the
     * row has one line to spend. The picker shows the full filename — that is
     * where you go when you need to know exactly which file. */
    size_t len = strlen(name);
    if (len > 5 && strcmp(name + len - 5, ".gguf") == 0) len -= 5;

    if (am->switching) {
        snprintf(buf, n, "%.*s \xc2\xb7 loading", (int)len, name);
        return;
    }

    /* The row is the menu velle actually uses, so it carries the two facts that
     * decide a pick — how big the model is and how hard it was squashed —
     * rather than making the picker the only place they exist. Both come from
     * the file's own header; the size label is preferred over the parameter
     * count for the same reason the pane prefers it (most files omit the
     * count). Either can be missing, and the row simply gets shorter.
     *
     * Nothing else is added here: this is drawn right-aligned and unclipped,
     * so a value that grows without limit walks into the row's label. */
    const syn_gguf_t *g = aimodel_info(s, am->selected);
    char tag[40] = {0};
    if (g && g->ok) {
        char pbuf[24] = {0};
        if (g->size_label[0])
            snprintf(pbuf, sizeof(pbuf), "%s", g->size_label);
        else if (g->params > 0)
            gguf_params_str(g->params, pbuf, sizeof(pbuf));

        if (pbuf[0] && g->quant[0])
            snprintf(tag, sizeof(tag), " \xc2\xb7 %s %s", pbuf, g->quant);
        else if (pbuf[0])
            snprintf(tag, sizeof(tag), " \xc2\xb7 %s", pbuf);
        else if (g->quant[0])
            snprintf(tag, sizeof(tag), " \xc2\xb7 %s", g->quant);
    }

    snprintf(buf, n, "%.*s%s", (int)len, name, tag);
}

int aimodel_row_cycle(syn_server_t *s, int dir)
{
    syn_aimodel_t *am = &s->aimodel;

    /* The directory is not watched, and the control panel may have been open
     * since before a model was dropped in. An empty list is worth a second look
     * before reporting it as one. */
    if (am->count == 0) aimodel_scan(s);
    if (am->count == 0) return 0;

    /* Refused rather than queued: synapd rejects a second reload while the
     * first is resident-loading anyway, and a queue would mean the row you let
     * go of is not the model you end up with. */
    if (am->switching) {
        snprintf(am->status, sizeof(am->status),
                 "still loading \xc2\xb7 wait for it to finish");
        return 0;
    }

    /* Clamped before stepping: a rescan can shrink the list under a cursor that
     * was valid when it was last set. */
    if (am->selected < 0 || am->selected >= am->count) am->selected = 0;

    am->selected = (am->selected + dir + am->count) % am->count;
    am->status[0] = '\0';
    return 1;
}

int aimodel_row_commit(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;

    /* Landing back on the model already loaded is the ordinary way out of a
     * cycle you did not mean to start — say nothing rather than "already
     * loaded", which would read as a failure. */
    if (am->loaded_idx == am->selected) {
        am->status[0] = '\0';
        return 0;
    }
    return aimodel_load_selected(s);
}

const char *aimodel_status_text(syn_server_t *s)
{
    return s->aimodel.status;
}

/* ── The cursor ──────────────────────────────────────────── */
/*
 * One column, two sections, and the section headings are slots of their own.
 *
 *   slot 0                      INSTALLED
 *   slot 1 … count              the models on the disk
 *   slot count+1                AVAILABLE
 *   slot count+2 … +n_cat       the search results
 *
 * Counting the headings as slots is what keeps the hit grid uniform: render.c
 * draws one row per slot, so a click lands on the row it points at without a
 * second layout calculation that could drift from the drawn one.
 */

int aimodel_slots(const syn_aimodel_t *am)
{
    return am->count + am->n_cat + 2;
}

int aimodel_slot_is_head(const syn_aimodel_t *am, int slot)
{
    return slot == 0 || slot == am->count + 1;
}

int aimodel_cursor_slot(const syn_aimodel_t *am)
{
    if (am->cat_sel >= 0) return am->count + 2 + am->cat_sel;
    return am->count ? 1 + am->selected : 0;
}

/* Put the cursor on a slot, skipping the headings in the direction of travel.
 * Returns 1 if it moved. */
static int aimodel_cursor_to(syn_server_t *s, int slot, int dir)
{
    syn_aimodel_t *am = &s->aimodel;
    int n = aimodel_slots(am);

    while (slot >= 0 && slot < n && aimodel_slot_is_head(am, slot))
        slot += dir ? dir : 1;
    if (slot < 0 || slot >= n) return 0;

    if (slot <= am->count) {
        am->selected = slot - 1;
        am->cat_sel  = -1;
    } else {
        am->cat_sel = slot - am->count - 2;
        aimodel_touch_detail(s);
    }

    /* Follow the cursor with the window, one row at a time. */
    if (slot < am->scroll)                   am->scroll = slot;
    if (slot >= am->scroll + AIMODEL_ROWS)   am->scroll = slot - AIMODEL_ROWS + 1;
    if (am->scroll > n - AIMODEL_ROWS)       am->scroll = n - AIMODEL_ROWS;
    if (am->scroll < 0)                      am->scroll = 0;
    return 1;
}

static void aimodel_move(syn_server_t *s, int dir)
{
    syn_aimodel_t *am = &s->aimodel;
    if (am->count == 0 && am->n_cat == 0) return;
    aimodel_cursor_to(s, aimodel_cursor_slot(am) + dir, dir);
}

/* ── Input ───────────────────────────────────────────────── */

/* Enter, on whichever section the cursor is in: load what is here, fetch what
 * is not. Two verbs on one key because they are the same intention — "use this
 * model" — and the row says which one it will be. */
static void aimodel_confirm(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;

    if (am->cat_sel >= 0) aimodel_start_download(s);
    else                  aimodel_load_selected(s);

    synui_render_aimodel(s);
}

/* The search box. The only panel here that takes text, so it is deliberately
 * the smallest thing that works: printable ASCII in, Backspace out, Enter
 * searches, Esc leaves the box without closing the panel. */
static int aimodel_key_typing(syn_server_t *s, xkb_keysym_t sym)
{
    syn_aimodel_t *am = &s->aimodel;
    size_t len = strlen(am->query);

    switch (sym) {
    case XKB_KEY_Escape:
        am->typing = 0;
        am->query[0] = '\0';
        aimodel_request_search(s);        /* back to the default listing */
        synui_render_aimodel(s);
        return 1;

    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        am->typing = 0;
        am->cat_sel = am->n_cat ? 0 : -1;
        am->scroll  = 0;
        aimodel_request_search(s);
        synui_render_aimodel(s);
        return 1;

    case XKB_KEY_BackSpace:
        if (len) am->query[len - 1] = '\0';
        synui_render_aimodel(s);
        return 1;

    default:
        break;
    }

    /* One byte at a time: the query is bound for a URL, and url_escape_query()
     * keeps only what may appear in one — so anything wider than ASCII would be
     * dropped there anyway, and is not accepted here. */
    char buf[8];
    int n = xkb_keysym_to_utf8(sym, buf, sizeof(buf));
    if (n == 2 && (unsigned char)buf[0] >= 0x20 && (unsigned char)buf[0] < 0x7f &&
        len + 1 < sizeof(am->query)) {
        am->query[len]     = buf[0];
        am->query[len + 1] = '\0';
    }
    synui_render_aimodel(s);
    return 1;
}

int aimodel_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    (void)mods;
    syn_aimodel_t *am = &s->aimodel;
    if (!am->visible) return 0;

    if (am->typing) return aimodel_key_typing(s, sym);

    switch (sym) {
    case XKB_KEY_Escape:
        /* Esc backs out of the armed delete before it closes the panel. With
         * a confirmation on screen, Esc means "not that" — closing the whole
         * picker instead would be answering a different question. */
        if (am->del_armed >= 0) {
            aimodel_delete_cancel(s);
            synui_render_aimodel(s);
            return 1;
        }
        aimodel_hide(s);
        return 1;

    /* Delete arms, and a second Delete confirms. Nothing is removed by one
     * keystroke: these files are gigabytes that took a long download each. */
    case XKB_KEY_Delete:
    case XKB_KEY_KP_Delete:
        if (am->del_armed >= 0) aimodel_delete_confirm(s);
        else                    aimodel_delete_arm(s);
        synui_render_aimodel(s);
        return 1;

    case XKB_KEY_Up:
    case XKB_KEY_k:
        /* Any move disarms. The confirmation names one model, so the moment
         * the cursor leaves it the question on screen is no longer the one
         * that would be answered. */
        aimodel_delete_cancel(s);
        aimodel_move(s, -1);
        synui_render_aimodel(s);
        return 1;

    case XKB_KEY_Down:
    case XKB_KEY_j:
        aimodel_delete_cancel(s);
        aimodel_move(s, +1);
        synui_render_aimodel(s);
        return 1;

    /* On a catalogue row, Left/Right are the quantisation — the one choice
     * inside a repo that changes what you get, and the info pane is showing
     * the list they step through. On an installed row they do nothing, because
     * there is nothing to step through. */
    case XKB_KEY_Left:
    case XKB_KEY_h:
    case XKB_KEY_Right:
    case XKB_KEY_l: {
        syn_aimodel_cat_t *c = cat_current(am);
        if (c && c->n_files > 0) {
            int dir = (sym == XKB_KEY_Right || sym == XKB_KEY_l) ? 1 : -1;
            c->sel_file = (c->sel_file + dir + c->n_files) % c->n_files;
        }
        synui_render_aimodel(s);
        return 1;
    }

    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
    case XKB_KEY_space:
        /* Enter is "load", never "yes". Only Delete confirms a delete —
         * otherwise the key you press to use a model would, one keystroke
         * earlier in the wrong state, destroy it. */
        aimodel_delete_cancel(s);
        aimodel_confirm(s);
        return 1;

    case XKB_KEY_slash:
        am->typing   = 1;
        am->query[0] = '\0';
        synui_render_aimodel(s);
        return 1;

    case XKB_KEY_r:
        /* The directory is not watched — a model dropped in while the panel is
         * open should not need a close and reopen to appear. The search is
         * refreshed with it, since this is the "show me what is true now" key. */
        aimodel_scan(s);
        if (am->selected >= am->count) am->selected = am->count ? am->count - 1 : 0;
        aimodel_request_search(s);
        snprintf(am->status, sizeof(am->status), "rescanned");
        synui_render_aimodel(s);
        return 1;

    default:
        break;
    }

    /* Swallow everything while up, exactly as the other panels do: a stray key
     * reaching the desktop from an open modal is its own surprise. */
    return 1;
}

int aimodel_motion(syn_server_t *s, double lx, double ly)
{
    syn_aimodel_t *am = &s->aimodel;
    if (!am->visible) return 0;

    /* The info pane's file rows first: they overlap the list's row band, and a
     * pointer over the right column is choosing a quantisation, not a repo. */
    int frow = hit_index_at(&am->hit_files, lx, ly);
    if (frow >= 0) {
        syn_aimodel_cat_t *c = cat_current(am);
        if (c && frow < c->n_files && frow != c->sel_file) {
            c->sel_file = frow;
            synui_render_aimodel(s);
        }
        return 1;
    }

    int slot = hit_index_at(&am->hit, lx, ly);
    if (slot >= 0 && !aimodel_slot_is_head(am, slot) &&
        slot != aimodel_cursor_slot(am)) {
        aimodel_cursor_to(s, slot, 0);
        synui_render_aimodel(s);
    }
    return hit_in_panel(&am->hit, lx, ly);
}

int aimodel_scroll(syn_server_t *s, double lx, double ly, double delta)
{
    (void)lx; (void)ly;
    syn_aimodel_t *am = &s->aimodel;
    if (!am->visible) return 0;

    /* Moves the cursor, never loads and never downloads: a wheel is how you
     * look down a list, and neither a model swap nor a 4 GB fetch is something
     * to trip into with a flick. */
    aimodel_move(s, delta > 0 ? 1 : -1);
    synui_render_aimodel(s);
    return 1;
}

int aimodel_click(syn_server_t *s, double lx, double ly, uint32_t button,
                  uint32_t time_msec)
{
    (void)time_msec;   /* only the pickers need it, for their double click */
    syn_aimodel_t *am = &s->aimodel;
    if (!am->visible) return 0;

    /* Clicking away closes, as every other modal here does. */
    if (!hit_in_panel(&am->hit, lx, ly)) {
        aimodel_hide(s);
        return 1;
    }

    int on_file = hit_index_at(&am->hit_files, lx, ly) >= 0;
    aimodel_motion(s, lx, ly);            /* act on the row pointed at */

    if (button != BTN_LEFT) return 1;

    /* A click in the info pane picks the quantisation and stops there: the
     * download is the deliberate second act, on the row or on Enter. */
    if (on_file) return 1;

    int slot = hit_index_at(&am->hit, lx, ly);
    if (slot < 0 || aimodel_slot_is_head(am, slot)) return 1;   /* chrome */

    aimodel_confirm(s);
    return 1;
}
