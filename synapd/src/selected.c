/*
 * selected.c — which model is allowed, and which one was chosen last.
 *
 * Two jobs that belong together because they share the one validator:
 *
 *   synapd_model_resolve()  the privilege boundary. A model name arrives over
 *                           a socket and decides which file the daemon opens.
 *   synapd_selected_*()     the choice made in the desktop, so a switch lasts
 *                           past the end of the process.
 *
 * Pulled out of socket_server.c so both can be tested without linking llama, a
 * thread pool and a listening socket — the same reason wire.c and profile.c are
 * their own files. The boundary in particular had no test at all while it lived
 * next to the epoll loop, which is a poor place for the one function in the
 * daemon whose job is to be un-fooled.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

#include "synapd.h"
#include "selected.h"
#include "log.h"

/*
 * Turn a requested model name into a path that is safe to open.
 *
 * The name arrives over a socket, so it is untrusted input that decides which
 * file the daemon opens. The confinement is deliberately crude rather than
 * clever: any '/' at all is a rejection, so the request cannot describe a path
 * — only a name inside SYNAPD_MODEL_DIR. There is nothing to escape, no
 * traversal to filter for, and no realpath() race to lose.
 *
 * Returns 0 and fills out[] on success, -1 with a reason in *why otherwise.
 */
int synapd_model_resolve(const char *name, char *out, size_t out_len,
                         const char **why)
{
    if (!name || !*name)            { *why = "no model named";              return -1; }
    if (strchr(name, '/'))          { *why = "model must be a bare filename, not a path"; return -1; }
    if (name[0] == '.')             { *why = "model name may not start with a dot";       return -1; }
    if (strlen(name) > 200)         { *why = "model name too long";         return -1; }

    size_t n = strlen(name);
    if (n < 6 || strcmp(name + n - 5, ".gguf") != 0) {
        *why = "model must be a .gguf file";
        return -1;
    }

    snprintf(out, out_len, "%s/%s", SYNAPD_MODEL_DIR, name);

    struct stat st;
    if (stat(out, &st) != 0)     { *why = "no such model in the models directory"; return -1; }
    if (!S_ISREG(st.st_mode))    { *why = "model is not a regular file";           return -1; }
    return 0;
}

/* ── Remembering the choice ───────────────────────────────── */
/*
 * Record the model that is now loaded, so the next start uses it.
 *
 * Written only AFTER the load succeeded. Recording the request instead would
 * persist a model that turned out to be corrupt — the restore path in
 * switch_thread() would put the working one back in memory, and the next boot
 * would walk straight into the broken one again with the evidence three
 * restarts behind it.
 *
 * Best-effort by design: this is a convenience, and a read-only /var or a full
 * disk must not turn a successful switch into a failure. It logs and gives up.
 */
void synapd_selected_save(const char *bare_name)
{
    if (!bare_name || !*bare_name) return;

    /* Temp file + rename, so a reader never sees a half-written name — and so
     * a crash mid-write leaves the previous choice rather than a truncated one
     * that would silently fail validation and fall back at the next start. */
    char tmp[sizeof(SYNAPD_SELECTED_FILE) + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", SYNAPD_SELECTED_FILE);

    FILE *f = fopen(tmp, "w");
    if (!f) {
        syn_log(LOG_WARNING, "synapd: cannot record the model choice in %s: %s",
                SYNAPD_SELECTED_FILE, strerror(errno));
        return;
    }
    fprintf(f, "%s\n", bare_name);
    int bad = (fflush(f) != 0) || (fsync(fileno(f)) != 0);
    if (fclose(f) != 0) bad = 1;

    if (bad || rename(tmp, SYNAPD_SELECTED_FILE) != 0) {
        syn_log(LOG_WARNING, "synapd: could not save the model choice: %s",
                strerror(errno));
        unlink(tmp);
        return;
    }
    syn_log(LOG_INFO, "synapd: %s will be loaded at the next start", bare_name);
}

int synapd_selected_load(char *out, size_t out_len)
{
    if (!out || out_len == 0) return 0;
    out[0] = '\0';

    FILE *f = fopen(SYNAPD_SELECTED_FILE, "r");
    if (!f) return 0;               /* no choice on record — the ordinary case */

    char line[256] = {0};
    char *got = fgets(line, sizeof(line), f);
    fclose(f);
    if (!got) return 0;

    line[strcspn(line, "\r\n")] = '\0';
    if (!line[0]) return 0;

    /* Validated through the same resolver as a name off the socket. The file
     * lives in a directory only synapd can write, but "only we write it" is the
     * assumption that stops being true the day something else does, and the
     * check is free. */
    char resolved[512];
    const char *why = "invalid";
    if (synapd_model_resolve(line, resolved, sizeof(resolved), &why) != 0) {
        /* Deleted models are the expected way to get here, so this is not an
         * error: say what is being ignored and carry on to the ExecStart one. */
        syn_log(LOG_NOTICE,
                "synapd: ignoring the remembered model %s (%s) — using the "
                "configured one instead", line, why);
        return 0;
    }

    snprintf(out, out_len, "%s", line);
    return 1;
}
