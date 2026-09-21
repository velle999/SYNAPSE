/*
 * kmod_vm_test.c — synapse_kmod's entry points, attacked as root, in a VM.
 *
 * This is /init. run-vm-tests.sh packs it into an initramfs beside a freshly
 * built synapse_kmod.ko and boots a stock kernel under qemu, one MODE per
 * boot, so nothing here ever runs against the machine you are sitting at.
 * That matters: several of these cases are expected to crash an unfixed
 * module, and the ones that load the module with a bad parameter or unload it
 * mid-flight are exactly the ones you do not want to learn about on a desktop.
 *
 * Every line of output that matters starts with PASS, FAIL, GAP or INFO:
 *
 *   PASS  the module did what it claims
 *   FAIL  it did not — a bug
 *   GAP   a documented coverage limit, confirmed rather than assumed; not a
 *         failure, and the runner does not count it as one
 *   INFO  context for the reader
 *
 * The runner also treats any kernel "BUG:", "Oops" or "Call Trace" on the
 * serial console as a failure of the mode it appeared in.
 *
 * Modes (kft=<mode> on the kernel command line):
 *   main    the string entry points, the probes, sysfs writes, the device
 *   drop    a 16-slot ring: lapping, the in-band "!dropped" marker, lseek
 *   ring0   load with synapse_ring_size=0, then cause an event
 *   unload  flip synapse_events at runtime, rmmod, open the device again
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-only
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/ptrace.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <linux/reboot.h>

#define KO        "/synapse_kmod.ko"
#define DEV       "/dev/synapse-events"
#define SYSK      "/sys/kernel/synapse/"

static int failures;

static void out(const char *tag, const char *fmt, ...)
{
    va_list ap;
    char msg[1024];

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    printf("%s %s\n", tag, msg);
    fflush(stdout);
    if (strcmp(tag, "FAIL") == 0)
        failures++;
}

#define PASS(...) out("PASS", __VA_ARGS__)
#define FAIL(...) out("FAIL", __VA_ARGS__)
#define GAP(...)  out("GAP",  __VA_ARGS__)
#define INFO(...) out("INFO", __VA_ARGS__)
#define EXPECT(cond, ...) do { if (cond) PASS(__VA_ARGS__); else FAIL(__VA_ARGS__); } while (0)

/* ── plumbing ─────────────────────────────────────────────────────────── */

static void power_off(void)
{
    printf("KFT-END failures=%d\n", failures);
    fflush(stdout);
    sync();
    reboot(LINUX_REBOOT_CMD_POWER_OFF);
    for (;;) pause();
}

static int load_module(const char *params)
{
    int fd = open(KO, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -errno;
    int rc = (int)syscall(SYS_finit_module, fd, params, 0);
    int e = errno;
    close(fd);
    return rc == 0 ? 0 : -e;
}

static int write_file(const char *path, const void *buf, size_t len)
{
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -errno;
    ssize_t n = write(fd, buf, len);
    int e = errno;
    close(fd);
    return n < 0 ? -e : (int)n;
}

static int write_str(const char *path, const char *s)
{
    return write_file(path, s, strlen(s));
}

static int read_file(const char *path, char *buf, size_t len)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -errno;
    ssize_t n = read(fd, buf, len - 1);
    int e = errno;
    close(fd);
    if (n < 0) return -e;
    buf[n] = '\0';
    return (int)n;
}

static long stat_field(const char *name)
{
    char buf[4096], key[64];
    if (read_file(SYSK "stats", buf, sizeof(buf)) < 0) return -1;
    snprintf(key, sizeof(key), "%s=", name);
    char *p = strstr(buf, key);
    return p ? strtol(p + strlen(key), NULL, 10) : -1;
}

/* Everything the device has for us since `fd` was opened, appended to *acc. */
static char feed[1 << 20];
static size_t feed_len;

/* Each test drains once, so the buffer holds only what that test caused. */
static void drain(int fd)
{
    feed_len = 0;
    for (;;) {
        if (feed_len + 65536 + 1 > sizeof(feed)) break;
        ssize_t n = read(fd, feed + feed_len, 65536);
        if (n <= 0) break;
        feed_len += (size_t)n;
    }
    feed[feed_len] = '\0';
}

