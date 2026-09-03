#!/usr/bin/env bash
# pot.sh — synnet's message template.
#
# ⛔ THREE THINGS ARE NOT IN HERE, AND EACH FOR ITS OWN REASON.
#
#   · THE JOURNAL. Every syslog() line stays English. `journalctl -u synnet` is
#     what a person is told to run when the firewall fails — syn-settings' own
#     network pane says so in as many words — and what they will paste into a
#     search or a bug report. A journal that changes language per desktop is a
#     journal nobody else can read.
#
#   · THE STATE FILE. /run/synnet/firewall.state is key=value, and
#     syn-settings reads `state`, `links` and `reasserts` out of it. It is a
#     protocol between two programs that happens to be text.
#
#   · THE AI PROMPTS. synnet asks synapd "Reply with just BLOCK or ALLOW",
#     then matches on those two words. A translated prompt is a model answering
#     a different question, in a language whose reply tokens nothing matches.
#
# tests/i18n_test.sh fails on a `_()` in any of the three.
#
# ⛔ AND **NOT** --omit-header, WHICH SILENTLY MANGLES THE MSGIDS. With no header
# there is no Content-Type to declare a charset, so xgettext writes the .pot as
# ASCII and DROPS every non-ASCII character from the strings it extracted, with
# no warning about the loss — a msgid that lost a character never matches the
# source string, so it is permanently English however well translated.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
root=$1
out=${2:-$root/po}

xgettext --language=C --from-code=UTF-8 --no-location \
         --add-comments=TRANSLATORS \
         --keyword=_ --keyword=N_ --keyword=P_:1,2 \
         --package-name=synnet \
         -o "$out/synnet.pot" "$root"/src/*.c

printf 'pot.sh: %s msgids\n' "$(grep -c '^msgid ' "$out/synnet.pot")"
