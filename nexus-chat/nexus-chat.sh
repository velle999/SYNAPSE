#!/usr/bin/env bash
#
# nexus-chat — run the bundled NEXUS P2P app in an isolated Firefox profile.
#
# MOZ_APP_REMOTINGNAME is what makes this a distinct app rather than a browser
# tab: it sets Firefox's Wayland app_id, and synui's dock keys its .desktop and
# icon lookup off app_id (synui/src/icons.c). Without it every Firefox-backed
# app collides on the app_id "firefox".
#
# SynapseOS Project — packaging under GPL-2.0-or-later; the app itself is MIT.
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

APP=nexus-chat
APP_ROOT=/usr/share/nexus-chat
PROFILE="${XDG_DATA_HOME:-$HOME/.local/share}/$APP/profile"

mkdir -p "$PROFILE"

# A fresh Firefox profile otherwise opens the onboarding tab and the
# "make me your default browser" prompt on top of the app on first launch.
if [ ! -e "$PROFILE/user.js" ]; then
    cat >"$PROFILE/user.js" <<'EOF'
user_pref("browser.aboutwelcome.enabled", false);
user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("browser.startup.homepage_override.mstone", "ignore");
user_pref("datareporting.policy.dataSubmissionEnabled", false);
user_pref("toolkit.telemetry.reportingpolicy.firstRun", false);
EOF
fi

# An invite fragment ("#room=ABC123&s=...") preloads a room. Anything else is
# ignored rather than opened, so this can't be turned into a generic URL opener
# by whatever ends up invoking it.
url="file://$APP_ROOT/index.html"
if [ $# -gt 0 ]; then
    case "$1" in
        '#'*) url="$url$1" ;;
    esac
fi

exec env MOZ_APP_REMOTINGNAME="$APP" \
    firefox --no-remote --profile "$PROFILE" "$url"
