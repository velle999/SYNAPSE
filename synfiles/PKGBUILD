# Maintainer: Velle Sinclair <brncomputerhelp@gmail.com>
#
# synfiles — the SynapseOS file browser.
#
# It IS the file manager now — velle, 2026-08-10: "have it replace dolphin and
# drop that from being default." So it declares MimeType=inode/directory and
# ships /usr/share/applications/mimeapps.list naming itself as the
# distribution default for folders.
#
# Dolphin is not removed and is not broken; it simply stops being what opens a
# folder. It stays installable, keeps its own entry, and both file managers
# still read the same KIO service menus (Extract, Crop, Mount ISO, Run with
# Wine, Set as Wallpaper) — synfiles reads $XDG_DATA_DIRS/kio/servicemenus
# itself, so nothing written for one is lost by using the other.
#
# A user who prefers Dolphin gets it back with one command, and their choice
# outranks ours because it lands in their own config:
#   xdg-mime default org.kde.dolphin.desktop inode/directory
pkgname=synfiles
# pkgver stays 0.1.0 and releases move pkgrel. build-all.sh writes
# "$name-0.1.0.tar.gz" and transforms paths to "$name-0.1.0/" for every
# component (build-all.sh:123), so bumping pkgver leaves makepkg looking for a
# tarball nothing creates.
pkgver=0.1.0
# 53: NETWORK PLACES — the sidebar can find shares, not just show mounted ones.
#   velle: "add network places in files that scans for network drives." The
#   Network heading only appeared once something was ALREADY mounted, which is
#   backwards for the one section whose problem is reaching a share you have not
#   mounted yet: the only way to a NAS was to know its smb:// URI and type it.
#   `synfiles netscan` asks mDNS (avahi-browse) and NetBIOS (nmblookup) — never
#   a port sweep; announcement is consent — and enumerates each SMB host's
#   shares as guest, dropping the administrative ones. `synfiles netmount <uri>`
#   hands it to `gio mount` and prints where gvfs put it.
#   ⚠ Arch's smbclient ships NO /etc/samba/smb.conf (that belongs to the server
#   package), and the samba tools REFUSE TO RUN without one. `-s /dev/null` when
#   there is none — and only then, or somebody's real workgroup config would be
#   ignored.
# 52: The vendor mimeapps.list answers for IMAGES too, not just folders — the
#   same walkover that made a terminal the answer to "open this folder" had made
#   a BROWSER the answer to "open this PNG", because SynapseOS shipped no image
#   viewer for anything else to be. synui has one now (its own crop panel grew a
#   viewer face), so image/png and image/jpeg name synui-view.desktop. This file
#   is the only vendor mimeapps.list on the system — two packages cannot both
#   own /usr/share/applications/mimeapps.list — which is why an association for
#   somebody else's .desktop lives here.
# 50: Refresh on the FILE menu as well as the empty-space one. A pane full of
#   files has no empty space to right-click, so the only menu reachable was the
#   one without it — and F5 is no answer to somebody whose hand is on the mouse.
# 54: the right-click menu's six "Open with …" rows are one row and a submenu.
#   For an ordinary PNG they were most of the menu's height, carried its
#   longest and most-elided labels ("Open with GNU Image Manipulati…") and
#   pushed Properties and Compress off the bottom of a short window. The row is
#   added only when there ARE applications behind it — an arrow onto an empty
#   panel is the failure the old flat-list comment warned about. ⚠ The flyout
#   is a SIBLING of ctxMenu, not a child: ctxFlick clips, so a child would be
#   sliced off at the menu's own right edge; and it flips to the LEFT of the
#   menu when there is no room, which a right-click near the window edge makes
#   the ordinary case. Hover opens it, no timers; the click handler RETURNS on
#   the submenu row or the menu would close under the flyout it just opened.
# 55: the active pane is the house ink, not a near-white. The folder under it
#   follows the theme accent and only its HUE moves — it is as light as the
#   violet it was drawn from on every theme — so a near-white sheet on it was
#   never carrying much contrast and had none left on the pale accents. It also
#   puts this icon in the same ink as syn-settings, syn-arcade and syn-disks.

