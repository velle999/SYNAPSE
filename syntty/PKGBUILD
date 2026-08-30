# Maintainer: Velle Sinclair <brncomputerhelp@gmail.com>
#
# syntty — the SynapseOS terminal.
#
# The name is Finnish: `synty` is birth, or origin. Torvalds was Finnish, and
# the terminal is what gives birth to every process on the machine. The doubled
# consonant is Finnish orthography and it is also `tty`, sitting inside the
# name — and it keeps the word clear of Synty Studios, which the single-t
# spelling would have walked straight into.
#
# ⚠ IT SHIPS NOW, AND THAT TOOK SIX REGISTRATIONS IN ONE COMMIT: build-all.sh's
# KNOWN= *and* a build rule beneath it, syn-update's COMPONENTS,
# archiso/build.sh's PACKAGES (what is BUILT into the ISO's local repo),
# archiso/packages.x86_64 (what the live image INSTALLS), syn-install's
# SEL_CORE (what the TARGET gets), and the line that came out of preflight's
# UNREGISTERED table. A name on one of those lists and not the others is the
# failure this repo keeps re-learning, and it surfaces fifteen minutes into
# somebody's install as "target not found".
#
# ⚠ IT IS THE DEFAULT TERMINAL as of pkgrel 19 — synui's `terminal =` names it
# and kitty is the fallback, kept for the three `--hold` callers syntty cannot
# serve yet. It is 359 KB against kitty's 65 MiB, and it links no GL at all, so
# it opens on a machine where the GPU stack does not.
#
# What it has: a pty, a VT parser, a cell grid, a glyph atlas, a wl_shm window,
# deadline rendering, damage tracking, the kitty keyboard and graphics
# protocols, OSC 133 marks, the alternate screen, the pointer, the clipboard, a
# config file that follows the desktop theme, and tabs. What it does not have
# yet: splits, remote control, hyperlinks, `--hold`.
#
# The stage ordering was the point. Every claim this program is built on is a
# number, and a terminal that draws needs a compositor, a seat and a human to
# produce one; a terminal that only parses can be measured in CI. The baseline
# it exists to beat was taken before a line of it was written, with hyperfine
# inside a headless cage, on the machine it is being written on:
#
#                startup      fresh RSS    2.6 MB parsed
#   kitty        230.3 ms      264 MB        118.6 ms
#   foot          24.9 ms       21 MB        117.9 ms
#
# Two things in that table decide the whole design. Throughput is a DEAD HEAT,
# so there is no champion to copy and no lead to erode. And of kitty's 264 MB,
# 188 MB is the graphics stack it maps by creating a GL context — 83.5 MB of
# libLLVM, 88 MB of nvidia EGL, 15.8 MB of gallium — against 32 MB of kitty's
# own working set and 5.4 MB of Python. So the single biggest decision here is
# that this links no GL at all, and it is why the only library dependency below
# is libutil, for forkpty.
#
# ── 0.1.0-23: the keyboard was missing three things, and one file explains all
#
# Reported as "shift+tab does nothing, and I cannot HOLD a key". Two faults,
# and the second is not a bug so much as a feature that was never written.
#
#   1. ⚠ WAYLAND SENDS NO REPEATED KEY EVENTS. A client is told the seat's rate
#      and delay once, in wl_keyboard.repeat_info, and is then expected to run
#      the timer itself; the compositor sends exactly one press and one release
#      however long a key is held. This handled that event and threw both
#      numbers away, so there was NO key repeat at all — holding backspace
#      erased one character, holding an arrow moved one cell — and nothing
#      warned, because nothing had gone wrong. It reads as the keyboard being
#      ignored rather than as a missing timer, which is how it survived every
#      stage: every test types one character at a time.
#
#   2. ⚠ THE LEGACY ENCODER IGNORED MODIFIERS ENTIRELY, which is three keys:
#
#      · Shift+Tab sent NOTHING. xkb resolves the Tab key with Shift held to
#        ISO_Left_Tab — a different keysym, which produces no text — so it fell
#        through every branch and wrote zero bytes. Verified against a us
#        keymap rather than assumed. It is `ESC [ Z`, terminfo's kcbt, and that
#        fits none of the modifier rules: `CSI 1;2I` is what the pattern would
#        suggest and nothing reads it.
#      · Shift+Arrow was BYTE-IDENTICAL to Arrow, because xkb puts the shift in
#        the modifier state and hands back the same keysym. Nothing extending a
#        selection could tell; it moved the cursor instead.
#      · Alt+key sent the bare key. Alt is a PREFIX, not a level shift, so every
#        meta binding in readline and bash was dead.
#
#      F1..F12 were not in the table at all and sent nothing either.
#
# The fix that matters most is neither of those: the encoder MOVED, out of
# win.c's wl_keyboard listener and into src/key.c, behind `syntty key
# shift+tab`. It needed a compositor, a focused surface and a person pressing a
# key to reach one byte of it — so no test could run it, and these sat there
# for as long as the window has existed. mouse.c's header has claimed since it
# was written that "that split is the same one the keyboard has"; it was not
# true, and now it is. Every expected sequence in the suite is cross-checked
# against `infocmp xterm-256color`, not against this implementation.
#
# ⚠ The repeat TIMER still cannot be tested — it lives in the poll loop, which
# needs a seat. What is asserted is that the encoder gives the right bytes when
# it is called again, which is all a repeat does. Losing focus stopping it is
# the one to keep an eye on: the release of a key held while the window loses
# focus is delivered to whoever has focus NOW, so a repeat left running there
# types forever from a key nobody is touching.
#
# ── 0.1.0-24: DECCKM, the mode that decides what the arrow keys are ─────────
#
# `ESC[?1h` was counted as an UNHANDLED sequence. It is DECCKM — terminfo's
# `smkx`, which every full-screen program emits on the way in — and in that
# state the cursor keys are SS3: `ESC O A` rather than `ESC [ A`. So vim, less
# and mc turned it on, bound the shape they had asked for, and were sent the
# other one for the whole life of the window.
#
# ⚠ THE ALTERNATE FORM IS FOR UNMODIFIED KEYS ONLY, and that is not an
# interpretation: SS3 has nowhere to put a parameter, terminfo records it that
# way, and kitty's protocol specification says it in as many words — "only in
# cursor key mode and only when no modifiers are present". Shift+Up stays
# `CSI 1;2A` in both modes.
#
# ⚠ AND IT IS THE CURSOR KEYS, WHICH INCLUDES HOME AND END. The numbered block
# does not move: there is no SS3 form of a `~` sequence to move it to. Checked
# against `infocmp xterm-256color`, which carries both halves — `smkx=\E[?1h\E=`
# and, for that state, kcuu1=\EOA kcud1=\EOB kcuf1=\EOC kcub1=\EOD khome=\EOH
# kend=\EOF. That is also why khome reads \EOH while an unmodified Home sends
# CSI H: terminfo records the APPLICATION-mode value, because ncurses calls
# smkx. Both are right, in their own mode.
#
# ⚠ AND RIS NOW CLEARS THE MODES, which it never did. `reset` is what somebody
# types after a full-screen program has died without cleaning up, and what it
# has to undo is exactly that group — a crashed vim leaves mouse reporting on,
# so every click types `<0;40;12M` at the shell, and leaves DECCKM on, so every
# arrow arrives in a shape the line editor was not expecting. Clearing the
# screen and the attributes, which was all RIS did, fixes neither, and `reset`
# appearing not to work is how somebody ends up closing the window.
#
# ── 0.1.0-25: the suite failed at random, and it was the suite ──────────────
#
# Roughly one run in five went red, naming a DIFFERENT true assertion each
# time — the font cache, the graphics payload guard, the cell box. Every one of
# them was correct; the pipeline reporting them was not.
#
# ⚠ `producer | grep -q` UNDER `set -o pipefail` REPORTS 141 FOR A MATCH.
# grep -q exits the instant it matches, whatever is still writing into the pipe
# takes SIGPIPE, and pipefail promotes that to the pipeline's status. Measured
# at about 1.2% per pipeline against the real binary, over 88 call sites.
#
# ⚠ AND THIS WAS FIXED ONCE ALREADY, one process too far to the left. That
# round made the helpers capture into a variable so the syntty BINARY could not
# be what died — but the helper still ends by writing that variable into the
# same pipe, and the shell doing it is just as killable. Assertions now match
# through `seen`, which reads to EOF before grepping and greps a FILE, so it is
# safe by construction rather than by grep's good manners. The suite also
# GREPS ITSELF for the bad shape now, because twice is a pattern.
#
# Nothing about this was a syntty defect — but a suite that goes red at random
# teaches people to ignore it, and check() below runs it, so it failed package
# builds at random too. That is the part that made it urgent.
#
# ⚠ And one real leak, found while hunting it: main() never released the config
# it loaded. `cfg.font` is heap, and the merge hands the POINTER to the options
# rather than copying, so it can only be freed after the subcommand returns —
# which a `return` in the middle of the dispatch chain skipped. Harmless-looking
# and not harmless: LeakSanitizer reports it and MAKES THE PROCESS EXIT 1, so a
# sanitiser build returned failure from every successful run on any machine
# that has a config file, and success on any machine that does not.
pkgname=syntty
# pkgver stays 0.1.0 and releases move pkgrel. build-all.sh writes
# "$name-0.1.0.tar.gz" and transforms paths to "$name-0.1.0/" for every
# component, so bumping pkgver leaves makepkg looking for a tarball nothing
# creates.
pkgver=0.1.0
pkgrel=33
pkgdesc="SynapseOS terminal — a Wayland terminal that links no GL"
arch=('x86_64')
url="https://github.com/velle999/SYNAPSE"
license=('GPL-2.0-or-later')

