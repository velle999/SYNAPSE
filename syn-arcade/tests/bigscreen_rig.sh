#!/bin/bash
# bigscreen_rig.sh — render big screen mode in a HEADLESS nested synui, drive
# it with controller words, and screenshot every state.
#
# ⚠ NOT run by `meson test`, and it must not be: it needs a DRM render node and
# it boots a compositor. syn_arcade_test.sh is the suite; this is the thing that
# answers the question a suite of greps cannot — does the interface actually
# DRAW, and does a button press do what the QML says it does.
#
# Usage:
#   tests/bigscreen_rig.sh build/syn-arcade ../synui/_build/synui \
#                          data/syn-arcade-big.qml
#
#   SIZE=1024x768 tests/bigscreen_rig.sh ...   # any screen shape, not just 16:9
#
# It found real things on its first run: the away hint pill was reported
# missing when it was merely BEHIND synui's own welcome panel (which is why the
# rig now turns that off), and the launch path was proven by what landed in
# launch.log rather than by reading the code.
#
# Every seatbelt from synui's own smoke.sh, because this rig shares a machine
# with a live desktop:
#   HOME / XDG_CONFIG_HOME / XDG_RUNTIME_DIR / XDG_CACHE_HOME  → a temp dir, so
#     nothing reads or writes the real session's config, and libwayland's
#     wayland-0 fallback cannot find the live compositor.
#   SYNUI_CONFIG → an empty file, so no autostart from /etc or $HOME runs here.
#   SYNUI_RUNNING=1 → stops synui pushing WAYLAND_DISPLAY into the SHARED
#     session D-Bus activation environment, which would repoint the live
#     desktop's activated services at this throwaway socket.
#   power timeouts at 86400 → the rig must not blank or suspend mid-run.
#
# `syn-arcade` is a STUB on PATH: everything goes to the real binary except the
# three streams the shell owns, which are stood in for so the interface can be
# driven and observed —
#   big nav   reads a FIFO this script writes controller words into
#   big keys  appends what the on-screen keyboard types to a log
#   big run   is a sleep, so "an application is running" is true without one
set -u

REAL=${1:?usage: bigrig.sh /path/to/syn-arcade /path/to/synui /path/to/qml [vpointer_click]}
SYNUI=${2:?}
QML=${3:?}
# ⚠ OPTIONAL, and the wheel block below is SKIPPED LOUDLY without it rather
# than quietly passing. It is synui's tests/vpointer_click — a real pointer,
# because a wheel is the one input here that `big nav` cannot stand in for:
# the FIFO delivers words, and the whole question about a wheel is whether the
# event reaches the words at all.
VPTR=${4:-}

TMP=$(mktemp -d /tmp/bigrig.XXXXXX) || exit 1
chmod 700 "$TMP"
OUT=$TMP/out
mkdir -p "$OUT" "$TMP/cache/syn-arcade" "$TMP/bin"

cleanup() {
    [ -n "${QS_PID:-}" ] && kill -9 "$QS_PID" 2>/dev/null
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    echo "TMP kept: $TMP"
}
trap cleanup EXIT INT TERM

# ── the stub ────────────────────────────────────────────────────────────────
cat > "$TMP/bin/syn-arcade" <<STUB
#!/bin/bash
# ⚠ Every call logged, and it is the only way to tell "the shell never asked"
# apart from "the shell asked and the answer was empty" — two failures that
# look identical on screen, as an EMPTY SHELF.
printf '%s\n' "\$*" >> "$TMP/calls.log"
if [ "\${1:-}" = big ] && [ "\${2:-}" = nav ]; then exec cat "$TMP/nav.fifo"; fi
if [ "\${1:-}" = big ] && [ "\${2:-}" = keys ]; then
    while IFS= read -r line; do printf '%s\n' "\$line" >> "$TMP/typed.log"; done
    exit 0
fi
if [ "\${1:-}" = big ] && [ "\${2:-}" = run ]; then
    printf 'run %s\n' "\$*" >> "$TMP/launch.log"
    # ⚠ ITS OWN PID, for the one tile that is supposed to be ENDED when the
    # interface comes back. \`exec\` keeps this pid, so the file names the
    # process the shell has to kill — and "did Guide end the visualizer" stops
    # being a thing that can only be read in the QML.
    [ "\${3:-}" = visualizer ] && printf '%s\n' "\$\$" > "$TMP/visualizer.pid"
    exec sleep 300
fi
if [ "\${1:-}" = big ] && [ "\${2:-}" = mouse ]; then
    printf 'mouse\n' >> "$TMP/launch.log"; exec sleep 300
