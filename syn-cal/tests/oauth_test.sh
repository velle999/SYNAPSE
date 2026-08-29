#!/usr/bin/env bash
#
# oauth_test.sh — plays the browser, so the whole flow can run with no account.
#
# oauth_test prints the authorisation URL on stdout and then blocks on its
# loopback listener. This reads the URL, pulls the redirect_uri and the state
# out of it, and fetches the redirect exactly as a browser would.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

BIN=${1:-./build/oauth_test}
HERE=$(cd "$(dirname "$0")" && pwd)
[ -x "$BIN" ] || { echo "not executable: $BIN" >&2; exit 1; }

T=$(mktemp -d); FAKE=""; APP=""
cleanup() {
    [ -n "$FAKE" ] && kill "$FAKE" 2>/dev/null
    [ -n "$APP" ] && kill "$APP" 2>/dev/null
    rm -rf "$T"
}
trap cleanup EXIT

# The fake provider prints the port it landed on.
python3 "$HERE/fake_oauth.py" > "$T/port" 2>"$T/fake.err" &
FAKE=$!
for i in $(seq 1 100); do
    [ -s "$T/port" ] && break
    kill -0 "$FAKE" 2>/dev/null || { echo "  FAIL  fake provider died"; cat "$T/fake.err"; exit 1; }
    sleep 0.05
done
TOKEN_URL="http://127.0.0.1:$(cat "$T/port")/token"

"$BIN" "$TOKEN_URL" > "$T/url" 2>"$T/out" &
APP=$!

# ⛔ WAIT FOR THE URL, NOT FOR A SLEEP. The listener is not up until the process
# has printed, and racing it makes this fail on a loaded machine only.
URL=""
for i in $(seq 1 200); do
    URL=$(head -1 "$T/url" 2>/dev/null)
    [ -n "$URL" ] && break
    kill -0 "$APP" 2>/dev/null || break
    sleep 0.05
done

if [ -z "$URL" ]; then
    echo "  FAIL  the client never printed an authorisation URL"
    cat "$T/out"
    exit 1
fi

qs=${URL#*\?}
field() { echo "$qs" | tr '&' '\n' | sed -n "s/^$1=//p" | head -1; }
urldec() { python3 -c 'import sys,urllib.parse;print(urllib.parse.unquote(sys.argv[1]))' "$1"; }

REDIR=$(urldec "$(field redirect_uri)")
STATE=$(field state)

# The three things the URL must carry, checked here because this is where the
# URL actually is.
echo "$qs" | grep -q "code_challenge_method=S256" \
  && echo "  ok    the authorisation URL asks for S256, not plain" \
  || { echo "  FAIL  the URL does not request S256"; exit 1; }
echo "$qs" | grep -q "access_type=offline" \
  && echo "  ok    …and for offline access, or no refresh token ever arrives" \
  || { echo "  FAIL  the URL does not ask for offline access"; exit 1; }
case "$REDIR" in
  http://127.0.0.1:*) echo "  ok    …and redirects to the loopback, not to a host" ;;
  *) echo "  FAIL  redirect_uri is $REDIR"; exit 1 ;;
esac

# Play the browser.
curl -s -o /dev/null --max-time 10 "${REDIR}?code=fake-code-123&state=${STATE}"

wait "$APP"; rc=$?
APP=""
cat "$T/out"
exit $rc
