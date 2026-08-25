# SynapseOS environment

# ~/.local/bin on PATH for every login shell, and so for the whole session
# tree greetd exec's below it. It cannot live in ~/.bashrc: that file is read
# only by interactive *bash*, and the programs that need this are not bash —
# synsh looks cliamp up with an execvp-style PATH walk and reports "no music
# player is installed" when it misses, and synui's AI "CMD:" children exec via
# /bin/sh. Guarded so re-sourcing a login shell does not stack duplicates.
case ":${PATH}:" in
    *":$HOME/.local/bin:"*) ;;
    *) export PATH="$HOME/.local/bin:$PATH" ;;
esac

export XDG_SESSION_TYPE=wayland
# Portal backend routing keys off this (synui-portals.conf). Unset, portal falls
# back to GTK, which cannot ScreenCast on wlroots — no screen sharing at all.
export XDG_CURRENT_DESKTOP=synui

# Qt picks its platform theme plugin by matching XDG_CURRENT_DESKTOP against the
# names it knows — and "synui" is not one of them, so it loaded NO theme at all
# and every Qt app ran on Qt's built-in palette: Base #FFFFFF, Text #000000.
#
# That is invisible in most of a KDE app, because Dolphin applies our generated
# colour scheme itself (KColorSchemeManager, via [UiSettings] in dolphinrc) and
# that repaints the chrome, the sidebar and the breadcrumb. What it does NOT
# reach is Dolphin's icon view: KStandardItemListWidget::textColor() reads
# styleOption().palette, which is still the untouched stock palette — so file
# names were drawn in #000000 on the theme's dark view, on EVERY dark theme, and
# no colour key could move them. Setting every foreground in the scheme to pure
# red left them black; that is how thoroughly detached they are.
#
# The palette therefore has to be right BEFORE the app builds its views, which
# only a platform theme can do. xdgdesktopportal is the one to name: the plugin
# is already installed, and it reads org.freedesktop.appearance color-scheme —
# the same dark/light signal synui-apply-theme already writes via gsettings — so
# Qt apps follow a theme switch with no extra plumbing. Qt's file dialogs go
# through the portal as a result, which on wlroots is where they should go.
#
# plasma-integration would give the theme's exact colours instead of a generic
# near-white, but it pulls xdg-desktop-portal-kde, and a second portal backend
# is exactly what the ScreenCast note above warns about.
export QT_QPA_PLATFORMTHEME=xdgdesktopportal

# MangoHud's Vulkan implicit layer keys off MANGOHUD=1, and that one variable
# loads VK_LAYER_MANGOHUD_overlay into EVERY Vulkan client in the session — a
# game, a browser, a video player, a QML app that touched QtMultimedia, a test.
#
# ⛔ SO IT IS NOT EXPORTED HERE ANY MORE. On AMD the layer segfaults the client
# inside its own vkCreateDevice hook and on NVIDIA it never does: it took the
# live wallpaper (synui 409), synstudio (0.1.0-16) and synstudio's own test
# suite down on the ThinkPad while the dev desktop stayed happy, and every fix
# was another DISABLE_MANGOHUD=1 in another launcher.
#
# The hud comes from the launcher instead, which is the only place an overlay
# can be turned on anyway — nothing can inject one into a process already up:
# `syn game steam` (the whole library inherits it), `syn game -- ./game`,
# `syn game -- wine foo.exe`, or synui-game-run directly.
#
# ⚠ AND IT IS ONE LINE TO HAVE THE OLD BEHAVIOUR BACK — `syn game hud on`,
# which is a reasonable choice on a machine that has never seen the crash.
# /etc/synapseos/mangohud.conf carries the whole argument.
for _mh in "${XDG_CONFIG_HOME:-$HOME/.config}/synapseos/mangohud.conf" \
           /etc/synapseos/mangohud.conf; do
    [ -r "$_mh" ] && { . "$_mh"; break; }
done
[ "${MANGOHUD_EVERYWHERE:-0}" = 1 ] && export MANGOHUD=1
unset _mh
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
