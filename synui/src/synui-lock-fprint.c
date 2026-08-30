/*
 * synui-lock-fprint — the native lock's fingerprint reader, via pam_fprintd.
 *
 * The sibling of synui-lock-auth, and for the same reason: PAM must never run
 * inside the compositor. This one is worse than the password path, not better —
 * pam_fprintd does not return until a finger is presented or it times out, so
 * running it in the wl_event loop would freeze the whole desktop for the length
 * of a swipe. So lock.c forks this and reads its output off the event loop.
 *
 * Unlike the password helper, this one talks the whole time it runs: it is a
 * long-lived process waiting for a finger, and it has things to say while it
 * waits ("Place your finger on the reader", "Swipe was too short"). So the
 * protocol is line-based rather than one byte, child → parent only:
 *
 *     A\n          authenticated — unlock
 *     F\n          a finger was presented and did not match; retry allowed
 *     T\n          nobody presented one before pam_fprintd gave up; retry, and
 *                 do NOT spend the lock's failure budget on it
 *     R\n          the reader is not answering RIGHT NOW; retry a few times
 *     U\n          no fingerprint auth on this machine at all; do not retry
 *     M<text>\n    show <text> under the clock
 *
 * Exactly one of A/F/T/R/U is written, last, and then the process exits.
 *
 * R and U are the same PAM code read two different ways, and telling them apart
 * is the whole point of this pass.
 *
 * pam_fprintd asks fprintd for a device list and returns PAM_AUTHINFO_UNAVAIL
 * when there is none — which is the common case, because this is a distro that
 * ships on desktops with no reader at all. But it returns exactly the same code
 * when the device is there and simply cannot be used YET: fprintd still being
 * activated on the bus, the reader re-enumerating on the USB bus after a
 * resume, or the device still claimed by the process that had it before. Those
 * clear up in seconds.
 *
 * Treating that code as permanent is what made the reader "only there
 * sometimes" AFTER the timeout fix below: a lock that comes up on resume — the
 * most common way a laptop locks — asked once, was told the machine had no
 * reader, and never asked again for the rest of that lock. Locking by hand a
 * minute later worked, which is exactly the shape that makes it look random.
 *
 * So a runtime PAM_AUTHINFO_UNAVAIL is now R: the lock retries it a few times
 * over the following seconds and then gives up for good.
 *
 * ⚠ AND THE COMMON CASE IS UNCHANGED. A machine with no fprintd never gets as
 * far as PAM: the preflight below finds no pam_fprintd.so and answers U on the
 * first fork, exactly as before. Only a machine that HAS fprintd and no usable
 * device pays the extra attempts.
 *
 * U is kept for what can be PROVEN permanent without waiting: the two setup
 * failures checked below, and the PAM codes that mean the stack itself is
 * broken rather than the device absent.
 *
 * The two setup failures — no service file, no pam_fprintd.so — are checked
 * HERE rather than left to PAM, because PAM reports both as an ordinary
 * rejection and the whole retry policy hangs off telling those apart:
 *
 *   - With no /etc/pam.d/synui-lock-fprint, Linux-PAM falls back to the `other`
 *     service, which is pam_deny on every sane system (Arch's certainly), so
 *     pam_authenticate returns PAM_AUTH_ERR — the same answer as a finger that
 *     did not match. Verified, not assumed: without the preflight this helper
 *     returned F on a box that has no reader at all.
 *   - With the service file present but fprintd not installed, the result
 *     depends on how PAM's dispatcher treats a module it could not dlopen,
 *     which is not something a retry loop should be betting on.
 *
 * Both are permanent for the life of the process, so answering U is right: the
 * lock asks once, is told no, and stops.
 *
 * Not setuid, like synui-lock-auth, and it authenticates getpwuid(getuid()) —
 * the account the compositor already runs as. Nothing it is told can change
 * which account that is, because it is told nothing: this helper reads no input
 * at all.
 *
 * PAM service: "synui-lock-fprint" (/etc/pam.d/synui-lock-fprint), which is
 * pam_fprintd ALONE. It deliberately does not include system-auth: the password
 * stack is synui-lock's job, running in the other helper, and a stack with both
 * would make a failed swipe fall through to a password prompt this process has
 * no password to answer.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <pwd.h>
#include <stdbool.h>
#include <time.h>
#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FP_PAM_SERVICE "synui-lock-fprint"

/*
 * An attempt that lasted at least this long ended because pam_fprintd stopped
 * waiting, not because a finger was rejected: a real swipe is answered in well
 * under a second, and pam_fprintd's own default patience is 30. Deliberately
 * below that so a shortened timeout still reads as a timeout, and far above any
 * plausible match so a genuine rejection never does.
 */
