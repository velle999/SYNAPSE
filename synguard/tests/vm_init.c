/*
 * vm_init.c — /init for tests/run-vm-bpf-tests.sh.
 *
 * Mounts what the BPF suites need, runs each test named in /tests.list as
 * root with its output on the second serial port, and powers off. The suites
 * need root and a kernel with BPF-LSM active; this is how they get both
 * without touching the machine that runs them.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <linux/reboot.h>

int main(void)
{
    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);
    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
    /* No tmpfs over /tmp: the initramfs is already writable, and a build
     * directory under /tmp holds a path bpf_failsafe_test writes to. */
    mount("securityfs", "/sys/kernel/security", "securityfs", 0, NULL);
    mount("bpf", "/sys/fs/bpf", "bpf", 0, NULL);
    mkdir("/var", 0755); mkdir("/var/lib", 0755); mkdir("/var/lib/synguard", 0755);

    int out = open("/dev/ttyS1", O_WRONLY | O_NOCTTY);
    if (out >= 0) { dup2(out, 1); dup2(out, 2); }
    setvbuf(stdout, NULL, _IOLBF, 0);

    char lsm[256] = "";
    int lf = open("/sys/kernel/security/lsm", O_RDONLY);
    if (lf >= 0) { ssize_t n = read(lf, lsm, sizeof(lsm) - 1); if (n > 0) lsm[n] = 0; close(lf); }
    printf("SGVM-BEGIN lsm=%s\n", lsm);

    int failed = 0;
    FILE *list = fopen("/tests.list", "r");
    char line[256];
    while (list && fgets(line, sizeof(line), list)) {
        line[strcspn(line, "\n")] = 0;
        if (!line[0]) continue;
        printf("SGVM-RUN %s\n", line);
        pid_t p = fork();
        if (p == 0) {
            chdir("/");
            execl(line, line, (char *)NULL);
            _exit(127);
        }
        int st = 0;
        waitpid(p, &st, 0);
        int rc = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
        printf("SGVM-EXIT %s %d\n", line, rc);
        if (rc != 0 && rc != 77) failed++;
    }
    printf("SGVM-END failures=%d\n", failed);
    fflush(stdout);
    sync();
    reboot(LINUX_REBOOT_CMD_POWER_OFF);
    return 0;
}