/* A line of the feed containing every one of the needles, or NULL. */
static const char *find_line(const char *a, const char *b)
{
    static char line[2048];
    const char *p = feed;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        if (n >= sizeof(line)) n = sizeof(line) - 1;
        memcpy(line, p, n);
        line[n] = '\0';
        if (strstr(line, a) && (!b || strstr(line, b)))
            return line;
        if (!nl) break;
        p = nl + 1;
    }
    return NULL;
}

static int count_fields(const char *line)
{
    int n = 0, in = 0;
    for (; *line; line++) {
        if (*line == ' ' || *line == '\n') in = 0;
        else if (!in) { in = 1; n++; }
    }
    return n;
}

/* The filename field (6th) of a wire line. */
static const char *field(const char *line, int idx, char *out, size_t len)
{
    int f = 0;
    const char *p = line;
    while (*p && f < idx) {
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        f++;
    }
    size_t n = 0;
    while (p[n] && p[n] != ' ' && p[n] != '\n' && n + 1 < len) n++;
    memcpy(out, p, n);
    out[n] = '\0';
    return out;
}

static pid_t spawn(void (*fn)(void *), void *arg)
{
    pid_t p = fork();
    if (p == 0) { fn(arg); _exit(0); }
    return p;
}

static void reap(pid_t p)
{
    int st;
    waitpid(p, &st, 0);
}

/* ── the cases ────────────────────────────────────────────────────────── */

/*
 * The path is copied onto the child's STACK before the exec. A string literal
 * lives in the binary's read-only data, and after fork() the child has no page
 * table entry for it until it touches it — which the probe, running with
 * preemption off, cannot do. do_execve_literal() below is that case on purpose.
 */
static void do_execve(void *arg)
{
    char path[64];
    snprintf(path, sizeof(path), "%s", (const char *)arg);
    char *argv[] = { path, NULL };
    execve(path, argv, NULL);
}

static void do_execve_literal(void *arg)
{
    (void)arg;
    char *argv[] = { NULL };
    execve("/tmp/kft-literal-in-fresh-child", argv, NULL);
}

static void do_execveat_path(void *arg)
{
    char path[64];
    snprintf(path, sizeof(path), "%s", (const char *)arg);
    char *argv[] = { path, NULL };
    syscall(SYS_execveat, AT_FDCWD, path, argv, NULL, 0);
}

static void do_execveat_memfd(void *arg)
{
    (void)arg;
    char empty[8] = "";
    int fd = (int)syscall(SYS_memfd_create, "sg-fileless", 0);
    /* Not a valid ELF, so the exec fails with ENOEXEC — the probe fires at
     * entry either way, which is all this needs. */
    write(fd, "#!/nonexistent\n", 15);
    char *argv[] = { empty, NULL };
    syscall(SYS_execveat, fd, empty, argv, NULL, AT_EMPTY_PATH);
}

static void test_exec(int dev)
{
    pid_t p;

    p = spawn(do_execve, "/tmp/kft-execve");            reap(p);
    p = spawn(do_execveat_path, "/tmp/kft-execveat");   reap(p);
    p = spawn(do_execveat_memfd, NULL);                 reap(p);
    p = spawn(do_execve_literal, NULL);                 reap(p);
    drain(dev);

    EXPECT(find_line(" 59 ", "/tmp/kft-execve"),
           "execve: the path reaches the feed");
    EXPECT(find_line("/tmp/kft-execveat", NULL),
           "execveat(AT_FDCWD, path): the path reaches the feed");

    const char *l = find_line("fd:", NULL);
    EXPECT(l != NULL,
           "execveat(memfd, \"\", AT_EMPTY_PATH): reported as an fd exec, not an empty filename");

    if (!find_line("kft-literal-in-fresh-child", NULL))
        GAP("exec: a path in a page the forked child has not touched yet is reported with NO filename");
    else
        INFO("exec: the literal path in a fresh child WAS reported");
}

