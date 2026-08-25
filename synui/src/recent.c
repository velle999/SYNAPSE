/*
 * recent.c — the applications this desktop has opened, newest first.
 *
 * ⚠ THE COMPOSITOR IS THE ONLY THING THAT CAN KEEP THIS LIST, and that is the
 * whole reason it lives here rather than in whichever program wants to draw it.
 * An application on this desktop is started from the start menu (quickshell,
 * with Quickshell.execDetached), from the dock, from the app grid, from the
 * desktop's own icons, from a file manager, from a terminal, or by another
 * application entirely. Recording a launch in any ONE of those is a list that
 * disagrees with what the person actually used within a day — and recording it
 * in all of them is six copies of the same rule, five of which will fall out of
 * step. There is exactly one thing every launch has in common: a window turns
 * up. So a window turning up is what is written down.
 *
 * ⚠ IDS, NOT ROWS. What is stored is the app_id and nothing else — no name, no
 * icon, no command. Those are resolved when the list is READ (icon_lookup(),
 * which is the same .desktop resolution the dock and the app grid use), so an
 * application that has been uninstalled since simply resolves to nothing and is
 * dropped, and one that comes back returns to its place. A copy of the .desktop
 * fields here would be a second definition of the application to go stale.
 *
 * ⚠ AND IT IS A LIST OF APPLICATIONS, NOT OF WINDOWS. A window closing does not
 * remove anything: "recently opened" is a history, and a history that emptied
 * itself when you quit something would only ever describe what is already on
 * screen — which is what `synctl clients` is for.
 *
 * Every failure in here is silent, deliberately. This runs on the map path of
 * every window on the desktop; a compositor that refused to show a window
 * because it could not append to a history file would be a far worse program
 * than one with no history at all.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>

#include "synui.h"

/*
 * ⚠ SIXTEEN, and the number is chosen against the READER rather than the file.
 * Big screen mode's Recent bar draws eight and the start menu will want about
 * as many; keeping twice that means an application that has been pushed off the
 * end of what anybody DRAWS is still remembered for a little while, so closing
 * and reopening the shelf does not shuffle the tail of it around. Sixteen lines
 * of at most 128 bytes is also a file small enough to rewrite whole on every
 * window, which is what makes the writer below as simple as it is.
 *
 * It is RECENT_KEEP_MAX in synui.h rather than a number here, because it is
 * also the size of the array every caller of recent_apps_load() has to
 * declare — a reader sized to a different number than the writer would be a
 * silent overrun the day one of them changed.
 */
#define RECENT_KEEP RECENT_KEEP_MAX

static bool recent_path(char *buf, size_t n)
{
    return syn_config_path(buf, n, "recent-apps");
}

/*
 * Read the list as it stands. Returns how many ids were written.
 *
 * The caller's array is `max` slots of RECENT_ID_MAX bytes. Anything longer
 * than that in the file is skipped rather than truncated: a truncated app_id is
 * a DIFFERENT application, and one that would then be looked up, resolved to
 * nothing, and quietly dropped — which looks exactly like the entry never
 * having been written.
 */
int recent_apps_load(char out[][RECENT_ID_MAX], int max)
{
    char path[256];
    if (!out || max <= 0 || !recent_path(path, sizeof(path))) return 0;

    FILE *f = fopen(path, "r");
    if (!f) return 0;             /* no file yet is the ordinary first run */

    int n = 0;
    char line[512];
    while (n < max && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0]) continue;
        if (strlen(line) >= RECENT_ID_MAX) continue;
        snprintf(out[n], RECENT_ID_MAX, "%s", line);
        n++;
    }
    fclose(f);
    return n;
}

/*
 * Note that an application was opened. Rewrites the list with `app_id` first.
 *
 * ⚠ MOVED TO THE FRONT, NOT ADDED AGAIN. Opening a second window of something
 * already in the list must not list it twice — a Recent row with four copies of
 * the browser in it is a row with three wasted tiles.
 */
void recent_apps_note(const char *app_id)
{
    char path[256];
    if (!app_id || !*app_id) return;
    if (strlen(app_id) >= RECENT_ID_MAX) return;
    if (!recent_path(path, sizeof(path))) return;

    char old[RECENT_KEEP][RECENT_ID_MAX];
    int have = recent_apps_load(old, RECENT_KEEP);

    /* Already at the front: the common case by a mile — a window opening
     * beside the one just opened — and worth not rewriting the file for. */
    if (have > 0 && strcmp(old[0], app_id) == 0) return;

    syn_config_ensure_dir();
    FILE *f = fopen(path, "w");
    if (!f) return;

    fprintf(f, "%s\n", app_id);
    int written = 1;
    for (int i = 0; i < have && written < RECENT_KEEP; i++) {
        if (strcmp(old[i], app_id) == 0) continue;   /* it moved to the top */
        fprintf(f, "%s\n", old[i]);
        written++;
    }
    fclose(f);
}
