# Maintainer: Velle Sinclair <brncomputerhelp@gmail.com>
#
# syn-arcade — the SynapseOS game assistant.
#
# Three things a desktop owes a person who plays games on it, and that nothing
# on this system did before:
#
#   · the overlay — frame rate, temperatures and load — turned on, moved and
#     turned off with a key, INSIDE a game that is already running
#   · game controllers: what is plugged in, whether its sticks are centred,
#     whether its motors work, and what its buttons are mapped to
#   · the two compositor shortcuts that drive the first of those
#
# And, since 0.1.0-2, a fourth: BIG SCREEN MODE — the ten-foot interface. A
# television is not a monitor with a bigger number; it is four metres away and
# driven with a controller, and every desktop convention fails at that distance.
# `syn-arcade big start` puts up one full-screen layer-shell surface of tiles:
# the installed Steam library with its cover art, Steam Big Picture, whatever
# launchers and media players are on the machine, and the machine's own
# switches. `big autostart on` opens it at login instead of the desktop.
#
# ⚠ 0.1.0-3 fixes the two things that made big screen mode look half-finished,
# and both are worth knowing about because neither produced a single warning.
#
#   1. Nothing HORIZONTAL worked — left, right, the shoulder-button page jumps,
#      Home, and the mouse moving along one shelf — while up and down were
#      perfect. The selection keeps one column index per shelf in a QML `var`
#      property, and the obvious way to update it (take the object, set a key,
#      assign it back) does not notify: Qt compares the incoming QVariant with
#      the stored one, finds the identical JS object, and drops the write. No
#      binding that reads it is ever re-evaluated. Up and down survived only
#      because the row is an int. It read as a controller that was half wired
#      up, and it read as INTERMITTENT too, because the next row change
#      published every swallowed column move at once.
#
#   2. super+F10 was in nobody's config. Installing a package writes nothing
#      into a user's home: the keybind block lives in ~/.config/synui/synuirc
#      and is only ever written by `binds install`. So 0.1.0-2 added the key to
#      the defaults and to blocks installed from then on, and every machine
#      that had already installed one kept the two overlay keys it was born
#      with. The feature shipped, the docs named the key, `binds show` printed
#      the key, and pressing it did nothing. `binds refresh` re-renders an
#      existing block — keeping every combo the user chose, refusing rather
#      than duplicating a key that is already bound elsewhere, and writing
#      nothing when the result is unchanged — and the session profile runs it
#      at every login.
#
# ⚠ 0.1.0-5 stops the graphical window letting a wireless pad FALL ASLEEP while
# it is on screen — which looked exactly like this application disconnecting the
# controller, and was reported as that.
#
# xpad submits its USB interrupt URB when the input device is OPENED, and only
# then. With nothing holding the node open the endpoint is never polled, a
# 2.4GHz dongle sees no traffic, and the pad's own idle timer switches it off.
# Steam Input holds every pad open for as long as it runs, so a controller stays
# alive under Steam and dies under anything that does not — and `pads list`
# opens each node, reads what it needs, and exits. Measured on an 8BitDo
# Ultimate: 4-5s with nothing holding it, 43s under Steam, no disconnect at all
# in five minutes with one blocking read on the node.
#
# So the window now also runs `pads hold`, which holds every pad open for as
# long as it is up and reads nothing into anything. Big screen mode never had
# the bug: `big nav` already holds the whole set across its rescan loop.
#
# ── 0.1.0-6: big screen mode STEPS ASIDE instead of closing ─────────────────
#
# The one thing that made it feel unfinished. Every tile called Qt.quit(), so
# opening the controller window or the browser CLOSED the television interface
# and getting back to it meant finding a keyboard. A console does not do that:
# it goes away while you use the thing, and it comes back.
#
# So the shell now stays alive and unmaps its surface, and there are three ways
# back: the application exiting (`big run <id> --wait` lives exactly as long as
# what it started), the GUIDE button at any time, and `big show`.
#
#   ⚠ A launcher that returns IMMEDIATELY is a hand-off, not a close. Firefox
#     and Steam are single-instance: started while already running, the second
#     process passes its arguments to the first over a socket and exits. Taking
#     that as "they finished" would throw the television back over a browser
#     somebody just opened — reliably, and only on the machines where it was
#     already running. Under three seconds is treated as a hand-off.
#
#   ⚠ UNMAPPED, not transparent. A full-screen overlay-layer surface left on
#     top of a game is a composite pass per frame and the end of direct
#     scanout, for a screen nobody can see.
#
# And the reverse of the Guide tile: `big guard`, an autostart in the same
# managed synuirc block, watches the pad while big screen mode is NOT running
# so the guide button opens it from the desktop. It deliberately does nothing
# while the shell is up — the shell's own `big nav` is reading the same button,
# and both acting on one press is a race that either bounces the interface or
# starts two. It holds every pad open too, which is `pads hold`'s job for free.
# ⚠ An autostart only starts at the NEXT LOGIN; nothing can add one to a
# session that is already running.
#
# ── …and the four things that needed it ─────────────────────────────────────
#
#   · THE CONTROLLER AS A MOUSE, for the browser. `big mouse` is the one place
#     in this package that synthesises input, and it is bounded: a separate
#     process, started only while a pointer-driven application is on screen,
#     killed when the interface comes back, and it moves a POINTER rather than
#     pressing keys — stick drift moves a visible cursor instead of typing into
#     every text field on the machine. It goes through virtual-pointer-v1,
#     which synui had to grow for this (and treats as a privileged global, so
#     a sandboxed client cannot do it).
#   · AN ON-SCREEN KEYBOARD, console style, opened with Start and closed with B
#     or its own Close key. It types through wtype — which already ships here
#     and already speaks virtual-keyboard-v1 — over one long-lived stream
#     rather than a process per key. ⚠ Its window takes NO keyboard focus: the
#     whole job is typing into the window underneath, and a surface that
#     grabbed the keyboard to draw a keyboard would type into itself.
#   · A NEWS SHELF below the system row, from RSS via curl, cached for twenty
#     minutes. ⚠ A fetch that comes back empty does not overwrite the cache:
#     the usual reason for empty is a television switched on before the Wi-Fi
#     came up, and old news beats no news.
#   · MEDIA: a Music tile for whichever player is installed, and Plex and
#     Jellyfin servers found on the network — Plex by its GDM M-SEARCH to
#     239.0.0.250:32414 (the address is the SENDER's, not in the reply), and
#     Jellyfin by the literal string "who is JellyfinServer?" on port 7359.
#     Both are also asked over broadcast, because plenty of home networks do
#     not forward multicast, and localhost is probed directly afterwards
#     because a server does not reliably answer its own broadcast.
#
# A Terminal tile too, which is `syntty` first for the same reason everything
# else here is: a big screen tile that opened a different terminal from
# Super+Return is the kind of small inconsistency that makes a desktop feel
# assembled rather than designed.
#
# ── 0.1.0-7: a stationary mouse was steering the television ─────────────────
#
# Reported from the sofa as three separate faults, and they are one bug:
#
#   · up and down would not take consistently
#   · the selection jumped, several shelves at a time
#   · pressing A launched ONE APP BEHIND the tile that was highlighted
#
# None of them is a controller fault, and the pad was innocent again. Qt
# re-delivers a hover event at the LAST KNOWN cursor position on every frame in
# which the scene graph is dirty, which is correct — a tile sliding out from
# under a stationary pointer really has stopped being hovered. But every
# selection move in this interface animates: the shelf column slides for 200ms,
# the strip slides for 200ms under ApplyRange, the tile scales for 140ms. So one
# press of the d-pad is a dozen frames of tiles being dragged past a cursor
# nobody is touching, and the tile MouseArea's `onEntered` wrote row and column
# on every one of them.
#
# That is a feedback loop with the controller on one side and the animation on
# the other: hover sets the selection, the selection scrolls the strip, the
# scroll drags another tile under the cursor, and that sets the selection again.
# It launched exactly one behind because ApplyRange scrolls by exactly one tile
# width, so A activated the state while the screen still showed the picture from
# before the move.
#
# Hover is now gated on the cursor's SCENE position actually changing — sitting
# still while the world moves underneath is not input. It cannot be spelled with
# `onEntered`, which carries no coordinates; onPositionChanged does.
#
#   ⚠ It only bites once the surface has EVER seen a pointer, which is why it
#     arrived with the controller-as-mouse in 0.1.0-6 and why NO rig here could
#     have caught it: coming back from an app leaves the cursor wherever the
#     stick left it, which is over the tiles, and nothing in the headless rig
#     moves a pointer at all. Every screenshot it takes is of a seat that has
#     never had one.
#
# ⚠ Its controller navigation SYNTHESISES NOTHING. `syn-arcade big nav` reads
# the event nodes udev already grants this user and writes WORDS — "up",
# "accept" — down a pipe that exactly one process is reading. The obvious
# alternative, a uinput device turning stick movement into arrow keys, is a
# system-wide input device: stick drift would type into whatever is focused,
# which on this machine has meant typing into the user's browser. Nothing here
# is visible to the compositor or to any other application.
#
# ⚠ The overlay keybind works because of a mechanism worth writing down, since
# the obvious reading of it is wrong. MangoHud is an implicit Vulkan layer
# living INSIDE the game's process: there is no socket, no signal, and
# `mangohudctl` does not reach it (it speaks over a SysV message queue that
# only the separate `mangoapp` process listens on — the shipped libMangoHud.so
# imports no msgget at all). What libMangoHud DOES do is watch its own config
# file with inotify and reparse on every change. So rewriting that file is a
# live control channel into every running game at once, and an ordinary
# compositor keybind can drive it. /etc/MangoHud.conf's own comment says a
# toggle "cannot be a compositor keybind"; that was true of a signal or an IPC
# call, and is not true of the file.
#
# ⚠ Two consequences, both load-bearing and both handled by the session profile
# this package installs:
#
#   1. The config file must EXIST BEFORE THE GAME STARTS. inotify_add_watch on
#      a missing path fails, and that process then watches nothing for the rest
#      of its life — every overlay keybind silently dead in that game. Hence
#      `syn-arcade hud ensure` at login.
#
#   2. MangoHud reads exactly ONE config file, and /etc/MangoHud.conf (which
#      synui ships) OUTRANKS ~/.config/MangoHud/MangoHud.conf. There is no
#      merging. So on a stock install the user's own overlay config was never
#      read at all, and nothing a user set could win. The profile sets
#      MANGOHUD_CONFIGFILE, which collapses that list to one writable path;
#      `syn-arcade hud adopt` moves the settings already in effect across so
#      nothing anybody tuned is lost in the move.
#
# ⚠ Controller writes — rumble and deadzones — need write access to
# /dev/input/eventN, which is root:input 0660, and the user is NOT in `input`
# on a stock install. That is fine and needs no root, no polkit action and no
# setuid helper: udev tags JOYSTICKS (and only joysticks — not keyboards, not
# mice) for uaccess in 70-uaccess.rules:61, and systemd grants the logged-in
# seat user an ACL. So "permission denied" here means the device was not
# recognised as a joystick, or this is not the active seat — never "use sudo",
# and the binary says so rather than teaching the wrong fix.
# ── 0.1.0-51: thirteen languages, and a line down the middle of every record ─
#
# syn-arcade said everything in English — the CLI you use over SSH, the desktop
# window, and the ten-foot interface on a television. 597 strings now in de, fr,
# es, pt, it, nl, pl, ru, ja, zh, ko, hi and ar, one catalog compiled twice: a
# .mo for the binary and JSON for the two quickshell windows, so a word they
# share is translated once and cannot disagree.
#
# ⛔ AND EVERY --rec RECORD STAYS ENGLISH, WHICH IS MOST OF THE WORK. The first
# row of every record NAMES THE COLUMNS and both windows key off those names;
# they match on values too — `installed === "yes"` in four places, a hud
# position is `top-left`, a fit scaler is `integer`, an `action` cell is
# `toggle:hud`. So the marking is by DESTINATION, not by file: `hud show` writes
# a sentence somebody reads and `hud --rec` writes a record, out of the same
# function, a few lines apart.
#
#   _()   the human path.
#   N_()  a LABEL that travels in a record for a WINDOW to translate at the draw
#         site: in the catalog, unchanged in the row.
#
# ⚠ TWO PLACES NEEDED A SECOND TABLE, because an id and a label are two facts
# that happened to be spelled the same. `hud choices` prints an id column and a
# label column, and both were hud_positions[] — marking that one array would
# have put `top-left` in the catalog, and a German window would have offered a
# position no command accepts. Same for a big-screen SHELF: its `title` is this
# shell's identity for a row (the selection survives a rebuild by matching it,
# and the scroll position is remembered under it), so it stays English and a new
# `label` field carries the word on screen.
#
# ⚠ AND button_name() BECAME button_label(). The convention across this project
# is that a *_name() function is read by a PROGRAM — bus_name() returns USB and
# Bluetooth — but this one returns "left bumper" and "d-pad up", which are
# sentences. The two sibling functions could not keep the same suffix.
#
# ⛔ THE TEST THAT MATTERS IS A HOSTILE CATALOG. tests/i18n_test.sh builds one
# from the TEMPLATE with every msgstr marked and runs twenty --rec commands
# under it: a record that changes has a string reaching gettext, whether or not
# de.po happens to carry that entry today. A marked column name is invisible to
# a real-catalog diff until somebody translates it, and then it is a window that
# has stopped recognising its own records. It cannot be a static check either —
# `rec_row(3, N_("licence"), "GPL-2.0-or-later", "detail")` is a DATA row whose
# fields are all literals, and `pads info` prints a LABEL spelled `name` while
# `pads --rec` has a COLUMN spelled `name`. A grep for header spellings reported
# three false positives on its first run and was deleted.
#
# ── and four things the languages found ────────────────────────────────────
#
# ⚠ A FIXED 120 px LABEL COLUMN IS A MEASUREMENT OF ONE LANGUAGE'S WORDS.
# "Config file" fits it; "Konfigurationsdatei" printed straight over the path
# beside it. Verified in a nested headless synui under a German catalog — the
# only way to see it — and the column now grows for a label that needs it while
# staying aligned for the short ones.
#
# ⚠ THE SUITE'S SANDBOX ASSERTION WAS AN ABSOLUTE. "No wrapper reached the real
# applications menu" was `find ~/.local/share/applications -name 'syn-fit-*'` is
# EMPTY — which fails on any machine where somebody has actually used `fit new`.
# The machine this is written on has one for SimCity 3000, so the suite failed
# here for the same reason it would fail on any box the feature works on. It is
# snapshotted at the top and compared at the end now: the claim is that this run
# ADDED nothing.
#
# ⚠ SEVENTY-FOUR PIPELINES STILL RAN THE BINARY INTO `grep -q`. says() was added
# for this in an earlier round and 139 assertions use it, but `"$SA" big games
# --rec | cut -f2 | grep -qx "…"` has no says() in front: grep -q exits on the
# match, syn-arcade takes SIGPIPE mid-record, and pipefail reports 141 for an
# assertion that was TRUE. They go through `has` now — `grep -c` into a
# variable, which must read to EOF — and the suite greps ITSELF for the shape,
# because this file has now been fixed for it twice.
#
# ⚠ AND THE fit RIG DREW A WINDOW WITH NO WORDS IN IT AND CALLED IT A PASS. It
# copies the .qml to a temporary and runs quickshell on the copy, so the
# `import "qml"` for the translation singleton resolved to nothing: one WARN
# about an unresolvable import, a ReferenceError per lookup, and a screenshot of
# blank labels. It copies qml/ beside the file now, and a match on ERROR in that
# log is a FAILURE rather than a line printed and carried past.