static void test_open(int dev)
{
    char path[4200];

    mkdir("/root", 0700);

    /* Every byte the escaping exists for. */
    int fd = open("/root/a b\nc\\d\x01" "e\x7f", O_RDONLY);
    if (fd >= 0) close(fd);
    /* Longer than the 128-byte capture, and than PATH_MAX. */
    memset(path, 'A', sizeof(path) - 1);
    memcpy(path, "/root/", 6);
    path[sizeof(path) - 1] = '\0';
    fd = open(path, O_RDONLY);
    if (fd >= 0) close(fd);
    /* A pointer the kernel cannot read. */
    fd = (int)syscall(SYS_openat, AT_FDCWD, (const char *)1, O_RDONLY);
    /* A plain sensitive open, to anchor the others. */
    fd = open("/etc/shadow", O_RDONLY);
    if (fd >= 0) close(fd);
    drain(dev);

    const char *l = find_line("\\x20", "\\x0a");
    EXPECT(l && strstr(l, "/root/a\\x20b\\x0ac\\x5cd\\x01e\\x7f"),
           "open: space, newline, backslash, 0x01 and DEL are escaped");
    EXPECT(l && count_fields(l) == 9,
           "open: a hostile path is still exactly one field (9 on the line, got %d)",
           l ? count_fields(l) : -1);

    l = find_line("/root/AAAA", NULL);
    char fname[1024];
    if (l) field(l, 5, fname, sizeof(fname));
    EXPECT(l && strlen(fname) == 127,
           "open: a 4199-byte path is captured as its first 127 bytes (got %zu)",
           l ? strlen(fname) : (size_t)0);

    l = find_line("/etc/shadow", NULL);
    char ret[32];
    if (l) field(l, 8, ret, sizeof(ret));
    EXPECT(l && strcmp(ret, "-2") == 0,
           "open: a failed open carries its errno (ret=%s)", l ? ret : "none");
    PASS("open: an unreadable filename pointer did not crash the module");
}

/* The documented limits, confirmed rather than assumed. */
static void test_open_gaps(int dev)
{
    /* 1. The filter matches the string the caller typed. */
    chdir("/etc");
    int fd = open("shadow-relative", O_RDONLY);
    if (fd >= 0) close(fd);
    fd = open("/etc//shadow-slash", O_RDONLY);
    if (fd >= 0) close(fd);
    symlink("/etc/shadow-link-target", "/tmp/kft-link");
    fd = open("/tmp/kft-link", O_RDONLY);
    if (fd >= 0) close(fd);
    chdir("/");

    /* 2. A path in a page not yet mapped: the probe runs with preemption
     * off, so its copy cannot fault the page in and gives up. The syscall
     * then faults it in itself and opens the path. */
    int f = open("/tmp/kft-nonresident", O_RDWR | O_CREAT, 0600);
    const char *np = "/etc/shadow-nonresident";
    write(f, np, strlen(np) + 1);
    char *m = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, f, 0);
    fd = (int)syscall(SYS_openat, AT_FDCWD, m, O_RDONLY);
    if (fd >= 0) close(fd);
    munmap(m, 4096);
    close(f);

    /* 3. openat2 and 32-bit-free alternatives the probe set does not cover. */
    struct { unsigned long long flags, mode, resolve; } how = { O_RDONLY, 0, 0 };
    fd = (int)syscall(SYS_openat2, AT_FDCWD, "/etc/shadow-openat2", &how, sizeof(how));
    if (fd >= 0) close(fd);
    fd = (int)syscall(SYS_open, "/etc/shadow-legacy-open", O_RDONLY);
    if (fd >= 0) close(fd);
    drain(dev);

    if (!find_line("shadow-relative", NULL))
        GAP("open: openat(cwd=/etc, \"shadow-relative\") is not reported — the filter sees the caller's string, not the file");
    else
        INFO("open: a relative path WAS reported");
    if (!find_line("shadow-slash", NULL))
        GAP("open: /etc//shadow-slash is not reported (a doubled slash defeats the prefix)");
    else
        INFO("open: /etc//shadow-slash reported");
    if (!find_line("kft-link", NULL) && !find_line("shadow-link-target", NULL))
        GAP("open: a symlink to /etc/shadow-* under /tmp is not reported");
    if (!find_line("shadow-nonresident", NULL))
        GAP("open: a path in a not-yet-faulted page is not reported (the probe cannot fault it in)");
    else
        INFO("open: the non-resident path WAS reported");
    if (!find_line("shadow-openat2", NULL))
        GAP("open: openat2() is not probed");
    if (!find_line("shadow-legacy-open", NULL))
        GAP("open: legacy open() is not probed");
}

