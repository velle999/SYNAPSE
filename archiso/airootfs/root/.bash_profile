if [ "$(tty)" = "/dev/tty1" ]; then
    if [ ! -f /var/lib/synguard/.firstboot_done ]; then
        exec /usr/bin/syn-firstboot
    else
        exec /usr/bin/synui
    fi
fi