pkgname=syn-arcade
# pkgver stays 0.1.0 and releases move pkgrel. build-all.sh writes
# "$name-0.1.0.tar.gz" and transforms paths to "$name-0.1.0/" for every
# component, so bumping pkgver leaves makepkg looking for a tarball nothing
# creates.
pkgver=0.1.0
# ── 0.1.0-41: the music rows had a second surface, and the errands ────────
# ── still behaved as though they had one ──────────────────────────
#
# Reported from the desktop rather than the sofa: Spotify's row invited a sign-in
# and did nothing when it was pressed, and the YouTube page — with a Firefox
# session already configured and being read correctly — was three rows of text
# that could not be pressed at all. The desktop music widget draws THIS
# package's source picker and YouTube page — the same rows,
# from the same `--rec` output, so the two cannot grow different libraries — and
# two things here were written when the television was the only reader.
#
# ⚠ A NOTE THAT NAMES ONE SURFACE IS WRONG ON THE OTHER. The Search row said
# "type a search on the television" to somebody looking at a 268px card on their
# wallpaper. What happens is the same on both and the note says that now: a
# terminal comes up to type into, which on the television is the one the
# on-screen keyboard is pointed at.
#
# ⚠ AND `fill` IS THE TELEVISION'S RULE. term_run_and_hold() and
# big_music_browse() fullscreened the terminal they opened, which is right for
# something launched from four metres away and wrong for a button on a
# wallpaper: pressing Sign in threw a fullscreen terminal over whatever somebody
# was working in. Both now pass big_running() — the shell's presence rather than
# a guess at it, since the lock is held for exactly as long as that process
# lives — so it fills the screen on the television and opens a window on the
# desktop.
#
# The shell half of the report is synui's; the rows themselves were already
# right, which is why nothing else here changed.
#
# ── 0.1.0-40: Game Pass on the television, and a tile that would have ─────
# ── been installed and invisible ──────────────────────────────────
#
# A Greenlight tile on the Play shelf, beside Moonlight, which is the shelf it
# belongs on for the same reason: it is a game that runs somewhere else and
# arrives here as a video stream. Xbox Cloud Gaming and remote play from a
# console in the house, both of which mean Game Pass on a screen four metres
# away without a Windows machine anywhere in the room.
#
# ⚠ A CLIENT AND NOT A BROWSER, and this file already knows why. browser_prog()
# carries the note about --kiosk, which this package shipped once and reverted
# the moment there was an on-screen keyboard. Pointing the Web tile at
# xbox.com/play would have re-made that mistake and added two of its own:
#
#   · the stream is THROTTLED on a Linux user-agent. Microsoft drops the
#     resolution, quietly, and the workaround is spoofing a Windows UA — a
#     string in a config that breaks with no message the day the sniff
#     changes, on a machine whose owner is sitting on a sofa.
#   · it would need vptr. The controller-as-a-mouse exists BECAUSE a browser
#     cannot be driven by words on a pipe (vptr.c), so every session would be
#     played through a simulated cursor. Greenlight takes the pad directly.
#
# ⚠ AND IT IS THE ONLY TILE IN apps_table() THAT MAY NOT BE ON PATH. Greenlight
# is distributed as a Flatpak, and a Flatpak is invisible to have(), which is
# `command -v` and nothing else. Written as have("greenlight") — which is what
# every other row in that table is — the tile would have drawn nothing, logged
# nothing, and failed on exactly the install this package recommends. Hence
# greenlight_prog(): PATH first, because a native binary is somebody's
# deliberate choice and costs one fork to find, then `flatpak info` BY ID.
#
# ⚠ `flatpak info <id>`, never `flatpak list | grep`. list prints the whole
# installation formatted for a person, and a grep over it matches a remote
# name or a branch as readily as an app. The suite pins the case that separates
# a real probe from a lazy one: a machine WITH Flatpak and WITHOUT Greenlight
# must grow no tile, and the stub answers for that one id alone so a pass
# cannot be an accident.
#
# ⚠ Its exec is THREE words — `flatpak run <id>` — where every other one is one
# or two. big_run splits on spaces into an argv, which already handled it; a
# launcher that had taken only the first word would have run bare `flatpak`
# and opened nothing. Asserted, because nothing else in the table proves it.
#
# The tile id stays `greenlight` rather than the Flatpak's. tile_for() matches
# a window's app-id against the tile id and against the LAST dot-component of
# that app-id, so a window reporting io.github.unknownskl.greenlight finds this
# row on the Running shelf in the FIRST pass, with no second spelling anywhere
# to fall out of step. The second pass would not have found it: it reads the
# first word of the exec, which for a Flatpak is `flatpak` and is evidence of
# nothing at all.
#
# ⚠ DETECTED, NEVER DEPENDED ON, and that is a judgement about the upstream
# rather than packaging habit. Greenlight's v2 is in maintenance mode — critical
# fixes only, v3 with no date — and it talks to a service whose owner can change
# it on any morning. As a detected row it behaves like Moonlight: if it stops
# working, or is never installed, the tile is simply not on the shelf. Nothing
# to unship, no broken install, no ISO regression. There is no optdepends line
# for the same reason there is none for Moonlight, and one more: the package we
# recommend is not a pacman package at all.
#
# ── 0.1.0-37: the Plex tile opened with no mouse and no keyboard ────────────
#
# Reported as "the controller mouse isn't working when I launch Plex in big
# screen like the rest of the apps" — and the comparison in that sentence is
# the whole diagnosis. Every app tile beside it on the Media shelf had a mouse.
# Plex did not.
#
# ⚠ IT IS NOT THE PLEX TILE. There is no Plex tile on this machine. Only
# `plex-media-server` is installed — the SERVER — and neither plex-desktop nor
# plexhtpc is, so apps_table() adds no Plex row at all. What is on the shelf is
# the server DISCOVERED on the network by GDM broadcast, and pressing that is
# not launching an application: it is opening somebody's web interface in a
# browser.
#
# ⚠ AND THE TWO KINDS OF TILE CAME OUT OF DIFFERENT TABLES WITH DIFFERENT
# COLUMNS. `big apps` emits eleven columns including `pointer` and `keys`;
# `big media` emitted six, and neither of those was among them. The shell gates
# the controller-as-mouse and the on-screen keyboard on those columns BY NAME —
# `shell.activeApp.pointer === "1"` — so a record without the column read
# `undefined`, which is not "1", and both were simply never started.
#
# Nothing warned. The tile launched, the browser opened full-screen on the
# television, and the pad did nothing — which reads from a sofa as a Plex
# problem, a browser problem or a pad problem, and is none of the three. The
# pad has now been innocent a fourth time.
#
# The fix is the two columns, appended per this file's own rule, and always 1:
# unlike an app tile this is not a judgement call, because a server tile is a
# URL in a browser and a browser is the exact case the pointer was written for.
# The news shelf, which opens a browser by the very same route, has always
# passed "1"/"1" — this shelf was never given the columns to pass.
#
# ⚠ AND THE CACHE FROM THE OLD BUILD IS REJECTED, which is not fussiness. The
# shell's first draw reads media.tsv WITHOUT --refresh, columns are read by
# name so a six-column file stays perfectly readable, and age cannot tell "old"
# from "wrong". Without the check the update would land and the first press of
# Plex would still have no mouse — for the ten minutes of MEDIA_TTL, which is
# ten minutes of looking exactly like the bug that was just fixed.
#
# ⚠ One existing test asserted `iconfile` was the LAST media column. That was
# true only while it was the newest one, and it made the format's documented
# rule — new columns go on the END — into something that fails the suite. It
# now asks for the column by name, like the shell does.
# ── 0.1.0-36: the toggle was LOST, and -35 answered it by skipping ──────────
#
# Reported as "the music isn't starting when I click the Music tab, and when I
# load a playlist it won't play — I have to skip to get it to play". Measured
# on velle's own playlist against the real player, and the previous fix was
# wrong about WHY.
#
# ⚠ THE FIRST TOGGLE IS SIMPLY LOST. A cliamp that has just come up and been
# given its first track answers `toggle` by doing nothing, often enough to be
# the normal case here — and then sits at `stopped` indefinitely. Sent a SECOND
# toggle, on the SAME track, it starts within four seconds. Watched directly:
# stuck at 12s, one plain re-toggle, playing at 16s.
#
# 0.1.0-35 mistook that stall for a dead track and answered it with `next`. So
# it THREW AWAY A SONG THAT WAS NEVER BROKEN — entry 1 of that playlist reports
# `public`, plays on its own in two seconds, and was skipped anyway — then
# waited twelve seconds and did it again. Thirty-four seconds from press to
# sound, with two songs missing off the front. From four metres, thirty-four
# seconds of nothing IS a button that does not work.
#
# So the recovery asks again before it asks elsewhere: re-toggle the same
# track, up to three times, and only then consider a skip. ⚠ A re-ask that
# lands late is undone — `toggle` from `playing` is PAUSE, and the settle
# samples once a second, so a `paused` player found straight after a nudge is
# toggled back. That is the 0.1.0-29 fault, guarded rather than risked.
#
# ⚠ AND THE FIRST WAIT IS FIVE SECONDS NOW, NOT FIFTEEN. Fifteen was the price
# of skipping being the only answer: throwing away a merely-slow song is
# expensive, so it had to be nearly certain. Re-asking costs nothing if the
# track was already starting, so the wait only has to beat a healthy start
# (measured 2–4s).
#
# ── and the one track that really was dead ──────────────────────────────────
#
# ⚠ THE FRONT OF THE QUEUE IS ASKED BEFORE IT IS PLAYED. `--flat-playlist` is
# what makes reading a station fast enough to be a button, and its entries
# carry a real title, duration and view count for videos that answer "Video
# unavailable" the moment anything plays them; `%(availability)s` is null for
# every entry there, available or not, so there is nothing in that listing to
# filter on. Asked about ONE video, yt-dlp answers in about a second and
# run_capture() turns its non-zero exit into NULL:
#
#     yt-dlp --simulate --print "%(id)s" <dead>  → rc 1 in 2s
#     yt-dlp --simulate --print "%(id)s" <good>  → rc 0 in 1s
#
# so a dead head is walked past before the queue is filled, rather than
# discovered fifteen seconds later with the television silent. Bounded at eight
# questions, never past the end of the list, and asked WITH the session so a
# members-only track is not dropped from under the person it plays for.
#
# Measured end to end on the reported playlist: 34s and two songs lost, down to
# 9–10s and the right song.
#
# ── ⚠ AND THE FIXTURE COULD NOT HAVE CAUGHT ANY OF IT ───────────────────────
#
# The stub is handed a PATH that is a REPLACEMENT — stub directories and no
# /bin — so `cat` and `rm` inside it are "command not found", silently. Which
# means `rm -f "$CLIAMP_STUCK"` on its `next` line NEVER REMOVED ANYTHING: the
# fixture could model a player that was stuck and could not model one
# RECOVERING, which is exactly the half these two releases got wrong. It is
# builtin-only now — state is a file's emptiness, tested with -s and cleared
# with `: >` — and `$CLIAMP_DEAF` models the lost toggle that started all this.

