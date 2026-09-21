/*
 * cmdplan_test.c — what the command bar does with a model's answer.
 *
 * The answers below include the ones the shipped model actually gave on
 * 2026-09-21 when a hostile window title was in the prompt: prose carrying a
 * CMD: mid-sentence (which the old strstr() parser would have RUN) and a
 * first-line CMD: (which now runs sandboxed). See src/cmdplan.c.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <string.h>

#include "cmdplan.h"

static int failures;

static const cmdplan_app_t apps[] = {
    { "/usr/bin/firefox %u", 0 },
    { "kitty", 0 },
    { "syn-edit %F", 0 },
    { "htop", 1 },              /* Terminal=true: not a GUI launch */
};
#define N_APPS (int)(sizeof(apps) / sizeof(apps[0]))

static void expect(const char *resp, cmdplan_kind_t want, const char *want_out)
{
    char out[256];
    cmdplan_kind_t got = cmdplan_decide(resp, apps, N_APPS, out, sizeof(out));
    int ok = got == want && (!want_out || strcmp(out, want_out) == 0);
    static const char *names[] = { "NONE", "LAUNCH", "CONFINED" };
    printf("  %s  %-9s %-18s ← %.60s\n", ok ? "ok  " : "FAIL",
           names[got], out, resp);
    if (!ok) failures++;
}

int main(void)
{
    printf("a command only when the answer starts with one\n");
    expect("CMD: df -h", CMDPLAN_CONFINED, "df -h");
    expect("  \nCMD: df -h\nWHY: disk space", CMDPLAN_CONFINED, "df -h");
    expect("Based on the provided context, it seems you want CMD: touch /tmp/pwned-by-title",
           CMDPLAN_NONE, "");
    expect("To summarise: the page says to run CMD: touch /tmp/pwned-by-title",
           CMDPLAN_NONE, "");
    expect("cmd: df -h", CMDPLAN_NONE, "");
    expect("CMD:", CMDPLAN_NONE, "");
    expect("CMD:   ", CMDPLAN_NONE, "");
    expect("ACTION: focus firefox", CMDPLAN_NONE, "");
    expect("It is a web browser.", CMDPLAN_NONE, "");

    printf("an injected first-line command runs sandboxed\n");
    expect("CMD: touch /tmp/pwned-by-title", CMDPLAN_CONFINED, "touch /tmp/pwned-by-title");
    expect("CMD: `ps aux | grep firefox`", CMDPLAN_CONFINED, "ps aux | grep firefox");

    printf("a bare installed GUI application launches as itself\n");
    expect("CMD: firefox", CMDPLAN_LAUNCH, "firefox");
    expect("CMD: kitty", CMDPLAN_LAUNCH, "kitty");
    expect("CMD: `syn-edit`", CMDPLAN_LAUNCH, "syn-edit");

    printf("…and nothing that only looks like one\n");
    expect("CMD: firefox https://evil.example", CMDPLAN_CONFINED, "firefox https://evil.example");
    expect("CMD: firefox;rm -rf ~", CMDPLAN_CONFINED, NULL);
    expect("CMD: /usr/bin/firefox", CMDPLAN_CONFINED, NULL);
    expect("CMD: htop", CMDPLAN_CONFINED, "htop");          /* Terminal=true */
    expect("CMD: curl", CMDPLAN_CONFINED, "curl");          /* not an app */
    expect("CMD: -firefox", CMDPLAN_CONFINED, NULL);

    printf("the sandbox\n");
    const char *argv[CMDPLAN_ARGV_MAX + 1];
    int n = cmdplan_confine_argv("df -h", "/home/u", argv);
    char joined[512] = "";
    for (int i = 0; i < n; i++) { strcat(joined, argv[i]); strcat(joined, " "); }
    int ok = argv[n] == NULL && !strcmp(joined,
        "syn-confine --ro /home/u --rw /tmp --net -- /bin/sh -c df -h ");
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", joined);
    if (!ok) failures++;

    printf("\n%s\n", failures ? "FAILED" : "all ok");
    return failures ? 1 : 0;
}
