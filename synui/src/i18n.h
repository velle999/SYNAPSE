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

#endif /* SYNUI_I18N_H */