# ── 0.1.0-35: a track that will not play, and the silence it made ──────────
#
# The other half of what -34 was reported for, and the half that was still
# broken after it: Plex and radio played, YouTube would not start unless it was
# skipped and then played, and even then only sometimes — and the Music tile
# never started anything at all.
#
# ⚠ MEASURED AGAINST A REAL PLAYER, because this one could not be reasoned out
# from the outside: A QUEUED YOUTUBE URL THAT CANNOT BE RESOLVED LEAVES CLIAMP
# `stopped` FOR EVER. It does not skip the track, it does not end the queue, it
# writes nothing to any stream this program can read. Watched for 24 seconds:
# no movement at all.
#
# ⚠ AND SUCH A TRACK LOOKS PERFECT ON THE WAY IN. Stations are enumerated with
# `--flat-playlist` — YouTube's own listing for the playlist rather than a
# resolution of each entry, which is what makes it fast enough to be a button —
# and that listing hands back a real title, a real duration and a real view
# count for a video that answers "Video unavailable" the instant anything tries
# to play it, signed in or not. `%(availability)s` is `NA` there, so there is
# nothing to filter on. A playlist somebody built over years will have a few;
# this machine's had EIGHT, and one of them was first in the list.
#
# So: 54 tracks queued perfectly, `toggle` sent to the one at the front, and a
# television playing nothing. Skipping moved off it, and whether the next track
# worked decided whether it was worth doing — which is exactly how it was
# reported.
#
# The player is now asked to start and then asked whether it really did. Still
# stopped once the state has settled → step past that track and start again,
# up to three times, then say so rather than pretending.
#
# ⚠ THE WAIT IS THE WHOLE THING, and this package has the scars: a "make sure
# it started" check was written once before and turned a reliable station into
# one that started about half the time, because the state LAGS the command and
# `toggle` from `playing` is PAUSE. Fifteen seconds is more than twice the
# longest start ever measured here, nothing acts until it is up, and a player
# that is playing or paused is never touched.
#
# ⚠ ONE RESCUE AT A TIME, on a kernel lock. Fifteen seconds is long enough
# that the natural answer is to press the button again, and two of these are
# two processes sending `next` and `toggle` on their own timers — the second
# landing on the music the first has just started.
#
# ── 0.1.0-34: the Music tile has something to play again ───────────────────
#
# Reported straight after 0.1.0-33: music could not be started at all any more,
# and the Music tile should bring back whatever was playing last. One sentence,
# and both halves are the same fix.
#
# ⚠ THE PREVIOUS RELEASE IS WHAT EXPOSED IT, AND IT WAS STILL THE RIGHT FIX.
# Two facts, harmless apart:
#
#   · THE QUEUE DOES NOT SURVIVE THE PLAYER. `--provider` is a start-up flag
#     and what it preloads is the whole of what a fresh player has — measured
#     on this machine, `radio` comes up with eleven stations and `ytmusic`
#     with NOTHING. Every other queue here is one this package fills a track
#     at a time over cliamp's socket, and nothing on the other end writes it
#     down.
#   · SINCE 0.1.0-33 THE PLAYER DOES NOT SURVIVE QUIT, because a headless
#     player left running is music with no way to stop it.
#
# Together: Quit, then press Music, and the tile started a bare player, sent
# `toggle` to an empty queue and played silence. Everything about it looked
# like it worked — the tile responded, the player came up, the Now Playing row
# appeared. Measured before the fix: `"state":"stopped","index":-1`, and no
# `total` key at all, which is how cliamp says the queue is empty.
#
# So whatever fills a queue now writes down what filled it, and the tile puts
# it back: press Music on a machine with no player and the last station,
# album or library comes back on. A player that is up WITH something queued is
# resumed exactly as before — reloading it would restart the station from its
# first track every time somebody pressed Music after a pause, which is a
# worse bug wearing the same clothes.
#
# ⚠ THE RECORD CARRIES ITS SOURCE, and that is not bookkeeping. Replaying a
# YouTube station goes through the same path that PLAYS one, which writes
# `music_source` — so resuming a station after somebody had deliberately moved
# the picker to Plex would silently undo the choice they just made. A record
# from another source is not this source's to resume.
#
# ⚠ AND IT IS A REFERENCE, NEVER A TRACK URL: a Plex rating key, a station
# URL, a music directory — the thing that was asked for. The URLs cliamp is
# actually handed carry the Plex token, and nothing here writes one into a
# cache file.
#
# ⚠ ONLY OUR OWN PLAYER IS RESTARTED, on the same marker and the same argument
# as `release` in -33: putting a station back means restarting the player,
# because `--provider` is a start-up flag, and a cliamp somebody has open in a
# terminal is not this launcher's to restart.
#
# ── 0.1.0-33: the chord stays out of games, and Quit lets go of the music ───
#
# Both reported against 0.1.0-32, and both are the same shape of mistake: a
# thing that keeps running after the moment it belonged to.
#
# ── the chord only STARTS from the television's own screen ──────────────────
#
# `big nav` keeps reading the pad while this interface is stepped aside — that
# is how Guide comes back from inside a game — so L3+R3 was live in the game
# too. And L3+R3 is a REAL BINDING in plenty of them: sprint, melee, crouch.
# Left as it shipped, a shortcut meant for a launcher would throw a
# full-screen visualizer over somebody's game mid-fight, several times an
# evening.
#
# So it does not launch while the interface is away. ⚠ `away` is BOTH
# conditions at once, which is why there is one test and not two: it is true
# while an application is in front, and true when Guide has simply put the
# interface away. Turning the visualizer OFF is above that guard and stays
# ungated — that press always arrives while stepped aside, because the
# visualizer is what is on the screen.
#
# ── Quit lets go of the music ───────────────────────────────────────────────
#
# Reported as "I select Quit and it stays open in the background, or at least
# it leaves the music running". The second half was exactly right.
#
# The player this interface starts is HEADLESS — `script -qfc cliamp …`, a TUI
# on a pty with no terminal attached. It has no window; it is not a toplevel,
# so nothing in the dock or the switcher can reach it; and synui's bar has no
# MPRIS controls. Quitting and leaving it playing is music with NO WAY TO STOP
# IT short of opening a terminal and typing `cliamp stop`.
#
# ⚠ AND "ALWAYS STOP IT ON THE WAY OUT" IS THE WRONG FIX. A cliamp somebody
# has open in a terminal is not headless and is not ours — big screen mode
# drives it happily over the same socket while it is up, and ending it on the
# way out would be this launcher reaching over somebody's music. So a marker in
# $XDG_RUNTIME_DIR records the one thing that tells them apart: whether THIS
# package started the player. `big music release` ends only what it claims,
# and the claim is written only where a player was really started —
# music_ensure_running()'s "already running" path deliberately does not make
# one. It is dropped when the player stops, and when the player turns out to
# have gone by itself.
#
# ⚠ EVERY WAY OUT GOES THROUGH ONE FUNCTION NOW. There were three doors onto
# Qt.quit() — the tile, Escape, and `big stop` over the control socket — and a
# fix applied to one of them would have been a way out that still left the
# music behind.
#
# ⚠ AND QUITTING CANNOT HANG. `release` sends a SIGTERM and waits for the
# socket to go, which is fast but not guaranteed; a four-second timer races the
# exit handler, because a Quit that hung on a music player refusing to die
# would be a television nobody can get out of.

