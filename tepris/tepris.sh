#!/usr/bin/env bash
#
# tepris — run the bundled TEPRIS game in an isolated, kiosked Firefox profile.
#
# MOZ_APP_REMOTINGNAME is what makes this a distinct app rather than a browser
# tab: it sets Firefox's Wayland app_id, and synui's dock keys its .desktop and
# icon lookup off app_id (synui/src/icons.c). Without it every Firefox-backed
# app collides on the app_id "firefox".
#
# SynapseOS Project — GPLv2 (packaging); the app itself is MIT.

set -euo pipefail

APP=tepris
APP_ROOT=/usr/share/tepris
PROFILE="${XDG_DATA_HOME:-$HOME/.local/share}/$APP/profile"

mkdir -p "$PROFILE"

# A fresh Firefox profile otherwise opens the onboarding tab and the
# "make me your default browser" prompt on top of the game on first launch.
# The game keeps its high score in localStorage, which lives in this profile.
#
# user.js is fully generated (no user-editable content — the high score lives in
# localStorage/prefs.js), so we rewrite it every launch. That makes new prefs
# self-heal onto profiles seeded by an older launcher instead of being stuck
# with whatever the very first run wrote.
#
# media.autoplay.default=0 / blocking_policy=0: without these Firefox blocks
# audible autoplay until a user gesture, so the game's sounds+BGM stay silent.
# This is a dedicated single-app kiosk profile, so allowing autoplay is safe.
cat >"$PROFILE/user.js" <<'EOF'
user_pref("browser.aboutwelcome.enabled", false);
user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("browser.startup.homepage_override.mstone", "ignore");
user_pref("datareporting.policy.dataSubmissionEnabled", false);
user_pref("toolkit.telemetry.reportingpolicy.firstRun", false);
user_pref("media.autoplay.default", 0);
user_pref("media.autoplay.blocking_policy", 0);
EOF

# --kiosk: fullscreen, no browser chrome. The game binds Space/arrows/Shift,
# so a URL bar in the way would eat keystrokes as much as it would look wrong.
exec env MOZ_APP_REMOTINGNAME="$APP" \
    firefox --no-remote --kiosk --profile "$PROFILE" \
    "file://$APP_ROOT/index.html"