fi
# Music is DRIVEN rather than launched, so the stub answers as a running
# player instead of standing in for one. Without this the rig's HOME has no
# cliamp socket, the menu correctly shows no music row, and the row could be
# broken in any way at all with every screenshot still looking right.
if [ "\${1:-}" = big ] && [ "\${2:-}" = music ]; then
    case " \$* " in
        *" status "*)
            printf 'state\ttitle\tpath\n'
            printf 'playing\tFixture%%20Track\thttp://example.invalid/stream\n' ;;
        *" vis "*)
            # ⚠ FRAMES THAT DIFFER. A stub emitting one repeated frame draws a
            # shape that never moves, which is indistinguishable on a
            # screenshot from a visualizer bound to the wrong field and stuck
            # on its first value. These three rotate, so the bars are visibly
            # unequal and the animation has somewhere to go.
            while :; do
                printf '%s\n' '{"ok":true,"visualizer":"Bars","bands":[0.95,0.72,0.58,0.41,0.33,0.26,0.19,0.12,0.07,0.03]}'
                sleep 0.08
                printf '%s\n' '{"ok":true,"visualizer":"Bars","bands":[0.61,0.88,0.44,0.67,0.29,0.38,0.14,0.21,0.05,0.02]}'
                sleep 0.08
                printf '%s\n' '{"ok":true,"visualizer":"Bars","bands":[0.78,0.55,0.91,0.36,0.52,0.18,0.31,0.09,0.11,0.04]}'
                sleep 0.08
            done ;;
        *" source "*)
            # ⚠ THE LIST AND THE CHOICE ARE THE SAME VERB, told apart by
            # whether there is an id after it. A stub that answered the list to
            # both would make "the source was chosen" unobservable: the picker
            # would redraw and nothing would prove a command had run.
            if [ -n "\${4:-}" ] && [ "\${4}" != --rec ]; then
                printf 'source %s\n' "\${4}" >> "$TMP/music.log"
                exit 0
            fi
            # ⚠ ytmusic and spotify carry the two actions that are not
            # "open cliamp", because on most machines that is what big.c
            # answers: YouTube Music plays through yt-dlp, which nobody has
            # by default, and Spotify needs an account. A fixture that said
            # \`browse\` for both would screenshot the row this change exists
            # to replace. (⚠ The backticks are ESCAPED: this comment is inside
            # an unquoted heredoc, where a bare pair of them is a command
            # substitution — \"browse: command not found\" on every run, and a
            # comment in the generated stub with the words eaten out of it.)
            printf 'id\tname\tcurrent\taction\tnote\n'
            printf 'plex\tPlex\t0\talbums\t\n'
            printf 'ytmusic\tYouTube%%20Music\t0\tinstall\tneeds%%20yt-dlp%%20—%%20press%%20to%%20install%%20it\n'
            printf 'spotify\tSpotify\t0\tsetup\tpress%%20to%%20sign%%20in%%20—%%20needs%%20Spotify%%20Premium\n'
            printf 'local\tLocal%%20files\t0\tplay\t\n'
            printf 'radio\tRadio\t1\tplay\t\n' ;;
        *" plex "*)
            if [ -n "\${4:-}" ] && [ "\${4}" != --rec ]; then
                printf 'plex %s\n' "\${4}" >> "$TMP/music.log"
                exit 0
            fi
            # ⚠ MORE ALBUMS THAN THE PANEL CAN SHOW. A dozen rows is what
            # makes the list SCROLL, and a fixture of four would screenshot
            # perfectly while the selection walked off the bottom edge on a
            # real library of a hundred and thirty.
            printf 'id\tname\tartist\tyear\n'
            i=1
            for a in "2Pacalypse%20Now|2Pac|1991" \
                     "All%20Eyez%20on%20Me|2Pac|1996" \
                     "Hybrid%20Theory|Linkin%20Park|2000" \
                     "Meteora|Linkin%20Park|2003" \
                     "The%20Dark%20Side%20of%20the%20Moon|Pink%20Floyd|1973" \
                     "Wish%20You%20Were%20Here|Pink%20Floyd|1975" \
                     "Kind%20of%20Blue|Miles%20Davis|1959" \
                     "A%20Love%20Supreme|John%20Coltrane|1965" \
                     "Rumours|Fleetwood%20Mac|1977" \
                     "Nevermind|Nirvana|1991" \
                     "OK%20Computer|Radiohead|1997" \
                     "Discovery|Daft%20Punk|2001"; do
                printf '%s\t%s\t%s\t%s\n' "\$i" \
                    "\$(printf '%s' "\$a" | cut -d'|' -f1)" \
                    "\$(printf '%s' "\$a" | cut -d'|' -f2)" \
                    "\$(printf '%s' "\$a" | cut -d'|' -f3)"
                i=\$((i + 1))
            done ;;
        *)  printf '%s\n' "\$*" >> "$TMP/music.log" ;;
    esac
    exit 0
fi
# ⚠ THE MEDIA BUTTONS ARE STUBBED, AND THIS ONE IS A SEATBELT AS MUCH AS A
# FIXTURE. \`big transport\` asks the SESSION BUS what is playing and then sends
# it Play/Pause — and D-Bus is not reached through XDG_RUNTIME_DIR alone:
# DBUS_SESSION_BUS_ADDRESS is inherited from the shell that ran this rig, so
# the real binary here would find the LIVE desktop's music player and pause
# somebody's album from inside a test. Same family as the SYNUI_SOCKET note
# below. (The variable is unset as well; this is the brace to that belt.)
#
# ⚠ AND IT ANSWERS AS SOMETHING PLAYING, because the buttons are drawn only
# while something is — a stub that said "nothing" would screenshot a footer
# with no media row in it and prove nothing about either.
if [ "\${1:-}" = big ] && [ "\${2:-}" = transport ]; then
    case " \$* " in
        *" status "*)
            printf 'player\tapp\tstate\ttitle\tartist\tcannext\tcanprev\tcanplay\tcanpause\n'
            printf 'cliamp\tCliamp\tplaying\tFixture%%20Track\tThe%%20Fixtures\t1\t1\t1\t1\n' ;;
        *)  printf 'transport %s\n' "\${3:-}" >> "$TMP/transport.log" ;;
    esac
    exit 0
fi
exec "$REAL" "\$@"
STUB
chmod +x "$TMP/bin/syn-arcade"

# synui-apply-theme rewrites files in \$HOME literally; HOME is redirected, and
# this is the belt to that brace.
printf '#!/bin/sh\nexit 0\n' > "$TMP/bin/synui-apply-theme"
chmod +x "$TMP/bin/synui-apply-theme"
# synui-clock is the live desktop's; a stub keeps the header deterministic.
printf '#!/bin/sh\nprintf %%s "{\\"text\\":\\"20:15\\",\\"date\\":\\"Fri 15 August\\"}"\n' \
    > "$TMP/bin/synui-clock"
chmod +x "$TMP/bin/synui-clock"

