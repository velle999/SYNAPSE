/*
 * netwatch_test.c — the connect() wire format and the worm/C2 detector.
 *
 * Two things are pinned here, both of which shipped broken:
 *
 *  1. A comm containing a SPACE ("Socket Thread" — Firefox's socket thread)
 *     used to shift every field after it on the kmod's whitespace-delimited
 *     wire, so synguard read the comm's second word ("Thread") as the connect()
 *     destination. Every dest then hashed to the same constant: the distinct-
 *     host count froze at 1, the fan-out worm rule could never fire, and the
 *     connect-rate rule fired on Firefox instead and called it a worm scan.
 *     A worm only had to call prctl(PR_SET_NAME, "evil worm") to go blind.
 *
 *  2. The thresholds themselves. "20 distinct hosts OR 80 connects in 10s" is
 *     an ordinary web page. The detector has to stay quiet for a browser and
 *     still catch a subnet sweep.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#include "synguard.h"

/* ── Stubs: netwatch_connect() and kmod_parse_event() are pure, but they live
 * in event_processor.c alongside the pipeline, which references the rest of the
 * daemon. None of it is reached from these tests. ───────────────────────── */
void action_alert(synguard_state_t *s, const sg_alert_t *a) { (void)s; (void)a; }
void action_deny(synguard_state_t *s, const sg_event_t *e, const char *r)
{ (void)s; (void)e; (void)r; }
void action_quarantine(synguard_state_t *s, const sg_event_t *e) { (void)s; (void)e; }
sg_verdict_t rules_evaluate(synguard_state_t *s, const sg_event_t *e,
                            const sg_rule_t **m)
{ (void)s; (void)e; (void)m; return VERDICT_ALLOW; }
int synguard_ai_classify(synguard_state_t *s, const sg_event_t *e,
                         const char *context, sg_ai_result_t *r)
{ (void)s; (void)e; (void)context; (void)r; return -1; }
int sg_ai_build_context(const sg_event_t *e, char *out, size_t n)
{ (void)e; if (n) out[0] = '\0'; return -1; }
int  rules_load(synguard_state_t *s, const char *dir) { (void)s; (void)dir; return 0; }
void rules_free(synguard_state_t *s) { (void)s; }
int  secfeed_init(void) { return 0; }
void secfeed_close(void) { }

static int failures;

#define CHECK(cond, ...) do {                                   \
    if (!(cond)) {                                              \
        failures++;                                             \
        printf("  FAIL: "); printf(__VA_ARGS__); printf("\n");  \
    }                                                           \
} while (0)

/* Feed one connect() from `pid` to `dest`, return the threat it trips.
 * netwatch only writes `reason` on the call that trips, so callers clear it
 * once up front and it then holds the reason of the alert that fired. */
static sg_threat_t connect_to(uint32_t pid, const char *comm, const char *dest,
                              char *reason, size_t rlen)
{
    sg_event_t e;
    memset(&e, 0, sizeof(e));
    e.pid        = pid;
    e.syscall_nr = 42;
    e.evt_type   = EVT_SOCKET;
    snprintf(e.comm, sizeof(e.comm), "%s", comm);
    snprintf(e.filename, sizeof(e.filename), "%s", dest);

    return netwatch_connect(&e, reason, rlen);
}

/* ── 1. The wire format ──────────────────────────────────────────────── */
static void test_parse_comm_with_space(void)
{
    printf("comm with a space does not eat the destination\n");

    sg_event_t e;
    /* Exactly what the kmod emits for Firefox's "Socket Thread" connect():
     * the space is escaped, so the field stays one token. */
    const char *line =
        "1752460000000 703988 1000 42 Socket\\x20Thread 104.18.35.227:443 04 2";

    CHECK(kmod_parse_event(line, &e) == 0, "line failed to parse");
    CHECK(strcmp(e.comm, "Socket Thread") == 0,
          "comm = \"%s\", want \"Socket Thread\"", e.comm);
    CHECK(strcmp(e.filename, "104.18.35.227:443") == 0,
          "filename = \"%s\", want the destination \"104.18.35.227:443\"",
          e.filename);
    CHECK(e.pid == 703988, "pid = %u", e.pid);
    /* The trailing flags/arg0 only parse if the earlier fields didn't shift. */
    CHECK(e.has_arg0 && e.arg0 == 2, "trailing arg0 lost (fields shifted)");

    /* An ordinary comm is unchanged by the escaping — old format, still fine. */
    const char *plain = "1752460000000 1234 1000 42 firefox 1.2.3.4:443 04 0";
    CHECK(kmod_parse_event(plain, &e) == 0, "plain line failed to parse");
    CHECK(strcmp(e.comm, "firefox") == 0, "comm = \"%s\"", e.comm);
    CHECK(strcmp(e.filename, "1.2.3.4:443") == 0, "filename = \"%s\"", e.filename);
}

