/*
 * bpf_override_test.c — /etc/synguard/bpf-enforce, the admin's way to decline
 * the kernel gate the unit arms by default.
 *
 * The property worth pinning is that ONLY a clear "off" turns the gate off.
 * Anything else — a missing file, an empty one, a typo, a symlink — leaves the
 * unit in charge, which since 0.1.0-44 means armed. A reader that took "of" or
 * "offline" or a symlink to somewhere else as "off" would be a way to disarm
 * the machine by accident, or by anyone who can plant a link.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "synguard.h"

static int failures;

static void check(const char *what, const char *content, int want)
{
    char path[] = "/tmp/sg-override-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) { perror("mkstemp"); exit(2); }
    if (content && write(fd, content, strlen(content)) < 0) { perror("write"); exit(2); }
    close(fd);

    int got = sg_bpf_override_off(path);
    printf("  %s  %s\n", got == want ? "ok  " : "FAIL", what);
    if (got != want) failures++;
    unlink(path);
}

int main(void)
{
    printf("what turns the kernel gate off\n");
    check("\"off\"",                         "off",                  1);
    check("\"off\\n\"",                      "off\n",                1);
    check("indented, CRLF",                  "  off\r\n",            1);
    check("a comment, then off",             "# Settings\noff\n",    1);
    check("off with a trailing comment word", "off  # why\n",        1);

    printf("what does not\n");
    check("empty",                           "",                     0);
    check("\"on\"",                          "on\n",                 0);
    check("\"of\"",                          "of\n",                 0);
    check("\"offline\"",                     "offline\n",            0);
    check("\"OFF\" (the word is lowercase)", "OFF\n",                0);
    check("only comments",                   "# off\n# off\n",       0);
    check("\"0\"",                           "0\n",                  0);
    check("on first, off second",            "on\noff\n",            0);

    printf("what cannot be read as a setting\n");
    int got = sg_bpf_override_off("/nonexistent/synguard/bpf-enforce");
    printf("  %s  a missing file\n", got == 0 ? "ok  " : "FAIL");
    if (got != 0) failures++;

    char target[] = "/tmp/sg-override-target-XXXXXX";
    int fd = mkstemp(target);
    if (fd < 0 || write(fd, "off\n", 4) != 4) { perror("target"); return 2; }
    close(fd);
    char link[] = "/tmp/sg-override-link-XXXXXX";
    fd = mkstemp(link);
    close(fd);
    unlink(link);
    if (symlink(target, link) != 0) { perror("symlink"); return 2; }
    got = sg_bpf_override_off(link);
    printf("  %s  a symlink to a file saying off\n", got == 0 ? "ok  " : "FAIL");
    if (got != 0) failures++;
    unlink(link);
    unlink(target);

    printf("\n%s\n", failures ? "FAILED" : "all ok");
    return failures ? 1 : 0;
}
