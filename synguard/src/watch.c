/*
 * watch.c — the BPF report filter, derived from the rules. See sg_watch.h.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <string.h>

#include "sg_watch.h"

/* The bytes of `pat` before its first fnmatch metacharacter. A backslash
 * escape counts as one: stopping at it can only make the prefix shorter,
 * which reports more, never less. */
static size_t literal_len(const char *pat)
{
    return strcspn(pat, "*?[\\");
}

static int add(sg_watch_t *out, int n, int max, const char *p, size_t len,
               int home, uint32_t mask)
{
    for (int i = 0; i < n; i++)
        if (out[i].home == home && strlen(out[i].prefix) == len &&
            strncmp(out[i].prefix, p, len) == 0) {
            out[i].evt_mask |= mask;
            return n;
        }
    if (n >= max) return -1;
    memset(&out[n], 0, sizeof(out[n]));
    memcpy(out[n].prefix, p, len);
    out[n].home = home;
    out[n].evt_mask = mask;
    return n + 1;
}

static void note_skip(char *skipped, size_t skipsz, const char *name)
{
    if (!skipped || !skipsz) return;
    size_t used = strlen(skipped);
    snprintf(skipped + used, skipsz - used, "%s%s", used ? ", " : "", name);
}

int sg_watch_derive(const sg_rule_t *head, sg_watch_t *out, int max,
                    char *skipped, size_t skipsz)
{
    int n = 0;
    if (skipped && skipsz) skipped[0] = '\0';

    for (const sg_rule_t *r = head; r; r = r->next) {
        uint32_t mask = r->evt_mask & (EVT_OPEN | EVT_EXEC);
        /* 0xFF is "any event": a rule that matches every event with a path
         * still names that path for opens and execs. */
        if (!mask || !r->path_pattern[0]) continue;

        const char *p = r->path_pattern;
        int home = 0;
        if (strncmp(p, "/home/*/", 8) == 0) {
            p += 8;
            home = 1;
        }
        size_t len = literal_len(p);
        if (len >= SG_WATCH_PREFIX_MAX) len = SG_WATCH_PREFIX_MAX - 1;

        if (home && len == 0) {
            /* "/home/-star-/-star-": nothing to narrow by after the user, so
             * watch /home/ itself — for execs only; for opens it is every
             * file anybody touches. */
            if (mask & EVT_OPEN) {
                note_skip(skipped, skipsz, r->name);
                mask &= ~(uint32_t)EVT_OPEN;
            }
            if (mask && (n = add(out, n, max, "/home/", 6, 0, mask)) < 0) return -1;
            continue;
        }
        if (!home && (len == 0 || (len == 1 && p[0] == '/'))) {
            if (mask & EVT_OPEN) {
                note_skip(skipped, skipsz, r->name);
                mask &= ~(uint32_t)EVT_OPEN;
            }
            if (!mask) continue;
            if (len == 0) { p = "/"; len = 1; }
        }
        if ((n = add(out, n, max, p, len, home, mask)) < 0) return -1;
    }
    return n;
}