# ── 0.1.0-32: the visualizer on a shortcut — both stick clicks, or V ────────
#
# Asked for, and it is the right tile to give one to: the visualizer is the one
# thing somebody turns on WHILE something else is already playing, and three
# rows into the Start menu is the wrong distance for that.
#
# ⚠ A CHORD, AND THE STICK CLICKS STILL MEAN NOTHING APART. The nav stream
# deliberately drops every button a menu has no meaning for — a stream carrying
# spare events is one where a new button silently becomes a navigation command
# — so L3 and R3 are still absent from that map. Only the PAIR says a word, and
# the word is "visualizer".
#
# ⚠ LATCHED, ONCE PER HOLD. Both sticks going down is two events and whichever
# lands second completes the chord; without the latch a hold that wobbled —
# one thumb lifting and returning while the other stayed down — would toggle
# twice and read as the press having been ignored. Latched PER PAD, because two
# controllers on a sofa are two people and a chord is one pair of thumbs.
#
# ⚠ AND IT IS THE ONE THING IN THAT SWITCH THAT WATCHES RELEASES. Everything
# else there acts on the press; this has to see the release to clear its latch,
# which is why it is offered the code before the button map and handed the edge
# rather than a press-only filter.
#
# On the shell side it is handled BEFORE every guard in nav(), because it
# addresses the machine rather than the screen: it means the same thing with
# the Start menu open, with a close dialog up, and with the selection parked on
# a media button. And above the keyboard branch in navAway(), which is the half
# that would have broken silently — the visualizer is turned OFF from in front
# of it, and a word falling through to the on-screen keyboard would have been
# typed as a letter.
#
# Stopping it is `comeBack()` and not a second mechanism: the tile is
# `transient`, so it ends exactly as pressing Guide would — the path 0.1.0-28
# made actually let go of projectM.
#
# V on a keyboard is the same call, so the two input paths stay one
# implementation. ⚠ It only reaches the shell while the interface is ON SCREEN:
# stepped aside, keyboard focus belongs to the application in front, which is
# why the pad chord is the one that can turn it back off.
#
# The legend names it, and only where projectM is installed — the same rule the
# X button already follows, one button further along.
#
# ⚠ Proven in the rig rather than asserted at: the chord itself needs a real
# pad, but the WORD it produces is what the rig already speaks, so everything
# the shell does with it is driven — launched from the main screen with no menu
# open, and stopped by a second press sent while stepped aside. Removing the
# navAway half leaves it "STILL RUNNING after the second press".

# ── 0.1.0-31: the browser prompt takes a NUMBER ─────────────────────────────
#
# Reported the first time the sign-in was used, and it is entirely fair:
#
#     1  firefox   · installed here
#     2  vivaldi   · installed here
#     …
#     Browser (or Enter to cancel): 1
#     syn-arcade: yt-dlp cannot read cookies from '1'
#
# 0.1.0-30 printed an unnumbered list and asked for a NAME. Everything else on
# this system takes a number — the search picker two rows away takes a number —
# and spelling out `vivaldi` on an on-screen keyboard is precisely the errand
# the terminal trick exists to keep short. So the list is numbered, and the
# INSTALLED browsers are numbered first: on a machine with one browser the
# answer is `1` rather than however far down a fixed list it happened to sit.
#
# A name still works, for whoever is at a keyboard. ⚠ The digit test is on the
# WHOLE answer rather than the first character, or `2fast` would be a number
# and a browser whose name began with a digit could never be typed.
#
# ── three things the assertions for it got wrong first ──────────────────────
#
# Worth writing down, because each one failed against code that was correct:
#
#   · `script` was called by NAME inside the cut-down stub PATH, which is a
#     REPLACEMENT of three directories and does not contain /usr/bin. Command
#     not found, no output, four assertions red.
#   · `grep -q` on the pipe, for a pattern matching the FOURTH line of a
#     nine-line list: it exits on the match, the writer dies of SIGPIPE, and
#     pipefail reports 141. The assertions whose text sat at the END of the
#     output got away with it. The answer goes to a FILE now.
#   · `firefox$` as an anchor, against output that came off a PTY and therefore
#     ends every line CR-LF. The anchor cannot match with the CR still to come.
#
# And the fixture itself: `have()` shells out with the PATH it was handed, so
# inside the suite NOTHING is installed. The test now stubs `chromium` — THIRD
# in the fixed list — and asserts it comes back as 1, which makes the
# reordering the thing under test rather than the printing.

# ── 0.1.0-30: your OWN playlists, and a search you can type from the sofa ───
#
# 0.1.0-29 put YouTube Music stations on the television. Two things were still
# missing, and one of them was a regression this release owns up to.
#
# ── signing in, and why it is a BROWSER rather than a Google Cloud project ──
#
# "Log in so I can use my own playlists" has two possible answers here and they
# are wildly different amounts of work:
#
#   OAuth client     what cliamp's own wizard asks for. Unlocks search and
#                    browse INSIDE CLIAMP'S TUI, and takes a Google Cloud
#                    project, an enabled API and a client id and secret copied
#                    out of a web console — after which somebody is driving a
#                    terminal interface from four metres away.
#   browser cookies  what yt-dlp takes. Unlocks somebody's own PRIVATE
#                    playlists and Liked Music, listed as rows on the
#                    television and played with a d-pad.
#
# Both are offered. The second is what the row leads with, because it is the
# one that answers the question that was asked. `big music yt login` writes the
# browser name to big.conf and hands it to yt-dlp; nothing here reads, logs or
# caches a cookie — yt-dlp opens the store itself, in its own process, for the
# length of one command.
#
# ⚠ AND IT VERIFIES RATHER THAN WRITING THE SETTING DOWN, because every failure
# in this area is silent. A browser that was never signed in decrypts its
# cookies perfectly and answers 401, on a stderr a television never shows —
# measured here, Vivaldi with no YouTube session gave SEVEN cookies, `v10`
# every one, and nothing at all. So the setting is written, the account is
# asked what it has, and what comes back is printed by name. A failure takes
# the setting BACK OUT: leaving it would put --cookies-from-browser on every
# enumeration from then on and keep answering "no playlists" as though the
# account were empty.
#
# ⚠ The cookies go on EVERY enumeration, not only the one that lists a library.
# A private playlist is private at PLAY time too, and without them a station
# that lists perfectly resolves to nothing when it is pressed.
#
# ── typing, which is a TERMINAL and not a text field ────────────────────────
#
# The on-screen keyboard types through wtype into whatever holds keyboard
# focus, and the shell's own surface deliberately holds none — a menu that
# grabbed the keyboard to draw a keyboard would type into itself. So there is
# no text field to put on that page. `Search…` opens a TERMINAL on the
# television with `keys: "1"`, which is the same mechanism the install and
# sign-in rows have always used, and types into that.
#
# `big music yt find` asks for a query, lists what came back, and plays the
# number chosen — or keeps it as a station first with `s<number>`, which is
# what finally makes stations addable without a keyboard.
#
# ⚠ The two verbs that read stdin ask whether there IS anybody to ask
# (`isatty`) and open that terminal themselves when there is not. Launched by
# the television as a plain process they have no tty at all: fgets returns on
# EOF immediately and the command flashes and exits having done nothing, which
# is exactly the shape of a dead button. It cannot loop — the command inside
# the terminal has a tty, so it takes the interactive branch.
#
# ── and the regression, which is the interesting half ───────────────────────
#
# ⚠ 0.1.0-29 LEFT `cliamp setup` REACHABLE FROM NOWHERE. In -28 the YouTube
# Music row's action WAS `setup`, so the OAuth route was one press away; -29
# replaced that action with the stations page and never put the route back.
# The C answer changed and nothing in the shell noticed — which is the same
# second-roster failure this file keeps warning about, committed by the
# release that added an assertion against it.
#
# So which errands exist is big.c's answer now, in the same list as the
# stations and marked with a `kind` column: Search, then Your playlists or Sign
# in depending on whether there is a session, then cliamp's own search while it
# still has no client. The shell dispatches on the kind and knows none of the
# reasoning.

