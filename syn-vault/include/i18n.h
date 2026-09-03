/*
 * i18n.h — syn-vault's own words, in the user's language.
 *
 * ⛔ THE RECORD PROTOCOL IS NEVER TRANSLATED. `syn-vault --rec …` emits a
 * header row naming the columns — `name`, `state`, `mount`, `stray` — and
 * data/syn-vault.qml keys off those names. It matches on a VALUE too:
 * `state === "open"` decides, in seven places, whether a row offers Lock or
 * Unlock, whether the mount path is shown, and what colour the row is.
 *
 * ⚠ AND `open`/`locked` ARE DRAWN AS WELL AS MATCHED, which is the whole
 * difficulty here. `syn-vault status foo` prints "'foo' is open." to a person
 * and `--rec` puts `open` in a column a program reads — the same two words,
 * two destinations. They are two SEPARATE strings in the source for that
 * reason: the record's is a literal that never moves, and the sentence is a
 * whole marked sentence per branch.
 *
 * ⚠ AND THIS PROGRAM GUARDS SOMEBODY'S ENCRYPTED FILES. A record that changed
 * shape in one language is a window that thinks a locked vault is open — and
 * offers to mount over a directory whose contents are NOT in the vault, which
 * is the one failure this program exists to prevent.
 *
 *   _()   the human path — what a person reads on a terminal.
 *   N_()  a LABEL that travels in a record for the WINDOW to translate at the
 *         draw site. It puts the string in the catalog and returns it
 *         unchanged, so the record still carries the English word.
 *   P_()  ngettext, for anything counted.
 *
 * ⛔ N_() IS FOR A LABEL, NEVER FOR A VAULT NAME OR A PATH. `syn-vault open
 * <name>` takes back the name this printed.
 *
 * ⚠ ONE .po, TWO COMPILED FORMS: JSON for the QML window, a .mo for this
 * binary, so a word they share is translated once and cannot disagree.
 *
 * ⚠ usage() IN main.c IS DELIBERATELY OUT, as it is in syn-disks, syn-play,
 * syn-arcade, syntty, synpkg and syn-edit. It is one fputs of a manual page,
 * every line a command spelling with a column of text aligned to it. Whether
 * that whole set moves is one decision, taken once, for all of them.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYN_VAULT_I18N_H
#define SYN_VAULT_I18N_H

#include <libintl.h>

#define SYN_VAULT_GETTEXT_DOMAIN "syn-vault"

#define _(s)          gettext(s)
#define N_(s)         (s)
#define P_(a, b, n)   ngettext(a, b, n)

/* Called once from main(), before anything prints. */
void syn_vault_i18n_init(void);

#endif /* SYN_VAULT_I18N_H */