# ⚠ LUTRIS AND HEROIC, STUBBED ONTO PATH, and this is not padding.
#
# apps_table() lists both behind a have() check, so on a machine with neither
# the Play bar is two tiles and the row it shares with Media and Apps fits with
# room to spare — which is the ONE case that proves nothing. The case that
# matters is the day somebody installs a game launcher: four tiles on Play no
# longer fit beside the other two shelves at the 15% squeeze, and before the
# bar rule the packer answered that by breaking the row into three. Installing
# Lutris would have rearranged the whole television.
#
# So the rig renders the crowded row, because the roomy one cannot tell a
# working packer from a broken one. Nothing is ever executed: `big run` is
# already a sleep, and these exist only to be FOUND.
for launcher in lutris heroic; do
    printf '#!/bin/sh\nexit 0\n' > "$TMP/bin/$launcher"
    chmod +x "$TMP/bin/$launcher"
done

# ⚠ AND CLIAMP, for a reason worth spelling out: the Music Source row exists
# only when the player is one big screen mode can DRIVE, which big.c decides by
# asking whether cliamp is installed. Without this stub the row is there on the
# developer's machine and absent on CI — and the walk below counts rows. One
# `down` would land on Desktop instead, which steps the interface aside (and
# one row further is Quit, which ends it outright), and the run would finish
# four screenshots in while looking like it had finished properly.
#
# Nothing ever runs it: every cliamp call goes through `big music`, which the
# syn-arcade stub above answers itself.
printf '#!/bin/sh\nexit 0\n' > "$TMP/bin/cliamp"
chmod +x "$TMP/bin/cliamp"

# projectM, for the same kind of reason: the Visualizer row is only there when
# it is installed, and a menu screenshot that does not contain it proves
# nothing about how it looks. It is never launched here — the walk below stays
# off that row, and `big run` is a sleep anyway.
printf '#!/bin/sh\nexit 0\n' > "$TMP/bin/projectM-pulseaudio"
chmod +x "$TMP/bin/projectM-pulseaudio"

