#!/usr/bin/env bash
#
# tepris — run the bundled TEPRIS game in an isolated, kiosked Firefox profile.
#
# MOZ_APP_REMOTINGNAME is what makes this a distinct app rather than a browser
# tab: it sets Firefox's Wayland app_id, and synui's dock keys its .desktop and
# icon lookup off app_id (synui/src/icons.c). Without it every Firefox-backed
# app collides on the app_id "firefox".
#
# SynapseOS Project — packaging under GPL-2.0-or-later; the app itself is MIT.
# SPDX-License-Identifier: GPL-2.0-or-later

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
user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", true);
user_pref("dom.allow_scripts_to_close_windows", true);
user_pref("browser.sessionstore.resume_from_crash", false);
EOF

# We deliberately do NOT use --kiosk. Kiosk mode is permanently fullscreen with
# no way out, which left the game with no working windowed state and no way to
# quit. Instead we run an ordinary window and hide the browser chrome with
# userChrome.css, so the game's own buttons control fullscreen and quitting,
# and synui gives it a normal titlebar.
mkdir -p "$PROFILE/chrome"
cat >"$PROFILE/chrome/userChrome.css" <<'EOF'
/* Single-app profile: hide tab strip and nav bar so the game gets the whole
   window, without kiosk mode's inability to leave fullscreen. */
#TabsToolbar,
#nav-bar,
#navigator-toolbox > toolbar:not(#toolbar-menubar) {
  visibility: collapse !important;
}
EOF

# The #app fragment tells the page it is running as a dedicated window, which
# is what enables its quit button (see CAN_CLOSE_WINDOW in tepris.js).
exec env MOZ_APP_REMOTINGNAME="$APP" \
    firefox --no-remote --new-window --profile "$PROFILE" \
    "file://$APP_ROOT/index.html#app"
