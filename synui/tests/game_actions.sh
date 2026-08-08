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
    # The return type is matched loosely rather than listed: it started as
    # (void|int), which silently matched NOTHING once the functions worth
    # checking returned syn_view_t * and syn_output_t *. A body() that finds no
    # body makes every grep against it count 0, so the checks would have failed
    # loudly here — but a *new* check written against a pointer-returning
    # function would look like a real defect instead of a broken matcher.
    awk -v fn="$1" '
        $0 ~ "^(static )?[A-Za-z_][A-Za-z0-9_]* \\*?" fn "\\(" { inside = 1 }
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
echo "=== every action is reachable and wired from the panel ==="
# Three things have to agree for a toggle to work, and nothing links them:
#   the panel row's .key  ->  the string config_parse_kv matches  ->  the field
# settings.state stores the row's .key and replays it through config_parse_kv on
# every reload, so a row whose .key does not match the parser toggles, saves,
# and then silently does nothing the next time synui starts. The row would look
# right the whole time.
panel=${3:-}
if [ -n "$panel" ] && [ -f "$panel" ]; then
    for key in game_drop_effects game_pause_wallpaper game_stop_bar game_quiet_kmod \
               game_output; do
        check "panel has a row for $key" "1" \
              "$(grep -c "\.key = \"$key\"" "$panel")"
        check "that row points at the $key field" "1" \
              "$(grep -c "CFG($key)" "$panel")"
        check "config_parse_kv knows $key" "1" \
              "$(grep -c "strcmp(key, \"$key\")" "$config")"
    done
else
    echo "  skip  panel wiring (no ctlpanel.c passed)"
fi

echo ""
echo "=== there is ONE definition of \"this is a game\" ==="
# The detector and the fullscreen placement both have to answer it, and if they
# answer it separately they will drift: a window sent to the main screen that
# then does not trigger game mode (or the reverse) is worse than either bug
# alone, and neither half looks wrong on its own.
check "game_find_view asks the shared predicate" "1" \
      "$(body game_find_view "$game" | grep -c 'game_view_is_game(s, v)')"
check "game_output_for asks the same predicate" "1" \
      "$(body game_output_for "$game" | grep -c 'game_view_is_game(s, view)')"
# ...and the predicate must not decide on is_xwayland alone, which is the hole
# gamescope fell through.
check "the predicate handles Wayland-native clients" "1" \
      "$(body game_view_is_game "$game" | grep -c 'if (!view->is_xwayland)')"
check "gamescope is a game wrapper by default" "1" \
      "$(grep -c '"gamescope"' "$config")"

layout=${4:-}
if [ -n "$layout" ] && [ -f "$layout" ]; then
    # A game's fullscreen output is the compositor's call. The clients cannot
    # be told: an X11 game follows RandR order and gamescope on the Wayland
    # backend ignores --prefer-output AND the X11 primary flag.
    check "fullscreen placement asks game mode first" "1" \
          "$(body fullscreen_target_output "$layout" | grep -c 'game_output_for(s, view)')"
else
    echo "  skip  fullscreen placement (no layout.c passed)"
fi

echo ""
echo "=== games open on the main screen unless told otherwise ==="
# Anchored at the defaults function's indent: the `primary` arm of the parser
# assigns the same constant, so an unanchored match counts two and would go on
# counting two if the default were deleted outright.
check "game_output defaults to primary" "1" \
      "$(grep -c '^    cfg->game_output = GAME_OUT_PRIMARY;' "$config")"
# The enum round-trip, which is the three-things rule in its nastiest form: the
# panel writes an ENUM as its display name folded to lower case, so every name
# in the row's list has to be a spelling config_parse_kv accepts. A name that
# is not parses back as "unrecognised", and the row silently reverts at the
# next login having looked correct all session.
if [ -n "$panel" ] && [ -f "$panel" ]; then
    names=$(grep 'ctl_names_game_output\[\]' "$panel" |
            sed 's/.*{//; s/}.*//; s/[",]/ /g' | tr 'A-Z' 'a-z')
    [ -n "$names" ] || { echo "  FAIL  no ctl_names_game_output[]"; fails=$((fails + 1)); }
    for n in $names; do
        check "config_parse_kv accepts game_output = $n" "1" \
              "$(grep -c "strcmp(val, \"$n\")" "$config")"
    done
fi

echo ""
if [ "$fails" -eq 0 ]; then
    echo "all game mode symmetry checks passed"
else
    echo "$fails check(s) failed"
fi
exit $([ "$fails" -eq 0 ] && echo 0 || echo 1)