#define FP_IDLE_SECS 10

/* Where Linux-PAM looks for a service: /etc/pam.d, and since 1.6 the vendor
 * fallback /usr/lib/pam.d. Both, because the package installs to the first and
 * a future move to the second must not silently disable the reader. */
static const char *const pam_service_dirs[] = {
    "/etc/pam.d", "/usr/lib/pam.d",
};

/* And where it looks for a module. Arch uses the first; the rest are here so
 * this is not quietly wrong if the helper is ever built anywhere else. */
static const char *const pam_module_dirs[] = {
    "/usr/lib/security", "/lib/security", "/usr/lib64/security",
};

static int found_in(const char *const *dirs, size_t ndirs, const char *name)
{
    for (size_t i = 0; i < ndirs; i++) {
        char path[512];
        if (snprintf(path, sizeof(path), "%s/%s", dirs[i], name) >= (int)sizeof(path))
            continue;
        if (access(path, F_OK) == 0) return 1;
    }
    return 0;
}

/* Write one whole line, retrying short writes. Nothing here is worth failing
 * over: if the parent has gone, the next write gets EPIPE and we exit. */
static void emit(const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(STDOUT_FILENO, buf + off, len - off);
        if (w <= 0) return;
        off += (size_t)w;
    }
}

/* Relay one PAM message as an M line. The text comes from pam_fprintd and is
 * shown to the user, so it is bounded and stripped of newlines — a message
 * carrying one would otherwise forge a second line of protocol. */
static void emit_msg(const char *text)
{
    if (!text) return;

    char line[256];
    line[0] = 'M';
    size_t n = 0;
    while (text[n] && n < sizeof(line) - 3) {
        char c = text[n];
        line[n + 1] = (c == '\n' || c == '\r') ? ' ' : c;
        n++;
    }
    if (n == 0) return;
    line[n + 1] = '\n';
    emit(line, n + 2);
}

/* pam_fprintd drives the swipe entirely through info and error text — "Place
 * your finger on the reader", "Swipe was too short, try again" — so the
 * conversation is a pure relay. It answers no prompts.
 *
 * Refusing prompts is a security property, not a shortcut. A prompt reaching
 * this process means the stack is not the pam_fprintd-only one this service is
 * supposed to be, and the one answer available here is the empty string — which
 * against a pam_unix configured `nullok` authenticates. Returning PAM_CONV_ERR
 * makes a misconfigured stack fail shut instead. */
static int conv_fn(int num_msg, const struct pam_message **msg,
                   struct pam_response **resp, void *data)
{
    (void)data;
    if (num_msg <= 0) return PAM_CONV_ERR;

    for (int i = 0; i < num_msg; i++) {
        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF ||
            msg[i]->msg_style == PAM_PROMPT_ECHO_ON)
            return PAM_CONV_ERR;
    }

    struct pam_response *r = calloc(num_msg, sizeof(*r));
    if (!r) return PAM_BUF_ERR;

    for (int i = 0; i < num_msg; i++) {
        if (msg[i]->msg_style == PAM_TEXT_INFO ||
            msg[i]->msg_style == PAM_ERROR_MSG)
            emit_msg(msg[i]->msg);
    }

    *resp = r;                       /* all NULL: info messages want no answer */
    return PAM_SUCCESS;
}

