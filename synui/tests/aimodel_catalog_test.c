/*
 * aimodel_catalog_test.c — the download catalogue's parsers and its refusals.
 *
 * The AVAILABLE section of the model picker is filled from Hugging Face at
 * runtime, which makes every string in it hostile input by construction: a
 * repo id becomes part of a URL, and a filename inside a repo becomes the name
 * of a file ROOT WRITES into synapd's models directory. There is no way to
 * exercise that against the live API in a test — so what is pinned here is the
 * pure half: given a response body, what does synui believe, and given what it
 * believes, what will it agree to send.
 *
 * The refusals matter more than the parsing. syn-model re-checks all of them
 * on the privileged side (tests/fetch_validate_test.sh over there), and this
 * is the half that stops a bad request being made at all — belt and braces on
 * purpose, because either one failing alone is not a bug that shows up until
 * it is being exploited.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <string.h>

#include "synui.h"

/* ── The compositor, stubbed ─────────────────────────────────
 * aimodel.c reaches for the panel, the daemon and the control panel; none of
 * that is what this test is about, and none of it is reached by the pure
 * functions below. */
void synui_render_aimodel(syn_server_t *s)   { (void)s; }
void synmon_want_refresh(syn_server_t *s)    { (void)s; }
void ctlpanel_refresh(syn_server_t *s)       { (void)s; }
void synui_child_reset_signals(void)         { }
void ctlpanel_child_closed(syn_server_t *s, const char *who)
{ (void)s; (void)who; }
int synmon_send_reload(const char *model_name, char *out, size_t out_len)
{ (void)model_name; (void)out; (void)out_len; return 0; }

static int fails;
#define CHECK(cond, ...) do {                                   \
    if (!(cond)) { printf("  FAIL — "); printf(__VA_ARGS__);    \
                   printf("\n"); fails++; }                     \
    else         { printf("  ok   — "); printf(__VA_ARGS__);    \
                   printf("\n"); }                              \
} while (0)

/* ── The search listing ──────────────────────────────────── */

/* Shaped like a real /api/models answer, including the parts that have broken
 * hand-written scanners before: a description carrying braces and quotes, and
 * the license arriving as a tag rather than a field. */
static const char SEARCH[] =
"[{\"_id\":\"1\",\"id\":\"TheBloke/Mistral-7B-Instruct-v0.2-GGUF\",\"likes\":412,"
  "\"downloads\":1234567,\"tags\":[\"transformers\",\"gguf\",\"license:apache-2.0\"],"
  "\"cardData\":{\"description\":\"a {brace} and a \\\"quote\\\" in prose\"},"
  "\"pipeline_tag\":\"text-generation\"},"
 "{\"_id\":\"2\",\"id\":\"microsoft/Phi-3-mini-4k-instruct-gguf\",\"likes\":88,"
  "\"downloads\":4321,\"tags\":[\"gguf\",\"license:mit\"]},"
 "{\"_id\":\"3\",\"id\":\"bad id/with space\",\"downloads\":9},"
 "{\"_id\":\"4\",\"id\":\"../../etc/passwd\",\"downloads\":9},"
 "{\"_id\":\"5\",\"id\":\"Qwen/Qwen2-0.5B-Instruct-GGUF\",\"downloads\":500}]";

static void test_search(void)
{
    syn_aimodel_cat_t cat[8];
    int n = aimodel_parse_search(SEARCH, sizeof(SEARCH) - 1, cat, 8);

    CHECK(n == 3, "three usable repos out of five entries (got %d)", n);
    if (n < 3) return;

    CHECK(strcmp(cat[0].id, "TheBloke/Mistral-7B-Instruct-v0.2-GGUF") == 0,
          "repo id survives a description full of braces and quotes");
    CHECK(strcmp(cat[0].author, "TheBloke") == 0, "author is the half before the slash");
    CHECK(strcmp(cat[0].name, "Mistral-7B-Instruct-v0.2-GGUF") == 0,
          "name is the half after it");
    CHECK(cat[0].downloads == 1234567, "downloads (got %lld)", cat[0].downloads);
    CHECK(cat[0].likes == 412, "likes (got %lld)", cat[0].likes);
    CHECK(strcmp(cat[0].license, "apache-2.0") == 0,
          "license comes out of the tag list (got \"%s\")", cat[0].license);
    CHECK(strcmp(cat[0].params, "7B") == 0,
          "parameter count read from the name (got \"%s\")", cat[0].params);

    CHECK(strcmp(cat[1].license, "mit") == 0, "second repo's license");
    CHECK(cat[1].params[0] == '\0',
          "no parameter count invented for Phi-3-mini (got \"%s\")", cat[1].params);

    for (int i = 0; i < n; i++) {
        CHECK(strstr(cat[i].id, " ") == NULL, "no listed id carries a space");
        CHECK(strstr(cat[i].id, "..") == NULL, "no listed id carries a dot segment");
    }
    CHECK(strcmp(cat[2].id, "Qwen/Qwen2-0.5B-Instruct-GGUF") == 0,
          "a good entry after two rejected ones is still listed");
    CHECK(strcmp(cat[2].params, "0.5B") == 0,
          "a fractional parameter count (got \"%s\")", cat[2].params);
}

