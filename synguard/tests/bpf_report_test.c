/*
 * bpf_report_test.c — the LSM hooks report the file the kernel resolved.
 *
 * The kmod's probes read the path the caller typed, at syscall entry, so a
 * relative path, `//`, `..`, a symlink, openat2, io_uring and a path in a page
 * not yet faulted in all reach a watched file unseen (SECURITY-ROADMAP §5; the
 * kmod's own VM suite records each as a GAP). Each of those is driven here
 * against the real attached hooks, and each must arrive as a report naming
 * the resolved path. So must the negatives: an unwatched file in a home
 * directory, and an unwatched system file, must not.
 *
 * Needs root and a kernel with BPF-LSM active; reports meson SKIP (77)
 * otherwise. tests/run-vm-bpf-tests.sh runs it in a VM, as root, on a stock
 * kernel, with nothing on the host touched.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/io_uring.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include "synguard.h"
#include "sg_bpf.h"

static int failures, checks;
#define CHECK(cond, ...) do { checks++;                                    \
    if (cond) { printf("PASS "); } else { failures++; printf("FAIL "); } \
    printf(__VA_ARGS__); printf("\n"); fflush(stdout); } while (0)

#define MAXR 512
static struct sg_bpf_report got[MAXR];
static int ngot;

static void collect(void *ctx, const struct sg_bpf_report *r)
{
    (void)ctx;
    if (ngot < MAXR) got[ngot++] = *r;
}

static void drain(void)
{
    for (int i = 0; i < 20; i++) {
        sg_bpf_poll_reports();
        usleep(10000);
    }
}

/* A report for `path` with event `evt` from this process tree since `from`. */
static const struct sg_bpf_report *seen(int from, uint8_t evt, const char *path)
{
    for (int i = from; i < ngot; i++)
        if (got[i].evt == evt && strcmp(got[i].path, path) == 0)
            return &got[i];
    return NULL;
}

static void touch(const char *p)
{
    int fd = open(p, O_WRONLY | O_CREAT, 0644);
    if (fd >= 0) close(fd);
}

static void try_open(const char *p, int flags)
{
    int fd = open(p, flags);
    if (fd >= 0) close(fd);
}

