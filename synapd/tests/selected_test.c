/*
 * selected_test — the model-name boundary, and the choice that outlives the
 * process.
 *
 * SYNAPD_MODEL_DIR and SYNAPD_SELECTED_FILE are redirected by meson at a
 * scratch tree this test creates. That redirection is the whole reason the test
 * can exist: the confinement's job is to refuse everything outside one
 * directory, which cannot be exercised against the real one without either
 * running as synapd or writing into the live models directory.
 *
 * The refusals are the point. synapd_model_resolve() decides which file a
 * daemon opens on behalf of a name that arrived over a socket, and every case
 * below is a way that has been got wrong in some other program.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "synapd.h"
#include "selected.h"

static int fails = 0;

static void ok(const char *what, int cond)
{
    printf("  %-52s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* Every rejection carries a reason, and the reason is shown to the user in the
 * panel — an empty one would render as a blank error. */
static void refuses(const char *name, const char *label)
{
    char out[512];
    const char *why = NULL;
    int rc = synapd_model_resolve(name, out, sizeof(out), &why);
    ok(label, rc != 0 && why && *why);
}

static void mkfile(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    assert(f);
    fputs(content, f);
    fclose(f);
}

int main(void)
{
    /* SYNAPD_MODEL_DIR is <builddir>/seltest/models; make both levels. */
    char parent[512];
    snprintf(parent, sizeof(parent), "%s", SYNAPD_MODEL_DIR);
    char *slash = strrchr(parent, '/');
    assert(slash);
    *slash = '\0';
    mkdir(parent, 0755);
    mkdir(SYNAPD_MODEL_DIR, 0755);

    char good[512], sub[512], notgguf[512];
    snprintf(good,    sizeof(good),    "%s/good.gguf",  SYNAPD_MODEL_DIR);
    snprintf(sub,     sizeof(sub),     "%s/adir.gguf",  SYNAPD_MODEL_DIR);
    snprintf(notgguf, sizeof(notgguf), "%s/notes.txt",  SYNAPD_MODEL_DIR);
    mkfile(good, "x");
    mkfile(notgguf, "x");
    mkdir(sub, 0755);      /* a DIRECTORY whose name ends in .gguf */
    unlink(SYNAPD_SELECTED_FILE);

    printf("boundary:\n");

    char out[512];
    const char *why = NULL;
    ok("a plain name in the models dir resolves",
       synapd_model_resolve("good.gguf", out, sizeof(out), &why) == 0);
    ok("  …to the path inside SYNAPD_MODEL_DIR",
       strcmp(out, good) == 0);

    refuses("../../etc/passwd.gguf",      "traversal is refused");
    refuses("/etc/passwd.gguf",           "an absolute path is refused");
    refuses("sub/model.gguf",             "any slash at all is refused");
    refuses(".hidden.gguf",               "a leading dot is refused");
    refuses("notes.txt",                  "a non-.gguf name is refused");
    refuses("gone.gguf",                  "a file that is not there is refused");
    refuses("adir.gguf",                  "a DIRECTORY named *.gguf is refused");
    refuses("",                           "an empty name is refused");
    refuses(".gguf",                      "the bare suffix is refused");

    /* 200 is the documented cap; 260 is past it and past most PATH_MAX
     * assumptions a caller might make downstream. */
    char toolong[300];
    memset(toolong, 'a', sizeof(toolong) - 1);
    toolong[sizeof(toolong) - 1] = '\0';
    memcpy(toolong + 260, ".gguf", 6);
    refuses(toolong, "an over-long name is refused");

    printf("remembered choice:\n");

    char got[256];
    ok("nothing recorded reads as no choice",
       synapd_selected_load(got, sizeof(got)) == 0);

    synapd_selected_save("good.gguf");
    ok("a saved choice reads back",
       synapd_selected_load(got, sizeof(got)) == 1 &&
       strcmp(got, "good.gguf") == 0);

    /* The point of validating on the way IN as well as out: a hand-edited or
     * tampered state file must not become a path the daemon opens. */
    mkfile(SYNAPD_SELECTED_FILE, "../../../etc/shadow\n");
    ok("a traversal in the state file is ignored",
       synapd_selected_load(got, sizeof(got)) == 0);

    mkfile(SYNAPD_SELECTED_FILE, "deleted-since.gguf\n");
    ok("a model deleted since is ignored, not fatal",
       synapd_selected_load(got, sizeof(got)) == 0);

    /* Written by a shell redirect, so no trailing newline. */
    mkfile(SYNAPD_SELECTED_FILE, "good.gguf");
    ok("a name with no trailing newline still loads",
       synapd_selected_load(got, sizeof(got)) == 1 &&
       strcmp(got, "good.gguf") == 0);

    mkfile(SYNAPD_SELECTED_FILE, "\n");
    ok("an empty state file reads as no choice",
       synapd_selected_load(got, sizeof(got)) == 0);

    /* Saving must not leave the temp file behind: it sits in the models' own
     * state directory, and a stray one there is confusing at best. */
    synapd_selected_save("good.gguf");
    char tmp[520];
    snprintf(tmp, sizeof(tmp), "%s.tmp", SYNAPD_SELECTED_FILE);
    ok("the write leaves no .tmp behind", access(tmp, F_OK) != 0);

    unlink(good); unlink(notgguf); rmdir(sub);
    unlink(SYNAPD_SELECTED_FILE);

    printf(fails ? "selected_test: %d FAILED\n" : "selected_test: OK\n", fails);
    return fails ? 1 : 0;
}
