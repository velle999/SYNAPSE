/*
 * syn-kmod-status.c — SynapseOS kernel module status tool
 *
 * Reads /sys/kernel/synapse/ and displays a formatted report.
 * Also provides a simple test harness for:
 *   - Writing scheduling hints manually
 *   - Sending AI_CTX_SET to the current process
 *   - Reading the syscall event log
 *
 * Usage:
 *   syn-kmod-status           — show full status
 *   syn-kmod-status --stats   — show counters only
 *   syn-kmod-status --log     — dump syscall event log
 *   syn-kmod-status --hint pid=N nice=D class=interactive
 *   syn-kmod-status --ctx-set "I am compiling a large project"
 *
 * SynapseOS Project — GPLv2
 * https://synapseos.dev
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <getopt.h>

#include "synapse_kmod.h"

/* ── ANSI colors ──────────────────────────────────────────── */
#define C_BRAND  "\033[38;5;51m"
#define C_OK     "\033[38;5;82m"
#define C_WARN   "\033[38;5;214m"
#define C_ERR    "\033[38;5;196m"
#define C_DIM    "\033[2m"
#define C_BOLD   "\033[1m"
#define C_RESET  "\033[0m"

/* ── Sysfs helpers ────────────────────────────────────────── */
static int kmod_present(void)
{
    struct stat st;
    return stat(SYNAPSE_SYSFS_ROOT, &st) == 0;
}

static int read_sysfs(const char *path, char *buf, size_t len)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t r = read(fd, buf, len - 1);
    close(fd);
    if (r < 0) return -1;
    buf[r] = '\0';
    /* Strip trailing newline */
    if (r > 0 && buf[r-1] == '\n') buf[r-1] = '\0';
    return 0;
}

static int write_sysfs(const char *path, const char *data)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        return -1;
    }
    ssize_t r = write(fd, data, strlen(data));
    close(fd);
    return r < 0 ? -1 : 0;
}

/* ── Status display ───────────────────────────────────────── */
static void show_status(void)
{
    if (!kmod_present()) {
        printf(C_ERR "✗ synapse_kmod not loaded\n" C_RESET);
        printf(C_DIM "  Load with: modprobe synapse_kmod\n" C_RESET);
        return;
    }

    char buf[512];

    printf(C_BRAND "╭─────────────────────────────────────╮\n");
    printf("│  synapse_kmod  ·  SynapseOS         │\n");
    printf("╰─────────────────────────────────────╯\n" C_RESET "\n");

    /* Version */
    if (read_sysfs(SYNAPSE_SYSFS_ROOT "/version", buf, sizeof(buf)) == 0)
        printf(C_DIM "version: " C_RESET "%s\n\n", buf);

    /* Daemon status */
    if (read_sysfs(SYNAPSE_SYSFS_STATUS, buf, sizeof(buf)) == 0) {
        int alive = strncmp(buf, "ALIVE", 5) == 0 ||
                    strncmp(buf, "READY", 5) == 0;
        printf("synapd:  %s%s%s\n",
               alive ? C_OK : C_WARN,
               alive ? "● connected" : "○ not connected",
               C_RESET);
        if (!alive)
            printf(C_DIM "         last: %s\n" C_RESET, buf);
    }
    printf("\n");

    /* Config */
    if (read_sysfs(SYNAPSE_SYSFS_CONFIG, buf, sizeof(buf)) == 0) {
        printf(C_BOLD "config:\n" C_RESET);
        char *line = strtok(buf, "\n");
        while (line) {
            printf("  %s\n", line);
            line = strtok(NULL, "\n");
        }
        printf("\n");
    }

    /* Stats */
    if (read_sysfs(SYNAPSE_SYSFS_STATS, buf, sizeof(buf)) == 0) {
        printf(C_BOLD "stats:\n" C_RESET);
        char *line = strtok(buf, "\n");
        while (line) {
            printf("  %s\n", line);
            line = strtok(NULL, "\n");
        }
    }
}

