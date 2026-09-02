# Maintainer: Velle Sinclair <brncomputerhelp@gmail.com>
#
# synpkg — the SynapseOS package manager.
#
# GPL-2.0-or-later is not a choice here: this links libalpm, which is
# GPL-2.0-or-later, so the binary can be nothing else.
pkgname=synpkg
# pkgver stays 0.1.0 and releases move pkgrel, which is the convention every
# SynapseOS component follows — but here it is a HARD constraint, not a habit:
# build-all.sh writes "$name-0.1.0.tar.gz" and transforms paths to
# "$name-0.1.0/" for every component it builds (build-all.sh:123). Bumping
# pkgver without changing that leaves makepkg looking for a tarball nothing
# creates.
pkgver=0.1.0
# 34: HOLDING AN UPDATE BACK. `synpkg ignore <package>` stops a package being
#   upgraded, `synpkg unignore` releases it, and `synpkg ignore` with no
#   arguments is the list — with the version each one is refusing, because a
#   hold whose cost you cannot see is a hold nobody revisits. The GUI gains a
#   Held back page and a Hold button on every pending update.
#
#   The list is pacman.conf's IgnorePkg, NOT a synpkg file. An ignore only this
#   program honoured would be a lie on a machine that also has pacman: someone
#   who holds a package back here and then runs `pacman -Syu` must not have it
#   upgraded by the tool they did not think to configure. synpkg has READ
#   IgnorePkg since the beginning; this adds the ability to write it.
#
#   Flatpak is held with `flatpak mask`, its own mechanism, for the same
#   reason. SynapseOS components are held by syn-update, which owns that state
#   — `synpkg system ignored` passes through, and PROBES first, because an
#   older syn-update does not have the verb.
#
# 35: the empty answers carry their header too. `aur updates --tsv` returned
#   early with NO header at all when the AUR was unreachable or when no
#   foreign packages are installed, and `flatpak updates --tsv` announced the
#   OLD five-column header when flatpak is not installed — the two paths where
#   the header is the only thing emitted, and so the two where getting it
#   wrong stays invisible. The first one aborted a build: the suite asserts
#   the update headers match each other, and it failed in check() on a laptop
#   that was simply offline. Both cases are now forced in the suite (a stub
#   curl that fails; an empty PATH) rather than waited for.
# 36: "Enable Flathub" and "Enable BlackArch" could be hovered but not clicked.
#
# 37: AND THEN ENABLE FLATHUB FAILED ON THE MACHINES IT IS FOR. Pressing it
#   opened a terminal that printed `flatpak is not installed — synpkg install
#   flatpak` and exited 1. Every other flatpak subcommand ASKS flatpak
#   something and is right to refuse without it; enable-flathub asks for the
#   FEATURE, and a machine that has never had flatpak is exactly the machine
#   somebody presses that button on. It installs flatpak now — saying so
#   first, because "Enable Flathub" quietly pulling in a package would be the
#   wrong kind of silent — and then adds the remote and fetches the appstream
#   index as before.
#
#   Asserted with a PATH holding one directory, containing one stub pkexec
#   that prints what it was handed: flatpak is genuinely absent and the
#   escalation is visible without granting one. ⚠ that stub needs #!/bin/bash
#   and not `#!/usr/bin/env bash` — env has no PATH to find bash on, and a
#   stub that cannot start looks exactly like an escalation that never
#   happened.
# 38: `synpkg search --all` — the super search. Every source at once: the
#   repositories, BlackArch, the AUR and Flathub, in ONE table, each row
#   labelled with where it came from. The GUI gains an "All sources" tab on it,
#   and every row on every page gains an icon.
#   ⚠ IT COULD NOT BE THREE COMMANDS CONCATENATED, and that is the whole of the
#   C change. The three sources had three TSV shapes — six columns for the
#   repositories, seven for the AUR (whose fifth is `votes` where the others
#   have `size`), seven for Flathub (whose seventh is `title`) — so running
#   them in sequence would interleave three headers with three sets of rows and
#   put vote counts in the size column. There is one shape now, and it is the
#   UNION rather than a new invention: name, installed, version, repo, size,
#   description, title, votes, flag. Every column already existed in one of the
#   three, `repo` was already the source label the GUI badges with, and a
#   source fills what it has.
#   `g_super` is a global and not a parameter because it has to reach
#   aur_render() and flatpak_row(), which are statics three call levels down in
#   ext.c with no route to an argument. It also suppresses the per-source
#   headers — cmd_search() writes the union header once, before the first
#   source runs.
#   ⚠ EVERY SOURCE FAILS ALONE. flatpak_search() returns non-zero when flatpak
#   is absent, when no remote is configured and when the appstream index was
#   never fetched — all three ordinary. Letting any of them set the exit status
#   would make --all fail on a box whose repositories answered perfectly well,
#   and a GUI reading the code would throw away rows it already had. Ordered
#   fast to slow (local dbs, one AUR round trip, flatpak) so the likely answer
#   is on screen while the network halves work. BlackArch needs no code — it is
#   a sync db and was already searched and already labelled — but --all says so
#   when the repo is not enabled, because "BlackArch found nothing" and
#   "BlackArch was never asked" look identical.
#   ⚠ THE GUI'S "All sources" STEP PASSES tab: "" DELIBERATELY. pkgRows() reads
#   it as `tab || sourceOf(r.repo)`, so a single-source pane tags every row with
#   its own tab; the super search must keep each row's OWN source, because that
#   is what rowAction() dispatches on. Forcing a tab there would fire
#   `flatpak install` at a pacman package. Search-only, too: "Browse" would mean
#   listing four repositories at once and "Installed" already has a page per
#   source.
#   Icons resolve in QML through Quickshell.iconPath(), like synfiles' — the
#   renderer already has the icon theme loaded and C would have to walk every
#   theme directory to answer it. Two tries, because `name` is the package name
#   for one kind of row and the flatpak ref for the other: verbatim, then the
#   last dot-segment lower-cased so org.mozilla.firefox finds firefox. ⚠ A
#   PACKAGE IS NOT AN APPLICATION and most have no icon at all, so the tile is
#   always drawn and falls back to a monogram tinted by the row's source — a
#   column that appeared for some rows and not others would read as icons being
#   broken rather than as a library not having one.
#   ⛔ The new tests COUNT rather than `| grep -q`: this suite runs under
#   `set -o pipefail`, grep -q exits on the first match, the producer takes
#   SIGPIPE and the pipeline reports 141 — a FAILURE on a match. It bit during
#   development, and the tell is that the identical construct passed for
#   Flathub (18 rows fit the pipe buffer) and failed for the repositories (239
#   did not).
# 39: the GUI drew very few icons. Mostly correct — measured on this box, 147 of
#   187 explicitly-installed packages have no icon under any name because they
#   are not applications (libraries, fonts, kernel modules, CLI tools), and the
#   monogram is the right answer for those rather than a fault.
#   ⚠ BUT AN APPLICATION'S ICON IS OFTEN NOT NAMED AFTER ITS PACKAGE, and the
#   two tries shipped in 38 only ever asked that. `retroarch` installs
#   `com.libretro.RetroArch`, `openrgb` installs `org.openrgb.OpenRGB`,
#   `calibre` installs `calibre-gui` — so the rows a person actually recognises
#   in a package list were the blank ones, which is what "very few icons" looks
#   like from outside. A third step reads the Icon= out of the .desktop files
#   and matches on their basename AND their Exec binary, because neither alone
#   matches a package reliably. 34 by name, 6 more by this, on 345 map entries.
#   ⚠ ONE SCAN PER WINDOW, not per row: /usr/share/applications is a few hundred
#   files and asking it per row would be that scan times the length of a search
#   result. ⚠ `ENDFILE` is GNU awk's; Arch's base pulls in gawk, and the failure
#   mode without it is the honest one — the map stays empty and every row falls
#   back to the two name tries, which is where this started.
# 40: the GUI still drew very few icons, and 39 had explained the wrong reason
#   for it. Every step in the chain up to here asks the LOCAL DISK — the icon
#   theme, then the installed .desktop files — so every one of them can only
#   answer for software that is already on the machine. The suggested list is
#   105 applications most of which are by definition NOT installed, so it drew a
#   monogram for all but the handful you happened to own. The lookups were not
#   failing; there was nothing on disk to find. Measured: 15 of 104 curated ids
#   resolved, and all 15 were installed.
#   ⚠ THAT IS WHAT APPSTREAM IS FOR. A distribution ships a catalogue of every
#   application in its repositories, icons included, so a software centre can
#   draw a package nobody has installed yet — it is what GNOME Software and
#   Discover read, and synpkg is that kind of program. archlinux-appstream-data
#   is a dependency from here, and a fourth step reads its cached icons: 54 more
#   curated rows get a face, 56 of 105 in total. The 49 still on a monogram are
#   `git`, `ripgrep`, `tmux`, `jq` — tools with no icon anywhere because they
#   have no face, and the monogram is correct for those.
#   ⚠ NO XML AND NO PARSER: a cached AppStream icon is `<pkgname>_<iconname>.png`
#   under `<origin>/<size>/`, so the package name is in the file name. The split
#   is at the FIRST underscore; of the 1,205 icons Arch ships today exactly one
#   file has a package name containing one, and a mis-split can only ever
#   produce a key that no package matches.
#   ⚠ ONE SCAN PER WINDOW, like the .desktop map beside it, and it is allowed to
#   find nothing — in a chroot without the data the map stays empty and every
#   row falls back to the monogram, which is where this started.
# 47: the CLI and the TUI speak thirteen languages — and the TSV does not.
#   258 msgids now, 257/257 in all thirteen: the QML window's words and the C
#   program's, in ONE .po per language compiled twice — to JSON for the window
#   and to a .mo for the binary. A word both front-ends use is translated once
#   and they cannot come to different conclusions about it.
#   ⛔ `--tsv` IS NEVER TRANSLATED, AND THAT IS THE WHOLE RULE. It is what
#   data/synpkg.qml parses and what the tests parse; a translated column or
#   status word makes the GUI depend on the user's locale, which is the bug
#   `pacman -Qi` taught this project twice. Every _() sits on the human side of
#   a `g_out == OUT_TSV` branch, and tests/i18n_test.sh proves it by RUNNING
#   every offline --tsv subcommand under a real de_DE locale and diffing.
#   ⛔ THAT CHECK WAS DECORATIVE UNTIL IT WAS BROKEN ON PURPOSE. The binary's
#   compiled-in localedir is under the install prefix, so an UNINSTALLED synpkg
#   loads no catalog and answers English in both locales — the suite passed
#   with a _() deliberately placed in a TSV row. synpkg_i18n_init() honours
#   $SYNPKG_LOCALEDIR now (nothing changes for an installed synpkg), the test
#   sets it, and the sabotage fails as it should.
#   ⛔ AND xgettext --omit-header SILENTLY MANGLES THE MSGIDS. With no header
#   there is no charset to declare, so it writes the template as ASCII and
#   DROPS every non-ASCII character it extracted: `%s.pacnew — merge it` came
#   out `%s.pacnew  merge it`, a msgid that can never match the source string
#   and would have been permanently English however well translated. There is
#   no warning about the loss. po/pot.sh refuses the flag and asserts the
#   round-trip.
#   ⛔ AND A .mo IS NAMED AFTER THE DOMAIN. A custom_target loop can only name
#   its output de.mo, which installs to the right directory under a name
#   libintl never looks for — a full catalog on disk and every string English.
#   meson's i18n module knows the rule; that is why po/meson.build uses it.
#   ⚠ THE TUI'S BOX AND MENU ARE COMPUTED NOW, not typed. A hand-counted run of
#   ─ and hand-counted padding are right in exactly one language. Both are
#   measured in COLUMNS (mbstowcs + wcswidth), because ソフトウェア is 18 bytes
#   and 12 columns; the Japanese banner closes and the three-column menu lines
#   up. ⚠ tests/i18n.sh caught a `%d tools%s` plural hack and a `", some
#   installed"` fragment on the way past.
#   ⚠ `--help` IS DELIBERATELY NOT MARKED: fifty lines of column-aligned text
#   whose every command name must be typed in English anyway.
pkgrel=47
pkgdesc="SynapseOS package manager: repositories, AUR, Flathub, BlackArch and SynapseOS itself"
arch=('x86_64')
url="https://github.com/velle999/SYNAPSE"
license=('GPL-2.0-or-later')

