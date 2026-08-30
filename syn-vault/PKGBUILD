# Maintainer: Velle Sinclair <brncomputerhelp@gmail.com>
#
# syn-vault — a password-locked folder for a user's own files.
#
# ⛔ THIS PACKAGE CONTAINS NO CRYPTOGRAPHY, AND THAT IS THE POINT. syn-vault
# makes directories, asks for a password without echoing it, and runs gocryptfs.
# It does not derive a key, choose a cipher, or write a header. Every one of
# those has a decade of published attacks behind it, and a file locker that gets
# one subtly wrong is indistinguishable from one that does not — until somebody
# else reads the files.
#
# ⚠ gocryptfs RATHER THAN LUKS, and the reason is in the word "userspace". A
# LUKS container needs root to attach a loop device and mount it, so a vault
# built on one is a polkit prompt away from every open and close, and nothing at
# all on a machine where polkit is unhappy. This needs FUSE and the user's own
# permissions.
#
# ⛔ THE PASSWORD NEVER TOUCHES A COMMAND LINE. /proc/<pid>/cmdline is
# world-readable; a password passed as an argument is visible to every process
# on the machine for as long as the command runs. syn-vault reads it from the
# terminal with echo off, or from stdin when stdin is a pipe — which is how the
# window passes one in. There is deliberately no --password option.
pkgname=syn-vault
pkgver=0.1.0
pkgrel=4
pkgdesc="A password-locked folder for your own files: an encrypted vault in userspace"
arch=('x86_64')
url="https://github.com/velle999/SYNAPSE"
license=('GPL-2.0-or-later')

depends=('glibc')

# ⛔ THE BACKEND IS A HARD DEPENDENCY, not an optdepend. A vault app installed
# without the thing that encrypts is an app that can refuse every command it
# has; "install gocryptfs to use the vault" is a sentence nobody should read
# after opening a vault application.
depends+=('gocryptfs')

# fusermount, for closing one. It comes with fuse3, which gocryptfs pulls in —
# named anyway, because this package calls it by name and a dependency that
# arrives only as somebody else's is a dependency that can leave.
depends+=('fuse3')

makedepends=('meson' 'ninja' 'gcc' 'pkgconf')

optdepends=('quickshell: the graphical window (syn-vault gui)'
            'synfiles: opens a vault from the file manager')

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
source=("$pkgname-$pkgver.tar.gz::https://github.com/velle999/SYNAPSE/releases/download/$pkgname-$pkgver-$pkgrel/$pkgname-$pkgver.tar.gz")
sha256sums=('SKIP')

build() {
    cd "$srcdir/syn-vault-0.1.0"
    meson setup build --prefix=/usr --buildtype=release
    meson compile -C build
}

check() {
    cd "$srcdir/syn-vault-0.1.0"
    # ⚠ The suite runs against a STUB gocryptfs and a scratch SYNVAULT_HOME. It
    # deliberately does not mount anything: a real FUSE mount needs /dev/fuse and
    # a user allowed to use it, neither of which a build container has, so a
    # suite that required them would be one that silently never ran.
    meson test -C build --print-errorlogs
}

package() {
    cd "$srcdir/syn-vault-0.1.0"
    meson install -C build --destdir="$pkgdir"
}
