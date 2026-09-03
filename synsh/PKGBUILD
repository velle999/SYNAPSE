# Maintainer: SynapseOS Project <dev@synapseos.dev>
pkgname=synsh
pkgver=0.1.0
# ── 0.1.0-29: the thirty-nine messages that were never messages ──────────────
#
# ⛔ THE CATALOG WAS COMPLETE, ALL FOURTEEN COLUMNS FILLED, AND tests/
# lang_test.sh PASSED — while `syn status` printed its labels in English beside
# values it had just translated, `syn ai on` printed the same two words as bare
# literals three lines from the T() that already had them, and every message in
# builtins.c, expand.c and half of intents.c had never been declared at all. A
# string that never reaches T() is not a MISSING translation; it is not a
# message, and nothing that counts translations can see it.
#
# 39 of them now, so 91 in each of the fourteen catalogs: cd, jobs and their
# three states, the alias and unalias diagnostics, the unterminated-quote
# errors, `syn status`'s labels, and everything the intents say while opening a
# browser, a file manager, a music player or setting an alarm.
#
# ⛔ THE GATE READS THE SOURCE, because that is the only place the absence is
# visible. Any printf/fputs literal that looks like a sentence and is not a T()
# fails the build. ⚠ With ONE per-site exemption that must carry a reason —
# `/* i18n-english: why */`, holding until the next blank line, which is how
# the help screen's examples stay in the one spelling every language's tables
# accept, and how `--version` stays a record. It was proved by removing a T()
# and watching it fail.
#
# ⛔ AND A SECOND GATE COUNTS THE SLOTS. A missing designated initialiser is a
# NULL that synsh_msg() answers in English — correct at runtime, invisible to
# everything else, and permanent.
#
# ── the banner said "Where the kernel thinks" in every language ──────────────
#
# Not because anybody chose that: the box padded to a hard-coded 25, the length
# of those English words, so any other wording put the right-hand │ in the wrong
# column. It is measured now — in COLUMNS, which is the third of the three
# possible answers: "カーネルが考える場所" is 30 bytes, 10 code points and 20
# columns, and only the last of them closes the box.
#
# ⚠ NO wcswidth(3). It needs setlocale(LC_CTYPE, "") and synsh deliberately
# never calls setlocale — its catalog is compiled in so that it works before
# /usr is complete. synsh_disp_width() decodes UTF-8 itself, and every range in
# it was checked against glibc's wcswidth() for all fourteen taglines.
# ⛔ Mn AND Me TAKE NO ROOM, Mc TAKES ONE, and that distinction is the whole of
# Devanagari: zeroing all of 093A–094F made the Hindi tagline 13 columns where a
# terminal draws 16. The suite asserts the box CLOSES in every language rather
# than that it is any particular width.
#
# ⚠ ONE MESSAGE TRAVELS INSIDE A SHELL COMMAND — the orphan-package line, which
# reaches the person through `echo` in a pipeline. It is quoted with the
# '"'"' idiom, because a translation containing an apostrophe would otherwise
# close the quote and turn the rest of the sentence into arguments.
#
# ── and 118 assertions that nothing ran ─────────────────────────────────────
#
# ⛔ `meson test` answered "No tests defined". tests/shell_test.sh and
# tests/lang_test.sh existed, passed, and were run when somebody remembered to
# type them, which is a habit and not a gate. Both are wired into meson.build
# now. ⚠ And both make their argument absolute first: the cases cd, so a
# relative ./build/synsh failed 53 of them at once with "No such file or
# directory" — which reads as a broken shell rather than a mistyped path.
pkgrel=29
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
