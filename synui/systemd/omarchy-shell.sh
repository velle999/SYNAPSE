#!/usr/bin/env bash
#
# omarchy-shell — open, close or toggle a plugin's panel, by the name their
# plugins call it.
#
# ⚠ THIS NAME IS NOT OURS, AND UNLIKE THE OTHER THREE THIS ONE IS THEIR SHELL'S
# OWN CLI. Omarchy's plugins reach their host through it, hardcoded in QML with
# no way to configure it — YT Mini's entire bar button is
#
#     bar.run("omarchy-shell shell toggle io.github.joshuaswarren.ytmini
#              '{\"clipboard\":true}'")
#
# and their README binds `omarchy-shell shell summon <id> '<json>'` to a key.
# On a desktop without the command that button is dead.
#
# ⛔ AND IT IS DEAD IN COMPLETE SILENCE, WHICH IS WHY THIS EXISTS AT ALL.
# `bar.run` hands a string to `sh -c` (PluginHost.run), so a missing binary is
# exit 127 inside a detached shell that nothing is watching: no Qt warning, no
# line in the bar log, no failed-to-start. Compare the Vitals widget, which runs
# its helper as the command itself and therefore logged 291 times. A plugin
# calling a command we do not ship is the quietest failure in the system.
#
# ⛔ WHAT IT IS NOT is a shim for `omarchy` or for their whole shell. It
# forwards the three panel verbs to synui's own `plugin` IPC handler and refuses
# everything else by name, loudly. A shim that silently accepted a verb it does
# not implement would be the same dead button wearing a working command's name.
#
# Usage:
#   omarchy-shell shell toggle <plugin-id> [payload-json]
#   omarchy-shell shell summon <plugin-id> [payload-json]
#   omarchy-shell shell close  <plugin-id>
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

# The shell the live bar runs. Overridable so the tests can drive a tree out of
# a checkout rather than the installed copy — the same knob synui-plugins takes.
SHELL_ROOT="${SYNUI_BAR:-/usr/share/synui/quickshell}"
CONFIG="$SHELL_ROOT/shell.qml"

usage() {
    cat >&2 <<EOF
usage: omarchy-shell shell <toggle|summon|close> <plugin-id> [payload-json]

  toggle   open the plugin's panel, or close it if it is open
  summon   open it, whether or not it is already open
  close    close it

  The payload is passed to the panel's open() untouched. What a plugin reads
  out of it is its own business: YT Mini takes {"url":…}, {"clipboard":true},
  {"grab":true}, {"radio":true}, {"corner":"tl"} and {"move":{…}}.
EOF
    exit 2
}

[ $# -ge 1 ] || usage

# ⚠ `shell` IS A SUBCOMMAND OF THEIRS AND IS ACCEPTED, NOT REQUIRED. Every call
# in the corpus spells it `omarchy-shell shell <verb>`, but their CLI has other
# subcommand groups we do not implement, and swallowing the group name only when
# it is actually there keeps `omarchy-shell toggle <id>` working for anyone
# typing it by hand.
[ "$1" = "shell" ] && shift

[ $# -ge 2 ] || usage
verb=$1
id=$2
payload=${3:-}

case "$id" in
    ""|-*) usage ;;
esac

command -v quickshell >/dev/null 2>&1 || {
    printf 'omarchy-shell: quickshell is not installed\n' >&2; exit 1; }
[ -r "$CONFIG" ] || {
    printf 'omarchy-shell: no shell at %s\n' "$CONFIG" >&2; exit 1; }

# ⚠ THE PAYLOAD-CARRYING SPELLINGS, ALWAYS — never bare `toggle`/`open`.
# quickshell matches an IPC call on arity, so the one-argument forms cannot be
# handed a payload at all and would drop it without a word. See the note in
# shell.qml for why there are two spellings rather than one optional argument.
case "$verb" in
    toggle) exec quickshell -p "$CONFIG" ipc call plugin toggleWith "$id" "$payload" ;;
    summon|open)
            exec quickshell -p "$CONFIG" ipc call plugin openWith   "$id" "$payload" ;;
    close)  exec quickshell -p "$CONFIG" ipc call plugin close      "$id" ;;
    *)
        # ⛔ BY NAME AND LOUDLY. The failure this whole file exists to end is a
        # command that is not there; a verb that is not there must not be
        # allowed to fail the same quiet way.
        printf 'omarchy-shell: unknown verb %s (toggle, summon, close)\n' "$verb" >&2
        exit 2 ;;
esac
