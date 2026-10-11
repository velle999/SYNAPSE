#!/usr/bin/env bash
# help_runs_nothing.sh — printing a helper's help runs nothing it mentions
#
# Both usage() functions are UNQUOTED heredocs, and in one of those a backtick
# is a command substitution. The help quotes commands in backticks, so
# `synui-widgets --help` RAN `synctl weather on` twice — the compositor's
# network switch — and `omarchy-weather-location --set "Oslo"`, which moves the
# desktop's weather to Oslo. A mistyped widget name prints the same usage, so a
# typo did it too. `synui-plugins --help` ran commands named `add` and `browse`.
#
# Stubs first on PATH log every call and refuse it, HOME is a scratch dir, and
# each help must log nothing, write nothing to stderr, and SHOW the commands.
#
# Usage: help_runs_nothing.sh /path/to/synui-widgets.sh /path/to/synui-plugins.sh
set -u

WIDGETS=${1:?usage: help_runs_nothing.sh synui-widgets.sh synui-plugins.sh}
PLUGINS=${2:?usage: help_runs_nothing.sh synui-widgets.sh synui-plugins.sh}

pass=0; fail=0
ok()  { printf 'ok %d - %s\n' $((pass + fail + 1)) "$1"; pass=$((pass + 1)); }
bad() { printf 'not ok %d - %s\n' $((pass + fail + 1)) "$1"; fail=$((fail + 1)); }

TMP=$(mktemp -d) || exit 1
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/bin" "$TMP/home"
for c in synctl omarchy-weather-location add browse quickshell curl \
         notify-send sudo run0 pkexec su doas; do
    printf '#!/bin/sh\necho "%s $*" >> "%s/calls"\nexit 1\n' "$c" "$TMP" > "$TMP/bin/$c"
    chmod +x "$TMP/bin/$c"
done

# run <name> <script> <arg>... — the help, in the scratch world
run() {
    local name=$1; shift
    : > "$TMP/calls"
    env HOME="$TMP/home" XDG_CONFIG_HOME="$TMP/home/.config" \
        XDG_CACHE_HOME="$TMP/home/.cache" XDG_STATE_HOME="$TMP/home/.local/state" \
        PATH="$TMP/bin:$PATH" bash "$@" > "$TMP/out" 2> "$TMP/err"
    if [ -s "$TMP/calls" ]; then bad "$name ran: $(tr '\n' ';' < "$TMP/calls")"
    else ok "$name runs nothing"; fi
    if [ -s "$TMP/err" ]; then bad "$name wrote to stderr: $(head -3 "$TMP/err" | tr '\n' ';')"
    else ok "$name writes nothing to stderr"; fi
}

# shows <name> <text> — the help, as printed, carries the quoted command
shows() {
    if grep -qF -- "$2" "$TMP/out"; then ok "$1 shows $2"
    else bad "$1 lost $2"; fi
}

for arg in --help help; do
    run "synui-widgets $arg" "$WIDGETS" "$arg"
done
shows synui-widgets '`synctl weather on`'
shows synui-widgets '`omarchy-weather-location --set "Oslo"`'

# A typo prints the same usage, to stderr, and that path must not run it either.
: > "$TMP/calls"
env HOME="$TMP/home" XDG_CONFIG_HOME="$TMP/home/.config" PATH="$TMP/bin:$PATH" \
    bash "$WIDGETS" clock sideways > /dev/null 2> "$TMP/err"
if [ -s "$TMP/calls" ]; then bad "synui-widgets with a bad action ran: $(tr '\n' ';' < "$TMP/calls")"
else ok "synui-widgets with a bad action runs nothing"; fi
if grep -qF 'command not found' "$TMP/err"; then bad "synui-widgets with a bad action: command not found"
else ok "synui-widgets with a bad action finds no stray command"; fi

for arg in --help help; do
    run "synui-plugins $arg" "$PLUGINS" "$arg"
done
shows synui-plugins '`add` does'
shows synui-plugins 'What `browse` offers'

echo "1..$((pass + fail))"
[ "$fail" -eq 0 ]
