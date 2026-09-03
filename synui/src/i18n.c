/*
 * i18n.c — bind the message catalogs. See i18n.h for the two macros and the
 * one rule about which strings may be translated.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <libintl.h>
#include <locale.h>
#include <stddef.h>
#include <string.h>

#include "i18n.h"

#ifndef SYNUI_LOCALEDIR
#define SYNUI_LOCALEDIR "/usr/share/locale"
#endif

void synui_i18n_init(void)
{
    bindtextdomain(SYNUI_GETTEXT_DOMAIN, SYNUI_LOCALEDIR);
    /*
     * ⚠ UTF-8 REGARDLESS OF THE LOCALE'S OWN CHARSET. Every string here ends
     * up in cairo through pango/FreeType, which take UTF-8 and nothing else.
     * Without this libintl converts the catalog to the locale's codeset, and a
     * non-UTF-8 locale — still legal, still installable — would hand the
     * renderer bytes it draws as replacement boxes. The catalogs are UTF-8;
     * say so and skip the conversion.
     */
    bind_textdomain_codeset(SYNUI_GETTEXT_DOMAIN, "UTF-8");
    textdomain(SYNUI_GETTEXT_DOMAIN);
}

/* ── The application's own catalog ─────────────────────────
 *
 * See i18n.h for the rule and the candidate order this implements.
 */

/*
 * Split `lang_COUNTRY.ENCODING@MODIFIER` into its parts.
 *
 * ⚠ THE ENCODING IS DROPPED, on BOTH sides of the comparison. The spec says a
 * key's locale is matched without it, and the desktop's own locale almost
 * always carries one (`de_DE.UTF-8`) while a .desktop key almost never does —
 * so comparing the strings whole matches nothing at all, on every desktop, in
 * every language. That is the single mistake this function exists to make
 * impossible.
 */
static void locale_split(const char *loc,
                         char lang[32], char country[32], char mod[32])
{
    lang[0] = country[0] = mod[0] = '\0';
    if (!loc || !*loc) return;

    size_t n = 0;
    while (loc[n] && loc[n] != '_' && loc[n] != '.' && loc[n] != '@') n++;
    if (n >= 32) n = 31;
    memcpy(lang, loc, n);
    lang[n] = '\0';

    const char *p = loc + n;
    if (*p == '_') {
        p++;
        size_t c = 0;
        while (p[c] && p[c] != '.' && p[c] != '@') c++;
        if (c >= 32) c = 31;
        memcpy(country, p, c);
        country[c] = '\0';
        p += c;
    }
    /* The encoding, skipped rather than kept: nothing below ever asks for it. */
    if (*p == '.') {
        p++;
        while (*p && *p != '@') p++;
    }
    if (*p == '@') {
        p++;
        size_t m = strlen(p);
        if (m >= 32) m = 31;
        memcpy(mod, p, m);
        mod[m] = '\0';
    }
}

int synui_desktop_locale_rank(const char *key, const char *base,
                              const char *locale)
{
    if (!key || !base) return -1;

    size_t bl = strlen(base);
    if (strncmp(key, base, bl) != 0) return -1;

    /* The plain key: always a candidate, always the weakest one. */
    if (key[bl] == '\0') return 0;

    /* `Name[…]` and nothing else — `NameFoo` and `Name[de` are not this key.
     * ⚠ `Names` MUST NOT MATCH `Name`, which is what the bracket test buys:
     * a .desktop carrying both would otherwise have the second overwrite the
     * first at rank 0. */
    if (key[bl] != '[') return -1;
    const char *inner = key + bl + 1;
    size_t il = strlen(inner);
    if (il < 2 || inner[il - 1] != ']') return -1;

    char want[64];
    if (il - 1 >= sizeof(want)) return -1;
    memcpy(want, inner, il - 1);
    want[il - 1] = '\0';

    /* No locale is the C locale, where only the plain key is a candidate. */
    if (!locale || !*locale) return -1;

    char klang[32], kcountry[32], kmod[32];
    char llang[32], lcountry[32], lmod[32];
    locale_split(want, klang, kcountry, kmod);
    locale_split(locale, llang, lcountry, lmod);

    /* A different language is never a candidate, whatever else matches. */
    if (!klang[0] || strcmp(klang, llang) != 0) return -1;

    int has_country = kcountry[0] != '\0';
    int has_mod     = kmod[0] != '\0';

    /*
     * ⚠ A KEY MAY NOT CARRY WHAT THE LOCALE DOES NOT ASK FOR. `Name[pt_BR]` on
     * a `pt_PT` desktop is not Portuguese-in-general, it is Brazilian
     * Portuguese, and the spec's candidate list simply does not contain it —
     * so it scores -1 rather than falling back to rank 1 and quietly handing a
     * Portuguese desktop the wrong Portuguese.
     */
    if (has_country && strcmp(kcountry, lcountry) != 0) return -1;
    if (has_mod     && strcmp(kmod,     lmod)     != 0) return -1;

    if (has_country && has_mod) return 4;
    if (has_country)            return 3;
    if (has_mod)                return 2;
    return 1;
}

const char *synui_desktop_locale(void)
{
    /*
     * ⚠ ASKED EVERY TIME, NOT CACHED, and the caching version of this was
     * written first. setlocale(LC_MESSAGES, NULL) is a read of a string the C
     * library already holds — there is nothing to save — while a cache is a
     * value that outlives the only thing that could change it, which makes the
     * function untestable in-process: a suite that scans once under C and once
     * under de_DE gets the first answer twice and passes against the bug.
     *
     * The pointer is the C library's own and stays valid until the next
     * setlocale(); every caller here uses it inside one scan.
     */
    const char *loc = setlocale(LC_MESSAGES, NULL);
    /* "C", "POSIX" and "C.UTF-8" are "no language", and their only candidate is
     * the plain key — which is what a NULL answer means to the rank above. */
    if (!loc || !*loc || !strcmp(loc, "C") || !strcmp(loc, "POSIX") ||
        !strncmp(loc, "C.", 2))
        return NULL;
    return loc;
}
