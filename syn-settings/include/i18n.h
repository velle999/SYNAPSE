/*
 * i18n.h — syn-settings' words, in the user's language.
 *
 * ⛔ THIS PROGRAM PRINTS NOTHING BUT ITS RECORD, AND THE RECORD IS NEVER
 * TRANSLATED. Every reader emits TSV that data/syn-settings.qml parses and that
 * `syn-settings --rec region | column -t` is meant to stay readable in. A column
 * that changes language with the desktop is a record two machines cannot
 * compare, and a GUI whose behaviour depends on the user's locale.
 *
 * ⚠ SO THE MARKING HERE IS N_() AND THERE IS NO _(). N_() adds the string to
 * the catalog and returns it unchanged, so the row still carries the English
 * word; the WINDOW translates it at the draw site with I18n.tr(f[1]) against
 * the same catalog, compiled to JSON for QML and to a .mo for anything C-side
 * that ever needs one. It is the arrangement synstudio's group names already
 * use.
 *
 * ⛔ WHICH COLUMNS ARE WORDS DEPENDS ON THE READER. Most panes emit
 * kind/key/value/state/detail/action, where `key` is the label a person reads
 * and `detail` is the sentence under it. region and time emit key/value/detail/
 * action; system emits kind/key/value/detail/action; kernel's first column is a
 * package name and is NOT a word. tests/i18n_test.sh pins each shape, because
 * getting it wrong translates a value the GUI matches on.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYN_SETTINGS_I18N_H
#define SYN_SETTINGS_I18N_H

/* ⚠ NO gettext() HERE ON PURPOSE. Nothing this binary prints is translated at
 * runtime; N_() is a marker for xgettext and nothing else. If a human-facing
 * CLI path is ever added, that is when _() and a bindtextdomain() arrive. */
#define N_(s) (s)

#endif /* SYN_SETTINGS_I18N_H */