# 56: renaming a file could silently take its EXTENSION off. Renaming
#   `tux95.png` to `tux95` in the window left an extensionless PNG: the bytes
#   were untouched and every listing looked normal, so nothing about the file
#   said anything was wrong.
#
#   ⛔ WHAT BREAKS IS DOWNSTREAM, AND IT BREAKS QUIETLY. synui's wallpaper
#   picker filters a folder by extension (wppick.c) and its thumbnailer chooses
#   a decoder the same way (wpthumb.c), so the picture left the wallpaper list
#   and previewed as blank — with no error, in a different application, from a
#   rename that had already been forgotten.
#
#   The inline editor did `selectAll()` on a name that included the suffix, so
#   typing a new name replaced it. An extension is not part of the name a
#   person is typing, and it cannot be removed as a side effect of typing one.
#
#   TWO HALVES, and the second is what makes it a rule rather than a nicety.
#   stemLen() keeps the extension out of the initial selection — visible,
#   reachable with the cursor, not destroyed by the first keystroke. keepExt()
#   then restores it at COMMIT for any new name that arrived without one, which
#   is what covers the paths that never touched that selection: a rename
#   started from the context menu, a pasted name, a hand-extended selection.
#   Typing an extension is how you change it, and that still works.
#   ⚠ A folder has no extension, and a leading dot is not one (`.bashrc`).
#
#   ⚠ BOTH DELEGATES. The list rows and the icon/compact cells are two
#   renderings of one row, and the editor has been fitted to only one of them
#   before — that is why Rename read as a dead button in Icons view.
#
#   The check RUNS the two functions in a real engine rather than grepping for
#   them, and asserts on PASS as well as the absence of FAIL: a run that
#   printed its marker and nothing else would satisfy a bare "no FAIL" while
#   proving nothing. ⚠ console.log needs QT_ASSUME_STDERR_HAS_CONSOLE.

# 58: the "Open with" flyout closed before a click landed in it. Reaching an
#   entry lower than the "Open with" row means moving the pointer down as well
#   as right, which grazes the row below for a frame — and the hover handler
#   closed the flyout the instant ANY other row was entered, so it read as
#   "the menu closes when I go for it": gone before the pointer arrived.
#   A 300ms grace timer (subCloseTimer) now stands in for the instant close;
#   entering the flyout itself, or re-entering "Open with", cancels it.

