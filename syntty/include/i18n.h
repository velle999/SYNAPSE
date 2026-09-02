/*
 * i18n.h — syntty's own words, in the user's language.
 *
 * ⛔ THE DIAGNOSTIC SUBCOMMANDS ARE PROTOCOL AND ARE NEVER MARKED, AND THAT IS
 * THE RULE HERE. `syntty dump`, `win --stats`, `render`, `fit`, `mouse`, `key`
 * and `paste` exist so a terminal that draws in pixels can be tested from a
 * shell, and tests/syntty_test.sh parses what they print:
 *
 *     sb=$(… --stats dump - 2>&1 >/dev/null | grep '^scrollback')
 *     fit_norm=$("$ST" fit 1600x900 --cell=8x16)
 *
 * A translated `scrollback` line is a suite that cannot read its own terminal,
 * on a machine whose only difference is its language. print_stats(),
 * print_reply() and every cmd_* writer stay English on purpose.
 *
 * ⚠ WHAT *IS* TRANSLATED is what a person reads when something goes wrong:
 * die(), and the handful of messages win.c prints about a font or a missing
 * display. Those are the ones nobody parses.
 *
 * ⚠ AND NOT THE USAGE BLOCK, which matches syn-edit and synpkg: it names
 * subcommands and flags the shell has to be given exactly, and the surrounding
 * prose is not worth splitting from them.
 *
 * ⚠ THERE IS NO JSON HALF HERE. syntty has no QML window — it draws itself — so
 * a catalog is compiled once, to a .mo, and read through libintl.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNTTY_I18N_H
#define SYNTTY_I18N_H

#include <libintl.h>

#define SYNTTY_GETTEXT_DOMAIN "syntty"

#define _(s)          gettext(s)
#define N_(s)         (s)
#define P_(a, b, n)   ngettext(a, b, n)

/* Called once from main(), before anything prints. */
void syntty_i18n_init(void);

#endif /* SYNTTY_I18N_H */
