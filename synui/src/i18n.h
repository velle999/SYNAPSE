/*
 * i18n.h — the compositor's own words, in the user's language.
 *
 * gettext, not a hand-written catalog, and the reason is the count: synui has
 * some 750 user-visible labels spread over forty files, and a catalog keyed by
 * hand is a list somebody has to keep in step with the source by remembering
 * to. xgettext reads the source instead, so a label that is added and not
 * translated is simply English — which is the right failure — and a label that
 * is CHANGED cannot silently keep an old translation, because the msgid moved.
 *
 * libintl is part of glibc, so this adds nothing to the ISO. `gettext` is a
 * makedepends only, for msgfmt.
 *
 * ── The two macros, and why there are two ──────────────────────────────────
 *
 *   _(s)   look it up NOW. Use at the point the string is drawn or printed.
 *   N_(s)  do nothing at all, but let xgettext see it. Use on a string in a
 *          STATIC TABLE, which is initialised before a locale exists and must
 *          stay the English key the catalog is looked up by.
 *
 * ⚠ A TABLE ENTRY NEEDS BOTH: N_() where it is declared, _() where it is used.
 * Marking only the declaration compiles and translates nothing; wrapping only
 * the use compiles and translates, but xgettext never sees the string, so no
 * translator is ever offered it. Both, or the string is quietly English.
 *
 * ⛔ AND NOT EVERY STRING IN A UI TABLE IS A LABEL. ctl_items[] carries the
 * settings key ("theme", "wallpaper_accent") in the field next to the label,
 * and synuirc is written with it. Translating one of those does not make a
 * German control panel — it makes a control panel that writes `Erscheinungsbild
 * = 1` into the config and silently loses every setting on the row. The rule
 * is: a string a PERSON READS is translatable; a string a FILE or a PROTOCOL
 * reads is not, however English it looks.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNUI_I18N_H
#define SYNUI_I18N_H

#include <libintl.h>

#define SYNUI_GETTEXT_DOMAIN "synui"

#define _(s)  gettext(s)
#define N_(s) (s)

/*
 * P_(singular, plural, n) — a string that counts something.
 *
 * ⛔ NOT "%d window%s" WITH "" OR "s". That trick is English grammar written
 * into the format string, and a translator cannot reach it: the catalog can
 * only replace the sentence, not the `s` the C code appends afterwards. German
 * would read "3 Fensters", and the languages this desktop ships are worse than
 * that — Polish and Russian pick between THREE forms by the last two digits of
 * the number, Arabic between SIX. There is no msgstr that can be right.
 *
 * ngettext takes both English forms and the count, and each catalog declares
 * its own Plural-Forms rule in the .po header, so the language decides how many
 * forms it has and which one this n selects.
 *
 * ⚠ THE COUNT IS PASSED TWICE — once to P_() to choose the form, and again to
 * printf to be printed. Both English forms must carry the same specifiers.
 */
#define P_(sing, plur, n) ngettext(sing, plur, n)

/*
 * _() for a string that may be NULL.
 *
 * ⚠ gettext(NULL) IS UNDEFINED BEHAVIOUR, and most rows of a UI table leave
 * their optional fields unset: of the control panel's 160 rows only 37 carry a
 * .section and 110 a .help. Writing _(it->help) at the one place help is read
 * would be a crash on the majority of rows, reached by hovering the wrong one.
 */
static inline const char *_opt(const char *s) { return s ? gettext(s) : s; }

/* Called once from main(), after setlocale(). Points libintl at the installed
 * catalogs and declares UTF-8 — without the codeset call libintl answers in
 * the locale's charset, and this compositor draws through cairo, which wants
 * UTF-8 whatever the locale says. */
void synui_i18n_init(void);

/*
 * ── The OTHER catalog: the one every application ships for itself ───────────
 *
 * A .desktop file carries its own translations, in keys the spec spells
 * `Name[de]`, `Name[pt_BR]`, `Name[sr@latin]`. Firefox ships thirty of them and
 * so does most of what anyone installs — so on a German desktop the compositor,
 * the bar and the panels were German and the APPLICATION LIST beside them was
 * English, because both readers here (appgrid.c and icons.c) took the plain key
 * and said so in a comment: "a real locale match belongs with the rest of
 * i18n". This is that.
 *
 * ⛔ NOT "the last key that starts with Name". The localised variants sit in
 * arbitrary order around the plain one, so last-seen hands a German desktop
 * whichever language the file happens to end with — which is how this went
 * unwritten rather than written wrong.
 *
 * synui_desktop_locale_rank() scores ONE key against ONE locale, and the caller
 * keeps the highest-scoring value it has seen for that field. The order is the
 * spec's own candidate list, best first:
 *
 *     4  Name[lang_COUNTRY@MODIFIER]
 *     3  Name[lang_COUNTRY]
 *     2  Name[lang@MODIFIER]
 *     1  Name[lang]
 *     0  Name                      — the fallback, and always a candidate
 *    -1  a different key, or a locale this desktop is not in
 *
 * ⚠ THE LOCALE IS A PARAMETER, not something this reads for itself, and that
 * is what makes it testable on a machine where de_DE is not installed — see
 * tests/desktop_locale_test.c. synui_desktop_locale() is where the running
 * compositor gets its answer.
 */
int synui_desktop_locale_rank(const char *key, const char *base,
                              const char *locale);

/*
 * The locale the desktop's own labels are already in: LC_MESSAGES as
 * setlocale() resolved it, or NULL for the C locale.
 *
 * ⚠ setlocale(), NOT getenv("LANG"), and the difference is a desktop whose
 * locale is not installed. There, setlocale(LC_ALL, "") fails, gettext hands
 * back English, and reading the environment instead would draw German
 * application names down an English menu. The application list follows the
 * rest of the desktop or it is not localisation, it is a second opinion.
 */
const char *synui_desktop_locale(void);

#endif /* SYNUI_I18N_H */