# ── 0.1.0-29: YouTube Music PLAYS on the television now, and needs no ───────
# ── account to do it ────────────────────────────────────────────────────────
#
# 0.1.0-28 stopped the YouTube Music row lying about what it needed. This is
# the other half: it does the thing.
#
# ⚠ THE FACT THE WHOLE RELEASE TURNS ON — the account is for BROWSING, and for
# nothing else. cliamp's `resolve.go` sends every YouTube URL through yt-dlp
# and the native client, neither of which ever sees an OAuth token; the Data
# API is only how its TUI searches. Measured on this machine with no
# credentials anywhere:
#
#     yt-dlp --flat-playlist --print "%(title)s" --print "%(webpage_url)s" \
#            "ytsearch3:daft punk one more time"     → titles and watch URLs
#     cliamp queue "https://www.youtube.com/watch?v=…" → the queue went 11 → 12
#
# So both halves a television needs — find something, and play it — were
# reachable all along. `big music yt` is a station list: one line of
# ~/.config/syn-arcade/ytmusic.list per station, a URL and a name, and a
# station is anything yt-dlp can enumerate — a playlist, an album, a track, or
# a `list=RD…` MIX, which is YouTube's own endless station and the closest
# thing here to radio. Choosing one queues it and starts it through exactly the
# path a Plex album takes, and the Start menu grows a page for it.
#
# `big music yt search <words>` is there too, and returns rows whose id IS the
# URL — so the shell will play a search result with the same command it plays a
# station with when the television grows a way to type.
#
# ── three things measured rather than assumed ───────────────────────────────
#
# ⚠ THE SOURCE IS WRITTEN BEFORE THE PLAYER IS RESTARTED, and that is what
# makes the queue empty. `--provider` is a start-up flag: a fresh cliamp on
# `radio` arrives with ELEVEN stations already queued and one on `ytmusic` with
# nothing at all. A station played while the setting still said radio queued
# sixty tracks at positions 12..71 and `toggle` played Lofi Stream — the press
# worked perfectly and the television played internet radio.
#
# ⚠ THE FIRST TRACK IS STARTED BEFORE THE REST ARE QUEUED. Sixty tracks is
# sixty `cliamp queue` processes; queueing the lot first is a quarter of a
# minute of silence after the press, which from four metres is a dead button.
# Started first, music is playing at t+6s while the queue fills behind it.
#
# ⚠ AND NOTHING TOGGLES A SECOND TIME. The obvious insurance — check the state
# once the queue is full, toggle again if it is not playing — was written, and
# it turned a reliable station into one that started about half the time.
# `toggle` from `playing` is PAUSE and the state LAGS the command by about two
# seconds, so the check ran early every time, always saw `stopped`, and always
# cancelled the start it was meant to insure. Two of four runs never played
# with it; four of four without.
#
# ── and the title, which is the bug that would have shipped ─────────────────
#
# cliamp reports a queued track's title as its PATH, so everything this package
# queues writes down the real name against a key. That key was "the path with
# everything from the `?` dropped" — right for Plex, whose query is a token
# that must never reach a cache file, and WRONG FOR YOUTUBE, where `?v=` is
# which song it is. Every track on the site keyed to
# `https://www.youtube.com/watch`, and the television called every song on a
# station `watch`. `music_key()` is the one home for that rule now, and cliamp
# naming a track itself no longer stops us looking up the name we wrote down.
#
# ⚠ Adding a station is still a command line — the on-screen keyboard types
# into the window UNDERNEATH the shell, so there is nothing on the television
# to type a URL into yet. The page says so rather than looking empty.

# ── 0.1.0-28: asking the visualizer to stop is not the same as it ───────────
# ── stopping, and YouTube Music never had credentials to browse with ────────
#
# Two reports from the sofa, and the first is 0.1.0-26's fix arriving at the
# same symptom it was written for: press Guide to come back from the
# visualizer, and the frozen visualizer is still there.
#
# The signal chain was right and it was not enough. SIGTERM's default action
# ends a process; a program that CATCHES it does not have to, and a program
# that catches it and answers from an event loop cannot if that loop is the
# thing that is stuck. projectM-pulseaudio is exactly that — `nm -D` shows it
# imports both `signal` and `pa_signal_new` — and covered by the interface it
# is occlusion-culled, gets no frame callbacks, and has no loop turning to
# deliver the quit TO. The journal has the whole shape of it: Guide at
# 16:08:16, and projectM printing TERMINATED at 16:08:24, eight seconds later
# and only once the television had stepped aside and handed it frames again.
#
# So the polite signal is asked FIRST and enforced SECOND: `alarm()` is armed
# as the TERM goes out, and SIGALRM sends the process group a SIGKILL, which no
# program catches and no stuck loop can sit on. Two seconds of grace, and ONLY
# for the tiles that asked for it — `insist` comes off the same `transient`
# column that decides what is signalled at all, so Ctrl+C on `big run
# steam-bpm --wait` still means "stop waiting" rather than "SIGKILL Steam".
#
# ⚠ AND THE RIG IS WHY IT SHIPPED. bigscreen_rig.sh stubs `big run` with
# `exec sleep 300`, and sleep dies of SIGTERM the way the manual says — so the
# rig agreed the fix worked against the one property projectM does not have.
# The suite now stands in for it with a process that CATCHES the signal and
# carries on, and asserts on the process being GONE rather than on the signal
# having been sent. Against polite-signal-only code that assertion fails.
#
# ── and the second: YouTube Music was a row that could not work ─────────────
#
# "The cliamp menu to install YouTube Music worked but now it just says open
# with cliamp, but cliamp never got setup and I don't see how."
#
# Both halves true. yt-dlp is what PLAYS a YouTube URL; a Google OAuth desktop
# client is what BROWSES for one, and `browse` is the second of those. cliamp
# v1.63.2 ships an EMPTY fallback credential pool — external/ytmusic/fallback.go
# declares `var fallbackCredentials []oauthCreds` with no entries — so with no
# client_id/client_secret in config.toml it prints "YouTube: no credentials
# available" to a stderr nobody on a sofa is reading and registers no YouTube
# provider at all. Measured against the installed binary.
#
# ⚠ INCLUDING AFTER ITS OWN WIZARD SAYS YES. `cliamp setup` calls its first
# YouTube Music mode "Use built-in credentials (recommended)" and writes
# `[ytmusic]\nenabled = true` against that empty pool. So the row now gates on
# the CREDENTIALS rather than on the section, and says which mode to pick.
#
# Also in this release:
#   · the note on a `setup` row is per SOURCE rather than per action. Both
#     services answer `setup` and they are not the same errand — setting up
#     YouTube Music was being told it needed Spotify Premium.

# ── 0.1.0-27: a band is ONE SHAPE of tile ───────────────────────────────────
#
# On a 1080p laptop with three games installed, the whole interface arrived in
# the top half of the screen with a hole in the middle of it. The packer was
# right by its own rules: three covers FIT, and a shelf that fits is packed
# into a band with whatever comes next — so Games shared a row with Play, Media
# and Apps. But a Row takes the height of its tallest child and aligns them all
# at the top, and a cover strip is about fourteen units tall against a bar's
# eight. Half that row was empty by construction, and the screen was a row
# short below it.
#
# ⚠ INVISIBLE ON A REAL LIBRARY, which is why it shipped: fifty games overflow,
# an overflowing shelf keeps its own row, and the machine this was written on
# has fifty-three. It takes a SMALL library to reach at all — a laptop, or a
# fresh install. `GAMES=3 SIZE=1920x1080 tests/bigscreen_rig.sh …` draws it.
#
# The band mechanism was always about sharing a row between shelves that are
# the same kind of thing — Media and Apps, three tiles each, two thirds of both
# wasted. Two shelves whose tiles are different heights are not that, however
# well they fit, so a shape change now ends the band. `isPortrait()` is the one
# home for that fact and both drawing paths read it.

