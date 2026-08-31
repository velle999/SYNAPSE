# Maintainer: Velle Sinclair <brncomputerhelp@gmail.com>
#
# syn-play — playlists, shuffle, quick open and history, on top of mpv.
#
# ── Why a front end and not a player ────────────────────────────────────────
#
# mpv already decodes everything, keeps a playlist, shuffles it, resumes where
# it stopped and reads m3u. What it does not have is a place to SEE any of that:
# the queue is a keypress away in an OSD, saved playlists are files you manage
# yourself, and there is no history at all. So this adds exactly those and
# delegates the rest.
#
# ⛔ EVERY FEATURE HERE ASKED MPV FIRST. Shuffle is `playlist-shuffle`. Its undo
# is `playlist-unshuffle`, which restores the order files were ADDED in — mpv
# keeps that, and nothing here could reconstruct it after the fact. Resume is
# `--save-position-on-quit` and the watch-later files it writes. A playlist is
# an m3u8 loaded with `loadlist`, so one saved here opens in mpv, in VLC and on
# a phone. A second implementation of any of those would be a second answer to
# disagree with the one actually playing the audio.
#
# ⚠ A FOLDER IS A PLAYLIST, and mpv is what says what is in it. `loadlist` on a
# directory expands it into its files at the moment it is asked — which is what
# makes a shuffle fair, since a folder left in the queue as ONE row is one
# ticket in the draw for however many tracks are inside it. Directories are
# still mpv's to walk, filtered by mpv's own `--directory-filter-types`; this
# only asks for `--directory-mode=recursive`, and for images to be left out of
# the filter to match the media list this program has always used.
#
# ── Why it does not link libmpv ─────────────────────────────────────────────
#
# ⚠ The window is quickshell, and quickshell cannot host libmpv — there is no
# way to hand a QML scene a video surface to render into. So mpv keeps its own
# window and this is a control surface beside it, over the JSON IPC socket. That
# is not only the available shape, it is the better one: `syn-play next` from a
# terminal, from a keybind, from the TUI and from the window are the same line
# to the same socket, and none of them has to be running for the others to work.
#
# ── What it is NOT ──────────────────────────────────────────────────────────
#
# ⚠ NOT the default handler for media files, deliberately. The .desktop claims
# no MimeType, so double-clicking a video in synfiles opens whatever was already
# opening it. Taking over a file association is a change to somebody's desktop
# that they did not ask a package to make; `syn-play` is there when it is
# wanted, and taking the association is one line in the .desktop when it is.
#
# ⚠ NOT a music player in cliamp's sense. cliamp is a Winamp-shaped TUI that
# big screen mode drives over its own socket, and it stays that. This is the
# thing you reach for when you have a folder of episodes and want the second
# one, or want last night's film back.
pkgname=syn-play
pkgver=0.1.0
pkgrel=8
pkgdesc="Playlists, shuffle, quick open and history for mpv — window, terminal UI and command line"
arch=('x86_64')
url="https://github.com/velle999/SYNAPSE"
license=('GPL-2.0-or-later')

# ⛔ NOTHING BUT libc. This program opens a unix socket and writes JSON at it;
# linking a player's library would put its ABI between a frontend and its own
# build for a feature set reached perfectly well over a documented text protocol
# that also works from a shell script.
depends=('glibc')

makedepends=('meson' 'ninja' 'gcc' 'pkgconf')

# ⚠ mpv is an OPTDEPEND, not a dependency, and that is a real decision rather
# than timidity: the parser, the history, the playlist writer and the quick-open
# search are all testable and useful without a player installed, and a frontend
# that refused to build on a machine with no mpv would be a frontend nobody can
# package. Without it, every transport command says what is missing.
optdepends=('mpv: the player itself — without it there is nothing to drive'
            'quickshell: the window (syn-play gui)'
            'yt-dlp: playing a URL, which mpv hands to it')

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
    cd "$srcdir/syn-play-0.1.0"
    meson setup build --prefix=/usr --buildtype=release
    meson compile -C build
}

check() {
    cd "$srcdir/syn-play-0.1.0"
    # ⛔ THE SUITE RUNS INSIDE A SCRATCH SYNPLAY_HOME, WHICH MOVES THE SOCKET
    # TOO. A test that said `stop` on the shared socket would stop the music the
    # person building the package was listening to, and one that used the real
    # data directory would write into their viewing history. Both are structural
    # here rather than careful. The mpv half skips itself when mpv is absent.
    meson test -C build --print-errorlogs
}

package() {
    cd "$srcdir/syn-play-0.1.0"
    meson install -C build --destdir="$pkgdir"
}
