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
            # synui.service may already be running (installed system)
            if systemctl is-active --quiet synui.service 2>/dev/null; then
                echo "synui is running via synui.service"
            elif [ -x /usr/bin/synui ]; then
                # Start the unit, the same way the kde/gnome branches above start
                # sddm/gdm. This used to exec synui straight out of the login
                # shell, which meant the live session's compositor was the one
                # thing on the ISO systemd knew nothing about: no
                # Restart=on-failure, no ExecStopPost to hand tty1 back to getty,
                # and none of the unit's environment — its ExecStartPre creates
                # /run/user/0 and runs synui-gfx-env, so the renderer selection
                # for VMs and nouveau was duplicated here by hand instead.
                #
                # The unit is deliberately NOT enabled on the ISO. It
                # Conflicts=getty@tty1.service, so anything that pulls it in at
                # boot stops the getty this wizard needs — and the wizard is the
                # only way to reach "Install to disk". Starting it here, after
                # the choice has been made, is what keeps both.
                #
                # Conflicts= then stops getty@tty1 and takes this shell with it.
                # That is fine and is exactly what starting sddm does: the start
                # job is already queued with systemd and completes on its own.
                systemctl start synui.service || \
                    echo "synui failed — dropping to shell"
            else
                echo "synui not found — dropping to shell"
            fi
            ;;
    esac
fi
