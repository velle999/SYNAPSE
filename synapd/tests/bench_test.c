/*
 * bench_test.c — synapd-bench reads the status line correctly, or not at all.
 *
 * ⛔ WHAT THIS EXISTS TO CATCH. SYN_MSG_STATUS is one line of `key=value`
 * pairs, and two of those values are FREE TEXT the daemon does not control:
 * model_name= comes out of the GGUF's own metadata, and switch_err= is a whole
 * llama error message. A parser written with bare strstr() reads the first
 * place a key name appears — which, for a model called "gen_tok=9999 special",
 * is inside somebody's model name. The benchmark then prints a number that
 * came from a string, and it looks exactly like a measurement.
 *
 * The same trap cost five rounds in a JSON reader here once: the first hit
 * that is not followed by '=' means KEEP LOOKING, not "absent".
 *
 * The helpers are static in src/bench.c and this file includes it, so what is
 * exercised is the shipped parser rather than a copy of it. See the note above
 * main() there.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define SYNAPD_BENCH_TEST 1
#include "../src/bench.c"

static int fails;

static void ok(const char *what, int cond, const char *detail)
{
    printf(cond ? "  ok    %s\n" : "  FAIL  %s  [%s]\n", what, detail ? detail : "");
    if (!cond) fails++;
}

static void eq_num(const char *what, double got, double want)
{
    char d[96];
    snprintf(d, sizeof d, "got %.3f want %.3f", got, want);
    ok(what, got == want, d);
}

/* A status line in the shape socket_server.c actually sends one. */
#define STATUS_OK \
    "synapd: model=loaded requests=41 active=0 ctx_used=1486 ctx_window=4096" \
    " model_name=\"Mistral Nemo Instruct 2407\" model_file=\"synapse.gguf\"" \
    " format=\"[INST]\" profile=mistral temp=0.80 top_p=0.95 top_k=40" \
    " gpu_layers=40 prefill_tok=412 prefill_ms=380.5 gen_tok=96 gen_ms=2100.0"

int main(void)
{
    /* This TU is the whole tool but exercises only its parser; the rest needs
     * a socket. Referenced here so the build stays warning-clean. */
    (void)dial; (void)one_run; (void)report; (void)report_last; (void)usage;

    printf("synapd-bench parsing\n");

    /* ── the ordinary line ──────────────────────────────────────────────── */
    eq_num("prefill_tok is read",      field_num(STATUS_OK, "prefill_tok", -1), 412);
    eq_num("gen_tok is read",          field_num(STATUS_OK, "gen_tok", -1), 96);
    eq_num("prefill_ms keeps its decimals", field_num(STATUS_OK, "prefill_ms", -1), 380.5);
    eq_num("gpu_layers is read",       field_num(STATUS_OK, "gpu_layers", -1), 40);
    eq_num("requests is read",         field_num(STATUS_OK, "requests", -1), 41);

    char buf[128];
    field_str(STATUS_OK, "model_file", buf, sizeof buf);
    ok("a quoted value is unquoted", !strcmp(buf, "synapse.gguf"), buf);
    field_str(STATUS_OK, "model_name", buf, sizeof buf);
    ok("a quoted value keeps its spaces",
       !strcmp(buf, "Mistral Nemo Instruct 2407"), buf);

    /* ── the trap ───────────────────────────────────────────────────────── */
    /*
     * A model whose NAME contains the key. Nothing stops a GGUF saying this,
     * and the parser must walk past it to the real field rather than reporting
     * 9999 tokens per prefill.
     */
    static const char hostile[] =
        "synapd: model=loaded requests=7 active=0 ctx_used=10 ctx_window=4096"
        " model_name=\"gen_tok=9999 prefill_ms=0.1 (a hostile name)\""
        " model_file=\"m.gguf\" format=\"[INST]\" profile=none"
        " temp=0.80 top_p=0.95 top_k=40"
        " gpu_layers=0 prefill_tok=11 prefill_ms=22.5 gen_tok=33 gen_ms=44.5";

    eq_num("a key inside a model NAME is not the field",
           field_num(hostile, "gen_tok", -1), 33);
    eq_num("...nor is the second one",
           field_num(hostile, "prefill_ms", -1), 22.5);

    /*
     * And the other half of the same rule: a key that is a SUFFIX of another
     * key must not match it. "tok=" lives inside "prefill_tok=" and
     * "gen_tok="; asking for the whole key name is what keeps them apart.
     */
    eq_num("a key that is a suffix of another does not match",
           field_num(STATUS_OK, "tok", -1), -1);
    eq_num("...and neither does a prefix",
           field_num(STATUS_OK, "prefill", -1), -1);

    /* ── an older daemon ────────────────────────────────────────────────── */
    /*
     * synapd before 0.1.0-55 sends the same line WITHOUT these keys. Absent has
     * to be distinguishable from zero, because the tool says "upgrade the
     * daemon" for one and prints a measurement for the other.
     */
    static const char old_daemon[] =
        "synapd: model=loaded requests=3 active=0 ctx_used=40 ctx_window=4096"
        " model_name=\"Mistral Nemo Instruct 2407\" model_file=\"synapse.gguf\""
        " format=\"[INST]\" profile=mistral temp=0.80 top_p=0.95 top_k=40";

    ok("an older daemon's line has no timing keys",
       field(old_daemon, "gen_tok") == NULL, "found one");
    eq_num("...and the caller's `missing` value comes back",
           field_num(old_daemon, "gen_tok", -1), -1);

    /* A model that has been loaded but never asked anything reports zeros, and
     * that is NOT the same as an old daemon. */
    static const char never_asked[] =
        "synapd: model=loaded requests=0 active=0 ctx_used=0 ctx_window=4096"
        " gpu_layers=40 prefill_tok=0 prefill_ms=0.0 gen_tok=0 gen_ms=0.0";
    ok("a loaded-but-unused model reports zeros, not absence",
       field(never_asked, "gen_tok") != NULL, "missing");

    /* ── rate() cannot divide by zero ───────────────────────────────────── */
    eq_num("no time means no rate, not infinity", rate(100, 0), 0.0);
    eq_num("a real rate is tokens per second", rate(50, 2000), 25.0);

    /* ── a value at the very start of the buffer ────────────────────────── */
    ok("a key at position 0 is still a key",
       field("gen_tok=5 requests=1", "gen_tok") != NULL, "missed it");

    printf(fails ? "\n%d FAILED\n" : "\nall parsing checks passed\n", fails);
    return fails ? 1 : 0;
}
