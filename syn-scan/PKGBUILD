# Maintainer: Velle Sinclair <brncomputerhelp@gmail.com>
#
# syn-scan — scan files at rest, on a machine that already watches itself.
#
# ⛔ THIS PACKAGE IS THE HALF THAT MOVES THE AUDIT, AND THE BINARY IS THE HALF
# THAT MAKES IT A PRODUCT. lynis matches malware scanners BY BINARY FILENAME —
# include/binaries switches on `clamscan`, `rkhunter`, `chkrootkit`, `maldet`
# with no version check and nothing executed. So no program called `syn-scan`
# passes a single MALW test, however good it is; what passes them is the
# depends= line below. Installing this binary AS `chkrootkit` would satisfy
# MALW-3275 tomorrow with no rootkit detection behind it, and it is not done
# here on purpose. See docs/MALWARE-SCANNER-DESIGN.md §2.
#
# ⛔ chkrootkit IS optdepends AND MUST STAY THAT WAY. It only exists in
# [blackarch], and [blackarch] is an install-time CHOICE — WANT_BLACKARCH is an
# ask_opt in syn-install.sh, default yes but declinable, and one profile sets it
# to 0. A hard dependency would make this package uninstallable on a machine
# that said no. An absent engine is reported and skipped, never an error.
pkgname=syn-scan
pkgver=0.1.0
# ── 0.1.0-1: the first one ──────────────────────────────────────────────────
#
# What it is: a front end over clamscan, rkhunter and chkrootkit that gives the
# three of them one schedule, one quarantine, one output format and one window.
#
# What it deliberately is not: a real-time scanner. synguard's BPF-LSM owns
# file_open and bprm_check_security already, and clamav-clamonacc.service is
# MASKED by this package's scriptlet rather than merely left off.
#
# The gap it fills, measured rather than assumed: synguard's entire rule
# vocabulary is exec/open/write/setuid/ptrace/mount/module — every one an
# ACTIVITY, not one of them CONTENT — and synapse_probe.c:382 says in its own
# words that it hooks "only opens of sensitive paths". So a file nobody has
# touched is invisible to it, and a Windows .exe is data to Linux until Wine
# runs it, at which point the LSM sees wine opening a file. This box carries
# 13,835 .exe/.dll/.msi under Games/, Downloads/, .wine/ and compatdata/.
# ── 0.1.0-2: the window could not have opened ───────────────────────────────
#
# ⛔ 0.1.0-1 SHIPPED THIRTEEN MESSAGE CATALOGS AND NOTHING THAT COULD READ THEM.
# meson installed qml/i18n/*.json and never installed data/qml/I18n.qml or its
# qmldir, so `import "qml"` resolved to nothing, quickshell refused the file,
# and `syn-scan gui` opened no window at all.
#
# Every test was green, because the binary was fine — the CLI, the TUI, the
# record protocol and the engines were all correct and all still are. It was
# found by listing the contents of a package built in a clean room from the
# published release, which is the only step that looks at what actually ships.
# A green makepkg proves nothing.
#
# tests/qml_test.sh is the gate now: every file the window imports has to be
# named in an install rule, the qmldir has to declare the singleton, and the
# window has to import the module.
pkgrel=2
pkgdesc='Scan files at rest for malware, and put what turns up aside'
arch=('x86_64')
url='https://github.com/velle999/syn-scan'
license=('GPL-2.0-or-later')

# clamav is the engine that satisfies HRDN-7230 and MALW-3282. 185 MiB
# installed, plus ~110 MB of signatures freshclam fetches on first run.
#
# ⛔ ITS DAEMON IS OPT-IN. clamd is 0.95 GB resident and this program does not
# use it — `clamscan` loads signatures per run. The scriptlet enables freshclam
# only. That gives up MALW-3284 and MALW-3286 (whose prerequisite is
# CLAMD_RUNNING), measured as index 66 instead of 67; the three tests that do
# land need no daemon.
depends=('glibc' 'clamav')
makedepends=('meson' 'ninja' 'gcc' 'pkgconf')
optdepends=('rkhunter: rootkit checks, and lynis MALW-3276'
            'chkrootkit: a second rootkit opinion, from [blackarch] — lynis MALW-3275'
            'quickshell: the graphical window (syn-scan gui)')
install="$pkgname.install"
source=("$pkgname-$pkgver.tar.gz::https://github.com/velle999/$pkgname/releases/download/$pkgver-$pkgrel/$pkgname-$pkgver.tar.gz")
sha256sums=('SKIP')

build() {
    cd "$srcdir/$pkgname-$pkgver"
    meson setup build --prefix=/usr --buildtype=release
    meson compile -C build
}

check() {
    cd "$srcdir/$pkgname-$pkgver"
    # ⚠ The suite runs against STUB engines and composes every path from
    # $SYNSCAN_HOME. It must never need clamav installed, and must never run a
    # real rootkit check against the build machine.
    meson test -C build --print-errorlogs
}

package() {
    cd "$srcdir/$pkgname-$pkgver"
    meson install -C build --destdir "$pkgdir"

    # ⛔ 0700. Quarantined files are still malware, and the directory is created
    # here rather than only by the binary so its mode is owned by the package.
    install -dm700 "$pkgdir/var/lib/$pkgname/quarantine"
}
