#ifndef PROFILE_H
#define PROFILE_H

/*
 * Per-model sampling profiles.
 *
 * A GGUF carries its own chat template, so synapd can work out how to FRAME a
 * prompt without being told. It carries nothing about how to SAMPLE one —
 * there is no "recommended temperature" key — and the right value genuinely
 * differs per model family (Mistral Nemo asks for 0.3, Gemma for 1.0). That
 * gap is what these files fill.
 *
 * Shipped defaults live in /usr/share/synapd/profiles.d, local overrides in
 * /etc/synapd/profiles.d. Data, not code, so a new model can be supported by
 * dropping in a file — the update target has no compiler.
 */

/* Longest section name that can match, and the buffer used to report it. */
#define PROFILE_MATCH_MAX  64

typedef struct {
    float temperature;
    float top_p;
    int   top_k;

    /* Which fields the matched profile actually specified. A profile that sets
     * only temperature must leave top_p/top_k on their existing values rather
     * than silently reset them to zero. */
    int   have_temperature;
    int   have_top_p;
    int   have_top_k;

    char  matched[PROFILE_MATCH_MAX];  /* section name that won, for the log */
    char  source[256];                 /* file it came from, for the log */
} synapd_profile_t;

/*
 * Resolve a sampling profile for one model identity.
 *
 * Matching is case-insensitive substring: a section named [mistral-nemo]
 * matches a model whose general.basename is "Mistral-Nemo". Each of basename,
 * name and arch is tried, so a file can key off whichever the GGUF actually
 * carries — many quantised models fill in only some of them.
 *
 * The LONGEST matching section wins, not the first or the last. That makes a
 * specific [mistral-nemo] beat a general [mistral] regardless of which file
 * each lives in, so adding a profile cannot reorder the ones already there.
 * Ties go to /etc, which is loaded second.
 *
 * Returns 1 if something matched, 0 if not. Never fails hard: an unreadable or
 * malformed profile directory means "no opinion", never a daemon that won't
 * start. Any of basename/name/arch may be NULL.
 */
int profile_resolve(const char *basename,
                    const char *name,
                    const char *arch,
                    synapd_profile_t *out);

/*
 * Same, over an explicit directory list (earlier entries lose equal-length
 * ties to later ones). profile_resolve() is this with the two real paths.
 *
 * Exists so the matching rules can be tested against a scratch directory
 * without writing to /usr — deliberately NOT an environment override, because
 * synapd's sampling behaviour should not be steerable by whatever happens to
 * be in its environment.
 */
int profile_resolve_dirs(const char *const *dirs, int n_dirs,
                         const char *basename,
                         const char *name,
                         const char *arch,
                         synapd_profile_t *out);

#endif