# 59: 58's own fix only covered the hop FROM a menu row INTO the flyout. The
#   flyout's own edge still closed instantly on exit — overshooting past the
#   entry you want, or correcting back after clipping the 4px overlap band,
#   both leave its bounds too, and read exactly like the bug 58 was supposed
#   to fix, just one boundary further in. Its exit now gets the same 300ms
#   grace as the row-to-flyout hop instead of an instant close.
# 61: EVERY NON-IMAGE FILE WAS A BLANK SQUARE. iconFor() ends at
#   Quickshell.iconPath(name, true), which answers "" for anything the icon
#   theme has not got — and on SynapseOS the theme has not got ANY of it,
#   because Qt never learns an icon theme NAME here at all.
#   ⚠ THE CAUSE IS OUTSIDE THIS PACKAGE. Qt chooses its base platform theme
#   from XDG_CURRENT_DESKTOP; ours says `SynapseOS` from a display manager and
#   `synui` from greetd, and Qt knows neither, so it falls back to the generic
#   Unix theme, which carries no icon theme. Probed with the live session's own
#   environment: folder, text-x-generic and application-x-generic all come back
#   empty, and start answering only under QT_QPA_PLATFORMTHEME=gtk3 or a
#   desktop name containing GNOME. Both of those ALSO drag the Qt palette from
#   dark to light — the bug synui 261 fixed — so neither is a fix to make for
#   the sake of a file manager. It is recorded, not worked around.
#   Thumbnails hid it: an image draws itself, so a folder of pictures looked
#   right and only the archives, ISOs and scripts were missing — "previews
#   still work but normal files are just blank now".
#   So a file is DRAWN, exactly as a folder already is and for the second half
#   of the same reason FolderIcon gives: it cannot go blank, it follows the
#   palette on the next frame, and it letters the extension, which tells a
#   .iso from a .deb better than the 27 mimetype icons Adwaita ships. Themed
#   icons still win where a theme provides them — iconFor() is unchanged, and
#   a .desktop launcher's own icon still takes precedence over both.
#   ⚠ The turned corner is a shade of the PAPER, not the background. Cutting it
#   away to root.cBg looks identical on an ordinary row and punches a
#   desk-coloured hole in the icon on a SELECTED one. And it is LIGHTER than
#   the paper on a dark desktop: the first draft darkened it, which on paper
#   that is already nearly black landed on the background colour and made the
#   corner vanish.
# 62: HOVERING OVER A FILE SAYS WHAT IT IS. Asked for after seeing Kylin's
#   file manager do it: rest the pointer on a file or a folder and a panel
#   gives the name in full, the type, when it changed, its size, and where a
#   symlink points. The icon view is the one that needed it — a cell is an icon
#   and a name elided to fit, and everything else about the file was in the
#   other view or behind Properties.
#   ⚠ THE TYPE IS ITS REAL NAME: "Tar archive (gzip-compressed)", not
#   `application/x-compressed-tar`. shared-mime-info ships one XML file per
#   type whose FIRST <comment> is the untranslated one, and mime.c reads it —
#   the same database every other file manager on the machine reads, so a file
#   is called the same thing here as it is anywhere else. A table written in
#   the QML would have been a second, worse copy of something already
#   installed.
#   ⚠ CACHED, AND ONLY THE HEAD OF THE FILE IS READ. A listing asks about the
#   same handful of types over and over — a folder of photographs has two —
#   and text/plain.xml is 25 KB of translations with the answer on line four.
#   /usr/bin lists in 87ms.
#   ⚠ AND THE DESCRIPTION TRAVELS ON THE ROW, a tenth column, rather than being
#   looked up when something wants to show it: a front-end that asked per item
#   would be a process per hover.
#   ⛔ THE TYPE BECOMES PART OF A PATH, so it is validated before it is used —
#   one slash, no "..", nothing outside a small character set. It comes from
#   globs2 today; a "type" of "../../etc/passwd" would be read off disk the day
#   one of those comes from somewhere else.
#   ⚠ `info`'s desc is PERCENT-ENCODED and its `mime` beside it is not. A type
#   name is one token; a description holds spaces and parentheses and, in a
#   localised database, any byte at all.
#   ⛔ AND A SCROLL TAKES THE PANEL DOWN. It is anchored to a place on SCREEN,
#   so the list slides out from under it. ⚠ A test written with a full-row
#   scroll PASSED AGAINST NO GUARD AT ALL — the item under the pointer changes,
#   Qt sends an exit, and the panel closes by itself. Only a SMALL scroll, one
#   that keeps the same item under the pointer, discriminates; the qmltest
#   scrolls ten pixels.
#   ⚠ AND THE PANEL LIVES AT THE WINDOW LEVEL, not inside a pane: parented to
#   the row it describes it is clipped by the view — at the bottom of a list
#   that is a two-pixel sliver — and cut off at the pane's edge in split view.
#   ⚠ A SECOND onPressed ON AN EXISTING MouseArea IS "Property value set
#   multiple times" — qmllint passes and quickshell refuses to LOAD. The hide
#   folds into the handler that is already there.
#   Nineteen checks in synfiles_test.sh, ten of them driving a real pointer
#   through tests/item_hover_info.qml. 368 pass.
# 63: A FOLDER'S SIZE ON THE HOVER PANEL, WHICH 62 LEFT OUT. It showed Type
#   and Modified for a folder and no size at all, because st_size for a
#   directory is the size of the directory ENTRY — the number that reads
#   "890 B" for a tree holding an ISO — and a line that has to be explained
#   does not belong on a tooltip. The real answer is a walk, and `synfiles du`
#   is the walk Properties already runs.
#   ⚠ THE DISK FIGURE LEADS: "1.0 MiB on disk", with what it CONTAINS on the
#   Contents line under it. A tree of small files costs a block each and takes
#   far more room than it holds, and that difference is the reason to ask —
#   Properties had been showing only the apparent size, which answers the wrong
#   question by a factor of six on a source tree. It leads with disk now too.
#   ⚠ ITS OWN PROCESS, NOT duProc. Properties owns that one and stops it when
#   it closes; sharing would mean a hover killing the walk behind an open
#   Properties panel, and an open panel stealing the hover's.
#   ⚠ AND THE WALK STARTS WHEN THE PANEL APPEARS, not when the pointer arrives.
#   The half-second delay is what makes crossing a grid of folders cost
#   nothing; a walk per icon brushed past would be a process per icon. It stops
#   with the panel, or its records land in whatever the pointer reached next.
#   Finished answers are cached per path, and ⚠ THROWN AWAY ON EVERY RELOAD —
#   after a copy, a delete or F5 a cached size is a confident wrong number, and
#   re-measuring on navigation is the price of never showing one.
#   ⚠ "1 files in 1 folders" — both counters go through one fmtMany() now.
#   Nine checks in synfiles_test.sh, three of them on `du`'s own two numbers.
#   377 pass.
pkgrel=74
pkgdesc="SynapseOS file browser: tabs, pinned places, recent files and volumes"
arch=('x86_64')
url="https://github.com/velle999/SYNAPSE"
license=('GPL-2.0-or-later')

