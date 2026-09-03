/*
 * i18n.h — syn-disks' own words, in the user's language.
 *
 * ⛔ THE RECORD PROTOCOL IS NEVER TRANSLATED, AND THAT IS THE RULE HERE.
 * `syn-disks --rec …` emits tab-separated rows that data/syn-disks.qml parses
 * and that tests/syn_disks_test.sh parses. The first row of a command NAMES THE
 * COLUMNS and the window keys off those names; many VALUES are matched on too —
 * a slot's kind is `free` or `part`, a mount's state is `mounted`, an action
 * result is `ok` or `error`. Those are identifiers, not words.
 *
 * ⚠ AND THIS PROGRAM WRITES PARTITION TABLES. A record that changed shape in
 * one language is not a cosmetic bug here: it is a window that has misread
 * which slot is free, on the screen where somebody is about to erase a disk.
 * When in doubt, do not mark it.
 *
 *   _()   the human path — what a person reads on a terminal.
 *   N_()  a LABEL that travels in a record for the WINDOW to translate at the
 *         draw site. It puts the string in the catalog and returns it
 *         unchanged, so the record still carries the English word.
 *   P_()  ngettext, for anything counted.
 *
 * ⛔ N_() IS FOR A LABEL, NEVER FOR A DEVICE NAME, A PATH, A SIZE OR A UUID.
 * `/dev/nvme0n1p2` is what the next command takes back.
 *
 * ⚠ ONE .po, TWO COMPILED FORMS: JSON for the QML window, a .mo for this
 * binary, so a word they share is translated once and cannot disagree.
 *
 * ⚠ usage() IN main.c IS DELIBERATELY OUT, as it is in syn-arcade, syntty,
 * synpkg and syn-edit. It is one fputs of a manual page, every line of it a
 * flag spelling and a column of text aligned to it, and a component that
 * translated its own would be the only one that did. Whether the whole set
 * moves is one decision, taken once, for all of them.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYN_DISKS_I18N_H
#define SYN_DISKS_I18N_H

#include <libintl.h>

#define SYN_DISKS_GETTEXT_DOMAIN "syn-disks"

#define _(s)          gettext(s)
#define N_(s)         (s)
#define P_(a, b, n)   ngettext(a, b, n)

/* Called once from main(), before anything prints. */
void syn_disks_i18n_init(void);

#endif /* SYN_DISKS_I18N_H */
