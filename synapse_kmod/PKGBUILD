# Maintainer: SynapseOS Project <dev@synapseos.dev>
# PKGBUILD — synapse_kmod (SynapseOS Kernel Module)
#
# Builds against the currently running kernel.
# For the SynapseOS kernel, replace $(uname -r) with the
# SynapseOS kernel package dependency.

pkgname=synapse_kmod
pkgver=0.1.0
pkgrel=1
pkgdesc="SynapseOS kernel module — AI_CTX syscalls, kprobes, AI scheduler bridge"
arch=('x86_64' 'aarch64')
url="https://github.com/synapseos/synapseos"
license=('GPL2')
depends=(
    "linux"
    "linux-headers"
)
optdepends=(
    'synapd: AI daemon (required for AI scheduling features)'
)
makedepends=(
    'linux-headers'
    'gcc'
    'make'
)

# For SynapseOS kernel variant:
# depends=('linux-synapse' 'linux-synapse-headers')

source=("$pkgname-$pkgver.tar.gz::https://github.com/synapseos/synapseos/archive/refs/tags/kmod-$pkgver.tar.gz")
sha256sums=('SKIP')

build() {
    cd "$srcdir/synapseos-kmod-$pkgver"
    make KERNELDIR="/lib/modules/$(uname -r)/build"

    # Build userspace tool
    gcc -O2 -Wall \
        -Iinclude \
        -o syn-kmod-status \
        tools/syn-kmod-status.c
}

package() {
    cd "$srcdir/synapseos-kmod-$pkgver"

    # Install kernel module
    install -Dm644 synapse_kmod.ko \
        "$pkgdir/lib/modules/$(uname -r)/extra/synapse_kmod.ko"

    # Install userspace tool
    install -Dm755 syn-kmod-status "$pkgdir/usr/bin/syn-kmod-status"

    # modprobe config: auto-load at boot
    install -dm755 "$pkgdir/etc/modules-load.d"
    echo "synapse_kmod" > "$pkgdir/etc/modules-load.d/synapse.conf"

    # modprobe options
    install -dm755 "$pkgdir/etc/modprobe.d"
    cat > "$pkgdir/etc/modprobe.d/synapse.conf" << 'EOF'
# SynapseOS kernel module options
options synapse_kmod \
    synapse_events=1 \
    synapse_sched=1 \
    synapse_daemon_timeout=30 \
    synapse_ring_size=4096
EOF
}
