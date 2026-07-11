# SynapseOS environment
export XDG_SESSION_TYPE=wayland
# Portal backend routing keys off this (synui-portals.conf). Unset, portal falls
# back to GTK, which cannot ScreenCast on wlroots — no screen sharing at all.
export XDG_CURRENT_DESKTOP=synui
# WAYLAND_DISPLAY is set by synui at runtime — do not hard-code it here

# Only set SynapseOS desktop ID when synui is the active compositor;
# KDE/GNOME need their own XDG_CURRENT_DESKTOP to function.
if [ -z "${XDG_CURRENT_DESKTOP:-}" ]; then
    DE=synui
    [ -f /etc/synapseos/desktop.conf ] && . /etc/synapseos/desktop.conf 2>/dev/null
    case "${DE:-synui}" in
        kde)   export XDG_CURRENT_DESKTOP=KDE ;;
        gnome) export XDG_CURRENT_DESKTOP=GNOME ;;
        *)     export XDG_CURRENT_DESKTOP=SynapseOS ;;
    esac
fi

# Pretty prompt for bash (before synsh loads)
PS1='\[\033[38;5;51m\]⚡\[\033[0m\] \[\033[1m\]\u@synapseos\[\033[0m\]:\[\033[38;5;214m\]\w\[\033[0m\]\$ '

# syn CLI shortcuts
alias s='syn'
alias sa='syn ask'
alias sd='syn do'
