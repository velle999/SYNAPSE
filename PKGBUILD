# Maintainer: SynapseOS Project <dev@synapseos.dev>
pkgname=synapd
pkgver=0.1.0
pkgrel=1
pkgdesc="SynapseOS AI inference daemon — persistent llama.cpp backend"
arch=('x86_64')
url="https://github.com/synapseos/synapseos"
license=('GPL2')
depends=('glibc' 'llama.cpp' 'systemd-libs')
makedepends=('meson' 'ninja' 'gcc' 'pkg-config')
optdepends=('synapseos-kmod: kernel module for syscall hooks')
backup=('etc/synapd/synapd.conf')
source=("$pkgname-$pkgver.tar.gz::https://github.com/synapseos/synapseos/archive/refs/tags/synapd-$pkgver.tar.gz")
sha256sums=('SKIP')

build() {
    cd "$srcdir/synapd"
    meson setup build \
        --buildtype=release \
        --prefix=/usr \
        -Dc_args="-O2"
    meson compile -C build
}

package() {
    cd "$srcdir/synapd"
    DESTDIR="$pkgdir" meson install -C build

    # Runtime directories
    install -dm750 "$pkgdir/run/synapd"
    install -dm750 "$pkgdir/var/lib/synapd"
    install -dm750 "$pkgdir/var/lib/synapd/models"
    install -dm750 "$pkgdir/var/log/synapd"

    # systemd service
    install -Dm644 systemd/synapd.service \
        "$pkgdir/usr/lib/systemd/system/synapd.service"

    # config
    install -Dm640 config/synapd.conf \
        "$pkgdir/etc/synapd/synapd.conf"
}
