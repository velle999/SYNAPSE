/*
 * recent_apps_test.c — the desktop's recently-opened list, as recent.c writes it.
 *
 * The REAL file is linked, not a reimplementation of its format, for the reason
 * state_reload_test.c gives: a test that wrote its own idea of the file would
 * pass while the compositor read a different one.
 *
 * What is actually worth asserting here is the ORDER and the CAP, because both
 * are what the list is for. A history that appended would put the browser you
 * opened this morning in front of the thing you are using now; one that grew
 * forever would hand every reader a file to walk. Neither fails loudly.
 *
 * ⚠ XDG_CONFIG_HOME IS REDIRECTED IN main() BEFORE ANYTHING ELSE RUNS. Without
 * that, the path resolves to the developer's own ~/.config/synui and this test
 * would rewrite the history of the desktop it is running on — which is the sort
 * of thing that gets noticed a week later, on a shelf that suddenly lists
 * `fixture-3`.
 *
 * ⚠ syn_config_path IS STUBBED HERE rather than config.c being linked, and the
 * trade is worth naming. Linking it pulls in a dozen state-file readers that
 * have nothing to do with this (state_reload_test.c carries that whole wall of
 * stubs, and a second copy of it here is a second thing to keep in step). What
 * is under test is the FILE — its order, its cap, and what it refuses — and the
 * stub below resolves exactly as the real one does for a set XDG_CONFIG_HOME.
 * The REAL resolution is covered end to end elsewhere: syn-arcade's
 * bigscreen_rig.sh seeds $XDG_CONFIG_HOME/synui/recent-apps and then checks
 * that the tiles actually draw.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "synui.h"

/* ── the two lines of config.c this needs ─────────────────────────────────── */

bool syn_config_path(char *buf, size_t n, const char *name)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (!xdg || !*xdg) return false;   /* main() sets it; nothing else runs */
    snprintf(buf, n, "%s/synui/%s", xdg, name);
    return true;
}

void syn_config_ensure_dir(void)
{
    char dir[512];
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (!xdg || !*xdg) return;
    snprintf(dir, sizeof(dir), "%s/synui", xdg);
    mkdir(dir, 0700);
}

static int failures;

static void check(const char *what, bool ok)
{
    printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) failures++;
}

/* The list as it stands, joined with commas — easier to assert against, and
 * easier to read when it is wrong, than a slot-by-slot comparison. */
static void listing(char *buf, size_t n)
{
    char ids[RECENT_KEEP_MAX][RECENT_ID_MAX];
    int have = recent_apps_load(ids, RECENT_KEEP_MAX);

    buf[0] = '\0';
    for (int i = 0; i < have; i++) {
        if (i) strncat(buf, ",", n - strlen(buf) - 1);
        strncat(buf, ids[i], n - strlen(buf) - 1);
    }
}

int main(void)
{
    char tmp[] = "/tmp/synui-recent-test.XXXXXX";
    if (!mkdtemp(tmp)) { perror("mkdtemp"); return 1; }
    setenv("XDG_CONFIG_HOME", tmp, 1);

    char got[2048];

    /* ── nothing yet ─────────────────────────────────────────────────────── */
    listing(got, sizeof(got));
    check("a desktop that has opened nothing has an empty list", !got[0]);

    /* ── newest first ────────────────────────────────────────────────────── */
    recent_apps_note("firefox");
    recent_apps_note("syntty");
    listing(got, sizeof(got));
    check("the newest is at the front", strcmp(got, "syntty,firefox") == 0);

    /* ⚠ MOVED, NOT ADDED AGAIN. An application opening a second window must
     * not take a second place on the shelf. */
    recent_apps_note("firefox");
    listing(got, sizeof(got));
    check("...and opening one again moves it rather than listing it twice",
          strcmp(got, "firefox,syntty") == 0);

    /* Already at the front is the common case — a window opening beside the
     * one just opened — and it must be a no-op rather than a reshuffle. */
    recent_apps_note("firefox");
    listing(got, sizeof(got));
    check("...and again, on the one already at the front, changes nothing",
          strcmp(got, "firefox,syntty") == 0);

    /* ── the cap ─────────────────────────────────────────────────────────── */
    for (int i = 0; i < RECENT_KEEP_MAX + 6; i++) {
        char id[32];
        snprintf(id, sizeof(id), "fixture-%d", i);
        recent_apps_note(id);
    }
    char ids[RECENT_KEEP_MAX][RECENT_ID_MAX];
    int have = recent_apps_load(ids, RECENT_KEEP_MAX);
    check("the list is capped rather than growing forever",
          have == RECENT_KEEP_MAX);
    check("...with the newest still at the front",
          have > 0 && strcmp(ids[0], "fixture-21") == 0);

    listing(got, sizeof(got));
    check("...and the oldest pushed off the end",
          strstr(got, "firefox") == NULL);

    /* ── what is refused ─────────────────────────────────────────────────── */
    recent_apps_note("");
    recent_apps_note(NULL);
    listing(got, sizeof(got));
    check("an empty app_id is not an application",
          have == RECENT_KEEP_MAX && strncmp(got, "fixture-21", 10) == 0);

    /* ⚠ SKIPPED, NOT TRUNCATED. A truncated app_id is a DIFFERENT application:
     * it would be looked up, resolved to nothing, and quietly dropped by the
     * reader — which looks exactly like the entry never having been written. */
    char huge[RECENT_ID_MAX + 40];
    memset(huge, 'x', sizeof(huge) - 1);
    huge[sizeof(huge) - 1] = '\0';
    recent_apps_note(huge);
    listing(got, sizeof(got));
    check("an app_id too long to store is refused, not cut short",
          strstr(got, "xxxx") == NULL);

    char path[512];
    snprintf(path, sizeof(path), "%s/synui/recent-apps", tmp);
    unlink(path);
    snprintf(path, sizeof(path), "%s/synui", tmp);
    rmdir(path);
    rmdir(tmp);

    printf("%s\n", failures ? "FAILED" : "all good");
    return failures ? 1 : 0;
}
