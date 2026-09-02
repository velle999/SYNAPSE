/*
 * i18n.h — synfiles' own words, in the user's language.
 *
 * ⛔ THE RECORD PROTOCOL IS NEVER TRANSLATED, AND THAT IS THE RULE HERE.
 * `synfiles --rec …` emits tab-separated rows that data/synfiles.qml parses and
 * the tests parse. The columns are matched rather than read: `state` is
 * compared against "ok"/"missing", `kind` decides which pane a row belongs to,
 * and `path`, `name` and `mime` are data. A translated one of those is a window
 * that stops recognising its own file manager.
 *
 * ⚠ AND SOME RECORD VALUES ARE NOT OURS TO TRANSLATE EITHER. The `label` column
 * of an action row is the `Name=` out of somebody's .desktop file; the desktop
 * environment already translated it, or did not, and either way it is theirs.
 *
 * ⚠ WHAT *IS* TRANSLATED is what a person reads: die(), warn(), and the
 * human branch of every command that has one — the side of `g_out == OUT_REC`
 * that prints columns for eyes rather than fields for a parser.
 *
 * ⚠ ONE .po, TWO COMPILED FORMS: JSON for the QML window, a .mo for this
 * binary, so a word both front-ends use is translated once.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNFILES_I18N_H
#define SYNFILES_I18N_H

#include <libintl.h>

#define SYNFILES_GETTEXT_DOMAIN "synfiles"

#define _(s)          gettext(s)
#define N_(s)         (s)
#define P_(a, b, n)   ngettext(a, b, n)

/* Called once from main(), before anything prints. */
void synfiles_i18n_init(void);

#endif /* SYNFILES_I18N_H */
