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
# 3: the other half — saved connections, a remembered password for each, and a
#   viewer of our own to spend it. The viewer is not preference: wayvnc
#   authenticates over VeNCrypt and no packaged VNC client can be handed a
#   username or a password without somebody typing it, so a manager wrapping one
#   would remember a password it could never use. gtk-vnc's credential callback
#   is the seam. ⚠ Where a password LIVES is decided in the script alone — a
#   keyring if one answers, a 0600 file if not — because secret-tool exits 0
#   with no keyring running, and only reading the write back tells those apart.
# 4: nothing could connect, and every layer looked healthy. Three faults, all
#   in the TLS handshake and all silent:
#
#   ⛔ THE CERTIFICATE NAMED A HOST NOBODY DIALS. `hostname` is not installed
#     on SynapseOS, so `$(hostname || echo synapseos)` always fell to the
#     literal, and there was no subjectAltName at all — so a client validating
#     against the IP it connected to could never match. Measured: 127.0.0.1 and
#     192.168.40.153 both failed "IP address mismatch"; only "synapseos"
#     passed. Now CN comes from `uname -n` and the SANs carry every name and
#     address, re-issued when the machine's address moves.
#   ⛔ THE VIEWER INVERTED vnc_display_set_credential. It returns non-zero on
#     FAILURE despite being declared gboolean; `if (!...)` turned every success
#     into an abort, before the certificate was ever sent.
#   ⛔ AND IT NEVER ANSWERED CA_CERT_DATA, which wayvnc's only supported
#     security type requires.
#
#   All three produced the same symptom and no client-side error: "Client
#   handshake timed out" in the SERVER's journal, which reads like a firewall.
# 5: it was switched on and it was not running, and Connect did nothing.
#   Two faults, unrelated except that each one is invisible from the side you
#   are standing on.
#
#   ⛔ THE UNIT LOST A RACE IT WAS NEVER GOING TO WIN, PERMANENTLY. The user
#     manager reaches default.target BEFORE the compositor exists — measured on
#     an installed system, `systemd --user` reached Main User Target at
#     19:40:50 with no session yet handed to it — so `run` found no
#     synui-display and died. With Restart=on-failure/RestartSec=3 against
#     systemd's DEFAULT start limit of five starts in ten seconds, every retry
#     was spent inside the race and the unit then gave up FOR THE WHOLE LOGIN.
#     ⚠ And the user manager outlives the session, so default.target is never
#     reached twice and a later login cannot re-trigger it either. `run` now
#     waits for the session instead of dying, the start limit is gone, and
#     Restart=always so the end of one session is picked up as the start of the
#     next. `syn-remote on` appeared to be the fix only because `enable --now`
#     starts the unit at a moment the compositor is already up.
#   ⛔ AND CONNECT WAS A SILENT NO-OP ON AN UNCHECKED CERTIFICATE. `connect`
#     asks before it pins, asking needs a tty, and the window ran it without
#     one — so it took its `[ -t 0 ]` else-branch, printed "run: syn-remote
#     trust <name>" on stderr and exited. actProc collected stdout only, so the
#     message went nowhere: the button did nothing, and the only trace on
#     either machine was "Client handshake timed out" in the SERVER's journal,
#     which reads like a firewall and is not one. Connect now goes through
#     syntty when the host is unpinned, exactly as the certificate and password
#     buttons already did, and actProc collects stderr so no button can ever
#     fail invisibly again.
# 6: the certificate could only ever vouch for addresses this machine HOLDS,
#   which is exactly the set that is wrong when it is reached through anything
#   else. cert_sans() enumerates `ip -4 -o addr show scope global`, so a public
#   IP in front of a port forward — or the dynamic-DNS name that resolves to it
#   — was never in there and could not be. Measured against a live server:
#   dialling 203.0.113.7 fails "IP address mismatch" and a DDNS name fails
#   "Hostname mismatch", while the LAN address passes. `syn-remote names
#   add|remove` is the escape hatch.
#   ⚠ ADDING ONE RE-ISSUES, because a name that is saved and not in the
#     certificate is the split this package already has a paragraph about. And
#     REMOVING one re-issues too — the check asks whether every WANTED name is
#     present, and a name that is no longer wanted is still present quite
#     happily.
#   ⛔ THE SAN LOOKUP IS ANCHORED. openssl prints the SANs on ONE
#     comma-separated line, so a plain grep for "DNS:synapse" is satisfied by
#     "DNS:synapse.local" — get it wrong and every run decides a present name
#     is missing and re-issues, silently breaking every client that pinned the
#     last certificate.
#   ⛔ AND THE VALUE IS VALIDATED. It reaches openssl inside -addext, where a
#     comma forges a second SAN.
pkgrel=6
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
# ⛔ gtk-vnc IS THE VIEWER, and the viewer is why this package has one at all.
# wayvnc authenticates over VeNCrypt — a username and a password inside the TLS
# session — and no packaged client can be handed either without a human typing
# it: TigerVNC's -passwd file is for classic VncAuth and vncviewer(1) documents
# no way to pass a username, and gtk-vnc's own gvncviewer example builds a
# dialog. gtk-vnc's credential API is the one seam where a REMEMBERED password
# can answer the server, which is the whole of `syn-remote saved`.
# ⚠ python IS the certificate fetcher and not an optional nicety. VNC does not
# start in TLS — the RFB banner, the security-type list and the VeNCrypt subtype
# are all in the clear first — so `openssl s_client`, which speaks TLS from the
# first byte, cannot reach the certificate at all. Trust-on-first-use needs
# something that can do the preamble; that is syn-remote-getcert.
depends=('bash' 'wayvnc' 'wlopm' 'openssl' 'systemd' 'gtk-vnc' 'gtk3' 'python')
# ⚠ synui ships /usr/lib/synui/synui-idle-inhibit, which is what holds the
# machine awake. Optional rather than required so this still installs on a
# SynapseOS built without the compositor — the screen is still woken, it just
# is not held, and the wrapper checks for the file rather than assuming it.
optdepends=('synui: hold the machine awake while somebody is connected'
            'openssh: reach a loopback-bound server from another machine'
            'quickshell: the Remote Desktop window — the CLI needs none of it'
            'syntty: where `saved <name> set` asks for a password'
            'libsecret: keep saved passwords in a keyring rather than a file')

