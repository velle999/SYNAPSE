/*
 * i18n.h — syn-play's own words, in the user's language.
 *
 * ⛔ THE RECORD PROTOCOL IS NEVER TRANSLATED, AND HERE IT HAS THREE READERS,
 * NOT ONE. `syn-play --rec …` is parsed by data/syn-play.qml, by
 * tests/cli_test.sh — and by syn-play ITSELF: tui.c sets `g_out = OUT_REC`,
 * runs sp_playlist_list() into a pipe and reads the rows back, because a second
 * directory walk would be a second idea of what a playlist file is. So a
 * translated record does not merely confuse a window; it makes the TUI stop
 * recognising its own output.
 *
 * ⛔ AND THE `serve` STREAM IS THE SAME PROTOCOL BY ANOTHER NAME. Its tags
 * (`s`, `q`, `h`, `f`, `playlist`, `q-more`, `q-begin`, `q-end`, `e`) and its
 * field names (`state`, `path`, `title`, `pos`, `duration`, `volume`, `index`,
 * `count`) are matched with `===` in the QML, and so are the VALUES of `state`:
 * `playing`, `paused`, `idle`, `stopped`. Those are identifiers, not words.
 *
 *   _()   the human path — what a person reads on a terminal or in the TUI.
 *   N_()  a LABEL that travels in a record for a WINDOW to translate at the
 *         draw site. It puts the string in the catalog and returns it
 *         unchanged, so the record still carries the English word.
 *   P_()  ngettext, for anything counted.
 *
 * ⛔ N_() IS FOR A LABEL, NEVER FOR A PATH, A URL, A PLAYLIST NAME OR A TIME.
 * `syn-play playlist load <name>` takes back the name this printed.
 *
 * ⚠ THE TUI DRAWS RECORD FIELDS. Its rows are the queue, the history and the
 * playlist list as the record carries them — a title, a path, a name. Its own
 * words are the chrome around them: the tab names, the key legend, the empty
 * states. Only the chrome is marked.
 *
 * ⚠ ONE .po, TWO COMPILED FORMS: JSON for the QML window, a .mo for this
 * binary, so a word they share is translated once and cannot disagree.
 *
 * ⚠ usage() IN main.c IS DELIBERATELY OUT, as it is in syn-disks, syn-arcade,
 * syntty, synpkg and syn-edit. It is one fputs of a manual page, every line a
 * command spelling with a column of text aligned to it. Whether that whole set
 * moves is one decision, taken once, for all of them.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYN_PLAY_I18N_H
#define SYN_PLAY_I18N_H

#include <libintl.h>

#define SYN_PLAY_GETTEXT_DOMAIN "syn-play"

#define _(s)          gettext(s)
#define N_(s)         (s)
#define P_(a, b, n)   ngettext(a, b, n)

/* Called once from main(), before anything prints. */
void syn_play_i18n_init(void);

#endif /* SYN_PLAY_I18N_H */
