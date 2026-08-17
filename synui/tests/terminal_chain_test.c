/*
 * terminal_chain_test.c — the `term` keybind opens ONE terminal.
 *
 * The chain behind Super+Return was `syntty || kitty || foot || alacritty ||
 * xterm` for three years, and `||` runs the next command when the previous one
 * exits NON-ZERO. A terminal's exit status is the status of the shell inside
 * it, so that chain asked "did your shell session succeed?" and answered "no,
 * open another terminal":
 *
 *   * closing the window. syntty's teardown closes the pty master, the shell is
 *     hung up, and 128 + SIGHUP is 129. Measured, not inferred.
 *   * typing `exit` after any command that failed, which carries that command's
 *     status out of the shell with it.
 *
 * So this test runs the command string the keybind actually spawns, against
 * STUB terminals on PATH that record being run and then fail the way a real one
 * does. What it counts is how many of them ran.
 *
 * Two things are deliberately not mocked. The string comes from
 * synui_terminal_cmd() rather than being restated here — a test that spelt the
 * chain itself would pass while the compositor spawned something else entirely.
 * And it is executed by /bin/sh rather than parsed, because the bug was in what
 * a shell does with the operators, not in the words.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "synui.h"

static char g_dir[256];
static char g_bin[320];
static char g_log[320];

/* A terminal that records that it was launched and then exits `code`. 129 is
 * what a real one reports when its window is closed, which is the case that
 * used to launch the next terminal in the chain. */
static void stub(const char *name, int code)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", g_bin, name);
    FILE *f = fopen(path, "w");
    assert(f);
    fprintf(f, "#!/bin/sh\necho %s >> \"%s\"\nexit %d\n", name, g_log, code);

    /* ⚠ fchmod ON THE OPEN FILE, not chmod(path) after closing it. The name is
     * resolved once and everything else talks to the descriptor; between an
     * fclose() and a chmod() the name can come to mean a different file, and
     * the mode lands on that one instead. It is a test rig writing into a
     * directory under /tmp — which is precisely where somebody else can create
     * the name first. CodeQL cpp/toctou-race-condition #14; the same shape as
     * the one tools/check-toctou.sh was written for, in the direction it did
     * not look. */
    assert(fchmod(fileno(f), 0755) == 0);
    fclose(f);
}

static void rig_init(void)
{
    snprintf(g_dir, sizeof(g_dir), "/tmp/synui-termchain-test-%d", (int)getpid());
    snprintf(g_bin, sizeof(g_bin), "%s/bin", g_dir);
    snprintf(g_log, sizeof(g_log), "%s/ran", g_dir);
    mkdir(g_dir, 0755);
    mkdir(g_bin, 0755);

    /* ONLY the stubs are reachable. A PATH that still had /usr/bin on it would
     * launch the developer's real terminal — twenty times, on a failing run. */
    setenv("PATH", g_bin, 1);
}

static void rig_cleanup(void)
{
    char p[512];
    const char *names[] = { "syntty", "kitty", "foot", "alacritty", "xterm" };
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        snprintf(p, sizeof(p), "%s/%s", g_bin, names[i]);
        unlink(p);
    }
    unlink(g_log);
    rmdir(g_bin);
    rmdir(g_dir);
}

/* Run the keybind's command for this `terminal =` setting and return what ran,
 * in order, one name per line. */
static void run_chain(const char *terminal, char *out, size_t n)
{
    unlink(g_log);

    syn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.terminal, sizeof(cfg.terminal), "%s", terminal);

    char cmd[192];
    synui_terminal_cmd(&cfg, cmd, sizeof(cmd));

    /* Exactly how spawntoggle.c runs it: /bin/sh -c, one string. */
    char sh[320];
    snprintf(sh, sizeof(sh), "/bin/sh -c '%s'", cmd);
    int rc = system(sh);
    (void)rc;   /* the chain's own status is not the contract; what RAN is */

    out[0] = '\0';
    FILE *f = fopen(g_log, "r");
    if (!f) return;
    size_t got = fread(out, 1, n - 1, f);
    out[got] = '\0';
    fclose(f);
}

static int lines(const char *s)
{
    int n = 0;
    for (const char *c = s; *c; c++)
        if (*c == '\n') n++;
    return n;
}

/* ── 1. The bug: a terminal that fails must not open another ── */

