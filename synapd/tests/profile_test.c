/*
 * profile_test.c — sampling-profile matching rules
 *
 * profile.c is pure (no llama, no sockets, no daemon state), so this links
 * just it — same shape as wire_test. Writes its fixtures to a scratch dir and
 * resolves against that, so it never reads the installed /usr profiles and
 * cannot be perturbed by what happens to be on the box.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "profile.h"

static int failures = 0;
static char dir_a[256], dir_b[256];

static void check(const char *what, int ok) {
    printf("%-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

static void write_file(const char *dir, const char *name, const char *body) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); exit(1); }
    fputs(body, f);
    fclose(f);
}

int main(void) {
    char tmpl[] = "/tmp/synapd-proftest-XXXXXX";
    char *base = mkdtemp(tmpl);
    if (!base) { perror("mkdtemp"); return 1; }

    snprintf(dir_a, sizeof(dir_a), "%s/system", base);
    snprintf(dir_b, sizeof(dir_b), "%s/local",  base);
    mkdir(dir_a, 0755);
    mkdir(dir_b, 0755);

    const char *const dirs[] = { dir_a, dir_b };
    synapd_profile_t p;

    /* ── The core rule: longest section wins, not first or last ──────────
     * [mistral] is written in the file that loads FIRST and appears BEFORE
     * [mistral-nemo] in it, so first-wins or last-wins would both pick
     * differently than specificity does. */
    write_file(dir_a, "10-defaults.conf",
        "[mistral]\n"
        "temperature = 0.7\n"
        "top_p = 0.95\n"
        "top_k = 40\n"
        "\n"
        "[mistral-nemo]\n"
        "temperature = 0.3\n");

    memset(&p, 0, sizeof(p));
    check("Nemo picks [mistral-nemo] over [mistral]",
          profile_resolve_dirs(dirs, 2, "Mistral-Nemo",
                               "Mistral Nemo Instruct 2407", "llama", &p)
          && !strcmp(p.matched, "mistral-nemo"));

    check("  ... and takes its temperature",
          p.have_temperature && p.temperature > 0.29f && p.temperature < 0.31f);

    /* A profile that sets only temperature must not silently zero the rest —
     * that is what the have_* flags are for. */
    check("  ... and does NOT claim top_p/top_k it never set",
          !p.have_top_p && !p.have_top_k);

    memset(&p, 0, sizeof(p));
    check("plain Mistral still matches [mistral]",
          profile_resolve_dirs(dirs, 2, "Mistral-7B-Instruct-v0.2",
                               "mistralai_mistral-7b-instruct-v0.2", "llama", &p)
          && !strcmp(p.matched, "mistral")
          && p.have_top_k && p.top_k == 40);

    /* ── Identity fallback ───────────────────────────────────────────────
     * v0.2's real GGUF carries general.name but no general.basename. */
    memset(&p, 0, sizeof(p));
    check("matches on general.name when basename is absent",
          profile_resolve_dirs(dirs, 2, NULL,
                               "mistralai_mistral-7b-instruct-v0.2", "llama", &p)
          && !strcmp(p.matched, "mistral"));

    write_file(dir_a, "15-qwen.conf",
        "[qwen]\n"
        "temperature = 0.7\n"
        "top_k = 20\n");

    memset(&p, 0, sizeof(p));
    check("matches on architecture when name and basename are absent",
          profile_resolve_dirs(dirs, 2, NULL, NULL, "qwen2", &p)
          && !strcmp(p.matched, "qwen")
          && p.have_top_k && p.top_k == 20);

    /* ── /etc overrides /usr at equal specificity ────────────────────── */
    write_file(dir_b, "99-local.conf",
        "[mistral-nemo]\n"
        "temperature = 0.9\n");

    memset(&p, 0, sizeof(p));
    check("equal-length tie goes to the later directory (/etc)",
          profile_resolve_dirs(dirs, 2, "Mistral-Nemo", NULL, "llama", &p)
          && p.temperature > 0.89f && p.temperature < 0.91f);

    /* ── No match is a normal outcome, not an error ──────────────────── */
    memset(&p, 0, sizeof(p));
    check("unknown model matches nothing",
          profile_resolve_dirs(dirs, 2, "Falcon-40B", "tiiuae falcon", "falcon", &p) == 0);

    /* ── Robustness: malformed input must not match or crash ─────────── */
    write_file(dir_a, "20-broken.conf",
        "this line has no section\n"
        "[unterminated\n"
        "temperature = 0.1\n"
        "[ok]\n"
        "temperature = \n"
        "= 0.5\n"
        "nonsense\n");

    memset(&p, 0, sizeof(p));
    check("malformed file does not break a good match",
          profile_resolve_dirs(dirs, 2, "Mistral-Nemo", NULL, "llama", &p)
          && !strcmp(p.matched, "mistral-nemo"));

    /* A file that is not .conf must be ignored entirely. */
    write_file(dir_a, "50-notes.txt",
        "[mistral-nemo]\ntemperature = 0.01\n");
    memset(&p, 0, sizeof(p));
    check("non-.conf files are ignored",
          profile_resolve_dirs(dirs, 2, "Mistral-Nemo", NULL, "llama", &p)
          && p.temperature > 0.5f);

    /* Absent directories are normal on a box with no /etc overrides. */
    const char *const missing[] = { "/nonexistent/synapd/profiles.d" };
    memset(&p, 0, sizeof(p));
    check("missing directory resolves to no profile, not a crash",
          profile_resolve_dirs(missing, 1, "Mistral-Nemo", NULL, "llama", &p) == 0);

    /* Clean up. */
    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", base);
    if (system(cmd) != 0) fprintf(stderr, "warning: could not remove %s\n", base);

    printf("\n%s\n", failures ? "FAILURES" : "all profile tests passed");
    return failures ? 1 : 0;
}
