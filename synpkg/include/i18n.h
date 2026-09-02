/*
 * i18n.h — synpkg's own words, in the user's language.
 *
 * ⛔ THE TSV PATH IS NEVER TRANSLATED, AND THAT IS THE WHOLE RULE HERE.
 * `synpkg --tsv <cmd>` is what data/synpkg.qml parses, and the tests parse it
 * too. A translated column heading or status word makes the GUI's behaviour
 * depend on the user's locale — which is the bug `pacman -Qi` taught this
 * project twice, in chibi and in syn-arsenal. Every `_()` in this tree is
 * therefore on the OUT_HUMAN side of a `g_out == OUT_TSV` branch, and
 * tests/i18n_test.sh fails on one that is not.
 *
 * ⚠ THE CATALOG IS SHARED WITH THE WINDOW. po/*.po already holds the QML's
 * msgids and is compiled to JSON for it; the same file is now also compiled to
 * a .mo for this binary. One .po per language, two compiled forms, so a word
 * the CLI and the GUI both use is translated once and cannot disagree.
 *
 * ⚠ USAGE TEXT IS DELIBERATELY NOT MARKED. `synpkg --help` is fifty lines of
 * column-aligned text whose every command name is a literal the user must type
 * in English regardless; translating it breaks the alignment and buys little
 * on a desktop whose front door is the GUI. If that changes, it is one block
 * and it can be marked then.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNPKG_I18N_H
#define SYNPKG_I18N_H

#include <libintl.h>

#define SYNPKG_GETTEXT_DOMAIN "synpkg"

#define _(s)          gettext(s)
#define N_(s)         (s)
#define P_(a, b, n)   ngettext(a, b, n)

/* Called once from main(), before anything prints. */
void synpkg_i18n_init(void);

#endif /* SYNPKG_I18N_H */
