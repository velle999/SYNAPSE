#!/bin/sh
# game_actions.sh — everything game mode does, it must undo
#
# Game mode borrows the machine from the desktop: it stops the AI daemon, drops
# the post-process pass, pauses the wallpaper engine, and can stop the bar and
# quiet the kernel module. Every one of those is a change to something the user
# did not ask to change, held only for as long as a game is up.
#
# So the invariant is symmetry, and it has two halves:
#
#   1. Anything taken in game_enter is given back in game_leave.
#   2. Anything that is a SEPARATE PROCESS is *also* given back in game_finish
#      — the compositor-shutdown path. config.effects dies with synui and needs
#      no undo, but a wallpaper engine that was stopped, a bar that was killed,
#      or a security module that was silenced stay that way after synui exits.
#      Leaving a desktop with no bar because the compositor died mid-game is the
#      exact failure the existing synapd restore in game_finish was written for.
#
# A missing undo has no symptom until the unlucky run, which is why this is a
# test and not a code review note.
#
# Source-level: game mode needs a seat, a fullscreen XWayland client and a GPU,
# none of which a build machine has. This asserts the shape of the code instead.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

game=${1:?usage: game_actions.sh <src/game.c> <src/config.c>}
config=${2:?missing src/config.c}

fails=0
check() {  # check <description> <expected> <actual>
    if [ "$2" = "$3" ]; then
        printf '  ok    %s\n' "$1"
    else
        printf '  FAIL  %s — expected %s, got %s\n' "$1" "$2" "$3"
        fails=$((fails + 1))
    fi
}

# The body of one function, so "is it undone in game_leave" cannot be satisfied
# by a match somewhere else in the file.
body() {  # body <function-name> <file>
    awk -v fn="$1" '
        $0 ~ "^(static )?(void|int) " fn "\\(" { inside = 1 }
        inside { print }
        inside && /^}/ { exit }
    ' "$2"
}

echo "=== syntax ==="
if [ -f "$game" ] && [ -f "$config" ]; then
    echo "  ok    sources present"
else
    echo "  FAIL  missing source"; exit 1
fi

enter=$(body game_enter "$game")
leave=$(body game_leave "$game")
finish=$(body game_finish "$game")

[ -n "$enter" ] && [ -n "$leave" ] && [ -n "$finish" ] || {
    echo "  FAIL  could not extract game_enter/game_leave/game_finish"; exit 1; }

echo ""
echo "=== every state flag set on entry is cleared on the way out ==="
# The flags mean "we did this and owe the undo", so each must be raised in
# game_enter and lowered in game_leave. ai_suspended is the original and the
# pattern the rest follow.
for flag in ai_suspended effects_dropped wallpaper_paused bar_stopped kmod_quieted; do
    check "game_enter sets $flag" "1" \
          "$(printf '%s\n' "$enter" | grep -c "s->game.$flag = 1")"
    check "game_leave clears $flag" "1" \
          "$(printf '%s\n' "$leave" | grep -c "s->game.$flag = 0")"
done

echo ""
echo "=== external processes are restored on compositor shutdown too ==="
# NOT effects_dropped: that is in-process state and synui is exiting. These
# three outlive it.
for flag in ai_suspended wallpaper_paused bar_stopped kmod_quieted; do
    check "game_finish restores $flag" "1" \
          "$(printf '%s\n' "$finish" | grep -c "s->game.$flag = 0")"
done

echo ""
echo "=== each undo runs the matching restore command ==="
# The SPAWN specifically, not any mention: both functions also name the command
# in their log line, and a log line that says a thing happened is precisely what
# must not be mistaken for the thing happening.
check "leave restores the wallpaper" "1" \
      "$(printf '%s\n' "$leave" | grep -c 'synui_spawn(s->config.game_wp_start_cmd)')"
check "leave restarts the bar" "1" \
      "$(printf '%s\n' "$leave" | grep -c 'synui_spawn(s->config.game_bar_start_cmd)')"
check "leave restores kmod events" "1" \
      "$(printf '%s\n' "$leave" | grep -c 'synui_spawn(s->config.game_kmod_restore_cmd)')"
check "finish restores the wallpaper" "1" \
      "$(printf '%s\n' "$finish" | grep -c 'synui_spawn(s->config.game_wp_start_cmd)')"
check "finish restarts the bar" "1" \
      "$(printf '%s\n' "$finish" | grep -c 'synui_spawn(s->config.game_bar_start_cmd)')"
check "finish restores kmod events" "1" \
      "$(printf '%s\n' "$finish" | grep -c 'synui_spawn(s->config.game_kmod_restore_cmd)')"

echo ""
echo "=== the post-process pass is saved, not assumed ==="
# Restoring a hardcoded 1 would switch effects ON for a user who had them off.
check "the prior value is saved" "1" \
      "$(printf '%s\n' "$enter" | grep -c 'effects_saved   = s->config.effects')"
check "the saved value is what comes back" "1" \
      "$(printf '%s\n' "$leave" | grep -c 'config.effects       = s->game.effects_saved')"
# Dropping it when it is already off would make game_leave turn it on.
check "only dropped when it was on" "1" \
      "$(printf '%s\n' "$enter" | grep -c 's->config.effects)')"

echo ""
echo "=== the wallpaper is only paused if one is running ==="
# `synui-wpengine restore` re-applies the SAVED state, so pausing something that
# was not running would START a wallpaper on exit that the user never had.
check "entry checks first" "1" \
      "$(printf '%s\n' "$enter" | grep -c 'game_wpengine_running()')"

echo ""
echo "=== defaults match what each action costs ==="
check "post-process drop is on by default" "1" \
      "$(grep -c 'game_drop_effects    = 1' "$config")"
check "wallpaper pause is on by default" "1" \
      "$(grep -c 'game_pause_wallpaper = 1' "$config")"
# RAM-only with a visible restart, and a near-zero saving that costs synguard
# its event stream. Both stay opt-in.
check "stopping the bar is OFF by default" "1" \
      "$(grep -c 'game_stop_bar        = 0' "$config")"
check "quieting the kmod is OFF by default" "1" \
      "$(grep -c 'game_quiet_kmod      = 0' "$config")"

echo ""
if [ "$fails" -eq 0 ]; then
    echo "all game mode symmetry checks passed"
else
    echo "$fails check(s) failed"
fi
exit $([ "$fails" -eq 0 ] && echo 0 || echo 1)