# ⚠ AND A SEEDED RECENT BAR, because an empty one is not drawn at all — and a
# shelf that is not drawn cannot show whether it PACKS. Recent is a bar, first
# among Play/Media/Apps, so it is the entry that decides whether four bars
# still fit one row of a television or break it into two. That is the exact
# failure the crowded launcher row above exists to catch, and adding a fourth
# bar without rendering it would have walked straight past it.
#
# ⚠ SEEDED IN TWO HALVES NOW, and the second half is the one that is easy to
# forget. The list synui keeps is app_ids, and `big recent` DROPS any id with
# no .desktop file behind it — an application this desktop cannot start again
# is not drawn as a tile that does nothing. So a seeded id whose .desktop does
# not exist renders an EMPTY BAR, which looks exactly like a broken shelf and
# tests nothing. The fixtures go in HOME's own applications directory, which
# icons.c searches first, so this does not depend on what the machine running
# the rig happens to have installed.
mkdir -p "$TMP/synui" "$TMP/.local/share/applications"
for f in "fixture-writer|Fixture Writer|accessories-text-editor" \
         "fixture-paint|Fixture Paint|applications-graphics"; do
    id=${f%%|*}; rest=${f#*|}; nm=${rest%|*}; ic=${rest##*|}
    cat > "$TMP/.local/share/applications/$id.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=$nm
Exec=/bin/true %U
Icon=$ic
DESK
done
# ⚠ NEWEST FIRST, which is the file's whole format — synui writes the id of
# the window that just mapped at the top and everything else below it.
printf 'fixture-writer\nfixture-paint\n' > "$TMP/synui/recent-apps"

mkfifo "$TMP/nav.fifo"

# Seeded caches, so the news and media shelves have something to draw without
# this rig touching the network.
cat > "$TMP/cache/syn-arcade/news.tsv" <<'NEWS'
id	title	source	link	feed
news-0	Half-Life 3 confirmed, again	Game Informer	https://example.com/1	news
news-1	The 12 best games of 2002	kotaku.com	https://example.com/2	news
news-2	A very long headline about a game nobody asked to be remade	Eurogamer	https://example.com/3	news
NEWS
# ⚠ SIX columns, and the sixth is a path into THIS tree. A cache is what the
# binary last wrote, so a fixture with yesterday's column count is testing a
# build nobody is running. The iconfile column is absolute because that is what
# big.c writes into it — the shell opens it as a file:// URL and has no working
# directory worth relying on.
cat > "$TMP/cache/syn-arcade/media.tsv" <<MEDIA
id	name	url	source	kind	iconfile
plex-1	Living Room	https://192.168.1.20:32400/web	plex	server	$PWD/data/icons/plex.svg
jellyfin-2	Loft Jellyfin	https://192.168.1.31:8096	jellyfin	server	$PWD/data/icons/jellyfin.svg
MEDIA

# A Steam library fixture, so the Games shelf is populated.
mkdir -p "$TMP/steam/steamapps"
cat > "$TMP/steam/steamapps/libraryfolders.vdf" <<'VDF'
"libraryfolders" { "0" { "path" "PLACEHOLDER" } }
VDF
sed -i "s|PLACEHOLDER|$TMP/steam|" "$TMP/steam/steamapps/libraryfolders.vdf"
# ⚠ ENOUGH TO OVERFLOW A SHELF. Two games cannot show how a row FILLS — the
# tiles just sit at the left and every screen looks alike — so the one thing
# the horizontal layout has to get right was invisible here until this fixture
# was long enough to run off the edge of the screen.
# ⚠ SIXTEEN, and the number is chosen against the WIDEST screen this rig can be
# asked for, not the default one. Ten overflows 16:9 but not 21:9, where the row
# simply ran out of games and looked like a layout that stops halfway — the
# fixture has to be longer than the widest shelf, or a wide screen silently
# tests nothing.
#
# ⚠ AND A SHORT LIBRARY IS A DIFFERENT LAYOUT, not merely a shorter one — which
# is why `GAMES=n` exists. A shelf that FITS is packed into a band with
# whatever comes next, so a laptop with three games draws Games, Play, Media
# and Apps on ONE row where a machine with fifty draws Games on its own. Every
# screenshot this rig ever took was of the crowded case.
ALLIDS="400 620 630 730 8930 4000 220 240 280 300 320 340 360 380 420 440"
IDS=$ALLIDS
if [ -n "${GAMES:-}" ]; then
    IDS=$(printf '%s\n' $ALLIDS | head -n "$GAMES" | tr '\n' ' ')
fi
for id in $IDS; do
cat > "$TMP/steam/steamapps/appmanifest_$id.acf" <<ACF
"AppState"
{
	"appid"		"$id"
	"name"		"Fixture Game $id"
	"StateFlags"	"4"
	"LastPlayed"	"1755200000"
	"SizeOnDisk"	"9663676416"
}
ACF
done

# ── Artwork, because a library with no pictures in it proves nothing ──
#
# ⚠ The fixture above seeded SIXTEEN games and NOT ONE PICTURE, and that gap
# went unnoticed for as long as the interface drew its title as TEXT. The
# moment the top of the screen became a hero band and a logo, every screenshot
# this rig produced showed the empty-art path and nothing else — a render that
# looks fine, is fine, and says nothing whatsoever about the thing being
# changed. Art that is absent is not a neutral fixture; it silently tests the
# fallback and calls it the layout.
#
# ⚠ Written into userdata/<id>/config/grid, NOT appcache/librarycache, and the
# reason is the file format. The user-grid path is the one Steam keeps as PNG;
# the download cache is JPEG, and this machine has no JPEG encoder that ships
# by default (no `magick`, no `convert`). rsvg-convert writes PNG and is
# already here for the icon theme. art_find() checks the user grid FIRST, so
# this is also the path a real override would take — the rig exercises the
# branch a person with SteamGridDB installed is actually on.
#
# The spread is deliberate and each row of it is a different code path:
#   220, 240   cover + hero + logo   — the full dressed banner
#   280        cover + hero, NO logo — the TEXT TITLE over art, the fallback
#                                      that is invisible if every game has a
#                                      logo, and which has to stay legible
#                                      against the brightest part of the band
#   300…440    cover only            — a shelf tile with no hero behind it
#   620…8930   nothing at all        — the no-art tile, still the common case
#                                      on a fresh install
GRID="$TMP/steam/userdata/1000/config/grid"
mkdir -p "$GRID"

# ⚠ A SKIP, not a failure. rsvg-convert is not a dependency of syn-arcade and
# must not become one of its rig: a machine without it should still be able to
# drive the interface and screenshot the layout, and be TOLD what it is not
# seeing rather than left to wonder why the band is empty.
if ! command -v rsvg-convert >/dev/null 2>&1; then
	echo "rig: rsvg-convert missing — NO artwork fixtures; hero/logo will render EMPTY" >&2
else
	# hue per appid, so the shelf is legible as a row of distinct games
	# rather than a row of the same picture sixteen times.
	art_hue() { echo $(( ( ${1} * 47 ) % 360 )); }

	svg2png() {  # svg2png <out.png> <w> <h> <svg-on-stdin>
		rsvg-convert -w "$2" -h "$3" -o "$1" - 2>/dev/null \
			|| echo "rig: rsvg-convert failed for $1" >&2
	}

	for id in 220 240 280 300 320 340 360 380 400 4000 420 440; do
		h=$(art_hue "$id")

		# The cover. 600×900 is the shape the shelf is built around,
		# so the fixture is that exact size — a differently-shaped
		# placeholder would letterbox and hide slot-snapping bugs.
		svg2png "$GRID/${id}p.png" 600 900 <<SVG
<svg xmlns="http://www.w3.org/2000/svg" width="600" height="900">
  <defs><linearGradient id="g" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="hsl($h,58%,42%)"/>
    <stop offset="1" stop-color="hsl($(( (h + 40) % 360 )),64%,16%)"/>
  </linearGradient></defs>
  <rect width="600" height="900" fill="url(#g)"/>
  <circle cx="300" cy="330" r="150" fill="hsl($h,70%,72%)" opacity="0.85"/>
  <rect x="0" y="700" width="600" height="200" fill="#000" opacity="0.45"/>
  <text x="300" y="800" font-family="sans-serif" font-size="86"
        font-weight="bold" fill="#ffffff" text-anchor="middle">$id</text>
</svg>
SVG
	done

	for id in 220 240 280; do
		h=$(art_hue "$id")

		# The hero. Wide and tall enough that the band crops it rather
		# than upscaling it — a hero smaller than the screen would test
		# the layout against a blurry stretch and prove the crop math
		# on the wrong pixels.
		svg2png "$GRID/${id}_hero.png" 1920 620 <<SVG
<svg xmlns="http://www.w3.org/2000/svg" width="1920" height="620">
  <defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1">
    <stop offset="0" stop-color="hsl($h,55%,30%)"/>
    <stop offset="0.55" stop-color="hsl($(( (h + 25) % 360 )),70%,48%)"/>
    <stop offset="1" stop-color="hsl($(( (h + 60) % 360 )),60%,22%)"/>
  </linearGradient></defs>
  <rect width="1920" height="620" fill="url(#g)"/>
  <circle cx="1450" cy="200" r="260" fill="hsl($h,85%,78%)" opacity="0.5"/>
  <circle cx="1120" cy="470" r="170" fill="hsl($(( (h + 90) % 360 )),85%,70%)" opacity="0.35"/>
  <path d="M0 620 L520 180 L980 620 Z" fill="#000" opacity="0.3"/>
</svg>
SVG
	done

	# ⚠ 280 gets NO logo on purpose. See the spread above.
	for id in 220 240; do
		h=$(art_hue "$id")

		# Transparent, as Steam's logos are — an opaque logo would sit
		# on the band as a visible rectangle and the rig would never
		# show that the compositing is right.
		# ⚠ The ink FILLS the canvas, and a fixture that did not was
		# misleading in a way that nearly changed the design. Drawn on a
		# 640×320 sheet with the words in the middle of it, the logo is
		# scaled to fit a box whose height is mostly whitespace — so it
		# renders half the size a real one would and the banner looks
		# under-weighted. Valve's logos are trimmed to their ink. This
		# one has to be too, or it is testing the wrong picture.
		svg2png "$GRID/${id}_logo.png" 640 208 <<SVG
<svg xmlns="http://www.w3.org/2000/svg" width="640" height="208">
  <text x="0" y="86" font-family="sans-serif" font-size="96"
        font-weight="bold" fill="hsl($h,90%,80%)">FIXTURE</text>
  <text x="0" y="196" font-family="sans-serif" font-size="96"
        font-weight="bold" fill="#ffffff">$id</text>
</svg>
SVG
	done
fi

export HOME="$TMP" XDG_CONFIG_HOME="$TMP" XDG_CACHE_HOME="$TMP/cache"
export XDG_RUNTIME_DIR="$TMP" XDG_STATE_HOME="$TMP/state"
export PATH="$TMP/bin:$PATH"
export SYN_ARCADE_STEAM="$TMP/steam" SYN_ARCADE_NO_NET=1
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export SYNUI_RUNNING=1
unset DISPLAY WAYLAND_DISPLAY

# ⚠ SYNUI_SOCKET, and it is the seatbelt this rig was MISSING.
#
# The live desktop exports it into every process it starts, including the shell
# this script was run from — and synctl prefers it over XDG_RUNTIME_DIR. So
# anything under this rig that shells out to synctl was talking to the REAL
# compositor, no matter how carefully HOME and XDG_RUNTIME_DIR were redirected.
# It went unnoticed while that was only `synctl outputs`, a read. It stopped
# being harmless the moment big screen mode grew `big close`, which would have
# closed a window on the live seat from inside a test.
#
# Unset, synctl falls back to $XDG_RUNTIME_DIR/synui-$WAYLAND_DISPLAY.sock —
# both of which point at the nested compositor by the time anything runs.
unset SYNUI_SOCKET

# ⚠ AND THE SESSION BUS, for exactly the same reason one step along. The media
# buttons ask MPRIS what is playing and send it transport commands, and D-Bus
# is found through DBUS_SESSION_BUS_ADDRESS FIRST — which this script inherits
# from the live desktop that started it. Redirecting XDG_RUNTIME_DIR is not
# enough on its own. With it unset there is no bus to find, and the stub above
# answers instead of anything reaching the real one.
unset DBUS_SESSION_BUS_ADDRESS

# ⚠ Every power timeout pushed out. A rig that blanks or suspends halfway
# through is a rig that screenshots a black screen and looks like a bug.
cat > "$TMP/synuirc" <<'RC'
# ⚠ synui's own welcome panel is an overlay surface in the MIDDLE of the
# screen, which is exactly where a centred hint pill goes. Off, or the rig
# screenshots synui's panel and reports big screen mode's hint as missing.
welcome_at_startup = off
dpms_timeout = 86400
lock_timeout = 86400
suspend_timeout = 86400
idle_timeout = 86400
RC
export SYNUI_CONFIG="$TMP/synuirc"

# ── the shape of the screen ─────────────────────────────────────────────────
#
# ⚠ THE RIG ONLY EVER ASKED ONE QUESTION ABOUT SIZE, and that is why an
# aspect-ratio bug lived in the layout: wlroots' headless backend gives 1280x720
# and nothing here ever changed it, so every screenshot this rig has ever taken
# was 16:9 — the one shape the interface was drawn on and the one shape whose
# leftover at the right-hand edge looked deliberate.
#
# There is no env var for a headless mode, but synui restores a saved mode per
# connector on new_output, so seeding its outputs.conf is how the rig asks for
# a different screen. HEADLESS-1 is what wlroots names the first one.
# ⚠ The pixel the pointer is put on has to follow the screen, not the 16:9
# default: a wheel poke at 640,360 on a 1024x768 rig is still on the screen and
# still proves something, but on a taller one it lands on a different row, and
# "the wheel moved the selection" would then be a different assertion per
# shape. Half of whatever the screen is, always.
# ⚠ `${SIZE:-}` and not `$SIZE`: this script runs under `set -u` and SIZE is
# the optional argument, so naming it bare is an unbound-variable exit on every
# ordinary run.
SCREEN_W=${SIZE:-1280x720}; SCREEN_W=${SCREEN_W%x*}
SCREEN_H=${SIZE:-1280x720}; SCREEN_H=${SCREEN_H#*x}
if [ -n "${SIZE:-}" ]; then
    case $SIZE in
        *x*) : ;;
        *) echo "SIZE must look like WIDTHxHEIGHT (got '$SIZE')" >&2; exit 2 ;;
    esac
    mkdir -p "$TMP/synui"
    # refresh=0 leaves the rate to the backend: a custom mode that also
    # insisted on a rate is a mode the headless backend can reject outright,
    # and a rejected mode falls back to 1280x720 SILENTLY — which would look
    # like the layout ignoring the screen rather than the rig failing to set it.
    printf 'output HEADLESS-1 enabled=1 width=%s height=%s refresh=0 scale=1\n' \
        "${SIZE%x*}" "${SIZE#*x}" > "$TMP/synui/outputs.conf"
