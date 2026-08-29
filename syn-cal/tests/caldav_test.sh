#!/usr/bin/env bash
#
# caldav_test.sh — start a real CalDAV server, point the client at it, stop it.
#
# ⚠ SKIPS RATHER THAN FAILS WHEN radicale IS ABSENT. It is a test-only
# dependency and not everyone building this has it; a suite that goes red on a
# machine with nothing wrong with it is a suite people learn to ignore.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

BIN=${1:-./build/caldav_test}
[ -x "$BIN" ] || { echo "not executable: $BIN" >&2; exit 1; }

if ! command -v radicale >/dev/null 2>&1; then
    echo "  skip  radicale is not installed, cannot test against a real server"
    exit 0
fi

T=$(mktemp -d)
PORT=0
PID=""

cleanup() {
    [ -n "$PID" ] && kill "$PID" 2>/dev/null
    [ -n "$PID" ] && wait "$PID" 2>/dev/null
    rm -rf "$T"
}
trap cleanup EXIT

# A free port, chosen by the kernel and then released. There is a race between
# releasing it and radicale binding it, and it is the least-bad option: a fixed
# port collides with whatever else is on this machine, and two of these suites
# running at once collide with each other.
PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1]);s.close()')

mkdir -p "$T/collections"

# ⛔ REAL BASIC AUTH, NOT --auth-type=none. Radicale's default rights are
# owner_only, and an anonymous request has no owner — so PROPFIND on the root
# answers 401 and discovery cannot start. Handing it a user is also the more
# useful test: it is the credential path Nextcloud, Fastmail and iCloud all use,
# and with anonymous access none of it is exercised.
printf 'tester:secret\n' > "$T/users"

radicale --storage-filesystem-folder="$T/collections" \
         --server-hosts="127.0.0.1:$PORT" \
         --auth-type=htpasswd \
         --auth-htpasswd-filename="$T/users" \
         --auth-htpasswd-encryption=plain \
         --logging-level=error >"$T/radicale.log" 2>&1 &
PID=$!

# ⛔ WAIT FOR THE PORT, NOT FOR A FIXED SLEEP. A sleep long enough to be safe on
# a loaded machine is a second wasted on every run, and one short enough not to
# be is a suite that fails at random on somebody else's laptop.
for i in $(seq 1 100); do
    if curl -s -o /dev/null --max-time 1 -u tester:secret "http://127.0.0.1:$PORT/"; then break; fi
    if ! kill -0 "$PID" 2>/dev/null; then
        echo "  FAIL  radicale exited before it listened:" >&2
        tail -5 "$T/radicale.log" >&2
        exit 1
    fi
    sleep 0.1
done

echo "radicale on 127.0.0.1:$PORT"
"$BIN" "http://127.0.0.1:$PORT"
rc=$?

if [ $rc -ne 0 ]; then
    echo "--- radicale log ---" >&2
    tail -20 "$T/radicale.log" >&2
fi
exit $rc
