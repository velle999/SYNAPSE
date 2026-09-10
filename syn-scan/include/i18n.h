/*
 * i18n.h — syn-scan's own words, in the user's language.
 *
 * ⛔ THE RECORD PROTOCOL IS NEVER TRANSLATED. `syn-scan … --rec` emits header
 * rows naming the columns — `engine`, `verdict`, `path`, `detail`, `id` — and
 * data/syn-scan.qml keys off those names.
 *
 * ⛔ AND THE `engine` AND `verdict` COLUMNS ARE MATCHED, NOT READ. The window
 * colours a row by comparing against `infected`, and `syn-scan scan --only
 * clamav` takes that exact engine id back. A translated verdict is a window
 * that draws an infected file as clean. Every engine carries an id AND a name
 * one struct field apart — `clamav` and "ClamAV" — and only the second is
 * marked.
 *
 *   _()   the human path — what a person reads on a terminal.
 *   N_()  a LABEL that travels in a record for the WINDOW to translate at the
 *         draw site. It puts the string in the catalog and returns it
 *         unchanged, so the record still carries the English word.
 *   P_()  ngettext, for anything counted.
 *
 * ⛔ N_() IS FOR A LABEL, NEVER FOR AN ENGINE id, A VERDICT id, A PATH OR A
 * SIGNATURE NAME. A signature name is upstream's string and is quoted, not
 * translated.
 *
 * ⚠ AND THE ENGINES THEMSELVES ARE READ UNDER LC_ALL=C. clamscan and rkhunter
 * are translated too; parsing their output in the user's locale means matching
 * on words that change language. See engine_popen() in synscan.h.
 *
 * ⚠ ONE .po, TWO COMPILED FORMS: JSON for the QML window, a .mo for this
 * binary, so a word they share is translated once and cannot disagree.
 *
 * ⚠ usage() IN main.c IS DELIBERATELY OUT, as it is in syn-clean, syn-disks,
 * syn-play, syn-vault, synnet, syn-arcade, syntty, synpkg and syn-edit.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYN_SCAN_I18N_H
#define SYN_SCAN_I18N_H

#include <libintl.h>

#define SYN_SCAN_GETTEXT_DOMAIN "syn-scan"

#define _(s)          gettext(s)
#define N_(s)         (s)
#define P_(a, b, n)   ngettext(a, b, n)

/* Called once from main(), before anything prints. */
void syn_scan_i18n_init(void);

#endif /* SYN_SCAN_I18N_H */