fi

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node — synui renders through fx_renderer (GLES2)"
    exit 77
fi

# ── boot ────────────────────────────────────────────────────────────────────
"$SYNUI" > "$TMP/synui.log" 2>&1 &
SYNUI_PID=$!

SOCK=
for i in $(seq 1 100); do
    SOCK=$(sed -n 's/.*running on WAYLAND_DISPLAY=\(wayland-[0-9]*\).*/\1/p' "$TMP/synui.log" | head -1)
    [ -n "$SOCK" ] && break
    kill -0 "$SYNUI_PID" 2>/dev/null || { echo "synui died:"; tail -20 "$TMP/synui.log"; exit 1; }
    sleep 0.1
done
[ -n "$SOCK" ] || { echo "no socket in 10s"; tail -20 "$TMP/synui.log"; exit 1; }
export WAYLAND_DISPLAY="$SOCK"
echo "compositor up on $SOCK"

# The nav FIFO needs a writer held OPEN for its whole life: closing it is a
# POLLHUP, which the shell correctly reads as the stream ending.
# ⚠ O_RDWR, not O_WRONLY. Opening a FIFO for writing BLOCKS until a reader
# appears — and the reader here is started by quickshell, further down. Opening
# it read-write never blocks, and holds the write end open for the whole run
# (closing it is a POLLHUP, which the shell reads as the stream ending).
exec 9<> "$TMP/nav.fifo"
say() { printf '%s\n' "$1" >&9; sleep "${2:-0.35}"; }