# pacman, not just libalpm: pacman-conf resolves pacman.conf for us (Include
# globs, mirrorlist indirection, SigLevel inheritance) and shipping a second
# parser for that file is how a manager installs from a repo the user disabled.
# curl fetches the BlackArch bootstrap and drives the AUR RPC.
# archlinux-appstream-data: the repositories' application catalogue, with an
# icon per application. It is how the window draws a face for a package that is
# not installed — see the note on pkgrel 40 — and it is the same data GNOME
# Software and KDE Discover read. A hard dependency rather than optional: a
# software centre whose list is a column of letters on a fresh install is the
# bug this fixes, and "install this other package and it gets better" is not an
# answer a person can be expected to find.
depends=('glibc' 'pacman' 'curl' 'archlinux-appstream-data')

makedepends=('meson' 'ninja' 'gcc')

# None of these are hard dependencies, and that is the point: `synpkg` has to
# work on a headless box, over SSH, and on a fresh install that has no desktop.
# Each front-end degrades to the terminal rather than failing.
optdepends=('quickshell: the graphical browser (synpkg gui)'
            'polkit: install and remove without sudo, from the GUI'
            'flatpak: the Flathub tab — search, install and update Flatpaks'
            'syn-update: update SynapseOS components through synpkg system'
            'git: build packages from the AUR'
            'base-devel: makepkg, for AUR builds'
            # ⚠ VERSIONED, because the flag is the whole reason this one is
            # named: --hold landed in syntty 0.1.0-27. An older syntty is
            # still FOUND by the chain's `command -v` and then dies at parse,
            # so the window never opens — which looks exactly like the button
            # doing nothing. foot stays listed for a box that has neither.
            'syntty>=0.1.0-27: terminal the GUI opens for a full system upgrade'
            'foot: the same, on an install that predates syntty')

