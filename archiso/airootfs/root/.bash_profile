if [ "$(tty)" = "/dev/tty1" ]; then
    if [ ! -f /var/lib/synapseos/firstboot.done ]; then
        /usr/bin/syn-firstboot || echo "syn-firstboot failed — dropping to shell"
        # After firstboot, fall through to start the chosen DE
    fi

    # Read DE choice (set by firstboot wizard or installer)
    DE=synui
    [ -f /etc/synapseos/desktop.conf ] && . /etc/synapseos/desktop.conf 2>/dev/null

    case "${DE:-synui}" in
        kde)   systemctl start sddm.service || echo "sddm failed — dropping to shell" ;;
        gnome) systemctl start gdm.service  || echo "gdm failed — dropping to shell" ;;
        tty)   ;; # fall through to normal shell
        *)
            if [ -x /usr/bin/synui ]; then
                /usr/bin/synui || echo "synui exited ($?) — dropping to shell"
            else
                echo "synui not found — dropping to shell"
            fi
            ;;
    esac
fi
