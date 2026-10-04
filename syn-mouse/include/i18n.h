/*
 * i18n.h — syn-mouse's own words, in the user's language.
 *
 * ⛔ THE RECORD PROTOCOL IS NEVER TRANSLATED. `syn-mouse --rec …` writes rows
 * whose first column names the kind — `profile`, `bind`, `mouse`, `active` —
 * and whose cells carry button names (`back`), modes (`toggle`) and key
 * spellings (`shift+1`). data/syn-mouse.qml keys off every one of those, and
 * `syn-mouse bind` takes them back as arguments.
 *
 * ⛔ AND A BUTTON NAME, A MODE AND A KEY NAME ARE COMMANDS. `back`, `toggle`
 * and `shift` are what a person types and what the bindings file stores; only
 * their LABELS ("Thumb button (back)") are marked, with N_(), for the window
 * to translate where it draws them.
 *
 * ⛔ THE JOURNAL IS A RECORD TOO. The daemon's log lines stay English: they
 * are what `journalctl --user -u syn-mouse` shows, what a person pastes into a
 * search and what a bug report carries. The toast it pops when a toggle flips
 * is drawn for a person, and that one is translated.
 *
 *   _()   the human path — what a person reads on a terminal or in a toast.
 *   N_()  a LABEL that travels in a record for the WINDOW to translate at the
 *         draw site. It puts the string in the catalog and returns it
 *         unchanged, so the record still carries the English word.
 *   P_()  ngettext, for anything counted.
 *
 * ⚠ usage() IN main.c IS DELIBERATELY OUT, as it is in syn-clean, syn-disks,
 * syn-arcade, syntty and the rest: one fputs of a manual page whose columns
 * are aligned to command spellings.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYN_MOUSE_I18N_H
#define SYN_MOUSE_I18N_H

#include <libintl.h>

#define SYN_MOUSE_GETTEXT_DOMAIN "syn-mouse"

#define _(s)          gettext(s)
#define N_(s)         (s)
#define P_(a, b, n)   ngettext(a, b, n)

/* Called once from main(), before anything prints. */
void syn_mouse_i18n_init(void);

#endif /* SYN_MOUSE_I18N_H */
