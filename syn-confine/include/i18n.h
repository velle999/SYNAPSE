/*
 * i18n.h — syn-confine's own words, in the user's language.
 *
 * ⚠ MOST OF WHAT COMES OUT OF A SANDBOX IS NOT THIS PROGRAM SPEAKING. The
 * confined command inherits the environment and phrases its own errors —
 * "Permission denied" comes from ITS libc, not from here. That is why
 * tests/syn_confine_test.sh pins LC_ALL before anything runs and says so at
 * length: it greps the CHILD's strerror, and a Japanese VM once failed seven
 * assertions and a whole package build on a sandbox that was working perfectly.
 *
 * So the strings in this file are the narrow set syn-confine itself writes:
 * the refusals, and the `--print` policy summary.
 *
 * ⚠ AND `--print` IS PROSE, NOT A RECORD. It is what somebody reads to decide
 * whether a sandbox is tight enough — "no TCP (UDP NOT covered)" is a sentence
 * whose whole job is to be understood. Nothing parses it but the suite, and the
 * suite pins the locale it asserts in.
 *
 * ⛔ EXIT 78 IS THE PROTOCOL, and it is a number. "The sandbox could not be
 * built" is told apart from "the command failed" by the status, never by the
 * wording — which is what lets that distinction survive translation at all.
 *
 *   _()   the human path — the refusals and the policy summary.
 *   N_()  marked where declared, translated where drawn.
 *   P_()  ngettext, for anything counted.
 *
 * ⚠ usage() IN THIS FILE'S .c IS DELIBERATELY OUT, as it is in syn-disks,
 * syn-play, syn-vault, syn-clean, synnet, syn-arcade, syntty, synpkg and
 * syn-edit. One decision for all of them, taken once.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYN_CONFINE_I18N_H
#define SYN_CONFINE_I18N_H

#include <libintl.h>

#define SYN_CONFINE_GETTEXT_DOMAIN "syn-confine"

#define _(s)          gettext(s)
#define N_(s)         (s)
#define P_(a, b, n)   ngettext(a, b, n)

#endif /* SYN_CONFINE_I18N_H */
