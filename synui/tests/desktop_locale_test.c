/*
 * desktop_locale_test — matching a .desktop file's own translations.
 *
 * Every application ships its labels in the file that describes it —
 * `Name[de]`, `Name[pt_BR]`, `Name[sr@latin]` — and until 595 both readers here
 * (appgrid.c, icons.c) took the plain `Name` and said so in a comment. So a
 * German desktop drew German panels around an English application list.
 *
 * synui_desktop_locale_rank() is the whole of the new decision, and it is a
 * PURE FUNCTION taking the locale as an argument, which is why this file can
 * assert every case on a machine where not one of these locales is installed.
 * The wiring — that the scan actually keeps the best-ranked value — is pinned
 * in appgrid_test.c beside the rest of the scan.
 *
 * The four traps, in the order they would bite:
 *
 *   1. THE ENCODING. A desktop's locale is `de_DE.UTF-8` and a .desktop key is
 *      `Name[de]`. Compare the strings whole and NOTHING matches, ever, in any
 *      language — which is a fix that looks right, ships, and changes nothing.
 *
 *   2. THE SPEC'S CANDIDATE LIST IS EXACT. `Name[pt_BR]` is not a candidate on
 *      a pt_PT desktop: it is Brazilian Portuguese, and handing it to Portugal
 *      because the language matched is worse than English, which at least
 *      nobody mistakes for their own dialect.
 *
 *   3. BEST WINS, NOT FIRST OR LAST. The localised keys sit in arbitrary order
 *      around the plain one, so ranks have to be comparable — `Name[de_DE]`
 *      must beat `Name[de]` however they are ordered in the file.
 *
 *   4. `Name` IS A PREFIX OF `Names`. A key that merely starts with the field's
 *      name is a different key, and scoring it 0 would let it overwrite the
 *      real one.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <string.h>

#include "i18n.h"

static int failures;

#define CHECK(cond, ...)                                        \
    do {                                                        \
        if (!(cond)) {                                          \
            failures++;                                         \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);\
            fprintf(stderr, __VA_ARGS__);                       \
            fprintf(stderr, "\n");                              \
        }                                                       \
    } while (0)

static void rank_is(const char *key, const char *locale, int want)
{
    int got = synui_desktop_locale_rank(key, "Name", locale);
    CHECK(got == want, "%s on locale %s ranked %d, expected %d",
          key, locale ? locale : "(none)", got, want);
}

int main(void)
{
    /* ── 1. the plain key is always a candidate, and the weakest ── */
    rank_is("Name", "de_DE.UTF-8", 0);
    rank_is("Name", NULL,          0);
    rank_is("Name", "C",           0);

    /* ── 2. the spec's four candidates, best first ──────────────── */
    rank_is("Name[sr_RS@latin]", "sr_RS.UTF-8@latin", 4);
    rank_is("Name[sr_RS]",       "sr_RS.UTF-8@latin", 3);
    rank_is("Name[sr@latin]",    "sr_RS.UTF-8@latin", 2);
    rank_is("Name[sr]",          "sr_RS.UTF-8@latin", 1);

    /* …and the ordering between them is what the caller compares. */
    CHECK(synui_desktop_locale_rank("Name[de_DE]", "Name", "de_DE.UTF-8") >
          synui_desktop_locale_rank("Name[de]",    "Name", "de_DE.UTF-8"),
          "Name[de_DE] must outrank Name[de] on a de_DE desktop");
    CHECK(synui_desktop_locale_rank("Name[de]", "Name", "de_DE.UTF-8") >
          synui_desktop_locale_rank("Name",     "Name", "de_DE.UTF-8"),
          "any locale match must outrank the plain key");

    /* ── 3. THE ENCODING IS DROPPED ON BOTH SIDES ───────────────── */
    rank_is("Name[de]",          "de_DE.UTF-8", 1);
    rank_is("Name[de_DE]",       "de_DE.UTF-8", 3);
    rank_is("Name[de_DE.UTF-8]", "de_DE.UTF-8", 3);   /* legacy, still matches */
    rank_is("Name[de]",          "de",          1);

    /* ── 4. a key may not carry what the locale does not ask for ── */
    rank_is("Name[pt_BR]",    "pt_PT.UTF-8",      -1);
    rank_is("Name[pt]",       "pt_PT.UTF-8",       1);
    rank_is("Name[de_AT]",    "de_DE.UTF-8",      -1);
    rank_is("Name[sr@latin]", "sr_RS.UTF-8",      -1);
    rank_is("Name[de]",       "fr_FR.UTF-8",      -1);
    /* A country in the locale is not a country in the key: `Name[de]` is the
     * general answer and stays a candidate — the case above proves that; this
     * is the reverse, a key narrower than the desktop. */
    rank_is("Name[de_DE]",    "de",               -1);

    /* ── 5. the C locale takes the plain key and nothing else ───── */
    rank_is("Name[de]", NULL, -1);
    rank_is("Name[de]", "",   -1);

    /* ── 6. a different key is not this field ───────────────────── */
    rank_is("Names",      "de_DE.UTF-8", -1);
    rank_is("Names[de]",  "de_DE.UTF-8", -1);
    rank_is("GenericName","de_DE.UTF-8", -1);
    rank_is("Exec",       "de_DE.UTF-8", -1);
    /* Malformed brackets are not a locale, and must not be read as one. */
    rank_is("Name[",      "de_DE.UTF-8", -1);
    rank_is("Name[]",     "de_DE.UTF-8", -1);
    rank_is("Name[de",    "de_DE.UTF-8", -1);

    /* ── 7. …and the base is a parameter, so the same call serves
     *        GenericName and Comment when a reader wants them ──── */
    CHECK(synui_desktop_locale_rank("GenericName[de]", "GenericName",
                                    "de_DE.UTF-8") == 1,
          "GenericName[de] must rank as a locale match for GenericName");
    CHECK(synui_desktop_locale_rank("Name[de]", "GenericName",
                                    "de_DE.UTF-8") == -1,
          "Name[de] is not GenericName");

    if (failures == 0)
        printf("desktop_locale_test: all checks passed\n");
    else
        printf("desktop_locale_test: %d check(s) failed\n", failures);
    return failures != 0;
}
