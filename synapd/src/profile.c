/*
 * profile.c — per-model sampling profiles
 *
 * See profile.h for why these exist and how matching is defined.
 *
 * Deliberately dependency-free: no llama, no sockets, no daemon state, so the
 * matching rules can be unit-tested the way wire.c is.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

/* strcasestr. Guarded because meson defines this project-wide too, and an
 * unguarded redefinition is a warning on every build. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <dirent.h>

#include "profile.h"
#include "log.h"

/* Overridable at compile time so an uninstalled build can be exercised against
 * a scratch directory. Not overridable at RUNTIME on purpose — see profile.h. */
#ifndef PROFILE_DIR_SYSTEM
#define PROFILE_DIR_SYSTEM  "/usr/share/synapd/profiles.d"
#endif
#ifndef PROFILE_DIR_LOCAL
#define PROFILE_DIR_LOCAL   "/etc/synapd/profiles.d"
#endif

/* Working state while scanning: the best match found so far. */
typedef struct {
    synapd_profile_t prof;
    size_t           best_len;   /* length of the winning section name, 0 = none */
} scan_t;

/* Strip leading and trailing whitespace in place. Returns the new start. */
static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return s;
}

/* Case-insensitive substring test, NULL-safe on the haystack. */
static int imatch(const char *haystack, const char *needle) {
    return haystack && *haystack && strcasestr(haystack, needle) != NULL;
}

/*
 * Does this section name match the model?
 *
 * Tried against each identity field because quantisers are inconsistent about
 * which ones they fill in: Mistral 7B v0.2's GGUF has only general.name, while
 * Nemo's carries basename and size_label too.
 */
static int section_matches(const char *section,
                           const char *basename,
                           const char *name,
                           const char *arch)
{
    return imatch(basename, section)
        || imatch(name,     section)
        || imatch(arch,     section);
}

/* Parse one profile file, updating the running best match. */
static void scan_file(const char *path,
                      const char *basename, const char *name, const char *arch,
                      scan_t *st)
{
    FILE *f = fopen(path, "re");
    if (!f) return;

    char line[512];
    char section[PROFILE_MATCH_MAX] = {0};
    int  active = 0;      /* current section matches AND is at least as specific */

    /* Values accumulate into a scratch profile so a section is only committed
     * once it is known to win — a matching-but-shorter section must not
     * half-overwrite the current best. */
    synapd_profile_t cur = {0};
    size_t cur_len = 0;

    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        if (!*p || *p == '#' || *p == ';') continue;

        if (*p == '[') {
            /* Commit the previous section if it won. */
            if (active && cur_len >= st->best_len) {
                st->prof = cur;
                st->best_len = cur_len;
            }

            char *close = strchr(p, ']');
            if (!close) { active = 0; continue; }
            *close = '\0';
            char *sec = trim(p + 1);

            snprintf(section, sizeof(section), "%s", sec);
            cur_len = strlen(section);

            memset(&cur, 0, sizeof(cur));
            snprintf(cur.matched, sizeof(cur.matched), "%s", section);
            snprintf(cur.source,  sizeof(cur.source),  "%s", path);

            /* >= so a later file (i.e. /etc) wins an equal-length tie. */
            active = *section
                  && section_matches(section, basename, name, arch)
                  && cur_len >= st->best_len;
            continue;
        }

        if (!active) continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim(p);
        char *val = trim(eq + 1);
        if (!*key || !*val) continue;

        if (!strcasecmp(key, "temperature")) {
            cur.temperature = strtof(val, NULL);
            cur.have_temperature = 1;
        } else if (!strcasecmp(key, "top_p")) {
            cur.top_p = strtof(val, NULL);
            cur.have_top_p = 1;
        } else if (!strcasecmp(key, "top_k")) {
            cur.top_k = atoi(val);
            cur.have_top_k = 1;
        } else {
            syn_log(LOG_WARNING, "profile: %s [%s]: unknown key \"%s\" ignored",
                    path, section, key);
        }
    }

    /* Commit the final section — there is no closing bracket to trigger it. */
    if (active && cur_len >= st->best_len) {
        st->prof = cur;
        st->best_len = cur_len;
    }

    fclose(f);
}

/* Read one directory's *.conf in lexical order. Missing directory is fine. */
static void scan_dir(const char *dir,
                     const char *basename, const char *name, const char *arch,
                     scan_t *st)
{
    struct dirent **ents = NULL;
    int n = scandir(dir, &ents, NULL, alphasort);
    if (n < 0) return;   /* absent or unreadable — no opinion, not an error */

    for (int i = 0; i < n; i++) {
        const char *dn = ents[i]->d_name;
        size_t len = strlen(dn);
        if (len > 5 && !strcmp(dn + len - 5, ".conf")) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", dir, dn);
            scan_file(path, basename, name, arch, st);
        }
        free(ents[i]);
    }
    free(ents);
}

int profile_resolve_dirs(const char *const *dirs, int n_dirs,
                         const char *basename,
                         const char *name,
                         const char *arch,
                         synapd_profile_t *out)
{
    if (!out || !dirs) return 0;

    scan_t st = {0};
    for (int i = 0; i < n_dirs; i++)
        scan_dir(dirs[i], basename, name, arch, &st);

    if (st.best_len == 0) return 0;
    *out = st.prof;
    return 1;
}

int profile_resolve(const char *basename,
                    const char *name,
                    const char *arch,
                    synapd_profile_t *out)
{
    /* System first, local second, so /etc wins equal-length ties. */
    static const char *const dirs[] = {
        PROFILE_DIR_SYSTEM,
        PROFILE_DIR_LOCAL,
    };
    return profile_resolve_dirs(dirs, 2, basename, name, arch, out);
}