# ── the shell ───────────────────────────────────────────────────────────────
#
# ⚠ SYN_BIG_LOGO by hand, because this rig starts quickshell DIRECTLY and never
# runs `big start` — which is where the header's dendrite mark is resolved. Left
# unset the header simply draws no emblem, so every screenshot would show the
# wordmark alone and look exactly like a working header.
QT_QPA_PLATFORM=wayland QS_APP_ID=syn-arcade-big SYNARCADE_BIN=syn-arcade \
    SYN_BIG_LOGO="$PWD/data/icons/synapse.svg" \
    quickshell -p "$QML" > "$TMP/qs.log" 2>&1 &
QS_PID=$!
sleep 4

grep -aE "ERROR|WARN|qs:" "$TMP/qs.log" | grep -viE "IPC server|Saving logs" | head -25
kill -0 "$QS_PID" 2>/dev/null || { echo "QUICKSHELL DIED"; tail -30 "$TMP/qs.log"; exit 1; }
echo "shell alive"

shot() { grim -o HEADLESS-1 "$OUT/$1.png" 2>/dev/null || grim "$OUT/$1.png"; }

shot 01-main

# ── the wheel, which is the one input the FIFO cannot fake ──────────────────
#
# ⛔ THE FIRST WHEEL SHIPPED IN 0.1.0-44 AND NEVER FIRED ONCE. It was a
# WheelHandler, whose `acceptedDevices` defaults to PointerDevice.Mouse — and
# QtWayland calls every pointer on every Wayland session a TOUCHPAD, because
# Wayland has no way to tell a client what kind of pointer a seat has. So the
# handler rejected every event a real mouse produced, silently, and six grep
# checks in syn_arcade_test.sh all passed against code that could not run.
#
# That is what this block exists for. It drives an actual wl_pointer axis event
# into the nested compositor through zwlr_virtual_pointer_v1 and compares
# SCREENSHOTS: a wheel that does nothing produces a picture identical to the
# one before it, which is exactly the failure a grep cannot see.
#
# ⚠ AND IT PUTS THE SELECTION BACK. Everything after this is a walk that counts
# rows and columns from a known start, so a block that left the selection three
# tiles along would break every step below it while looking like a wheel bug.
if [ -z "$VPTR" ]; then
    echo "rig: no vpointer_click given — SKIPPING the wheel; pass it as arg 4" >&2
else
    wheel_at=$(( SCREEN_W / 2 ))
    wheel_mid=$(( SCREEN_H / 2 ))

    # ⚠ Discrete notches, and `scroll` sends them as such — see the header of
    # vpointer_click.c. A continuous axis is a TOUCHPAD gesture, which Qt turns
    # into a pixelDelta this interface deliberately ignores.
    "$VPTR" "$wheel_at" "$wheel_mid" scroll 3; sleep 0.8
    shot 01e-wheel-along-the-row
    if cmp -s "$OUT/01-main.png" "$OUT/01e-wheel-along-the-row.png"; then
        echo "FAIL: three notches of the wheel changed NOTHING on screen" >&2
        WHEEL_FAILED=1
    else
        echo "wheel: three notches moved the selection"
    fi

    # ⚠ BACK TO THE START, and the assertion is that it lands EXACTLY there.
    # Three notches out and three back is the same tile, so the picture has to
    # be byte-identical — which also proves the notch accumulator is not
    # dropping or doubling steps in one direction.
    "$VPTR" "$wheel_at" "$wheel_mid" scroll -3; sleep 0.8
    shot 01f-wheel-back
    if cmp -s "$OUT/01-main.png" "$OUT/01f-wheel-back.png"; then
        echo "wheel: and three back is the same tile again"
    else
        echo "FAIL: the wheel does not come back to where it started" >&2
        WHEEL_FAILED=1
    fi

    # The sideways wheel a few mice have, on its own handler because one
    # WheelHandler answers one orientation. Positive is a tilt to the RIGHT,
    # which has to move the same way three notches down did.
    "$VPTR" "$wheel_at" "$wheel_mid" scroll 3 horiz; sleep 0.8
    shot 01g-wheel-tilt
    if cmp -s "$OUT/01e-wheel-along-the-row.png" "$OUT/01g-wheel-tilt.png"; then
        echo "wheel: a sideways tilt goes the same way as a turn"
    else
        echo "FAIL: tilting the wheel right is not three tiles right" >&2
        WHEEL_FAILED=1
    fi
    "$VPTR" "$wheel_at" "$wheel_mid" scroll -3 horiz; sleep 0.8
