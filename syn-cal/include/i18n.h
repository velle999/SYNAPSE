/*
 * i18n.h — syn-cal's own words, in the user's language.
 *
 * ⛔ THE RECORD PROTOCOL IS NEVER TRANSLATED, AND THAT IS THE RULE HERE.
 * `syn-cal --rec …` emits tab-separated rows that data/syn-cal.qml parses and
 * the tests parse. Both the field layout and several of the VALUES are matched
 * on rather than read:
 *
 *   - acc_kind_name() returns the account kind — "caldav", "google",
 *     "microsoft" — which the window branches on and `account add` parses back;
 *   - week_start_name() is the same word `syn-cal week-start set` accepts, so
 *     translating it would make the setting unreadable by the thing that wrote
 *     it;
 *   - ics_safe_name() is a FILENAME.
 *
 * ⚠ AND THE DATES ARE PROTOCOL TOO. The record's ISO-8601 keys (%04d-%02d-%02d)
 * are ordered and compared by the window; a localised month name in that column
 * is a calendar that cannot sort its own days. Month and day NAMES a person
 * reads are a separate thing and are translated.
 *
 * The convention synui already enforces keeps them apart: a function whose name
 * ends _name() is read by a PROGRAM and is never marked; tests/i18n_test.sh
 * fails on a `_()` inside one, and proves the rest by RUNNING every --rec
 * command under a real foreign locale and diffing.
 *
 * ⚠ ONE .po, TWO COMPILED FORMS: JSON for the QML window, a .mo for this
 * binary, so a word both front-ends use is translated once.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYN_CAL_I18N_H
#define SYN_CAL_I18N_H

#include <libintl.h>

#define SYN_CAL_GETTEXT_DOMAIN "syn-cal"

#define _(s)          gettext(s)
#define N_(s)         (s)
#define P_(a, b, n)   ngettext(a, b, n)

/* Called once from main(), before anything prints. */
void syn_cal_i18n_init(void);

#endif /* SYN_CAL_I18N_H */