/* ── Syscall log display ──────────────────────────────────── */
static void show_log(void)
{
    char buf[4096];
    if (read_sysfs(SYNAPSE_SYSFS_SYSCALL_LOG, buf, sizeof(buf)) < 0) {
        fprintf(stderr, C_ERR "Cannot read syscall log\n" C_RESET);
        return;
    }
    if (!*buf) {
        printf(C_DIM "(no events)\n" C_RESET);
        return;
    }
    printf(C_BOLD "timestamp_ns           pid  uid  nr   comm             file\n" C_RESET);
    printf(C_DIM  "─────────────────────────────────────────────────────────────\n" C_RESET);
    printf("%s\n", buf);
}

/* ── Manual hint ──────────────────────────────────────────── */
static void send_hint(const char *hint)
{
    char line[SYNAPSE_HINT_MAX_LEN];
    snprintf(line, sizeof(line), "HINT %s\n", hint);
    if (write_sysfs(SYNAPSE_SYSFS_AI_HINTS, line) < 0)
        fprintf(stderr, C_ERR "Failed to write hint\n" C_RESET);
    else
        printf(C_OK "Hint sent: %s\n" C_RESET, hint);
}

/* ── AI_CTX_SET test ──────────────────────────────────────── */
static void ctx_set_test(const char *intent)
{
    struct ai_ctx_set_args args = {0};
    args.flags = AI_CTX_FLAG_COMPUTE | AI_CTX_FLAG_INTERACTIVE;
    strncpy(args.intent, intent, sizeof(args.intent) - 1);

    long ret = syscall(NR_AI_CTX_SET, &args);
    if (ret == 0)
        printf(C_OK "AI_CTX_SET OK — intent registered with kernel\n" C_RESET);
    else if (errno == ENOSYS)
        printf(C_WARN "AI_CTX_SET: ENOSYS — kernel does not support AI_CTX\n"
               C_DIM "(requires Linux 7.0 or synapse_kmod kprobe shim)\n" C_RESET);
    else
        printf(C_ERR "AI_CTX_SET failed: %s\n" C_RESET, strerror(errno));
}

/* ── main ─────────────────────────────────────────────────── */
static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "\n"
        "SynapseOS kernel module status and test tool\n"
        "\n"
        "Options:\n"
        "  (no args)            Show full status\n"
        "  --stats              Show stats only\n"
        "  --log                Dump syscall event log\n"
        "  --hint HINT          Write scheduling hint (e.g. 'pid=1234 nice=-5 class=interactive')\n"
        "  --ctx-set INTENT     Test AI_CTX_SET syscall with given intent string\n"
        "  --ctx-get            Test AI_CTX_GET syscall (show current AI scheduling class)\n"
        "  -h, --help           This help\n",
        prog
    );
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        show_status();
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--stats") == 0) {
            char buf[512];
            if (read_sysfs(SYNAPSE_SYSFS_STATS, buf, sizeof(buf)) == 0)
                printf("%s\n", buf);
            else
                fprintf(stderr, "Cannot read stats (kmod loaded?)\n");

        } else if (strcmp(argv[i], "--log") == 0) {
            show_log();

        } else if (strcmp(argv[i], "--hint") == 0 && i+1 < argc) {
            send_hint(argv[++i]);

        } else if (strcmp(argv[i], "--ctx-set") == 0 && i+1 < argc) {
            ctx_set_test(argv[++i]);

        } else if (strcmp(argv[i], "--ctx-get") == 0) {
            struct ai_ctx_get_result res = {0};
            long ret = syscall(NR_AI_CTX_GET, &res);
            if (ret == 0) {
                printf("sched_class: %d\n", (int)res.sched_class);
                printf("nice_value:  %d\n", res.nice_value);
                printf("reason:      %s\n", res.reason);
            } else if (errno == ENOSYS) {
                printf(C_WARN "AI_CTX_GET: ENOSYS (no AI_CTX support)\n" C_RESET);
            } else {
                printf(C_ERR "AI_CTX_GET failed: %s\n" C_RESET, strerror(errno));
            }

        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;

        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    return 0;
}
