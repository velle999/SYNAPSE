pkgname=syn-model
pkgver=0.1.0
pkgrel=6
pkgdesc="SynapseOS model manager — download and manage LLM models"
arch=('any')
license=('GPL-2.0-or-later')
# polkit: the rules.d file below is what lets synui's model picker start a
#   download without an authentication agent it does not have.
depends=('curl' 'systemd' 'polkit')
# Flat, because makepkg resolves a local source by its BASENAME — a path with a
# directory in it is looked for in the build dir and reported missing.
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
    install -Dm755 "$srcdir/$pkgname-$pkgver/syn-model.sh" "$pkgdir/usr/bin/syn-model"

    install -Dm644 "$srcdir/$pkgname-$pkgver/syn-model-download@.service" \
        "$pkgdir/usr/lib/systemd/system/syn-model-download@.service"
    install -Dm644 "$srcdir/$pkgname-$pkgver/syn-model-delete@.service" \
        "$pkgdir/usr/lib/systemd/system/syn-model-delete@.service"
    # sysusers BEFORE tmpfiles is pacman's own hook order (20- then 21-), which
    # is the whole reason this file exists: the tmpfiles below names group
    # `synapse` and used to be installed on machines where nothing had created
    # it yet.
    install -Dm644 "$srcdir/$pkgname-$pkgver/syn-model-sysusers.conf" \
        "$pkgdir/usr/lib/sysusers.d/syn-model.conf"
    install -Dm644 "$srcdir/$pkgname-$pkgver/syn-model-tmpfiles.conf" \
        "$pkgdir/usr/lib/tmpfiles.d/syn-model.conf"
    install -Dm644 "$srcdir/$pkgname-$pkgver/49-syn-model-download.rules" \
        "$pkgdir/usr/share/polkit-1/rules.d/49-syn-model-download.rules"
}
