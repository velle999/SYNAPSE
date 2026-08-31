# Maintainer: Velle Sinclair <brncomputerhelp@gmail.com>
#
# syn-gfn — GeForce NOW as a dedicated web app.
#
# ⛔ NO BROWSER IN depends, AND THAT IS DELIBERATE. Nothing here needs one to
# install, the launcher names what is missing and how to get it, and pulling a
# whole browser into the ISO for a service not everybody uses is the wrong
# trade. It picks the first Chromium-family browser it finds at runtime — see
# `syn-gfn --list-browsers`.
#
# ⚠ FIREFOX IS NOT A FALLBACK — AND NO LONGER FOR THE OLD REASON. GeForce NOW
# stopped refusing Gecko on 2026-08-19, when NVIDIA and Mozilla shipped
# official support in Firefox 154 — for WINDOWS. On Linux, Firefox loads the
# site, lists the library and starts no game; it also has no Keyboard Lock API
# (measured on 154.0.1: navigator.keyboard is absent), so Escape would not
# reach a game even if one started. The launcher knows Firefox by name, says
# exactly that, and opens it on request for browsing the catalogue.
# GECKO_CAN_STREAM in syn-gfn.sh is the one line to change when this reaches
# Linux.
pkgname=syn-gfn
pkgver=0.1.0
pkgrel=4
pkgdesc="GeForce NOW in a browser that can hold the mouse — cloud gaming for SynapseOS"
arch=('any')
url="https://github.com/velle999/SYNAPSE"
license=('GPL-2.0-or-later')
depends=('bash')
# python3 writes the three site permissions (keyboard lock, pointer lock,
# automatic fullscreen) into the profile before the first launch. Without it
# the app still runs — the browser simply asks, and a prompt raised while the
# page is full screen with the pointer captured is a prompt nobody can see.
optdepends=('python: pre-grant the keyboard/pointer-lock permissions'
            'chromium: the browser to stream in'
            'vivaldi: the browser to stream in'
            'firefox: browse the catalogue (cannot stream on Linux yet)')
# ⛔ ONE TARBALL, NOT A LIST OF LOOSE FILES.
#
# Every file this package installs used to be named here individually, which
# builds perfectly from a checkout and cannot be published: there is nothing to
# attach to a release and nothing for an outsider's makepkg to fetch. The
# tarball collect-source.sh assembles is that one thing, and the URL after `::`
# is where everybody who is not us gets it. The filename BEFORE `::` is what
# makepkg looks for on disk first, so a build from this tree still uses the
# tarball build-all.sh just collected and never downloads.
#
# ⚠ AND package() NOW WORKS INSIDE THE EXTRACTED DIRECTORY. The files arrive at
# $srcdir/$pkgname-$pkgver/ rather than loose in $srcdir.
#
# ⛔ sha256sums STAYS 'SKIP' — a real checksum breaks every local build the
# moment the tree changes, which is every build that matters here.
source=("$pkgname-$pkgver.tar.gz::https://github.com/velle999/$pkgname/releases/download/$pkgver-$pkgrel/$pkgname-$pkgver.tar.gz")
sha256sums=('SKIP')

package() {
    cd "$srcdir/$pkgname-$pkgver"

    install -Dm755 syn-gfn.sh      "$pkgdir/usr/bin/syn-gfn"
    install -Dm644 syn-gfn.desktop "$pkgdir/usr/share/applications/syn-gfn.desktop"

    # Scalable only, into hicolor: the dock and the menu ask for whatever size
    # they draw at, and hicolor is the theme every other icon theme inherits
    # from, so the mark survives a theme switch.
    install -Dm644 syn-gfn.svg \
        "$pkgdir/usr/share/icons/hicolor/scalable/apps/syn-gfn.svg"
}
