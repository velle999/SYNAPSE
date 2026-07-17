/*
 * synui-lock-auth — verify the session owner's password for the native lock.
 *
 * The compositor must never run PAM itself: pam_unix's fail delay is ~2 s and
 * pam_faillock can block for minutes, and the wl_event loop cannot stall for
 * either. So lock.c forks this, hands the typed password down a pipe on stdin,
 * and reads one byte of verdict back on stdout — '1' authenticated, '0' not.
 *
 * Not setuid, and it does not need to be: pam_unix reaches /etc/shadow through
 * its own setuid helper (unix_chkpwd), so an ordinary process can authenticate
 * the user it already is. It authenticates getpwuid(getuid()) — the account the
 * compositor runs as — and nothing it is told can change which account that is.
 *
 * PAM service: "synui-lock" (/etc/pam.d/synui-lock), which just includes
 * system-auth, so the lock enforces exactly what login does — including
 * faillock, deliberately: a lock that is easier to brute-force than the login
 * behind it is not a lock.
 *
 * SynapseOS Project — GPLv2
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <pwd.h>
#include <security/pam_appl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Read the whole of stdin (the password) into a static buffer, minus a single
 * trailing newline. Bounded — a password longer than this is not one. */
static char g_pw[512];

static void read_password(void)
{
    size_t len = 0;
    ssize_t n;
    while (len < sizeof(g_pw) - 1 &&
           (n = read(STDIN_FILENO, g_pw + len, sizeof(g_pw) - 1 - len)) > 0)
        len += (size_t)n;
    if (len > 0 && g_pw[len - 1] == '\n') len--;
    g_pw[len] = 0;
}

static int conv_fn(int num_msg, const struct pam_message **msg,
                   struct pam_response **resp, void *data)
{
    (void)data;
    if (num_msg <= 0) return PAM_CONV_ERR;

    struct pam_response *r = calloc(num_msg, sizeof(*r));
    if (!r) return PAM_BUF_ERR;

    for (int i = 0; i < num_msg; i++) {
        /* Answer password prompts; ignore info/error text. */
        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF ||
            msg[i]->msg_style == PAM_PROMPT_ECHO_ON)
            r[i].resp = strdup(g_pw);
    }
    *resp = r;
    return PAM_SUCCESS;
}

int main(void)
{
    read_password();

    struct passwd *pw = getpwuid(getuid());
    if (!pw || !pw->pw_name) { write(STDOUT_FILENO, "0", 1); return 1; }

    struct pam_conv conv = { conv_fn, NULL };
    pam_handle_t *pamh = NULL;

    int rc = pam_start("synui-lock", pw->pw_name, &conv, &pamh);
    if (rc == PAM_SUCCESS)
        rc = pam_authenticate(pamh, 0);
    if (rc == PAM_SUCCESS)
        rc = pam_acct_mgmt(pamh, 0);        /* honour account expiry / faillock */

    if (pamh) pam_end(pamh, rc);

    explicit_bzero(g_pw, sizeof(g_pw));

    int ok = (rc == PAM_SUCCESS);
    write(STDOUT_FILENO, ok ? "1" : "0", 1);
    return ok ? 0 : 1;
}