static void test_one_terminal_only(void)
{
    stub("syntty", 129);    /* the window was closed */
    stub("kitty", 0);
    stub("foot", 0);

    char ran[256];
    run_chain("syntty", ran, sizeof(ran));

    if (strcmp(ran, "syntty\n") != 0) {
        printf("    with syntty first, exiting 129, these ran:\n%s", ran);
        printf("    — a terminal's exit status is its SHELL's. Closing the\n"
               "      window is not a reason to open a different terminal.\n");
        assert(0);
    }
    assert(lines(ran) == 1);
    printf("  a closed terminal opens no other .... ok\n");
}

/* ── 2. …and the same for the older default ────────────────── */

static void test_kitty_default(void)
{
    stub("kitty", 1);       /* `exit` after a command that failed */

    char ran[256];
    run_chain("kitty", ran, sizeof(ran));

    assert(strcmp(ran, "kitty\n") == 0);
    printf("  the pre-359 default too ............ ok\n");
}

/* ── 3. The chain still EXISTS, and picks by what is installed ─
 *
 * The fix would be trivially satisfied by a chain of one, so the fallback has
 * to be shown still working: with the head of the chain missing, the next
 * INSTALLED name runs. That is the whole reason the chain is there — a box
 * whose terminal package failed to install still gets a terminal instead of a
 * keybind that does nothing.
 */

static void test_falls_back_when_missing(void)
{
    char p[512];
    snprintf(p, sizeof(p), "%s/syntty", g_bin);
    assert(unlink(p) == 0);

    char ran[256];
    run_chain("syntty", ran, sizeof(ran));

    if (strcmp(ran, "kitty\n") != 0) {
        printf("    syntty removed from PATH, and this ran:\n%s", ran);
        assert(0);
    }
    printf("  falls back past a missing one ...... ok\n");
}

/* ── 4. Nothing installed is not a crash ───────────────────── */

static void test_nothing_installed(void)
{
    const char *names[] = { "kitty", "foot" };
    char p[512];
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        snprintf(p, sizeof(p), "%s/%s", g_bin, names[i]);
        unlink(p);
    }

    char ran[256];
    run_chain("syntty", ran, sizeof(ran));
    assert(ran[0] == '\0');
    printf("  no terminal at all ................. ok\n");
}

/* ── 5. An explicit choice is run verbatim ─────────────────── */
/*
 * `terminal = <anything else>` is a choice somebody typed, and a chain that
 * silently ran a different program when it was missing would hide the typo. It
 * must also arrive UNWRAPPED: the whole point of the setting is that it can
 * carry arguments, and `terminal = kitty --single-instance` put through a
 * `command -v` loop would look for a program with a space in its name.
 */

static void test_explicit_choice(void)
{
    syn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.terminal, sizeof(cfg.terminal), "wezterm --config x=1");

    char cmd[192];
    synui_terminal_cmd(&cfg, cmd, sizeof(cmd));
    assert(strcmp(cmd, "wezterm --config x=1") == 0);
    printf("  an explicit choice, verbatim ....... ok\n");
}

/* ── 6. An EMPTY terminal is still a terminal ──────────────── */
/*
 * The field starts empty (the struct is zeroed on every reload) and the
 * defaults fill it, so an empty one means something upstream went wrong. What
 * it must NOT mean is an empty command: spawn() returns -1 on one without
 * forking, so the key did nothing, logged nothing, and `synctl dispatch term`
 * still answered {"ok":true}. That reached a live desktop, and every part of it
 * was individually correct — which is why this is a test and not a comment.
 *
 * The contract is the same as `syntty`: whatever is installed, opens.
 */

static void test_empty_is_not_nothing(void)
{
    stub("syntty", 0);

    syn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));       /* terminal[0] == '\0' */

    char cmd[192];
    synui_terminal_cmd(&cfg, cmd, sizeof(cmd));

    if (cmd[0] == '\0') {
        printf("    an empty `terminal` produced an empty command, and\n"
               "    spawn() does not fork on one — the key would do nothing,\n"
               "    silently, and the IPC would still report success.\n");
        assert(0);
    }

    char ran[256];
    run_chain("", ran, sizeof(ran));
    if (strcmp(ran, "syntty\n") != 0) {
        printf("    with terminal empty, these ran:\n%s", ran);
        assert(0);
    }
    printf("  an empty setting still opens one ... ok\n");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    rig_init();
    printf("terminal_chain_test\n");
    test_one_terminal_only();
    test_kitty_default();
    test_falls_back_when_missing();
    test_nothing_installed();
    test_explicit_choice();
    test_empty_is_not_nothing();
    rig_cleanup();
    printf("terminal_chain_test: all ok\n");
    return 0;
}
