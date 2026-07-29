/*
 * aggregate_test.c — repeat-alert aggregation
 *
 * One boot produced ~1,050 alerts that were the system proving its own
 * identity: 319 systemd-userwork and 279 unix_chkpwd opening /etc/shadow, 448
 * sudo opening /etc/sudoers, 227 sudo setuid-ing to root. All true positives,
 * so `allow` would be a lie, and narrowing the rules by comm would trade
 * detection for quiet — anything that can read /etc/shadow is already
 * privileged and can prctl(PR_SET_NAME) to borrow a name off the exempt list.
 *
 * Aggregation costs nothing in detection, and this test is what says so:
 *
 *   1. The FIRST of any (comm, reason) pair alerts immediately. A genuinely
 *      new event is never delayed or hidden behind a counter.
 *   2. Repeats within the window are counted, not emitted.
 *   3. A different comm, or a different reason, is a different alert — one
 *      noisy pair must never mask another.
 *   4. CRITICAL is never aggregated. "Monitoring may be blinded" forty times
 *      is forty facts about the system, not one fact restated.
 *   5. A full table degrades to emitting, never to silence.
 *
 * action_alert() writes to the journal and the secfeed, so the test drives
 * alert_should_emit() through it and counts what comes out via a stub logger.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "synguard.h"

static int failures;
static int emitted;      /* counted by the sg_log stub below */

static void ok(const char *name, int cond)
{
    printf("  %s - %s\n", cond ? "ok  " : "FAIL", name);
    if (!cond) failures++;
}

/*
 * sg_log() is a macro around syslog(), so the count has to be taken there.
 * A strong definition here wins over libc's for this binary.
 */
void syslog(int pri, const char *fmt, ...)
{
    (void)pri; (void)fmt;
    emitted++;
}

/* Everything else action_engine.c reaches that this test does not exercise. */
void secfeed_publish(const sg_alert_t *a) { (void)a; }
int  sg_kill_tree(synguard_state_t *s, pid_t pid, const char *comm,
                  const char *reason)
{
    (void)s; (void)pid; (void)comm; (void)reason; return -1;
}
int  sg_freeze_tree(synguard_state_t *s, pid_t pid, const char *comm)
{
    (void)s; (void)pid; (void)comm; return -1;
}

static sg_alert_t mk(const char *comm, const char *reason, sg_threat_t threat)
{
    sg_alert_t a;
    memset(&a, 0, sizeof(a));
    a.timestamp = time(NULL);
    a.verdict   = VERDICT_ALERT;
    a.threat    = threat;
    snprintf(a.event.comm, sizeof(a.event.comm), "%s", comm);
    snprintf(a.reason, sizeof(a.reason), "%s", reason);
    return a;
}

int main(void)
{
    synguard_state_t s;
    memset(&s, 0, sizeof(s));
    printf("aggregate: repeat-alert collapsing\n");

    sg_alert_t shadow = mk("unix_chkpwd", "alert-shadow-access", THREAT_MEDIUM);

    /* 1 + 2: first emits, the next 278 do not. */
    emitted = 0;
    for (int i = 0; i < 279; i++)
        action_alert(&s, &shadow);
    ok("279 identical alerts emit exactly once", emitted == 1);
    ok("the suppressed count is only booked when the window closes",
       s.stats.alerts_suppressed == 0);

    /* 3: a different comm is a different alert. */
    emitted = 0;
    sg_alert_t shadow2 = mk("systemd-userwork", "alert-shadow-access",
                            THREAT_MEDIUM);
    for (int i = 0; i < 319; i++)
        action_alert(&s, &shadow2);
    ok("a different comm alerts on its own first occurrence", emitted == 1);

    /* 3b: same comm, different reason. */
    emitted = 0;
    sg_alert_t sudoers = mk("unix_chkpwd", "alert-sudoers-access",
                            THREAT_MEDIUM);
    for (int i = 0; i < 50; i++)
        action_alert(&s, &sudoers);
    ok("a different reason alerts on its own first occurrence", emitted == 1);

    /* 4: CRITICAL is never collapsed. */
    emitted = 0;
    sg_alert_t crit = mk("synguard", "monitoring may be blinded",
                         THREAT_CRITICAL);
    for (int i = 0; i < 40; i++)
        action_alert(&s, &crit);
    ok("40 CRITICAL alerts emit 40 times, never aggregated", emitted == 40);

    /* 5: table exhaustion degrades to emitting. 64 slots, 3 already taken by
     * the pairs above (CRITICAL never takes one). */
    emitted = 0;
    for (int i = 0; i < 200; i++) {
        char comm[16];
        snprintf(comm, sizeof(comm), "proc%d", i);
        sg_alert_t uniq = mk(comm, "alert-shadow-access", THREAT_MEDIUM);
        action_alert(&s, &uniq);
    }
    ok("every distinct alert emits even past a full table", emitted == 200);

    /*
     * Flush: nothing should be reported yet, because no window has closed.
     * The window is 60s and this test does not sleep through one — asserting
     * "not yet" is the honest check, and it is the one that catches a flush
     * that fires immediately and re-emits everything it just suppressed.
     */
    emitted = 0;
    alert_flush_summaries(&s);
    ok("flush emits nothing while windows are still open", emitted == 0);
    ok("nothing counted as suppressed before a window closes",
       s.stats.alerts_suppressed == 0);

    printf(failures ? "aggregate: FAILED (%d)\n" : "aggregate: all passed\n",
           failures);
    return failures ? 1 : 0;
}
