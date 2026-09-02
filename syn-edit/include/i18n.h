/*
 * i18n.h — syn-edit's own words, in the user's language.
 *
 * ⛔ THE RECORD PROTOCOL IS NEVER TRANSLATED, AND THAT IS THE RULE HERE.
 * `syn-edit --rec …` emits `key <TAB> value` rows that data/syn-edit.qml parses
 * and the tests parse. Those KEYS — mode, file, line, language, eol, modified —
 * are matched on, and so are several of the VALUES:
 *
 *   - ed_mode_name() returns "INSERT"/"NORMAL"/"REPLACE" and the window
 *     compares `st.mode === "INSERT"` in nine places;
 *   - syn_lang_name() and syn_tok_name() are the syntax engine's own words;
 *   - buf_name() is a path.
 *
 * A translated one of those is a window that stops recognising its own editor.
 * The convention synui already enforces is the one that keeps them apart: a
 * function whose name ends _name() is read by a PROGRAM and is never marked;
 * tests/i18n_test.sh fails on a `_()` inside one.
 *
 * ⚠ WHAT *IS* TRANSLATED is ed_message() — the one channel a person reads. It
 * travels as the VALUE of a `message` / `error` row, and the window draws it
 * without ever comparing it (it branches on the `msgerr` flag, not the text).
 *
 * ⚠ ONE .po, TWO COMPILED FORMS: JSON for the QML window, a .mo for this
 * binary, so a word both front-ends use is translated once.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYN_EDIT_I18N_H
#define SYN_EDIT_I18N_H

#include <libintl.h>

#define SYN_EDIT_GETTEXT_DOMAIN "syn-edit"

#define _(s)          gettext(s)
#define N_(s)         (s)
#define P_(a, b, n)   ngettext(a, b, n)

/* Called once from main(), before anything prints. */
void syn_edit_i18n_init(void);

#endif /* SYN_EDIT_I18N_H */
