/*
 * phrases.h — the things people say, in the languages SynapseOS installs in.
 *
 * Split out of intents.c because the DATA and the ACTIONS grew apart: the
 * actions are a couple of hundred lines that rarely change, and the phrase
 * lists are a thousand that change every time somebody notices a way of asking
 * we do not take. Keeping them in one file made the file about neither.
 *
 * ⚠ ONE LIST PER INTENT, ALL LANGUAGES TOGETHER — NOT A LIST PER LANGUAGE.
 * That is the design, not an accident of layout. Matching does not depend on
 * which language the shell was told to speak, so:
 *
 *   - a German who types "list files" out of habit still gets it, and an
 *     English speaker who picked up "aktualisieren" off a forum still gets it;
 *   - `--intent-check` answers the same for a given line whatever LANG the
 *     caller happens to have, which is what synui's command bar needs;
 *   - and there is no language-detection step to be wrong. Detecting the
 *     language of a three-word command line is a coin toss, and losing it
 *     would mean refusing a question we can plainly see.
 *
 * The cost is that a phrase must be unambiguous ACROSS languages, not merely
 * within one, which rules out short lookalikes.
 *
 * Everything here is folded through synsh_fold() before comparison, so entries
 * are written the way a person spells them — accents and all — and still match
 * somebody who leaves the accents off.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNSH_PHRASES_H
#define SYNSH_PHRASES_H

#include <stddef.h>
#include <stdbool.h>

/* ── Whole-line intents ───────────────────────────────────── */
extern const char *const SYNSH_P_TIME[];
extern const char *const SYNSH_P_DATE[];
extern const char *const SYNSH_P_MUSIC[];
extern const char *const SYNSH_P_FILES[];
extern const char *const SYNSH_P_YOUTUBE[];
extern const char *const SYNSH_P_UPDATE[];
extern const char *const SYNSH_P_ORPHANS[];
extern const char *const SYNSH_P_CANDO[];

/*
 * ── Intents with an argument ──────────────────────────────
 *
 * A CIRCUMFIX, not a prefix, because half the world puts the verb last.
 * "install firefox" and "instala firefox" are prefixes; "firefox をインストール"
 * and "firefox इंस्टॉल करो" are suffixes, and a prefix-only table would have
 * silently offered these intents to Latin-script languages only — which is the
 * exact shape of the bug this whole change exists to fix.
 *
 * `before` or `after` may be "": one empty end makes it an ordinary prefix or
 * suffix. What lies between them is the argument.
 *
 * ⚠ LONGEST FIRST WITHIN EACH TABLE. "search for X" and "search X" are both
 * real, and a table that tried "search" first would hand the package intent an
 * argument beginning with the word "for".
 */
typedef struct { const char *before; const char *after; } synsh_circumfix_t;

extern const synsh_circumfix_t SYNSH_C_INSTALL[];      /* .before == NULL ends it */
extern const synsh_circumfix_t SYNSH_C_UNINSTALL[];
extern const synsh_circumfix_t SYNSH_C_SEARCH[];
extern const synsh_circumfix_t SYNSH_C_ISINSTALLED[];
extern const synsh_circumfix_t SYNSH_C_ALARM[];

/*
 * Match `folded` (already synsh_fold()ed) against a circumfix table and copy
 * out the argument between the two ends. Returns false if no entry matches.
 *
 * `arg` may come back EMPTY — "update" has no argument and neither does a
 * bare "wecker" — so callers that need one must check, exactly as they did
 * when this was rest_after().
 */
bool synsh_phrase_arg(const char *folded, const synsh_circumfix_t *tab,
                      char *arg, size_t n);

/* ── The everyday commands ────────────────────────────────── */
typedef struct { const char *const *phrases; const char *cmd; } synsh_everyday_t;
extern const synsh_everyday_t SYNSH_EVERYDAY[];         /* .phrases == NULL ends it */

#endif /* SYNSH_PHRASES_H */
