/*
 * i18n.h — syn-arcade's own words, in the user's language.
 *
 * ⛔ THE RECORD PROTOCOL IS NEVER TRANSLATED, AND THAT IS THE RULE HERE.
 * Every `--rec` command emits percent-encoded, tab-separated rows that
 * data/syn-arcade.qml and data/syn-arcade-big.qml parse, and that
 * tests/syn_arcade_test.sh parses. The first row NAMES THE COLUMNS, and both
 * windows key off those names; many VALUES are matched on too — a hud position
 * is `top-left`, a pad axis is `ABS_X`, a fit scaler is `integer`, a filter is
 * `fsr`. Those are identifiers, not words, and a translated one is a window
 * that stops recognising its own records.
 *
 * ⚠ SO THE SPLIT IS BY DESTINATION, NOT BY FILE. The same subsystem prints
 * both: `hud show` writes a sentence somebody reads and `hud show --rec`
 * writes a record, out of the same function, a few lines apart.
 *
 *   _()   the human path — what a person reads on a terminal.
 *   N_()  a LABEL that travels in a record for a WINDOW to translate at the
 *         draw site. It puts the string in the catalog and returns it
 *         unchanged, so the record still carries the English word.
 *   P_()  ngettext, for anything counted.
 *
 * ⛔ N_() IS FOR THE LABEL COLUMN, NEVER THE ID COLUMN. `fit apps` prints an
 * id and a label; the id is what `fit run <id>` takes back. Marking an id
 * makes a window that offers the user a name no command accepts.
 *
 * ⚠ ONE .po, TWO COMPILED FORMS: JSON for the two QML windows, a .mo for this
 * binary, so a word they share is translated once and cannot disagree.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYN_ARCADE_I18N_H
#define SYN_ARCADE_I18N_H

#include <libintl.h>

#define SYN_ARCADE_GETTEXT_DOMAIN "syn-arcade"

#define _(s)          gettext(s)
#define N_(s)         (s)
#define P_(a, b, n)   ngettext(a, b, n)

/* Called once from main(), before anything prints. */
void syn_arcade_i18n_init(void);

#endif /* SYN_ARCADE_I18N_H */
