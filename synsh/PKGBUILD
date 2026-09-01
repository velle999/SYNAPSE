# Maintainer: SynapseOS Project <dev@synapseos.dev>
pkgname=synsh
pkgver=0.1.0
pkgrel=27
pkgdesc="SynapseOS natural language shell — AI-native command interface, in 14 languages"
arch=('x86_64')
url="https://github.com/velle999/SYNAPSE"
license=('GPL-2.0-or-later')
depends=('glibc' 'readline')
makedepends=('meson' 'ninja' 'gcc')
optdepends=('synapd: AI translation backend')
backup=('etc/synsh/synshrc')
# ── Where the source comes from, here and everywhere else ──────────────────
#
# ⛔ ONE source LINE SERVES BOTH, AND THAT IS DELIBERATE. build-all.sh runs
# tools/collect-source.sh, which drops $pkgname-$pkgver.tar.gz beside this file;
# makepkg finds it (`-> Found ...`) and never touches the URL. Anybody WITHOUT
# this checkout has no such file, so makepkg fetches the identical tarball from
# the release that carries this exact pkgver-pkgrel. A second PKGBUILD for
# outside use would be a second set of depends and install rules, free to drift
# from this one — and the person it broke for could not see this file at all.
#
# ⚠ ITS OWN REPOSITORY, NOT THIS ONE. The source release lives at
# github.com/velle999/$pkgname — which is also where the PKGBUILD is published
# as a clonable package repo — because putting them on SYNAPSE's releases page
# buried the ISO downloads under a component tarball per bump, and made the
# newest of those GitHub's "Latest release" for the whole project.
#
# ⚠ THE TAG CARRIES THE pkgrel, so the URL cannot point at the wrong source.
# preflight.sh already refuses a source edit that does not bump pkgrel, which
# means every change to what gets built moves this URL with it.
#
# ⛔ AND sha256sums STAYS 'SKIP'. A real checksum would break every LOCAL build
# the moment somebody edited a source file, because the tarball beside this file
# is regenerated from the working tree and would no longer match. The published
# asset is reproducible instead — collect-source.sh sorts and zeroes the
# timestamps, so `tools/collect-source.sh <name>` at the tagged commit
# re-derives it byte for byte. packaging/README.md has the whole of it.
source=("$pkgname-$pkgver.tar.gz::https://github.com/velle999/$pkgname/releases/download/$pkgver-$pkgrel/$pkgname-$pkgver.tar.gz")
sha256sums=('SKIP')

build() {
    cd "$srcdir/synsh-0.1.0"
    meson setup build --prefix=/usr --buildtype=release
    meson compile -C build
}

package() {
    cd "$srcdir/synsh-0.1.0"
    meson install -C build --destdir="$pkgdir"
    install -dm755 "$pkgdir/etc/synsh"
    install -Dm644 config/synshrc "$pkgdir/etc/synsh/synshrc" 2>/dev/null || touch "$pkgdir/etc/synsh/synshrc"
}