static void test_connect(int dev)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in = { .sin_family = AF_INET, .sin_port = htons(4444) };
    inet_pton(AF_INET, "10.1.2.3", &in.sin_addr);

    /* Two bytes: family only. Must not be logged as a made-up address. */
    connect(s, (struct sockaddr *)&in, 2);
    /* Huge addrlen, pointing at the end of a mapping. */
    char *pg = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    munmap(pg + 4096, 4096);
    struct sockaddr_in *edge = (struct sockaddr_in *)(pg + 4096 - sizeof(*edge));
    *edge = in;
    connect(s, (struct sockaddr *)edge, 1 << 20);
    /* The real thing. */
    connect(s, (struct sockaddr *)&in, sizeof(in));
    close(s);
    drain(dev);

    EXPECT(find_line("10.1.2.3:4444", NULL), "connect: the destination is reported");
    EXPECT(!find_line("0.0.0.0:0", NULL), "connect: a 2-byte sockaddr is not logged as 0.0.0.0:0");
    PASS("connect: addrlen 1<<20 at the edge of a mapping did not crash the module");
}

static void sleeper(void *arg) { (void)arg; for (;;) pause(); }

static void test_ptrace(int dev)
{
    pid_t a = spawn(sleeper, NULL), b = spawn(sleeper, NULL);
    char pa[32], pb[32];

    ptrace(PTRACE_ATTACH, a, 0, 0);
    waitpid(a, NULL, __WALL);
    ptrace(PTRACE_DETACH, a, 0, 0);
    ptrace(PTRACE_SEIZE, b, 0, 0);
    ptrace(PTRACE_DETACH, b, 0, 0);
    kill(a, SIGKILL); kill(b, SIGKILL);
    reap(a); reap(b);
    drain(dev);

    /* The event's args are not on the wire; the request is arg0. */
    snprintf(pa, sizeof(pa), " 08 16 ");
    snprintf(pb, sizeof(pb), " 08 %d ", PTRACE_SEIZE);
    EXPECT(find_line(" 101 ", pa), "ptrace: PTRACE_ATTACH is reported");
    EXPECT(find_line(" 101 ", pb), "ptrace: PTRACE_SEIZE is reported");
}

static void test_setuid(int dev)
{
    pid_t p = fork();
    if (p == 0) {
        setuid(0);
        syscall(SYS_setresuid, 0, 0, 0);
        _exit(0);
    }
    reap(p);
    drain(dev);
    EXPECT(find_line(" 105 ", " 40 "), "setuid(0) is reported");
    if (!find_line(" 117 ", NULL))
        GAP("setresuid(0,0,0) is not probed");
}