static void test_escape_roundtrip(void)
{
    printf("escape/unescape round-trips\n");

    const char *cases[] = {
        "Socket Thread", "firefox", "evil worm", "Web Content",
        "a\tb", "back\\slash", "",
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        char esc[SG_ESC_MAX_COMM], back[16];
        sg_str_escape(esc, sizeof(esc), cases[i]);
        CHECK(strchr(esc, ' ') == NULL && strchr(esc, '\t') == NULL,
              "escaped \"%s\" still contains whitespace: \"%s\"", cases[i], esc);
        sg_str_unescape(back, sizeof(back), esc);
        CHECK(strcmp(back, cases[i]) == 0,
              "round-trip of \"%s\" gave \"%s\"", cases[i], back);
    }
}

/* ── 2. The detector ─────────────────────────────────────────────────── */

/* A browser loading one page: ~80 connections to ~30 CDN hosts, all HTTPS,
 * scattered across many /24s. This is the traffic the old rule called a worm. */
static void test_browser_is_quiet(void)
{
    printf("a browser loading a page does not trip anything\n");

    char reason[200] = {0};
    sg_threat_t worst = THREAT_NONE;

    for (int i = 0; i < 80; i++) {
        char dest[64];
        /* 30 distinct hosts, each in its own /24, all on 443. */
        snprintf(dest, sizeof(dest), "%d.%d.%d.%d:443",
                 151 + (i % 3), 101, (i % 30), 90 + (i % 4));
        sg_threat_t t = connect_to(9001, "Socket Thread", dest,
                                   reason, sizeof(reason));
        if (t > worst) worst = t;
    }
    CHECK(worst == THREAT_NONE,
          "browser traffic raised threat %d (%s)", worst, reason);
}

/* A worm walking the LAN: many hosts inside ONE /24, on SMB. */
static void test_subnet_sweep_fires(void)
{
    printf("a subnet sweep fires\n");

    char reason[200] = {0};
    sg_threat_t worst = THREAT_NONE;

    for (int i = 1; i <= 25; i++) {
        char dest[64];
        snprintf(dest, sizeof(dest), "192.168.1.%d:445", i);
        sg_threat_t t = connect_to(9002, "worm", dest, reason, sizeof(reason));
        if (t > worst) worst = t;
    }
    CHECK(worst == THREAT_HIGH, "sweep did not raise HIGH (got %d)", worst);
    CHECK(strstr(reason, "subnet sweep") != NULL,
          "reason was \"%s\"", reason);
}

/* The evasion the field-shift handed out for free: a worm with a space in its
 * comm was invisible to the fan-out counter. It must not be any more. */
static void test_sweep_with_space_in_comm_fires(void)
{
    printf("a sweep still fires when the comm contains a space\n");

    char reason[200] = {0};
    sg_threat_t worst = THREAT_NONE;

    for (int i = 1; i <= 25; i++) {
        char dest[64];
        snprintf(dest, sizeof(dest), "10.0.5.%d:22", i);
        sg_threat_t t = connect_to(9003, "evil worm", dest, reason, sizeof(reason));
        if (t > worst) worst = t;
    }
    CHECK(worst == THREAT_HIGH,
          "sweep by \"evil worm\" did not raise HIGH (got %d)", worst);
}

/* A scanner hunting ssh/smb/rdp across the internet: wide fan-out, non-web. */
static void test_port_scan_fires(void)
{
    printf("a wide fan-out on non-web ports fires\n");

    char reason[200] = {0};
    sg_threat_t worst = THREAT_NONE;

    for (int i = 1; i <= 25; i++) {
        char dest[64];
        /* Each host in a different /24, so only the non-web rule can catch it. */
        snprintf(dest, sizeof(dest), "10.%d.%d.7:22", i, i);
        sg_threat_t t = connect_to(9004, "scanner", dest, reason, sizeof(reason));
        if (t > worst) worst = t;
    }
    CHECK(worst == THREAT_HIGH, "port scan did not raise HIGH (got %d)", worst);
    CHECK(strstr(reason, "port scan") != NULL, "reason was \"%s\"", reason);
}

