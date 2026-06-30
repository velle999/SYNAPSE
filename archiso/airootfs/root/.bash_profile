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
                # Ensure runtime dir exists with correct permissions
                export XDG_RUNTIME_DIR="/run/user/$(id -u)"
                mkdir -p "$XDG_RUNTIME_DIR"
                chmod 700 "$XDG_RUNTIME_DIR"
                export XDG_SESSION_TYPE=wayland
                export LIBSEAT_BACKEND=seatd

                # VM detection — force software rendering
                if [ -f /sys/class/dmi/id/sys_vendor ] && \
                   grep -qiE 'VirtualBox|VMware|QEMU|KVM|Xen|innotek' /sys/class/dmi/id/sys_vendor 2>/dev/null; then
                    export WLR_RENDERER=pixman
                    export WLR_NO_HARDWARE_CURSORS=1
                fi

                exec /usr/bin/synui
            else
                echo "synui not found — dropping to shell"
            fi
            ;;
    esac
fi
