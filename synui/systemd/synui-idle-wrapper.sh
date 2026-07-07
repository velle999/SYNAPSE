#!/bin/sh
# Waits for synui to publish its Wayland display, then execs swayidle to
# lock the session and blank outputs after inactivity.
n=0
while [ ! -f /tmp/synui-display ]; do
    sleep 0.5
    n=$((n + 1))
    if [ "$n" -ge 30 ]; then
        echo "synui-idle: timed out waiting for synui" >&2
        exit 1
    fi
done
export WAYLAND_DISPLAY="$(tr -d '[:space:]' < /tmp/synui-display)"

exec swayidle -w \
    timeout 300 'swaylock -f -c 000000' \
    timeout 600 'wlopm --off "*"' \
    resume 'wlopm --on "*"' \
    before-sleep 'swaylock -f -c 000000'