static void test_sysfs(void)
{
    char big[8192], buf[8192];
    int rc;

    /* status: bounded copy, and a heartbeat is recognised. */
    memset(big, 'A', sizeof(big));
    rc = write_file(SYSK "status", big, 4095);
    read_file(SYSK "status", buf, sizeof(buf));
    EXPECT(rc == 4095 && strlen(buf) == 256,
           "status: a 4095-byte write is stored as 255 bytes (read back %zu incl. newline)",
           strlen(buf));
    rc = write_file(SYSK "status", big, sizeof(big));
    INFO("status: an 8192-byte write returned %d", rc);
    long hb0 = stat_field("daemon_heartbeats");
    write_str(SYSK "status", "ALIVE requests=0 active=0 model=0\n");
    EXPECT(stat_field("daemon_heartbeats") == hb0 + 1, "status: ALIVE counts as a heartbeat");

    /* ai_hints: protected pids, junk, overflow, oversized classes. */
    long rej0 = stat_field("hints_rejected");
    write_str(SYSK "ai_hints", "HINT pid=1 nice=19 class=idle\n");
    EXPECT(stat_field("hints_rejected") == rej0 + 1, "ai_hints: pid 1 is refused");
    char hint[512];
    snprintf(hint, sizeof(hint),
             "HINT pid=99999999999999999999 nice=-99999999999 class=%0200d\n"
             "HINT pid=-5 nice=0 class=batch\n"
             "HINT\nHINT pid=\nHINT pid=%d nice=5 class=%s\n", 0, getpid(),
             "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    rc = write_str(SYSK "ai_hints", hint);
    EXPECT(rc == (int)strlen(hint), "ai_hints: malformed and overflowing lines are consumed without error");
    memset(big, '\n', sizeof(big));
    memcpy(big, "HINT pid=2 nice=1 class=batch", 29);
    rc = write_file(SYSK "ai_hints", big, sizeof(big));
    INFO("ai_hints: an 8192-byte write returned %d", rc);

    /* config: does it report what it did? */
    write_str(SYSK "config", "events_enabled=0");
    read_file(SYSK "config", buf, sizeof(buf));
    EXPECT(strstr(buf, "events_enabled=0") != NULL,
           "config: after events_enabled=0 it reads back 0 (read: %.40s)", buf);
    write_str(SYSK "config", "events_enabled=1");
    read_file(SYSK "config", buf, sizeof(buf));
    EXPECT(strstr(buf, "events_enabled=1") != NULL, "config: events_enabled=1 reads back 1");

    /* lockdown: idempotent pin, and junk refused. */
    EXPECT(write_str(SYSK "lockdown", "x") == -EINVAL, "lockdown: junk is EINVAL");
    write_str(SYSK "lockdown", "1");
    write_str(SYSK "lockdown", "1");
    write_str(SYSK "lockdown", "0");
    rc = (int)syscall(SYS_delete_module, "synapse_kmod", O_NONBLOCK);
    EXPECT(rc == 0, "lockdown: pin twice, unpin once, and the module unloads (rc=%d errno=%d)",
           rc, rc ? errno : 0);
}

static void test_device(int dev)
{
    char buf[256];

    /* Reads too small for one line make no progress but must not break. */
    ssize_t n1 = read(dev, buf, 1);
    ssize_t n5 = read(dev, buf, 5);
    INFO("device: 1-byte read=%zd, 5-byte read=%zd", n1, n5);
    /* A cursor moved far ahead of the writer. */
    lseek(dev, 0x7fffffff, SEEK_SET);
    ssize_t n = read(dev, buf, sizeof(buf));
    PASS("device: a read after lseek past the head returned %zd without crashing", n);
}

static void mode_main(void)
{
    int rc = load_module("synapse_ring_size=4096");
    EXPECT(rc == 0, "load with synapse_ring_size=4096 (rc=%d)", rc);
    if (rc) return;

    int dev = open(DEV, O_RDONLY | O_CLOEXEC);
    EXPECT(dev >= 0, "open " DEV);
    if (dev < 0) return;

    test_exec(dev);
    test_open(dev);
    test_open_gaps(dev);
    test_connect(dev);
    test_ptrace(dev);
    test_setuid(dev);
    test_device(dev);
    close(dev);
    test_sysfs();   /* last: it unloads the module */
}

static void mode_drop(void)
{
    int rc = load_module("synapse_ring_size=16");
    EXPECT(rc == 0, "load with synapse_ring_size=16 (rc=%d)", rc);
    if (rc) return;
    int dev = open(DEV, O_RDONLY | O_CLOEXEC);
    mkdir("/root", 0700);
    for (int i = 0; i < 100; i++) {
        char p[64];
        snprintf(p, sizeof(p), "/root/lap-%03d", i);
        int fd = open(p, O_RDONLY);
        if (fd >= 0) close(fd);
    }
    drain(dev);
    EXPECT(find_line("!dropped", NULL) != NULL, "a lapped reader is told in band (!dropped)");
    EXPECT(find_line("/root/lap-099", NULL) && !find_line("/root/lap-000", NULL),
           "after lapping, the newest events survive and the oldest are gone");
    char *d = strstr(feed, "!dropped ");
    if (d) INFO("marker: %.24s", d);
    close(dev);
}

static void mode_ring0(void)
{
    int rc = load_module("synapse_ring_size=0");
    if (rc) {
        PASS("synapse_ring_size=0 is refused at load (rc=%d)", rc);
        return;
    }
    FAIL("synapse_ring_size=0 LOADED — the next event divides by the ring size");
    /* The event, from a child: if it oopses, init survives to report it. */
    pid_t p = fork();
    if (p == 0) {
        /* On the stack: a literal in a fresh child is exactly the page the
         * probe cannot read, and then there is no event to divide by. */
        char path[32];
        snprintf(path, sizeof(path), "/etc/shadow");
        int fd = open(path, O_RDONLY);
        if (fd >= 0) close(fd);
        _exit(0);
    }
    reap(p);
    INFO("survived an event with a zero-size ring");
}

static void mode_unload(void)
{
    int rc = load_module("synapse_ring_size=64");
    EXPECT(rc == 0, "load (rc=%d)", rc);
    if (rc) return;
    int w = write_str("/sys/module/synapse_kmod/parameters/synapse_events", "0");
    if (w < 0) {
        PASS("the synapse_events parameter cannot be changed at runtime (%d)", w);
    } else {
        FAIL("the synapse_events parameter was changed at runtime");
    }
    rc = (int)syscall(SYS_delete_module, "synapse_kmod", O_NONBLOCK);
    EXPECT(rc == 0, "rmmod (errno=%d)", rc ? errno : 0);
    struct stat st;
    /* The open below is deliberately a second resolution: it is how the
     * test reaches a device node that outlived rmmod. */
    int gone = stat(DEV, &st) != 0;   /* toctou-ok: probing for a stale node on purpose */
    EXPECT(gone, "after rmmod " DEV " is gone");
    if (!gone) {
        INFO("opening the stale device, from a child...");
        pid_t p = fork();
        if (p == 0) {
            int fd = open(DEV, O_RDONLY);
            printf("INFO open returned %d errno=%d\n", fd, fd < 0 ? errno : 0);
            _exit(0);
        }
        reap(p);
    }
}

int main(void)
{
    char cmd[512] = {0};

    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);
    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
    mount("tmpfs", "/tmp", "tmpfs", 0, NULL);
    /* The initramfs has no /dev/console node, so init starts with no stdio;
     * attach it to the console now that devtmpfs has one. */
    int con = open("/dev/console", O_RDWR);
    if (con >= 0) { dup2(con, 0); dup2(con, 2); if (con > 2) close(con); }
    /* Results go to the SECOND serial port, and the kernel's console to the
     * first, so a printk can never land in the middle of a result line. */
    int res = open("/dev/ttyS1", O_WRONLY | O_NOCTTY);
    if (res < 0) res = open("/dev/console", O_WRONLY);
    if (res >= 0) { dup2(res, 1); if (res > 2) close(res); }
    setvbuf(stdout, NULL, _IOLBF, 0);
    mkdir("/etc", 0755);
    read_file("/proc/cmdline", cmd, sizeof(cmd));

    char *m = strstr(cmd, "kft=");
    char mode[32] = "main";
    if (m) sscanf(m + 4, "%31s", mode);
    printf("KFT-BEGIN mode=%s\n", mode);
    fflush(stdout);

    if      (!strcmp(mode, "main"))   mode_main();
    else if (!strcmp(mode, "drop"))   mode_drop();
    else if (!strcmp(mode, "ring0"))  mode_ring0();
    else if (!strcmp(mode, "unload")) mode_unload();
    else FAIL("unknown mode %s", mode);

    power_off();
    return 0;
}