makedepends=('pkgconf' 'gcc')

source=("$pkgname-$pkgver.tar.gz::https://github.com/velle999/$pkgname/releases/download/$pkgver-$pkgrel/$pkgname-$pkgver.tar.gz")
sha256sums=('SKIP')

# ⚠ ONE FILE, ONE gcc, NO meson. This is a script package with a single C
# program in it; a build system for one translation unit is more machinery than
# the thing it builds. -Wno-deprecated-declarations is for GValueArray, which
# GLib deprecated and gtk-vnc still marshals `vnc-auth-credential` with — the
# accessor the library's own example uses.
build() {
    cd "$srcdir/$pkgname-$pkgver"
    gcc -O2 -Wall -Wextra -Wno-deprecated-declarations \
        $(pkg-config --cflags gtk+-3.0 gtk-vnc-2.0) \
        -o syn-remote-view syn-remote-view.c \
        $(pkg-config --libs gtk+-3.0 gtk-vnc-2.0)
}

package() {
    cd "$srcdir/$pkgname-$pkgver"

    install -Dm755 syn-remote.sh "$pkgdir/usr/bin/syn-remote"

    # ⚠ A USER UNIT. On an installed system greetd starts synui as the user and
    # the Wayland socket is in that user's runtime directory; a system unit
    # would be looking in /run/user/0 for a session that is not there.
    install -Dm644 syn-remote.service \
        "$pkgdir/usr/lib/systemd/user/syn-remote.service"

    install -Dm755 syn-remote-gui.sh "$pkgdir/usr/bin/syn-remote-gui"
    install -Dm644 shell.qml "$pkgdir/usr/share/syn-remote/shell.qml"
    install -Dm644 syn-remote.desktop \
        "$pkgdir/usr/share/applications/syn-remote.desktop"

    # ⚠ /usr/lib, NOT /usr/bin. The viewer takes its password on stdin from
    # `syn-remote connect` and is not a thing to run by hand — a copy in the
    # PATH is an invitation to pass a password as an argument, which is exactly
    # what stdin is there to avoid.
    install -Dm755 syn-remote-view "$pkgdir/usr/lib/syn-remote/syn-remote-view"
    install -Dm755 syn-remote-getcert.py \
        "$pkgdir/usr/lib/syn-remote/syn-remote-getcert"

    # ⛔ NOT ENABLED HERE, and not by a scriptlet either. A package that
    # installs a remote desktop and switches it on is a package that opens a
    # machine somebody did not ask to open. `syn-remote on` is the whole
    # opt-in, and it is one command.
}