fi

# ── the library, which is what the top of the screen DRESSES ────────────────
#
# ⚠ THE LIBRARY IS THE FIRST ROW NOW, so 01-main already sits on a GAME — where
# it used to sit on the Play shelf, whose selected tile is an APPLICATION: no
# hero, no logo, and a banner that is text on black. Every screenshot this rig
# took before 0.1.0-13 was of that state, so the art band could have been
# drawing nothing at all and nothing here would have said so.
#
# The band has three states, which are three code paths, and they are reached
# by walking ALONG the first row rather than down into it:
shot 01b-game-art        # 220: hero + logo — the dressed banner
say right; say right 0.6
shot 01c-game-no-logo    # 280: hero but NO logo — the text title over art
say right 0.6
shot 01d-game-no-hero    # 300: cover only — banner over an empty band

# ── BANDS: shelves that fit are drawn side by side ─────────────────────────
#
# ⚠ DOWN IS NOT ONE SHELF, AND THE COUNT CHANGED AGAIN IN 0.1.0-16. The screen
# is three rows now — Games, then Play+Media+Apps packed across one, then the
# headlines — because the system switches left the shelves for the Start menu
# and the three short shelves are BARS that always share a row. A walk written
# against the old four rows stops on the wrong one and screenshots something
# else entirely while still looking like it worked, which is why every step
# below says where it expects to land.
say down 0.6
shot 02-bar-row              # Play, Media and Apps: three shelves, one row

# Right runs ALONG a band and crosses into the shelf drawn beside it — the
# thing a suite of greps cannot show. From the first Play tile that is two
# crossings: out of Play into Media, and out of Media into Apps.
say right; say right; say right; say right 0.6
shot 02b-crossed-along-the-bar

say down 0.6
shot 03-news                 # the third row, on its own

# ── the media buttons, which are BELOW the last shelf ───────────────────────
#
# ⚠ DOWN FROM THE BOTTOM BAND USED TO DO NOTHING, and that is the whole way in
# — no new button and nothing to learn. So this step is also the assertion that
# the interface has not simply stopped at the last row: what proves it is the
# highlight moving into the footer, and `transport next` landing in the log.
say down 0.6
shot 03k-media-buttons       # the footer row, play/pause chosen

say right 0.4                # onto skip-forward
say accept 0.7
shot 03l-media-pressed       # …and `transport next` in transport.log

say back 0.5                 # B leaves the buttons for the tiles again

say up 0.5
shot 03b-back-on-the-bar

# ── the Start menu ──────────────────────────────────────────────────────────
#
# Where the system switches went. Opened, moved down one, and closed with B.
#
# ⚠ NO `accept` ANYWHERE NEAR THIS. The first entry is Desktop, which steps the
# interface aside, and the one below it is Quit, which ends the process — an
# accept on either would finish the run at screenshot four with everything
# after it silently missing, and a rig that stops early looks a lot like a rig
# that finished.
say menu 0.8
shot 03c-start-menu           # Now Playing on top, then the five switches
# Right is next-track, and it does nothing on any row but the music one — so
# this is safe to press before knowing where the selection is, which `accept`
# is NOT (see below). It lands in music.log either way, which is how a row that
# drew but was not wired is told from one that works.
say right 0.5
shot 03d-start-menu-next
say down 0.4
shot 03e-start-menu-moved     # …and down one is Music Source

# ── the source picker and the library behind it ─────────────────────────────
#
# ⚠ ACCEPT IS SAFE HERE AND NOWHERE ELSE IN THIS MENU, and the reason is the
# same one the warning above gives: the selection is on Music Source, which is
# a PAGE. One row up is Now Playing and two rows down is Desktop, and an accept
# on the latter ends the run looking like a success.
say accept 1.0
shot 03g-source-picker        # five sources; Radio marked as the current one

# Plex is the first row, so this needs no movement — which is what makes it
# safe. Choosing it runs `big music source plex` (it lands in music.log) and
# the page that follows is the library, because that is what the `action`
# column said to do next.
say accept 1.6
shot 03h-plex-albums          # the album list, scrolling, artist and year

# ⚠ PAST THE BOTTOM OF THE PANEL, which is the whole assertion. Six rows fit
# and the fixture has twelve, so a selection still visible at row seven is a
# list that scrolled to keep it — and one that is NOT visible is a menu that
# has stopped responding as far as anybody watching can tell.
for _ in 1 2 3 4 5 6; do say down 0.25; done
shot 03i-plex-albums-scrolled

# Playing one. `big music plex <id>` in music.log is the proof that the row
# was wired to something; the menu goes back to its first page afterwards.
say accept 1.2
shot 03j-album-chosen

say back 0.6
shot 03f-start-menu-closed

# ── the visualizer, which must NOT survive coming back ──────────────────────
#
# ⚠ THE BUG THIS RELEASE EXISTS FOR: covered by the interface, projectM gets no
# frame callbacks, free-runs, and comes back frozen. So it is killed on the way
# back — and the QML half of that (signal the waiter, which passes it on) can
# only be proven by a process that really goes away.
#
# ⚠ THE COUNT, again: the menu is Now Playing, Music Source, Visualizer. One
# `down` too many is Desktop, which steps aside; two is Quit, which ends the
# run looking like it finished.
say menu 0.8
say down; say down 0.5
shot 03o-visualizer-row
say accept 1.6                # launches it; the interface steps aside
shot 03p-visualizer-away
VISPID=$(cat "$TMP/visualizer.pid" 2>/dev/null)
say guide 1.2                 # …and coming back must END it
shot 03q-back-from-visualizer
if [ -z "$VISPID" ]; then
    echo "VISUALIZER: never launched — the menu walk landed somewhere else"