# Nothing but libc. File types come from shared-mime-info's data files, mounting
# is delegated to udisks2/gvfs through their own tools, and the icon theme is
# resolved by the front-end that already has it loaded.
depends=('glibc')

# shared-mime-info supplies /usr/share/mime/globs2. Without it every file falls
# back to a generic icon — degraded, not broken — but it is a base package on
# any desktop, so depending on it costs nothing and removes a whole class of
# "why is everything a blank page".
depends+=('shared-mime-info')

makedepends=('meson' 'ninja' 'gcc')

optdepends=('quickshell: the graphical browser (synfiles gui)'
            'xdg-utils: open files in their default application'
            'util-linux: the Devices sidebar (lsblk)'
            'gvfs: network places — SMB, SFTP and MTP shares'
            # Discovery, which is a different job from mounting. Neither is
            # required and neither is sufficient: avahi finds a NAS, a Mac and a
            # modern Samba, and finds nothing at all in a house whose shares
            # live on Windows machines that only answer NetBIOS. `netscan` says
            # which one is missing rather than reporting an empty network.
            'avahi: find network shares announced over mDNS'
            'smbclient: find Windows shares over NetBIOS, and list what a server offers'
            'udisks2: mount removable media without root'
            # Two jobs, one package. Images are read in-tree, by magic bytes;
            # so are MP4 and MOV, which carry their size in a track header.
            # Matroska, WebM and AVI do not, and ffprobe is what already knows
            # how to ask — without it those show no resolution row.
            #
            # It is ALSO the thumbnailer: `synfiles thumb` takes a frame out of
            # a video and writes it to the shared cache (thumb.c). Without
            # ffmpeg a film keeps its generic icon unless something else on the
            # machine has already thumbnailed it — which is what synfiles did
            # for its whole life until now, so this is a feature going missing
            # rather than anything breaking.
            'ffmpeg: video thumbnails, and the resolution of Matroska/WebM/AVI'
            # Adds "Open in Disks" to a drive's right-click menu. Probed for at
            # startup, so without it the entry is simply absent rather than
            # present and dead.
            # Format… needs 0.1.0-6 or newer for `gui --format`; optdepends
            # cannot say so, so the window probes for the flag instead and
            # simply does not offer the entry against an older one.
            'syn-disks: Open in Disks, and Format… for removable media')

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
    cd "$srcdir/synfiles-0.1.0"
    meson setup build --prefix=/usr --buildtype=release
    meson compile -C build
}

check() {
    cd "$srcdir/synfiles-0.1.0"
    # Every destructive test runs inside a mktemp -d that the EXIT trap removes,
    # and the two commands that can modify a real file are pointed at a fixture
    # through SYNFILES_PLACES. A file manager whose test suite could touch real
    # data would be the most dangerous file in this repository.
    meson test -C build --print-errorlogs
}

package() {
    cd "$srcdir/synfiles-0.1.0"
    meson install -C build --destdir="$pkgdir"
}