int main(void)
{
    struct passwd *pw = getpwuid(getuid());
    if (!pw || !pw->pw_name) { emit("U\n", 2); return 1; }

    /* Preflight — see the header. Neither of these can be told apart from a
     * rejected finger once PAM has run, and both mean "not on this machine". */
    if (!found_in(pam_service_dirs,
                  sizeof(pam_service_dirs) / sizeof(*pam_service_dirs),
                  FP_PAM_SERVICE)) {
        emit("U\n", 2);
        return 1;
    }
    if (!found_in(pam_module_dirs,
                  sizeof(pam_module_dirs) / sizeof(*pam_module_dirs),
                  "pam_fprintd.so")) {
        emit("U\n", 2);              /* fprintd is an optdepend; usually absent */
        return 1;
    }

    struct pam_conv conv = { conv_fn, NULL };
    pam_handle_t *pamh = NULL;

    int rc = pam_start(FP_PAM_SERVICE, pw->pw_name, &conv, &pamh);
    if (rc != PAM_SUCCESS) {
        if (pamh) pam_end(pamh, rc);
        emit("U\n", 2);
        return 1;
    }

    /*
     * ⛔ TIMED OUT IS NOT REJECTED, AND THE LOCK COUNTS THEM DIFFERENTLY.
     *
     * pam_fprintd gives up waiting after a while and returns PAM_AUTH_ERR —
     * the SAME code as a finger that did not match. lock.c allows five
     * rejections before it stops offering the reader for the rest of the lock,
     * which is right for a wet fingertip and catastrophic for a timeout: a
     * laptop left locked and untouched burns the whole budget on nobody being
     * there. Five timeouts plus the 3s backoff is about two and a half
     * minutes, after which the reader is silently gone. Walk away from a
     * locked ThinkPad, come back, and there is no fingerprint prompt — while
     * locking it and swiping straight away works every time. Reported as
     * "it's only there sometimes right now for some reason".
     *
     * The two are told apart by HOW LONG the attempt took, not by the message
     * text — pam_fprintd's wording is translated and has changed across
     * releases, and a retry policy keyed on a string is a retry policy that
     * breaks in another language.
     */
    time_t began = time(NULL);
    rc = pam_authenticate(pamh, 0);
    bool waited = (time(NULL) - began) >= FP_IDLE_SECS;

    /* Honour account expiry and faillock exactly as the password path does. A
     * finger must not open an account a password would be refused for. */
    if (rc == PAM_SUCCESS)
        rc = pam_acct_mgmt(pamh, 0);

    pam_end(pamh, rc);

    /* "Unavailable" is anything that says the machine cannot do this at all, as
     * opposed to a finger that did not match. Retrying the former is a loop
     * that burns a fork per attempt and never succeeds; retrying the latter is
     * just letting the user swipe again. */
    switch (rc) {
    case PAM_SUCCESS:
        emit("A\n", 2);
        return 0;
    case PAM_AUTHINFO_UNAVAIL:
        /* ⚠ NOT PROOF THAT THIS MACHINE HAS NO READER. The same code comes back
         * from a device that is present but not yet usable — see the header.
         * The lock decides how many times that is worth re-asking; this process
         * only reports which kind of "no" it got. */
        emit("R\n", 2);
        return 1;
    case PAM_MODULE_UNKNOWN:         /* pam_fprintd.so not installed */
    case PAM_SYSTEM_ERR:
    case PAM_ABORT:
        emit("U\n", 2);
        return 1;
    default:                         /* PAM_AUTH_ERR, PAM_MAXTRIES, … */
        /* T: nobody presented a finger. Same "try again" as F, but it does not
         * spend the lock's patience — see FP_IDLE_SECS above. */
        emit(waited ? "T\n" : "F\n", 2);
        return 1;
    }
}