# ── 0.1.0-26: media buttons on the television, and a visualizer that is ─────
# ── ENDED rather than hidden ────────────────────────────────────────────────
#
# Three things from the sofa, and the first of them is the interesting one.
#
# ⚠ THE VISUALIZER CAME BACK FROZEN, and the way out was to resize its window
# twice with a mouse. Open it, press Guide to go back to the interface, press
# Guide again — a still picture. It is not a drawing bug and it is not synui's
# fault: a surface fully covered by an OPAQUE one is occlusion-culled by the
# compositor and gets no frame callbacks, and projectM does not idle without
# them. Measured on a headless rig: 62 frames a second uncovered, ZERO covered,
# and 100% of a core burnt drawing frames nobody will ever see.
#
# So the visualizer is now ENDED when the interface comes back. It has nothing
# to be away from — it draws the music that is playing anyway — and a fresh one
# is what the tile starts next time, full-screen, with nothing to restore.
#
# ⚠ AND KILLING THE SHELL'S PROCESS WAS NOT ENOUGH. `big run <id> --wait` gives
# the application its own SESSION, so a signal to the waiter left projectM
# drawing away behind the television with nothing holding a handle on it. The
# waiter forwards the signal to the whole process group now — which is the only
# way the shell can end anything at all, since a layer-shell surface cannot
# close a window and there is no pid anywhere in QML.
#
# ── MEDIA BUTTONS, in the middle of the legend along the bottom ─────────────
#
# ⚠ THEY DRIVE WHATEVER IS PLAYING, NOT CLIAMP. `big music` is one player over
# one socket, which is right for the Music tile and worth nothing the moment
# somebody is listening to Spotify or watching a film. `big transport` speaks
# MPRIS2 — the interface every media player on Linux implements — through
# busctl, which is on every machine because systemd is. It still drives cliamp
# over its own socket when cliamp is what is playing: cliamp's MPRIS reports
# the FILE PATH as the track title, and for a Plex stream that path carries the
# account token in its query.
#
# Reachable four ways, because a button reachable one way is a button half the
# room cannot press: DOWN from the last shelf (the one direction that used to
# do nothing), a mouse, and the media keys on a keyboard or a remote. They are
# drawn only while something is playing.
#
# ── YouTube Music and Spotify no longer dead-end ────────────────────────────
#
# Reported from the sofa: both rows opened cliamp, and cliamp appeared to
# support neither service. Both were true, for unrelated reasons: YouTube
# Music plays through yt-dlp, which is an OPTDEPEND of cliamp and is not on a
# stock install — without it the provider is there, is selectable, and returns
# nothing — and Spotify needs a [spotify] section and an account. So the rows
# say which, and pressing one installs the package or opens cliamp's own
# sign-in wizard on the television rather than opening an empty library.
#
# ⚠ The installer passes `--noconfirm` BEFORE the verb. synpkg stops parsing
# global options at the first non-option argument, and a front-end that cannot
# answer a prompt otherwise authenticates through polkit, declines itself, and
# installs nothing while reporting success. Twice now.