static void test_flood_is_medium(void)
{
    printf("hammering a single host is a flood, not a worm scan\n");

    char reason[200] = {0};
    sg_threat_t worst = THREAT_NONE;

    for (int i = 0; i < 320; i++) {
        sg_threat_t t = connect_to(9005, "beacon", "203.0.113.9:443",
                                   reason, sizeof(reason));
        if (t > worst) worst = t;
    }
    CHECK(worst == THREAT_MEDIUM, "flood raised %d, want MEDIUM", worst);
    CHECK(strstr(reason, "connect flood") != NULL, "reason was \"%s\"", reason);
    CHECK(strstr(reason, "worm") == NULL,
          "a single-host flood must not be called a worm scan: \"%s\"", reason);
}

/* glibc's getaddrinfo() sorts its answers (RFC 6724) by connect()ing a UDP
 * socket to each one with port 0 to learn the source address it would use. No
 * packet leaves. A name with twenty addresses is therefore twenty "hosts on a
 * non-web port" from one lookup, and Node's libuv workers, Firefox's DNS
 * Resolver and Chromium's thread pool all tripped the scan rule this way — 72
 * of 74 HIGH alerts in one week, every one to port 0. */
static void test_address_sorting_is_not_a_scan(void)
{
    printf("getaddrinfo's port-0 address probes are not a scan\n");

    char reason[200] = {0};
    sg_threat_t worst = THREAT_NONE;

    for (int i = 1; i <= 25; i++) {
        char dest[64];
        snprintf(dest, sizeof(dest), "[2606:4700::6810:%x]:0", i);
        sg_threat_t t = connect_to(9008, "libuv-worker", dest, reason, sizeof(reason));
        if (t > worst) worst = t;
    }
    for (int i = 1; i <= 25; i++) {
        char dest[64];
        snprintf(dest, sizeof(dest), "104.16.%d.9:0", i);
        sg_threat_t t = connect_to(9009, "DNS Resolver #3", dest, reason, sizeof(reason));
        if (t > worst) worst = t;
    }
    /* All in one /24, so the sweep rule would see it too if port 0 counted. */
    for (int i = 1; i <= 25; i++) {
        char dest[64];
        snprintf(dest, sizeof(dest), "104.18.32.%d:0", i);
        sg_threat_t t = connect_to(9010, "ThreadPoolForeg", dest, reason, sizeof(reason));
        if (t > worst) worst = t;
    }
    CHECK(worst == THREAT_NONE, "port-0 probes raised threat %d: \"%s\"", worst, reason);
}

static void test_loopback_ignored(void)
{
    printf("loopback chatter is not egress\n");

    char reason[200] = {0};
    sg_threat_t worst = THREAT_NONE;

    /* chibi talking to ollama, forever. */
    for (int i = 0; i < 400; i++) {
        sg_threat_t t = connect_to(9006, "chibi", "127.0.0.1:11434",
                                   reason, sizeof(reason));
        if (t > worst) worst = t;
    }
    CHECK(worst == THREAT_NONE, "loopback raised threat %d", worst);
}

/* The reason is copied into sg_secfeed_msg_t.reason[112]. The alert that
 * started all this lost its "(last=<dest>)" tail there — the single most useful
 * field — so the destination has to survive the truncation. */
static void test_reason_fits_the_secfeed(void)
{
    printf("the alert reason survives the 112-byte secfeed field\n");

    char reason[200] = {0};
    sg_threat_t t = THREAT_NONE;

    for (int i = 1; i <= 25 && t == THREAT_NONE; i++) {
        char dest[64];
        snprintf(dest, sizeof(dest), "192.168.44.%d:3389", i);
        t = connect_to(9007, "worm", dest, reason, sizeof(reason));
    }
    CHECK(t == THREAT_HIGH, "expected a sweep alert");

    sg_secfeed_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    snprintf(msg.reason, sizeof(msg.reason), "%s", reason);

    CHECK(strlen(reason) < sizeof(msg.reason),
          "reason is %zu bytes, secfeed carries %zu: \"%s\"",
          strlen(reason), sizeof(msg.reason), reason);
    CHECK(strstr(msg.reason, "last=") != NULL,
          "the destination was truncated off the feed: \"%s\"", msg.reason);
}

