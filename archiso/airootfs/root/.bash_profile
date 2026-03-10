# SynapseOS — auto-start synsh on tty1
if [ "$(tty)" = "/dev/tty1" ]; then
    exec /usr/bin/synsh
fi