# ── 0.1.0-22: FIT TO SCREEN — the gamescope wrapper, made once and kept ─────
#
# An old game renders at 640x480 or 1024x768 and has no idea what a 2560x1440
# monitor is, so it sits as a postage stamp in the middle of a black screen.
# gamescope fixes it, and the line that does is genuinely hard to remember:
#
#     gamescope -w 1024 -h 768 -W 2560 -H 1440 -f -F fsr -- wine Sims.exe
#
# ⚠ THE WHOLE TRAP IS THAT -w AND -W ARE DIFFERENT FLAGS. Lower case is the size
# the GAME renders at; upper case is the size of the SCREEN it is stretched
# onto. Swap them and the game is asked to render at the monitor's resolution —
# which is the thing being avoided, and which most of these games cannot do at
# all. Nothing warns; it looks like a bug in the game.
#
# And then the line has to be kept somewhere, which by hand means writing a
# .desktop file with Exec, Path, Icon and StartupWMClass right and remembering
# where it went the next time the resolution needs changing.
#
# `syn-arcade fit` keeps one small config file per wrapper, generates the
# command from it, and writes the menu entry — and, if asked, the desktop icon —
# that runs it. In the window it is a tab: pick a game from what is installed,
# press two resolutions, press Create.
#
# ⚠ The .desktop's Exec is `syn-arcade fit run <id>`, NOT the assembled
# gamescope line, so editing a wrapper changes what the menu entry does with no
# file in ~/.local/share/applications being rewritten and no menu cache to
# invalidate. The assembled command is written into that file as a comment, and
# `fit command <id>` prints it, so nothing about it is hidden.
#
# ⚠ `--from` on an entry that ALREADY carries a hand-written gamescope line
# takes it apart rather than wrapping it. Without that, the entries most worth
# wrapping — the ones somebody already fought with once — produced a wrapper
# around a wrapper: two nested micro-compositors, an extra composite of every
# frame. The sizes, the filter and the environment come out into fields that can
# be edited, and everything after `--` becomes the game's own command.
#
# ⚠ It warns about Lutris and Proton, because gamescope around Proton ABORTS
# INSTANTLY — SIGABRT, status 134, no window and nothing in any log, which sends
# people looking at the game rather than at the wrapper. Plain wine under
# gamescope is the combination that works.
#
# ⚠ The desktop icon needed a change in the COMPOSITOR to be visible at all:
# synui has no inotify watch on ~/Desktop, so a file written there did not
# appear until icons were toggled off and on. synui 0.1.0-374 adds
# `dispatch deskicons_refresh`, which `fit` calls after writing or removing one.
# Without it the checkbox would have been the exact shape of dead button this
# project keeps writing memos about: everything done correctly, nothing on
# screen.
# ── 0.1.0-23: the Visualizer tile was breaking the machine's audio ──────────
#
# Pressing it stopped the music, froze the player, and left everything quiet and
# muffled with the volume control making no difference. All three are one fault.
#
# projectM-pulseaudio does NOT honour PULSE_SOURCE — a claim that used to be in
# big.c's own comment. It enumerates sources with pa_context_get_source_info_list,
# connects to one BY NAME, and remembers the choice in its own Qt config. The
# saved device on this machine was `bluez_input.…` — the MICROPHONE of a
# Bluetooth headset. A Bluetooth device cannot do high-fidelity playback and
# microphone input at once, so opening that mic makes WirePlumber switch the card
# from A2DP to the HSP/HFP headset profile: the OUTPUT drops to 16kHz mono
# (quiet and muffled, and no volume setting can undo it, because the level was
# never the problem) and the sink is destroyed and rebuilt, which breaks every
# stream playing through it.
#
# So the device is now WRITTEN into the file projectM actually reads, it is only
# ever written when it is a MONITOR, and the name is verified against
# `pactl list sources` rather than composed and hoped for — `<default-sink>.monitor`
# is not a real device when synui's equaliser is in the chain. With no monitor
# to be found it refuses to start rather than opening a microphone.
#
# ⚠ And `tryFirstAvailablePlaybackMonitor` is set to FALSE, which is the
# opposite of what it sounds like: while it is true, projectM runs its own scan
# INSTEAD of opening the device named beside it, and that scan is what picks a
# microphone. Measured both ways round.
#
# Also in this release:
#   · a BIG SCREEN tab, with the settings that are easier to make with a
#     keyboard than with a pad from four metres away: which screen it opens on,
#     the music player, where the music comes from, at-login and the guide
#     button. Those rows are GONE from Shortcuts rather than repeated.
#   · `big output` and `big player` — the screen and the player were settings
#     that could only be spelled by hand-editing big.conf, which is no use to
#     somebody sitting in front of a television that opened on the wrong
#     monitor.
#   · the Start menu's keyboard key is S, for Start. It was P, which stood for
#     nothing and was remembered by nobody.
# ── 44 ──────────────────────────────────────────────────────────────────────
#
# Three things velle asked for from the sofa: GeForce NOW on the television, a
# row of what was opened recently, and a mouse wheel that browses.
#
# · A GeForce NOW tile on the play shelf, wherever syn-gfn is installed. ⚠ It
#   takes BOTH a pointer and a keyboard, unlike Moonlight and Greenlight beside
#   it: those are applications with their own controller support, and this is a
#   WEB PAGE until a game starts — choosing one takes a cursor and signing in
#   the first time takes letters. `full`, because a browser opens a window and
#   a window on a television is a titlebar and a strip of wallpaper around
#   somebody's game.
#
# · A Recent bar, newest first, capped at eight. ⚠ RECORDED BY `big run --wait`
#   ITSELF rather than by the shell, so there is no way into a tile that
#   forgets to remember it, and no second list to disagree with the first. It
#   stores IDS and looks each one up when the shelf is drawn, so a tile whose
#   program has been removed is simply not drawn and returns to its place if it
#   comes back — the same arrangement synui's plugin order has.
#   ⚠ It is a BAR (kind "app"), first among Play/Media/Apps, so it costs no row
#   of its own: a shelf here would push Games down, which is the change the
#   shelf order was rearranged to undo, and a whole row of a television for
#   four tiles is the mistake the system switches were moved behind Start to
#   fix. The way out (`desktop`, which has no command) and every `run` without
#   `--wait` are deliberately NOT presses — a "recently opened" row whose first
#   entry is "go back to the desktop" is a row nobody reads twice.
#
# · A mouse wheel, mapped onto the same words the pad and the keyboard send, so
#   it reaches the shelves, the Start menu, the media buttons and the on-screen
#   keyboard without any of them knowing a wheel exists. ⚠ Accumulated to
#   120-unit notches with the remainder kept: acting on every event makes a
#   trackpad tear through fifty covers, and rounding each event to a step makes
#   a fine wheel move nothing. And it moves the SELECTION, not a view — the
#   shelves' position is derived from what is selected, so a scrolled view
#   would snap back the moment anything else moved.
# 47: THE ARCADE WINDOW IGNORED THE DESKTOP FONT. Not one Text named a family
#   and all fifty-nine pixel sizes were literals, so it kept whatever face and
#   size Qt resolved at startup while every other window in the suite followed
#   ~/.config/synui/font.state. velle, 2026-08-25: "font size isn't system
#   wide. it's supposed to be. menus apps everywhere."
#   ⚠ BOTH HALVES HAVE TO BE BINDINGS. Qt resolves an application's default
#   font ONCE at startup and QML cannot change it afterwards, so naming the
#   family on every Text is the only way the face changes while the window is
#   open, and the size has to go through ui() for the same reason. Doing one
#   and not the other gives a window that follows the desktop right up until
#   somebody changes it — which looks correct at exactly the moment it is
#   tested. The three deliberate "monospace" faces stay literal: a command to
#   type is not prose. They still take the scale.
#   ⚠ THE BIG SCREEN TAKES THE FAMILY AND NOT THE SCALE, deliberately. It has
#   no pixel-size literals to wrap — every glyph on it is a multiple of `win.u`,
#   derived from the screen it is drawn on, which is what makes it readable
#   from a sofa on a 55" television and on a 15" laptop panel both. Multiplying
#   that by a percentage chosen for a desk monitor would break the one
#   relationship its layout depends on. A face is a face at any distance; a
#   ten-foot UI's sizes are not the desktop's.
#   tools/preflight.sh gates this repo-wide now, and names the exemption.
# 52: THE TELEVISION HAS A BACKGROUND, AND THREE MORE TILES.
#
#   Background. Big screen mode drew a flat #05060a behind its tiles, which on
#   a wall-sized panel four metres away reads as a screen that has not finished
#   loading — the desktop it is the other face of has a picture, and this
#   looked like a different, emptier machine. `background` in big.conf, and
#   the default `desktop` follows synui's wallpaper.
#   ⚠ IT READS synui's OWN CONFIG rather than a copy of it, in synui's own
#   order — wallpaper.state over synuirc, a per-output line over the global
#   key. A path copied in at install time would be right once and wrong the
#   first time somebody pressed Super+W.
#   ⚠ `matrix` and `none` resolve to NOTHING on purpose. The kanji rain is a
#   live GL surface with no still to hand a QML Image, so the alternative to
#   drawing nothing is a broken file:// URL that draws nothing anyway and says
#   so in a log nobody on a sofa is reading.
#
#   Twitch, YouTube and Spotify. None of the three ships a Linux application
#   worth putting on a television, so the tiles open the site in the browser
#   told to behave like one — --kiosk for Firefox's family, --app= for
#   Chromium's. ⛔ pointer AND keys on every row: that pair is what brings up
#   the controller-mouse and the on-screen keyboard, and a browser tile without
#   them is a web page on a television that cannot be clicked or typed into.
#   That is the bug the Plex tile shipped with. Switchable in the window; all
#   three are on where a browser exists.
#
#   The remote. Half a television remote's buttons landed on `default: return`.
#   Back, Select, Menu, Guide, Home, channel up/down and the transport keys an
#   infrared receiver sends (Key_Play/Stop/AudioRewind/AudioForward, which are
#   NOT the XF86Audio* spellings a keyboard sends) all reach the shell now.
#   ⛔ BACK IS NOT ESCAPE. Escape quits, deliberately — somebody at a keyboard
#   has a way back that somebody on a sofa does not. Folding Back in with it
#   would mean the most-pressed button on the remote closed the interface, and
#   the way back in is a key combination nobody holding a remote can press.
#   ⚠ An unknown Qt.Key_* in QML is `undefined`, and `case undefined:` never
#   matches an integer — a dead branch that raises nothing. The suite checks
#   every one of them against Qt's own qnamespace.h.
# 53: THE TELEVISION HAS SETTINGS, AND STOPS BLANKING IN THE MIDDLE OF THINGS.
#
#   Start ▸ Settings, three pages, every row a press of A. Which shelves the
#   television draws (Running, Games, Recent, Play, Media, Apps, News), what
#   the Start menu itself offers, and whether the screen is allowed to sleep.
#   `syn-arcade big settings` is the same thing at a prompt, and all of it is
#   big.conf.
#
#   ⚠ THE SHELL LEARNS NOTHING ABOUT WHAT A SETTING CAN BE. Every row is an id,
#   a word and the word its value goes by; A sends `big settings <id> next` and
#   the list is read again. So a switch and a three-way choice are the same row
#   in the QML, and a setting that grows a fourth value grows it in big.c
#   alone. There is no list of values in the shell to fall out of step.
#
#   ⛔ THE WAY OUT HAS NO SWITCH. Desktop and Quit survive every setting being
#   off, and the suite asserts it. This is a full-screen surface holding the
#   keyboard, and on a gamepad there is no key combination to fall back on — a
#   setting able to hide the exit is one able to trap somebody in front of
#   their own television. Sleep, restart and power off are hideable BECAUSE
#   leaving is still there when they are gone.
#
#   Power, which is the half that was missing rather than merely unreachable.
#   `keep_awake = never | playing | always`, defaulting to `playing`, and it
#   holds a Wayland idle inhibitor through synui's own synui-idle-inhibit.
#   ⚠ NOT A SECOND IDLE MACHINE. Dim, blank, lock and suspend stay synui's, and
#   every stage of that already asks whether anything holds an inhibitor — the
#   same rule the Sleep tile follows in running `systemctl suspend` rather than
#   reimplementing it. Timeouts here would be the couch and the desk drifting
#   apart.
#   ⚠ TWO CASES NOTHING ELSE COVERS, which is why the default is not `never`:
#   cliamp is a HEADLESS player with no surface and no MPRIS inhibit, so the
#   machine dims and suspends in the middle of an album; and a gamepad is not
#   Wayland input, so a controller-only game is read from evdev by this package
#   while the compositor counts the whole session as idle.
#   ⚠ RELEASED BY EXITING, never by being told to stop. `big awake` holds the
#   helper's stdin open and the helper exits on EOF, so a SIGKILL it cannot
#   catch releases the inhibitor too — asserted both ways. A helper that had to
#   be told would leave a machine that never sleeps again after one crash.
#   ⚠ AND NO access() IN FRONT OF THE fork. Checking the helper's name and then
#   execl'ing the same name resolves it twice, which check-toctou.sh refuses —
#   and the check could report nothing the failed exec does not. 127 from the
#   child is the answer to missing, not-executable and a failed exec alike.
#
#   ⚠ `page` WAS NOT ENOUGH ON ITS OWN ANY MORE. It meant "the music source
#   page" for as long as that was the only page there was, so the settings
#   pages arriving as `page` rows put a music note beside Shelves and Power.
#   Found by the rig's screenshots, not by a grep.
#
#   ⚠ AND B NOW GOES UP ONE LEVEL. These are the first pages two deep, and
#   Back sent every one of them to the top — which from Shelves reads as the
#   button having closed too much. Asserted through music.log rather than a
#   screenshot: row 0 of the settings page opens a page and row 0 of the main
#   page toggles the player, so the wrong answer writes a line.
# 54: THE SAME SETTINGS AT THE DESK, AND A PANEL THAT COULD NOT BE SCROLLED.
#
#   Shelves, Start menu and Keep the screen awake in `syn-arcade gui` too, on
#   the Big screen tab beside the settings that were already there. Both
#   surfaces draw whatever `big settings` says exists, so a row added to the
#   table in big.c turns up in both with no change to either window — and the
#   rule that Desktop and Quit have no switch lives in that table rather than
#   in either one, which is what stops one of them growing a switch the other
#   refuses to.
#
#   ⚠ `big choices <id>`, because a MOUSE PICKS AND A GAMEPAD CYCLES. The
#   television asks for the next value and never needs the list; the window
#   draws a row of chips and needs all of it. The values could not travel as a
#   joined field in the settings record: rec_row percent-encodes, and a comma
#   inside a label encodes to exactly what a comma separator does. Same shape
#   as `hud choices hud-position`, for the same reason.
#
#   ⛔ AND THE BIG SCREEN PANEL COULD NOT BE SCROLLED AT ALL. It was a
#   ColumnLayout anchored to fill the tab, so content past the window's height
#   was not merely off screen — there was no Flickable to drag and no scrollbar
#   to say there was more. It was already at the edge before this: Web apps sat
#   on the last visible row of a 720-high window. Now the same Flickable the
#   Fit editor uses, scrollbar and all — measured in the rig at h=566 ch=966,
#   so it really does have somewhere to go.
#   ⚠ The ColumnLayout's `Item { Layout.fillHeight: true }` tail filler went
#   with it. Inside a Flickable the content decides its own height, and a
#   fill-height spacer grows contentHeight without limit.
#
#   ⚠ THE RIG REACHES THE BOTTOM BY PATCHING `contentY`, which is how it gets
#   there with no input device — the three settings groups live below the fold,
#   so a panel that stopped scrolling would take them with it and the existing
#   04-big shot would still look perfect.
# 55: THE REMOTE, MEASURED RATHER THAN GUESSED.
#
#   velle, with one in hand: play, volume, the arrows and mute worked; pause did
#   not; the Media Center key did nothing; OK was not Enter and Info opened
#   nothing. Every one of those turned out to be a different fault, and the
#   fixes came from a probe rather than from reading Qt's headers — a nested
#   headless synui, wtype sending each keysym, and a QML client logging what
#   arrived.
#
#   ⛔ PAUSE IS NOT A MEDIA KEY. rc-core's rc6_mce table sends KEY_PAUSE for it
#   — evdev 119, the same code as a keyboard's Pause/Break — so xkb gives it the
#   plain `Pause` keysym and Qt gives it Qt::Key_Pause. Never Key_MediaPause,
#   which is what XF86AudioPause becomes and which no MCE remote sends. Play
#   worked and Pause did nothing, and the two looked like one feature half done.
#
#   ⛔ THREE BUTTONS ARRIVE AS key === 0 AND NO `case Qt.Key_*` CAN EVER CATCH
#   THEM. Qt has no enum for XF86OK, XF86Info or XF86MediaSelectProgramGuide, so
#   OK, Info and Guide had a key code of literally zero; KEY_NEXT and
#   KEY_PREVIOUS have no keysym AT ALL, xkeyboard-config maps nothing to those
#   keycodes. Matched on nativeScanCode instead — the evdev code plus 8, and the
#   suite checks every number against linux/input-event-codes.h rather than
#   trusting it.
#   ⚠ AND nativeVirtualKey IS NOT EXPOSED TO QML. Reading the keysym is the
#   obvious answer and the property simply is not there.
#
#   ⛔ Qt::Key_Cancel WAS WIRED TO BACK, AND IT IS THE STOP BUTTON. `Cancel` is
#   what xkeyboard-config puts on <STOP> (KEY_STOP, 128) and nothing else on any
#   evdev keyboard produces it — so the one button meaning "stop playing"
#   navigated up a shelf. A real Back sends KEY_BACK and is untouched.
#
#   THE GREEN BUTTON, from the desktop, as a synui bind: `bind = XF86AudioMedia
#   spawn syn-arcade big toggle`, written into the block beside super+F10.
#   ⚠ IT RUNS THE SAME COMMAND AS super+F10 AND THAT IS A TRAP IN THE READER.
#   binds_read recovers the user's chosen key by matching that command, so a
#   second line with it would be read back as their choice and written out as
#   `big` — silently replacing super+F10 with a key most keyboards do not have.
#   The reader checks the combo against MEDIA_KEY and records it separately.
#   ⚠ AND SOMEBODY ELSE'S BINDING WINS. The three configurable keys answer a
#   collision by refusing, because the answer is to pick another key; there is
#   no other key to pick for this one, so refusing would mean anybody who binds
#   their music player to it can never refresh their gaming keys again. Ours is
#   left out and the rest of the block refreshes.
#
#   THE POWER BUTTON ASKS RATHER THAN ACTS — a page of Desktop, Quit, Sleep,
#   Restart and Power off, opening on Desktop, which is the harmless row. Sleep,
#   restart and power off are three irreversible things and a remote has one
#   button for them. The page is `byShelf("system")` filtered to actions, so
#   `show_power` off takes the three away here exactly as it does behind Start
#   and the way out stays — which also means the page can never be empty.
#   ⚠ MCE's Power button sends KEY_POWER2, and xkeyboard-config maps NOTHING to
#   keycode 364. A keyboard's power key is KEY_POWER, which has XF86PowerOff.
#
#   RECORD IS synui's RECORDER. `big record` dispatches the compositor's own
#   `record` action — the same toggle super+shift+r is — so there is one answer
#   to whether this machine is recording. A recorder of this package's own would
#   disagree with the indicator the first time somebody used the keyboard.
#
#   ⚠ EVERY REMOTE BUTTON GOES THROUGH nav(), including the new ones. A button
#   wired straight to a function is one the FIFO cannot send and therefore one
#   the rig cannot drive — and the remote is exactly the device nobody has on
#   the desk to try by hand. `power` is a nav word for that reason.
#
#   ⚠ THE SETTINGS GROUP IS "SCREEN" NOW, not "Power". The Power button has a
#   page headed POWER; a settings page headed the same word but reached through
#   Start ▸ Settings, holding one row about blanking, is two answers to one
#   word. That group has only ever been about whether the screen may sleep.
# 56: THE ONE ROW ON THE SYSTEM MENU WITH NOTHING BESIDE IT. Reported from a
#   screenshot: seven rows carry a glyph and Settings carries a gap, which reads
#   as a drawing that failed to load rather than as a row that never had one.
#
#   ⛔ AND THE THREE LISTS THAT KEEP THE GLYPHS HONEST COULD NOT SEE IT. Every
#   tile's icon is checked three ways — the name in apps_table(), the file in
#   data/icons, the entry in meson's install list — and Settings is in none of
#   them, because it is a PAGE of that menu rather than something `big run` can
#   run, so apps_table() emits no row for it. A menu row the shell draws itself
#   is outside every check the tiles have.
#
#   The gear goes through icon_file() like everything else and arrives as
#   SYN_BIG_SETTINGS_ICON — the same arrangement as the dendrite mark in the
#   header, and for the same reason: the shell is a renderer handed a path that
#   exists or an empty string, and never has to know where this package
#   installed itself. ⚠ The suite asserts the row is actually GIVEN it, because
#   a variable that is set, read into a property and never reaches the row
#   passes every other check written for it.
pkgrel=57
pkgdesc="SynapseOS game assistant: in-game overlay, controller setup and gaming shortcuts"
arch=('x86_64')
url="https://github.com/velle999/SYNAPSE"
license=('GPL-2.0-or-later')