/* ── 3. The exemption list ─────────────────────────────────────────────
 *
 * Syncthing's relay pool is a port scan by shape (5 HIGH alerts in 5 boots,
 * all syncthing, 2026-10-15), so the program is exempted by its EXECUTABLE —
 * /proc/<pid>/exe, which a process cannot fake — never by its name. The pids
 * below are real children of this test, so that path is resolved for real: it
 * is this test binary's own. */
static pid_t child(void)
{
    pid_t p = fork();
    if (p == 0) { pause(); _exit(0); }
    return p;
}

static void reap(pid_t p) { kill(p, SIGKILL); waitpid(p, NULL, 0); }

static sg_threat_t scan_from(pid_t pid, const char *comm, int base)
{
    char reason[200] = {0};
    sg_threat_t worst = THREAT_NONE;
    for (int i = 1; i <= 25; i++) {
        char dest[64];
        snprintf(dest, sizeof(dest), "10.%d.%d.9:22067", base + i, i);
        sg_threat_t t = connect_to((uint32_t)pid, comm, dest, reason, sizeof(reason));
        if (t > worst) worst = t;
    }
    return worst;
}

static int write_list(const char *path, const char *body)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fputs(body, f);
    fclose(f);
    return 0;
}

static void test_exempt_by_executable(void)
{
    printf("an exempt executable's scan is not alerted; its name alone is not enough\n");

    char self[256];
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n <= 0) { printf("  skip: no /proc/self/exe\n"); return; }
    self[n] = '\0';

    char path[] = "/tmp/netwatch-exempt-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) { failures++; printf("  FAIL: mkstemp\n"); return; }
    close(fd);

    /* The shipped default: syncthing only. This test binary is not syncthing,
     * even when it calls itself that. */
    char longp[320];
    memset(longp, 'a', sizeof(longp) - 2);
    longp[0] = '/';
    longp[sizeof(longp) - 2] = '\n';
    longp[sizeof(longp) - 1] = '\0';
    char shipped[600];
    snprintf(shipped, sizeof(shipped), "# comment\n\n   /usr/bin/syncthing   "
             "# trailing comment\nnot/absolute\n%s", longp);
    write_list(path, shipped);
    int got = netwatch_exempt_load(path);
    CHECK(got == 1, "the shipped shape loads 1 path, ignoring comments, blanks, "
          "a relative path and a %zu-byte one (got %d)", sizeof(longp) - 2, got);

    pid_t a = child();
    sg_threat_t t = scan_from(a, "syncthing", 20);
    CHECK(t == THREAT_HIGH, "a process NAMED syncthing that is not /usr/bin/syncthing still alerts (got %d)", t);
    reap(a);

    /* Now list this binary itself: its scan is dropped. */
    char body[400];
    snprintf(body, sizeof(body), "/usr/bin/syncthing\n%s\n", self);
    write_list(path, body);
    got = netwatch_exempt_load(path);
    CHECK(got == 2, "two paths loaded (got %d)", got);

    pid_t b = child();
    t = scan_from(b, "anything", 60);
    CHECK(t == THREAT_NONE, "a listed executable's port scan is not alerted (got %d)", t);
    reap(b);

    /* A missing file exempts nothing. */
    CHECK(netwatch_exempt_load("/nonexistent/netwatch-exempt") == -1,
          "a missing list reports -1");
    pid_t c = child();
    t = scan_from(c, "anything", 100);
    CHECK(t == THREAT_HIGH, "with no list the same scan alerts again (got %d)", t);
    reap(c);

    unlink(path);
}

int main(void)
{
    printf("netwatch tests\n\n");

    test_parse_comm_with_space();
    test_escape_roundtrip();
    test_browser_is_quiet();
    test_subnet_sweep_fires();
    test_sweep_with_space_in_comm_fires();
    test_port_scan_fires();
    test_address_sorting_is_not_a_scan();
    test_flood_is_medium();
    test_loopback_ignored();
    test_reason_fits_the_secfeed();
    test_exempt_by_executable();

    printf("\n%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
