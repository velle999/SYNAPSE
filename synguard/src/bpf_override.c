/*
 * bpf_override.c — the admin's "leave the kernel gate unarmed" switch.
 *
 * The shipped unit passes --bpf-enforce: kernel enforcement is ON by default
 * since 0.1.0-44, after every property that makes it safe had been watched
 * working (docs/SECURITY-ROADMAP.md §1). This file is how a machine turns it
 * back off without editing the unit — which would shadow every later change to
 * it — and it is what Settings ▸ Security writes.
 *
 *   /etc/synguard/bpf-enforce     "off"  → load the gate, do not arm it
 *                                 absent, empty or anything else → the unit decides
 *
 * The kernel command line's synapse.bpf_enforce=0 is a different thing and
 * stays: it is for a machine that cannot boot far enough to reach Settings,
 * and it stops the gate LOADING at all (bpf_loader.c).
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "synguard.h"

/* 1 when `path` says off. Comment lines (#) and surrounding whitespace are
 * skipped; the first word decides. O_NOFOLLOW because this is read by root
 * from a directory an admin owns, and a symlink there is not a setting. */
int sg_bpf_override_off(const char *path)
{
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return 0;

    char buf[256];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return 0;
    buf[n] = '\0';

    for (char *line = buf; line && *line; ) {
        char *next = strchr(line, '\n');
        if (next) *next++ = '\0';
        line += strspn(line, " \t\r");
        if (*line && *line != '#') {
            size_t w = strcspn(line, " \t\r");
            return w == 3 && strncmp(line, "off", 3) == 0;
        }
        line = next;
    }
    return 0;
}
