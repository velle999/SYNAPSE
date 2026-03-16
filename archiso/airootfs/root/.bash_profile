if [ "$(tty)" = "/dev/tty1" ]; then
    if [ ! -f /var/lib/synapseos/firstboot.done ]; then
        exec /usr/bin/syn-firstboot
    else
        exec /usr/bin/synsh
    fi
fi