elif kill -0 "$VISPID" 2>/dev/null; then
    echo "VISUALIZER: pid $VISPID STILL RUNNING after Guide — NOT ended"
else
    echo "VISUALIZER: ended by coming back (pid $VISPID is gone)"
fi

# ── …and the SHORTCUT, which is the same thing without the menu walk ────────
#
# `visualizer` is the word the nav stream says when both stick clicks are held
# together (see nav_chord in pad.c). The chord itself cannot be driven here —
# that needs a real pad, or a uinput device on the seat running this — but the
# word it produces is exactly what this rig already speaks, so everything the
# SHELL does with it is testable: it must launch from anywhere, and it must
# stop from in front of the visualizer without going through Guide.
#
# ⚠ FROM THE MAIN SCREEN, with no menu open, which is the case a shortcut
# exists for. The Start menu walk above proves the tile; this proves the
# shortcut is wired to the same tile and not to a second launch path.
rm -f "$TMP/visualizer.pid"
say visualizer 1.6
shot 03r-visualizer-by-chord
VISPID2=$(cat "$TMP/visualizer.pid" 2>/dev/null)

# ⚠ AND THE SECOND PRESS IS SENT WHILE STEPPED ASIDE. That is the half that
# would break silently: navAway() handles the on-screen keyboard, and a word
# that fell through to it would be typed as a letter instead of acted on.
say visualizer 1.4
shot 03s-visualizer-off-by-chord
if [ -z "$VISPID2" ]; then
    echo "CHORD: the shortcut launched nothing"
elif kill -0 "$VISPID2" 2>/dev/null; then
    echo "CHORD: pid $VISPID2 STILL RUNNING after the second press — not a toggle"
else
    echo "CHORD: launched and stopped by the shortcut alone (pid $VISPID2 is gone)"
fi

# ── …and it must NOT launch over something else ─────────────────────────────
#
# ⚠ `big nav` keeps reading the pad while this interface is stepped aside —
# that is how Guide comes back from inside a game — so the chord is live in the
# game too, and L3+R3 is a real binding in plenty of them. A shortcut meant for
# a launcher must not throw a full-screen visualizer over somebody mid-fight.
#
# Guide first, to step aside with nothing running: that is the OTHER half of
# the same guard, and the cheaper one to stage here.
rm -f "$TMP/visualizer.pid"
say guide 0.9                 # step aside, nothing in front
say visualizer 1.4            # …and this must do nothing at all
shot 03t-chord-ignored-while-away
if [ -s "$TMP/visualizer.pid" ]; then
    echo "GATE: the chord launched the visualizer while stepped aside — NOT gated"
else
    echo "GATE: the chord did nothing while stepped aside, as it must"
fi
say guide 0.9                 # back to the interface

# Guide steps aside: the main surface must go, and the hint must appear.
say guide 0.9
shot 04-away

# …and comes back.
say guide 0.9
shot 05-back

# Walk to the Apps shelf (Web, Terminal, Controllers) and press A. The stub
# turns `big run` into a sleep, so "an application is running" is true with no
# application involved.
say up; say up; say up; say up; say up 0.4    # to the top, wherever we were
say down 0.6                                  # Games → the Play/Media/Apps row
say right; say right; say right; say right 0.5 # along Play and Media into Apps
shot 06-apps-shelf
say accept 1.4
shot 07-launched

# Start opens the on-screen keyboard.
say menu 1.0
shot 08-osk
say right; say right; say right; say right; say right; say right 0.3
say accept 0.5                                 # a letter from the qwerty row
say down; say down 0.3
say accept 0.5                                 # a letter two rows down
shot 09-osk-typed
say page-right 0.5                             # next layout
shot 10-osk-layout
say back 0.6                                   # B closes the keyboard
shot 11-osk-closed

say guide 0.9
shot 12-back-from-app

echo
echo "── what the keyboard typed ──"
cat "$TMP/typed.log" 2>/dev/null || echo "(nothing)"
echo "── what was launched ──"
cat "$TMP/launch.log" 2>/dev/null || echo "(nothing)"
echo "── music transport ──"
cat "$TMP/music.log" 2>/dev/null || echo "(nothing)"
echo "── the media buttons ──"
cat "$TMP/transport.log" 2>/dev/null || echo "(nothing)"
echo "── shell errors ──"
grep -aE "ERROR|WARN|qs:" "$TMP/qs.log" | grep -viE "IPC server|Saving logs" | head -25
echo "── screenshots ──"
ls -la "$OUT"

# ⚠ The FILES, into the directory — not `cp -r "$OUT" "$DEST"`, which is a
# different operation depending on whether the destination already exists.
# First run: the shots land in $DEST. Every run after: cp puts them in
# $DEST/out/ instead, and $DEST still holds the pictures from the FIRST run.
# Nothing fails, nothing is said, and the reviewer is looking at yesterday's
# render while reasoning about today's change — which cost an hour here,
# diagnosing an intermittency that was a stale screenshot every time.
DEST=${BIGRIG_OUT:-/tmp/bigrig-out}
mkdir -p "$DEST" && rm -f "$DEST"/*.png && cp "$OUT"/*.png "$DEST"/
echo "copied to $DEST"

# ⚠ AND AN EXIT STATUS, because the wheel block is the first thing here that
# ASSERTS rather than renders. Everything above it is for a person to look at;
# a failed comparison has to be something a script can see, or it is a line of
# output in the middle of eighty that nobody reads twice.
if [ -n "${WHEEL_FAILED:-}" ]; then
    echo "RIG FAILED: see the wheel block" >&2
    exit 1
fi