# ⚠ THIS LIST WAS `glibc` ALONE UNTIL pkgrel 14, three stages after the window
# landed. It works on the machine it is built on — wayland and freetype are
# installed there — and breaks on a fresh install, which is the one place a
# missing `depends` shows up. See reference_fresh_install_has_no_build_deps.
#
# A rasteriser, a font lookup, a compositor connection and a keymap. NO GL, and
# that is the decision worth 188 MB of kitty's 264 — see the header.
depends=('glibc' 'freetype2' 'fontconfig' 'wayland' 'libxkbcommon')

# wayland-protocols and wayland-scanner (in the `wayland` package) generate the
# xdg-shell, presentation-time and cursor-shape glue at BUILD time, so a
# protocol upgrade cannot leave a stale checked-in copy behind.
makedepends=('meson' 'ninja' 'gcc' 'wayland-protocols')

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
    cd "$srcdir/syntty-0.1.0"
    meson setup build --prefix=/usr --buildtype=release
    meson compile -C build
}

check() {
    cd "$srcdir/syntty-0.1.0"
    # ⚠ The suite must run with no seat, no compositor, no display and no
    # fonts — that is what stage 1 is for. It forks a real pty and runs real
    # commands on it, so it is not instantaneous, and meson's DEFAULT 30-second
    # timeout would report the kill as a build failure. The timeout is stated
    # in meson.build so a passing suite cannot fail the build.
    meson test -C build --print-errorlogs
}

package() {
    cd "$srcdir/syntty-0.1.0"
    meson install -C build --destdir="$pkgdir"
}
