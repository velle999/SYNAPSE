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

REAL=${1:?usage: bigrig.sh /path/to/syn-arcade /path/to/synui /path/to/qml}
SYNUI=${2:?}
QML=${3:?}

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
    printf 'run %s\n' "\$*" >> "$TMP/launch.log"; exec sleep 300
fi
if [ "\${1:-}" = big ] && [ "\${2:-}" = mouse ]; then
    printf 'mouse\n' >> "$TMP/launch.log"; exec sleep 300
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
for id in 400 620 630 730 8930 4000 220 240 280 300 320 340 360 380 420 440; do
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
QT_QPA_PLATFORM=wayland QS_APP_ID=syn-arcade-big SYNARCADE_BIN=syn-arcade \
    quickshell -p "$QML" > "$TMP/qs.log" 2>&1 &
QS_PID=$!
sleep 4

grep -aE "ERROR|WARN|qs:" "$TMP/qs.log" | grep -viE "IPC server|Saving logs" | head -25
kill -0 "$QS_PID" 2>/dev/null || { echo "QUICKSHELL DIED"; tail -30 "$TMP/qs.log"; exit 1; }
echo "shell alive"

shot() { grim -o HEADLESS-1 "$OUT/$1.png" 2>/dev/null || grim "$OUT/$1.png"; }

shot 01-main

# ── the library, which is what the top of the screen DRESSES ────────────────
#
# ⚠ 01-main sits on the Play shelf, where the selected tile is an APPLICATION:
# no hero, no logo, and a banner that is text on black. Every screenshot this
# rig produced was of that state, so the art band could have been drawing
# nothing at all and nothing here would have said so. The band only exists
# with a GAME selected, and it has three states which are three code paths:
say down 0.6
shot 01b-game-art        # 220: hero + logo — the dressed banner
say right; say right 0.6
shot 01c-game-no-logo    # 280: hero but NO logo — the text title over art
say right 0.6
shot 01d-game-no-hero    # 300: cover only — banner over an empty band

# Shelves, top to bottom: Play, Games, Media, Apps, System, News.
say down; say down; say down; say down; say down 0.6
shot 02-news
say up 0.5
shot 03-system

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
say down; say down; say down 0.6              # Play → Games → Media → Apps
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