/* ── The file tree ───────────────────────────────────────── */

/* The LFS shape is the one that matters: the real size and the 135-byte
 * pointer are both called "size", and reading the wrong one lists a 4 GB model
 * as 135 bytes. */
static const char TREE[] =
"[{\"type\":\"file\",\"oid\":\"a\",\"size\":135,"
  "\"lfs\":{\"oid\":\"b\",\"size\":4368439296,\"pointerSize\":135},"
  "\"path\":\"mistral-7b-instruct-v0.2.Q4_K_M.gguf\"},"
 "{\"type\":\"file\",\"size\":2393232896,\"path\":\"Phi-3-mini-4k-instruct-q4.gguf\"},"
 "{\"type\":\"file\",\"size\":100,\"path\":\"README.md\"},"
 "{\"type\":\"directory\",\"path\":\"subdir\"},"
 "{\"type\":\"file\",\"size\":900,\"path\":\"../escape.gguf\"},"
 "{\"type\":\"file\",\"size\":800,\"path\":\"model-00001-of-00003.gguf\"},"
 "{\"type\":\"file\",\"size\":700,\"path\":\"model.IQ3_XS.gguf\"}]";

static void test_tree(void)
{
    syn_aimodel_file_t f[8];
    int n = aimodel_parse_tree(TREE, sizeof(TREE) - 1, f, 8);

    CHECK(n == 3, "three installable GGUFs out of seven entries (got %d)", n);
    if (n < 3) return;

    CHECK(f[0].bytes == 4368439296LL,
          "LFS size wins over the pointer's 135 bytes (got %lld)", f[0].bytes);
    CHECK(strcmp(f[0].quant, "Q4_K_M") == 0,
          "quantisation read from the filename (got \"%s\")", f[0].quant);
    CHECK(f[1].bytes == 2393232896LL, "a plain size is read as-is");
    CHECK(strcmp(f[1].quant, "Q4") == 0,
          "a lowercase q4 is normalised (got \"%s\")", f[1].quant);
    CHECK(strcmp(f[2].quant, "IQ3_XS") == 0,
          "an IQ quantisation (got \"%s\")", f[2].quant);

    for (int i = 0; i < n; i++) {
        CHECK(strstr(f[i].file, "..") == NULL,
              "no listed path escapes the repo");
        CHECK(strstr(f[i].file, "-00001-of-") == NULL,
              "no split model is offered as a download");
    }
}

/* ── Reading names ───────────────────────────────────────── */

static void test_names(void)
{
    struct { const char *in, *want; } quants[] = {
        { "mistral-7b.Q4_K_M.gguf",  "Q4_K_M" },
        { "model-q8_0.gguf",         "Q8_0"   },
        { "model.f16.gguf",          "F16"    },
        { "model.BF16.gguf",         "BF16"   },
        { "seq4-model.gguf",         ""       },   /* not a token boundary */
        { "plainmodel.gguf",         ""       },
    };
    for (size_t i = 0; i < sizeof(quants) / sizeof(quants[0]); i++) {
        char out[16];
        aimodel_quant_of(quants[i].in, out, sizeof(out));
        CHECK(strcmp(out, quants[i].want) == 0,
              "quant of \"%s\" is \"%s\" (got \"%s\")",
              quants[i].in, quants[i].want, out);
    }

    struct { const char *in, *want; } params[] = {
        { "Mistral-7B-Instruct",  "7B"    },
        { "Qwen2-0.5B-Instruct",  "0.5B"  },
        { "Mixtral-8x7B",         "8x7B"  },   /* a mixture of experts is neither 7B nor 56B */
        { "Phi-3-mini-4k",        ""      },
        { "model-4096B-ctx",      ""      },   /* a context length is not a size */
        { "llama-7Bit-thing",     ""      },
    };
    for (size_t i = 0; i < sizeof(params) / sizeof(params[0]); i++) {
        char out[16];
        aimodel_params_of(params[i].in, out, sizeof(out));
        CHECK(strcmp(out, params[i].want) == 0,
              "params of \"%s\" is \"%s\" (got \"%s\")",
              params[i].in, params[i].want, out);
    }
}

/* ── What may leave the process ──────────────────────────── */

