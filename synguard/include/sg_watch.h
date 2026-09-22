/*
 * sg_watch.h — which paths the BPF-LSM hooks report (sg_bpf.h, "Reports").
 *
 * Kept apart from the loader so it can be tested without BPF or root: it is
 * pure string work over the parsed rules.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SG_WATCH_H
#define SG_WATCH_H

#include <stdint.h>
#include "synguard.h"

#define SG_WATCH_PREFIX_MAX 256

typedef struct {
    char     prefix[SG_WATCH_PREFIX_MAX];  /* literal, no wildcard */
    int      home;                          /* 1: the part after /home/<user>/ */
    uint32_t evt_mask;                      /* EVT_OPEN and/or EVT_EXEC */
} sg_watch_t;

/*
 * The literal part of every open and exec rule's path, merged: one entry per
 * distinct (prefix, home), its mask the union of the rules that produced it.
 * A rule of the shape "/home/-star-/<rest>" produces <rest>'s literal part as a
 * `home` entry. A rule with no path is skipped (the kmod reports it, and it
 * names no path to resolve). A rule whose path has no literal part — or an
 * OPEN rule whose literal part is just "/" — would report every open on the
 * machine, so it is refused: named in `skipped` (up to `skipsz` bytes,
 * comma-separated) and left to the kmod.
 *
 * Returns how many entries, at most `max`; more than that is -1.
 */
int sg_watch_derive(const sg_rule_t *head, sg_watch_t *out, int max,
                    char *skipped, size_t skipsz);

#endif