# libc, and since 0.1.0-6 libwayland-client. The three libraries it still does
# NOT link are each a deliberate choice:
#
#   libSDL     the natural way to read a gamepad, and it would pull a graphics
#              stack into a tool that has to work over SSH. The kernel already
#              describes every attached device in /sys/class/input — which is
#              also what makes the discovery path testable against a fixture.
#   libudev    the same list behind a library and a daemon, with nothing here
#              needing hotplug events.
#   libevdev   a wrapper over the six ioctls pad.c uses.
#
# wayland is here for exactly one command — `big mouse`, the controller as a
# pointer — because a web browser on a television takes pointer events and
# there is no way to give it any without being a Wayland client. Everything
# else in the binary still works with no display at all; `big mouse` says so
# and exits if there is no session.
depends=('glibc' 'wayland')

# The news shelf shells out to curl rather than linking an HTTP client: TLS,
# redirects, proxies and a CA store are a library's worth of decisions, all of
# which curl has already made. Named here even though pacman itself pulls curl
# in, because this calls the BINARY by name.
depends+=('curl')

# ⚠ A hard dependency, not an optdepend, and the reasoning is syn-disks'
# rather than a preference: without MangoHud the overlay tab of this
# application does nothing at all, and an app whose main feature is inert on a
# default install is not offering that feature, it is listing it. It is a
# ~2MB package from `extra` that anything else gaming-related pulls in anyway.
depends+=('mangohud')

# ⚠ A hard dependency for the same reason mangohud is, and it is the rule this
# package has settled on: the Visualizer row in big screen mode's Start menu
# EXISTS ONLY WHEN projectM IS INSTALLED, so as an optdepend the feature was
# listed in the package description and absent from every stock machine. A
# feature nobody can reach is not being offered.
#
# ⚠ The PULSEAUDIO build, never projectm-sdl. It captures through libpulse,
# which honours PULSE_SOURCE, and that is what lets `big visualizer` point it
# at the MONITOR of whatever is playing. projectMSDL opens a capture device by
# name out of its own enumeration and the first entry there is usually a
# microphone — a visualizer that dances to the room and sits still through the
# music, which reads as broken drawing rather than as the wrong device.
depends+=('projectm-pulseaudio')

# wayland ships wayland-scanner, which generates the client bindings for the
# vendored wlr-virtual-pointer protocol in protocols/. ⚠ The XML is vendored
# rather than taken from the wlr-protocols package, which is NOT installed on a
# stock SynapseOS box: depending on it would make the tree unbuildable on the
# machine it ships to for the sake of one 150-line MIT file.
# ⚠ sdl3 is a BUILD dependency and NOT a runtime one. `map learn` writes SDL's
# mapping format, whose GUID and joystick indices only SDL can supply, so it
# opens libSDL3 through dlopen at run time — the headers are here for the
# structures and the enums (an SDL_Event laid out by hand is a silent ABI trap)
# and nothing is linked. Check with: ldd /usr/bin/syn-arcade | grep -c SDL → 0
makedepends=('meson' 'ninja' 'gcc' 'wayland' 'sdl3')

optdepends=('quickshell: the graphical window (syn-arcade gui) AND big screen mode'
            'wtype: big screen mode’s on-screen keyboard types through it — without it the keyboard draws and types nothing'
            'synui: the compositor the gaming shortcuts are installed into. ⚠ `big mouse` needs a synui new enough to offer zwlr_virtual_pointer_manager_v1 (0.1.0-327 or later); it says so if not. ⚠ The Recent bar needs one new enough to answer `synctl recent` (0.1.0-489 or later) — the list of what this desktop has opened is the compositor’s, because a window mapping is the only thing every way of launching something has in common. An older synui simply draws no Recent bar'
            'firefox: the Web tile, and where a headline or a media server opens'
            'steam: the game library and Big Picture tiles in big screen mode'
            'gamescope: `syn-arcade fit` — the nested compositor that makes a low-resolution game fill the screen, and `big steam --gamescope` for a TV whose resolution is not the desktop’s'
            'retroarch: appears as a tile in big screen mode'
            'syntty: the Terminal tile (kitty, foot, alacritty and xterm are used in that order if it is absent)'
            'cliamp: THE music player for big screen mode, and packaged here since 0.1.0-23. It is the only one that can be DRIVEN rather than launched — it runs headless and takes transport over a socket — so with it the Music tile plays without opening a window, the Start menu gets a Now Playing row with a meter, a Music Source picker (Plex, YouTube Music, Spotify, local files, radio) and a browsable Plex library. Every other player is a window somebody has to get out of with a gamepad'
            'strawberry: the Music tile if cliamp is not installed (elisa, amberol, rhythmbox, lollypop, clementine, audacious, deadbeef, quodlibet, tauon, plexamp, spotify and vlc are tried in that order). Pick any of them with `syn-arcade big player <name>`, or the Big screen tab of the window'
            'plex-desktop: the Plex tile — a Plex server on the network is found by broadcast either way'
            'jellyfin-media-player: the Jellyfin tile, likewise'
            'sdl2-compat: SDL2 games that read the controller mappings'
            'sdl3: SDL3 games read the mappings — and `syn-arcade map learn` needs it to WRITE one, because the GUID and the button numbers in a mapping are SDL’s own')

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
    cd "$srcdir/syn-arcade-0.1.0"
    meson setup build --prefix=/usr --buildtype=release
    meson compile -C build
}

check() {
    cd "$srcdir/syn-arcade-0.1.0"
    # ⚠ The suite is dangerous in a way most are not, because three of the four
    # things this binary writes are files the LIVE desktop reads — the
    # compositor's synuirc most of all, since synui reads exactly one of those
    # and a two-line file written over it replaces the whole desktop
    # configuration.
    #
    # So every invocation runs with XDG_CONFIG_HOME redirected into a mktemp -d
    # that the EXIT trap removes, and the suite REFUSES TO START if that
    # redirection is not in place. Controllers are described in a fake
    # /sys/class/input; the ioctl paths (test, rumble, calibrate) are never
    # exercised, because a suite that opened a real device node would rumble a
    # pad and rewrite its deadzones on the machine running the build. The last
    # two assertions check that the real synuirc and MangoHud.conf were not
    # touched by the run.
    meson test -C build --print-errorlogs
}

package() {
    cd "$srcdir/syn-arcade-0.1.0"
    meson install -C build --destdir="$pkgdir"
}
