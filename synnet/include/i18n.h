/*
 * i18n.h — synnet's own words, in the user's language.
 *
 * synnet has no window and no `--rec`: what it prints on stdout is read by a
 * person, and everything else it emits is read by a program. The line between
 * them is what this file is about.
 *
 * ⛔ THE JOURNAL STAYS ENGLISH. Every syslog() line is left unmarked. When the
 * firewall fails to load, syn-settings' network pane tells the person
 * "`journalctl -u synnet` has what nft said" — that is the line they will read,
 * paste into a search, and attach to a bug report. A journal that changed
 * language with the desktop is one nobody else on the internet can help with,
 * and one this project's own docs would be quoting wrongly.
 *
 * ⛔ THE STATE FILE IS A PROTOCOL. /run/synnet/firewall.state is key=value —
 * `state`, `links`, `reasserts` — and syn-settings' network pane parses it to
 * decide whether this machine reports itself filtered. It is text between two
 * programs, not writing.
 *
 * ⛔ AND SO IS AN AI PROMPT. synnet asks synapd "Reply with just BLOCK or
 * ALLOW" and then matches on those two words. Translating the question would
 * be asking a different one, in a language whose answer nothing here reads.
 *
 * ⛔ THE nft SCRIPT LEAST OF ALL. It is a program's input.
 *
 *   _()   the human path — stdout and stderr, where a person is standing.
 *   N_()  a string marked where it is DECLARED and translated where it is
 *         DRAWN, for a table whose entries are printed elsewhere.
 *   P_()  ngettext, for anything counted.
 *
 * ⚠ usage() IN main.c IS DELIBERATELY OUT, as it is in syn-disks, syn-play,
 * syn-vault, syn-arcade, syntty, synpkg and syn-edit. It is one fprintf of a
 * manual page, every line a flag spelling with a column of text aligned to it.
 * Whether that whole set moves is one decision, taken once, for all of them.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNNET_I18N_H
#define SYNNET_I18N_H

#include <libintl.h>

#define SYNNET_GETTEXT_DOMAIN "synnet"

#define _(s)          gettext(s)
#define N_(s)         (s)
#define P_(a, b, n)   ngettext(a, b, n)

/* Called once from main(), before anything prints. */
void synnet_i18n_init(void);

#endif /* SYNNET_I18N_H */