static void test_refusals(void)
{
    const char *bad_names[] = {
        "../../etc/cron.d/pwn.gguf",   /* a path, not a name */
        "/etc/ld.so.preload",
        "sub/dir/model.gguf",
        "model.so",                    /* not a model */
        ".hidden.gguf",                /* dot-leading */
        "-rf.gguf",                    /* reads as an option */
        "model; rm -rf /.gguf",
        "model\n.gguf",
        "",
    };
    for (size_t i = 0; i < sizeof(bad_names) / sizeof(bad_names[0]); i++)
        CHECK(!aimodel_name_ok(bad_names[i]),
              "refuses filename \"%s\"", bad_names[i]);

    CHECK(aimodel_name_ok("mistral-7b-instruct-v0.2.Q4_K_M.gguf"),
          "accepts an ordinary GGUF filename");
    CHECK(aimodel_name_ok("Phi-3-mini-4k-instruct-q4.gguf"),
          "accepts a mixed-case GGUF filename");

    const char *bad_urls[] = {
        "http://huggingface.co/x/y.gguf",              /* not https */
        "https://evil.example.com/x.gguf",
        "https://huggingface.co.evil.example.com/x",   /* only starts with it */
        "https://huggingface.co@evil.example.com/x",   /* userinfo */
        "file:///etc/shadow",
        "https://huggingface.co/x y.gguf",             /* a second argv element */
        "https://huggingface.co/x\"y",
        "",
    };
    for (size_t i = 0; i < sizeof(bad_urls) / sizeof(bad_urls[0]); i++)
        CHECK(!aimodel_url_ok(bad_urls[i]), "refuses URL \"%s\"", bad_urls[i]);

    CHECK(aimodel_url_ok("https://huggingface.co/TheBloke/M-GGUF/resolve/main/m.gguf"),
          "accepts a huggingface resolve URL");

    /* The token names a file and a systemd unit instance. */
    char tok[72];
    CHECK(aimodel_token_of("mistral-7b.Q4_K_M.gguf", tok, sizeof(tok)) &&
          strcmp(tok, "mistral-7b") == 0,
          "token is the stem (got \"%s\")", tok);
    CHECK(!aimodel_token_of(".gguf", tok, sizeof(tok)),
          "a name that reduces to nothing has no token");
    CHECK(!aimodel_token_of("-lead.gguf", tok, sizeof(tok)),
          "a token may not start with a dash");

    /* Length: the token is bounded so it cannot overrun a unit name. */
    char longname[300];
    memset(longname, 'a', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = '\0';
    memcpy(longname + 280, ".gguf", 6);
    CHECK(!aimodel_name_ok(longname), "refuses an over-long filename");
    if (aimodel_token_of(longname, tok, sizeof(tok)))
        CHECK(strlen(tok) <= 63, "token stays within 63 characters (got %zu)",
              strlen(tok));
}

/* ── Truncation ──────────────────────────────────────────── */

/* Every field is a fixed array, and the strings come off the network. A repo
 * whose name is longer than the field must be cut, not overflowed. */
static void test_truncation(void)
{
    char body[2048];
    char longid[600];
    memset(longid, 'x', sizeof(longid) - 1);
    longid[sizeof(longid) - 1] = '\0';
    memcpy(longid + 40, "/", 1);
    snprintf(body, sizeof(body),
             "[{\"id\":\"%s\",\"downloads\":1}]", longid);

    syn_aimodel_cat_t cat[2];
    int n = aimodel_parse_search(body, strlen(body), cat, 2);
    CHECK(n == 0, "an id longer than the field is dropped, not truncated into "
                  "a different repo (got %d)", n);
}

/*
 * Dump mode: aimodel_catalog_test <search|tree> FILE
 *
 * Feeds a saved response body through the same parser the compositor uses and
 * prints what it made of it. The fixtures above are what CI pins; this is how
 * they get checked against what the API actually returns today, which is the
 * failure a fixture cannot catch — Hugging Face changing a field name would
 * leave every test above passing and the panel empty.
 */
static int dump(const char *what, const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) { perror(path); return 1; }

    static char body[8u * 1024 * 1024];
    size_t len = fread(body, 1, sizeof(body) - 1, fp);
    fclose(fp);
    body[len] = '\0';

    if (strcmp(what, "search") == 0) {
        syn_aimodel_cat_t cat[AIMODEL_CAT_MAX];
        int n = aimodel_parse_search(body, len, cat, AIMODEL_CAT_MAX);
        printf("%d repos from %zu bytes\n", n, len);
        for (int i = 0; i < n; i++)
            printf("  %-52s %-10s %-12s %8lld pulls\n",
                   cat[i].id, cat[i].params[0] ? cat[i].params : "-",
                   cat[i].license[0] ? cat[i].license : "-", cat[i].downloads);
        return n > 0 ? 0 : 1;
    }

    syn_aimodel_file_t f[AIMODEL_FILE_MAX];
    int n = aimodel_parse_tree(body, len, f, AIMODEL_FILE_MAX);
    printf("%d installable GGUFs from %zu bytes\n", n, len);
    for (int i = 0; i < n; i++)
        printf("  %-12s %12lld  %s\n",
               f[i].quant[0] ? f[i].quant : "-", f[i].bytes, f[i].file);
    return n > 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc == 3) return dump(argv[1], argv[2]);

    printf("aimodel: the search listing\n");   test_search();
    printf("aimodel: the file tree\n");        test_tree();
    printf("aimodel: reading names\n");        test_names();
    printf("aimodel: refusals\n");             test_refusals();
    printf("aimodel: truncation\n");           test_truncation();

    printf("\n%s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