# ⛔ THE RELEASE URL, AND IT CARRIES THE pkgrel. The filename before `::` is
# what makepkg looks for on disk, so a build from this checkout uses the
# tarball build-all.sh just collected and never downloads. The URL after it is
# for everybody else, and it names <pkgver>-<pkgrel> because that tag is the
# only thing that makes a published source unambiguously the one this PKGBUILD
# was written against.
#
# ⛔ AND sha256sums STAYS 'SKIP'. A real checksum would break every LOCAL build
# the moment the tree changed, which is every build that matters here.
source=("$pkgname-$pkgver.tar.gz::https://github.com/velle999/$pkgname/releases/download/$pkgver-$pkgrel/$pkgname-$pkgver.tar.gz")
sha256sums=('SKIP')

build() {
    cd "$srcdir/synpkg-0.1.0"
    meson setup build --prefix=/usr --buildtype=release
    meson compile -C build
}

check() {
    cd "$srcdir/synpkg-0.1.0"
    # Every test is a QUERY — see tests/synpkg_test.sh. A package manager whose
    # test suite can mutate the machine building it is one bad path away from
    # a very bad afternoon.
    meson test -C build --print-errorlogs
}

package() {
    cd "$srcdir/synpkg-0.1.0"
    meson install -C build --destdir="$pkgdir"
}
