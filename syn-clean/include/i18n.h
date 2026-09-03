/*
 * i18n.h — syn-clean's own words, in the user's language.
 *
 * ⛔ THE RECORD PROTOCOL IS NEVER TRANSLATED. `syn-clean --rec …` emits header
 * rows naming the columns — `id`, `label`, `what`, `bytes`, `files`, `root`,
 * `logins` — and data/syn-clean.qml keys off those names.
 *
 * ⛔ AND THE `id` COLUMN IS THE COMMAND. `syn-clean clean browsercache` takes
 * that exact string back, and the window sends it; a translated id is a window
 * asking to clean a category that does not exist. Every category carries an id
 * AND a label one struct field apart — `browsercache` and "Browser cache" —
 * and only the second is marked.
 *
 * ⚠ AND THIS PROGRAM DELETES SOMEBODY'S FILES. A record that changed shape in
 * one language is a window that has confused two categories, on the screen
 * where one of them SIGNS YOU OUT of every site you use.
 *
 *   _()   the human path — what a person reads on a terminal.
 *   N_()  a LABEL that travels in a record for the WINDOW to translate at the
 *         draw site. It puts the string in the catalog and returns it
 *         unchanged, so the record still carries the English word.
 *   P_()  ngettext, for anything counted.
 *
 * ⛔ N_() IS FOR A LABEL, NEVER FOR A CATEGORY id, A PATH OR A PROCESS NAME.
 * `conflicts` is matched against /proc/<pid>/comm.
 *
 * ⚠ ONE .po, TWO COMPILED FORMS: JSON for the QML window, a .mo for this
 * binary, so a word they share is translated once and cannot disagree.
 *
 * ⚠ usage() IN main.c IS DELIBERATELY OUT, as it is in syn-disks, syn-play,
 * syn-vault, synnet, syn-arcade, syntty, synpkg and syn-edit. It is one fputs
 * of a manual page, every line a command spelling with a column of text
 * aligned to it. Whether that whole set moves is one decision, taken once.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYN_CLEAN_I18N_H
#define SYN_CLEAN_I18N_H

#include <libintl.h>

#define SYN_CLEAN_GETTEXT_DOMAIN "syn-clean"

#define _(s)          gettext(s)
#define N_(s)         (s)
#define P_(a, b, n)   ngettext(a, b, n)

/* Called once from main(), before anything prints. */
void syn_clean_i18n_init(void);

#endif /* SYN_CLEAN_I18N_H */
