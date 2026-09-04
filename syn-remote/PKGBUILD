# Maintainer: Velle Sinclair <brncomputerhelp@gmail.com>
#
# syn-remote — the desktop, from somewhere else.
#
# A thin wrapper over wayvnc, which is the wlroots-native VNC server. synui
# implements zwlr_screencopy_manager_v1 for the picture and
# zwp_virtual_pointer_manager_v1 / zwp_virtual_keyboard_manager_v1 for the
# control, and hands all three to any native client — so this needs no portal
# and prompts nobody.
#
# ⛔ WHICH IS WHY "WAYLAND CANNOT DO REMOTE DESKTOP" DOES NOT APPLY HERE, and
# it is worth saying in the package because it is the first thing anybody says.
# The three real reasons that is true elsewhere are all about other stacks:
# GNOME and KDE gate capture behind a portal that asks a human per session;
# xdg-desktop-portal-wlr implements ScreenCast but NOT RemoteDesktop, so
# portal-based tools can watch a wlroots desktop and cannot touch it; and
# nothing exists to connect to before somebody logs in. Only the last is true
# here, and `syn-remote status` says so rather than failing obscurely.
#
# ⛔ THE PART THAT IS NOT wayvnc: A BLANKED OUTPUT CANNOT BE CAPTURED. Measured
# on synui — once the idle blank stage has fired, screencopy answers "failed to
# copy output" and a viewer gets nothing. power_blank_timeout defaults to 600,
# so an unattended machine goes dark to a viewer ten minutes after the last
# keypress and stays dark. syn-remote turns the outputs back on when somebody
# connects and holds a real idle inhibitor while they are there.
#
# 1: first release. Loopback by default, TLS and a password always, `listen
#   lan` the deliberate way to put it on the network — ⚠ synnet's default-drop
#   input chain accepts everything from 10/8, 172.16/12 and 192.168/16, so
#   binding to 0.0.0.0 really does mean the whole LAN, and there is no second
#   door to unlock afterwards.
pkgname=syn-remote
pkgver=0.1.0
# 2: the connection count reads ZERO unless the server is actually running,
#   whatever the state file says. synui's bar reads that count now (596), and a
#   state file outliving the thing that wrote it would leave an indicator
#   claiming somebody is watching this screen over a server that is not there —
#   worse than no indicator at all, and the failure synui's Recording.qml has a
#   paragraph about avoiding. ⚠ It narrows the window rather than closing it: a
#   watcher that died under a live wayvnc could still over-report until the unit
#   restarts.
pkgrel=2
pkgdesc="Remote desktop for SynapseOS — wayvnc, with the screen woken and held awake while somebody is connected"
arch=('any')
url="https://github.com/velle999/SYNAPSE"
license=('GPL-2.0-or-later')

# ⛔ wayvnc IS the server, so it is a real dependency and not an optdepend. The
# wrapper alone serves nothing, and a launcher that installs and then says
# "install the thing that does the work" is a package that does not work.
#
# wlopm is how a blanked output is turned back on (zwlr_output_power_management
# — synui implements it). openssl makes the self-signed certificate, once.
depends=('bash' 'wayvnc' 'wlopm' 'openssl' 'systemd')
# ⚠ synui ships /usr/lib/synui/synui-idle-inhibit, which is what holds the
# machine awake. Optional rather than required so this still installs on a
# SynapseOS built without the compositor — the screen is still woken, it just
# is not held, and the wrapper checks for the file rather than assuming it.
optdepends=('synui: hold the machine awake while somebody is connected'
            'openssh: reach a loopback-bound server from another machine')

source=("$pkgname-$pkgver.tar.gz::https://github.com/velle999/$pkgname/releases/download/$pkgver-$pkgrel/$pkgname-$pkgver.tar.gz")
sha256sums=('SKIP')

package() {
    cd "$srcdir/$pkgname-$pkgver"

    install -Dm755 syn-remote.sh "$pkgdir/usr/bin/syn-remote"

    # ⚠ A USER UNIT. On an installed system greetd starts synui as the user and
    # the Wayland socket is in that user's runtime directory; a system unit
    # would be looking in /run/user/0 for a session that is not there.
    install -Dm644 syn-remote.service \
        "$pkgdir/usr/lib/systemd/user/syn-remote.service"

    # ⛔ NOT ENABLED HERE, and not by a scriptlet either. A package that
    # installs a remote desktop and switches it on is a package that opens a
    # machine somebody did not ask to open. `syn-remote on` is the whole
    # opt-in, and it is one command.
}