/* io_uring's IORING_OP_OPENAT, by raw syscalls: no liburing on the target. */
static int uring_open(const char *path)
{
    struct io_uring_params p;
    memset(&p, 0, sizeof(p));
    int fd = (int)syscall(__NR_io_uring_setup, 4, &p);
    if (fd < 0) return -errno;

    size_t sq_sz = p.sq_off.array + p.sq_entries * sizeof(unsigned);
    size_t cq_sz = p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe);
    char *sq = mmap(NULL, sq_sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                    fd, IORING_OFF_SQ_RING);
    char *cq = mmap(NULL, cq_sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                    fd, IORING_OFF_CQ_RING);
    struct io_uring_sqe *sqes = mmap(NULL, p.sq_entries * sizeof(*sqes),
                                     PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                                     fd, IORING_OFF_SQES);
    if (sq == MAP_FAILED || cq == MAP_FAILED || sqes == MAP_FAILED) {
        close(fd);
        return -ENOMEM;
    }
    unsigned *tail  = (unsigned *)(sq + p.sq_off.tail);
    unsigned *mask  = (unsigned *)(sq + p.sq_off.ring_mask);
    unsigned *array = (unsigned *)(sq + p.sq_off.array);
    unsigned idx = *tail & *mask;

    memset(&sqes[idx], 0, sizeof(sqes[idx]));
    sqes[idx].opcode     = IORING_OP_OPENAT;
    sqes[idx].fd         = AT_FDCWD;
    sqes[idx].addr       = (uint64_t)(uintptr_t)path;
    sqes[idx].open_flags = O_RDONLY;
    array[idx] = idx;
    __atomic_store_n(tail, *tail + 1, __ATOMIC_RELEASE);

    int r = (int)syscall(__NR_io_uring_enter, fd, 1, 1, IORING_ENTER_GETEVENTS,
                         NULL, 0);
    struct io_uring_cqe *cqes = (struct io_uring_cqe *)(cq + p.cq_off.cqes);
    unsigned *chead = (unsigned *)(cq + p.cq_off.head);
    int res = r < 0 ? -errno : cqes[*chead & *(unsigned *)(cq + p.cq_off.ring_mask)].res;
    if (res >= 0) close(res);
    close(fd);
    return res;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--child"))
        return 0;                           /* the exec target */
    if (geteuid() != 0) {
        printf("SKIP: needs root\n");
        return 77;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (sg_bpf_init() != 0) {
        printf("SKIP: BPF-LSM did not load (is 'bpf' in the kernel's lsm= list?)\n");
        return 77;
    }

    /* The rules whose paths the hooks watch. */
    sg_rule_t shadow = {0}, home = {0}, execr = {0};
    snprintf(shadow.name, sizeof(shadow.name), "watch-shadow");
    shadow.evt_mask = EVT_OPEN;
    snprintf(shadow.path_pattern, sizeof(shadow.path_pattern), "/etc/kft-shadow");
    snprintf(home.name, sizeof(home.name), "watch-ssh-key");
    home.evt_mask = EVT_OPEN;
    snprintf(home.path_pattern, sizeof(home.path_pattern), "/home/*/.ssh/id_*");
    snprintf(execr.name, sizeof(execr.name), "watch-tmp-exec");
    execr.evt_mask = EVT_EXEC;
    snprintf(execr.path_pattern, sizeof(execr.path_pattern), "/tmp/kft-bin*");
    shadow.next = &home;
    home.next = &execr;

    int nw = sg_bpf_load_watch(&shadow);
    CHECK(nw == 3, "three watched prefixes are loaded (got %d)", nw);
    CHECK(sg_bpf_reports_start(collect, NULL) == 0, "reports start");

    mkdir("/etc", 0755);
    mkdir("/home", 0755);
    mkdir("/home/alice", 0755);
    mkdir("/home/alice/.ssh", 0700);
    touch("/etc/kft-shadow");
    touch("/etc/kft-unwatched");
    touch("/home/alice/.ssh/id_test");
    touch("/home/alice/notes.txt");
    drain();

    int from;

    from = ngot; try_open("/etc/kft-shadow", O_RDONLY); drain();
    CHECK(seen(from, 0x02, "/etc/kft-shadow"), "an absolute open is reported");

    from = ngot; chdir("/etc"); try_open("kft-shadow", O_RDONLY); chdir("/"); drain();
    CHECK(seen(from, 0x02, "/etc/kft-shadow"),
          "a RELATIVE open is reported, with the resolved path");

    from = ngot; try_open("/etc//kft-shadow", O_RDONLY); drain();
    CHECK(seen(from, 0x02, "/etc/kft-shadow"), "a doubled slash is reported, resolved");

    from = ngot; try_open("/tmp/../etc/kft-shadow", O_RDONLY); drain();
    CHECK(seen(from, 0x02, "/etc/kft-shadow"), "a '..' path is reported, resolved");

    symlink("/etc/kft-shadow", "/tmp/kft-link");
    from = ngot; try_open("/tmp/kft-link", O_RDONLY); drain();
    CHECK(seen(from, 0x02, "/etc/kft-shadow"),
          "a symlink to a watched file is reported as the file");

    struct { uint64_t flags, mode, resolve; } how = { O_RDONLY, 0, 0 };
    from = ngot;
    int fd = (int)syscall(SYS_openat2, AT_FDCWD, "/etc/kft-shadow", &how, sizeof(how));
    if (fd >= 0) close(fd);
    drain();
    CHECK(seen(from, 0x02, "/etc/kft-shadow"), "openat2 is reported");

    from = ngot;
    int ur = uring_open("/etc/kft-shadow");
    drain();
    if (ur == -ENOSYS || ur == -EPERM)
        printf("INFO io_uring unavailable on this kernel (%d); not checked\n", ur);
    else
        CHECK(seen(from, 0x02, "/etc/kft-shadow"),
              "an io_uring IORING_OP_OPENAT is reported (res=%d)", ur);

    /* A path in a page never faulted in: the kmod's probe cannot read it. */
    int pf = open("/tmp/kft-nonresident", O_RDWR | O_CREAT, 0600);
    const char *np = "/etc/kft-shadow";
    write(pf, np, strlen(np) + 1);
    char *m = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, pf, 0);
    from = ngot;
    fd = (int)syscall(SYS_openat, AT_FDCWD, m, O_RDONLY);
    if (fd >= 0) close(fd);
    munmap(m, 4096);
    close(pf);
    drain();
    CHECK(seen(from, 0x02, "/etc/kft-shadow"),
          "a path in a not-yet-faulted page is reported");

    from = ngot; try_open("/etc/kft-shadow", O_WRONLY); drain();
    const struct sg_bpf_report *w = seen(from, 0x02, "/etc/kft-shadow");
    CHECK(w && (w->flags & O_ACCMODE) == O_WRONLY,
          "the open's flags travel, so `access write` rules work (flags=%#x)",
          w ? w->flags : 0);
    CHECK(w && w->pid == (uint32_t)getpid() && w->uid == 0,
          "…with the opener's pid and uid");

    from = ngot; try_open("/home/alice/.ssh/id_test", O_RDONLY); drain();
    CHECK(seen(from, 0x02, "/home/alice/.ssh/id_test"),
          "a /home/<user>/ watch reports the key file");

    from = ngot;
    try_open("/home/alice/notes.txt", O_RDONLY);
    try_open("/etc/kft-unwatched", O_RDONLY);
    drain();
    CHECK(!seen(from, 0x02, "/home/alice/notes.txt"),
          "an unwatched file in a home directory is not reported");
    CHECK(!seen(from, 0x02, "/etc/kft-unwatched"), "an unwatched file is not reported");

    /* exec by a relative path */
    char self[4096];
    ssize_t sl = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (sl > 0) {
        self[sl] = '\0';
        int in = open(self, O_RDONLY), out = open("/tmp/kft-bin", O_WRONLY | O_CREAT | O_TRUNC, 0755);
        char buf[65536]; ssize_t r;
        while (in >= 0 && out >= 0 && (r = read(in, buf, sizeof(buf))) > 0) write(out, buf, (size_t)r);
        if (in >= 0) close(in);
        if (out >= 0) close(out);
    }
    from = ngot;
    pid_t c = fork();
    if (c == 0) {
        chdir("/tmp");
        char a0[] = "./kft-bin", a1[] = "--child";
        char *av[] = { a0, a1, NULL };
        execv(a0, av);
        _exit(127);
    }
    int st;
    waitpid(c, &st, 0);
    drain();
    CHECK(seen(from, 0x01, "/tmp/kft-bin"),
          "an exec by a relative path is reported, resolved");

    unsigned long long dropped = sg_bpf_reports_dropped();
    CHECK(dropped == 0, "no report was dropped (%llu)", dropped);

    sg_bpf_shutdown();
    printf("\n%d check(s), %d failure(s)\n", checks, failures);
    return failures ? 1 : 0;
}
