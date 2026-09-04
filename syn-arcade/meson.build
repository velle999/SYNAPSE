project('syn-arcade', 'c',
  version: '0.1.0',
  license: 'GPL-2.0-or-later',
  default_options: ['c_std=c11', 'warning_level=2', 'werror=false'])

cc = meson.get_compiler('c')

conf = configuration_data()
conf.set_quoted('SYNARCADE_VERSION', meson.project_version())
conf.set_quoted('SYNARCADE_DATADIR',
                get_option('prefix') / get_option('datadir') / 'syn-arcade')
# Where the .mo files land, and the domain they are named after. ⚠ The QML
# windows do NOT read these — quickshell has no translator, so they read JSON
# compiled from the same .po by po/meson.build. See include/i18n.h.
conf.set_quoted('SYNARCADE_LOCALEDIR',
                get_option('prefix') / get_option('datadir') / 'locale')

# Two paths that belong to OTHER packages, and that is why they are configured
# here rather than written into the C.
#
#   SYNUI_WALLPAPER_DEFAULT  what synui's `wallpaper = default` token resolves
#               to. Big screen mode follows the desktop's wallpaper, so it has
#               to interpret synui's own vocabulary — and `default` is the one
#               token that means a file rather than naming one.
# (`big background --rec` lists the picture files itself, from the same
# directories synui's Super+W picker scans — see big_background().)
#
# ⚠ NEITHER IS A BUILD DEPENDENCY. Both are read at RUN time, and a machine
# without synui or without the wallpapers simply has no picture to offer — the
# background falls back to the flat colour it drew before. Making these
# dependencies would mean a television interface that will not build without a
# compositor it does not link against.
conf.set_quoted('SYNUI_WALLPAPER_DEFAULT',
                get_option('prefix') / get_option('datadir') / 'synui' / 'wallpaper.png')
configure_file(output: 'config.h', configuration: conf)

# ONE dependency beyond libc — libwayland-client — and it is worth saying why
# it is here and why the three obvious ones still are not:
#
#   wayland-client  big screen mode's `big mouse` drives the compositor's
#               POINTER from a gamepad stick, because a web browser on a
#               television takes pointer events and there is no other way to
#               give it any. That is a Wayland protocol
#               (zwlr_virtual_pointer_v1) and therefore a Wayland client; no
#               tool on the system does it, the way wtype does for the
#               keyboard. Everything else in this binary still works with no
#               display at all, over SSH — only `big mouse` needs a session,
#               and it says so and exits if there is not one.
#   libSDL      would be the natural way to read a gamepad, and would pull a
#               graphics stack into a tool that has to work over SSH. The
#               kernel already describes every attached device in
#               /sys/class/input, which is also what makes the discovery path
#               testable against a fixture.
#
#               ⚠ ONE command needs it anyway and STILL does not link it.
#               `map learn` writes SDL's mapping format, which is keyed on
#               SDL's GUID and numbered with SDL's joystick indices — neither
#               of which can be derived from evdev, and both of which fail
#               SILENTLY when got wrong. So it asks SDL, through dlopen at run
#               time: the headers below are for the structures and the enums
#               (an SDL_Event laid out by hand is an ABI trap), nothing is
#               linked, sdl3 is an OPTdepend, and every other command still
#               works on a machine that has never had SDL installed.
#   libudev     the same list, behind a library and a daemon, with nothing
#               here needing hotplug events.
#   libevdev    a wrapper over the ioctls in pad.c. Six of them are used.
wayland_client = dependency('wayland-client')
wayland_scanner = find_program('wayland-scanner')

# ⚠ INCLUDES ONLY — partial_dependency, so this contributes -I and NOTHING to
# the link line. A plain dependency() here would put libSDL3 in DT_NEEDED and
# make it a hard runtime dependency of `pads list` over SSH, which is exactly
# what the note above refuses. Check with:
#     ldd build/syn-arcade | grep -c SDL     → 0
sdl3_headers = dependency('sdl3').partial_dependency(includes: true, compile_args: true)

# ⚠ VENDORED, not taken from the wlr-protocols package, and deliberately: that
# package is not installed on a stock SynapseOS box (nothing else needs it),
# so depending on it would make this tree unbuildable on the machine it ships
# to for the sake of one 150-line file. It is MIT, its copyright header is
# intact, and it is never to be relicensed — see the licensing policy.
virtual_pointer_xml = 'protocols/wlr-virtual-pointer-unstable-v1.xml'

virtual_pointer_header = custom_target('wlr-virtual-pointer-client-header',
  input: virtual_pointer_xml,
  output: 'wlr-virtual-pointer-unstable-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

virtual_pointer_code = custom_target('wlr-virtual-pointer-private-code',
  input: virtual_pointer_xml,
  output: 'wlr-virtual-pointer-unstable-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

sources = files(
  'src/main.c',
  'src/util.c',
  'src/hud.c',
  'src/pad.c',
  'src/binds.c',
  'src/sdlmap.c',
  'src/sdlwiz.c',
  'src/big.c',
  'src/vptr.c',
  'src/fit.c',
)

executable('syn-arcade',
  sources + [virtual_pointer_header, virtual_pointer_code],
  dependencies: [wayland_client, sdl3_headers, cc.find_library('dl', required: false)],
  include_directories: include_directories('include', '.'),
  install: true)

install_data('data/syn-arcade.qml', install_dir: get_option('datadir') / 'syn-arcade')

# ⛔ THE TRANSLATION SINGLETON, IN ITS OWN MODULE DIRECTORY. Not beside the two
# .qml files above: a qmldir makes its directory an explicit QML module, and
# those are the files quickshell is pointed AT. The generated catalogs land
# beside it (po/meson.build installs qml/i18n/<lang>.json), because I18n.qml
# resolves them against its OWN file.
install_data('data/qml/I18n.qml', 'data/qml/qmldir',
             install_dir: get_option('datadir') / 'syn-arcade' / 'qml')

# Big screen mode's shell — a separate QML file, not a tab of the one above,
# because it is a different SURFACE: a full-screen layer-shell window with
# exclusive keyboard focus, where the other is an ordinary desktop window. They
# share the record format and nothing else.
install_data('data/syn-arcade-big.qml',
             install_dir: get_option('datadir') / 'syn-arcade')

# Big screen mode's tile glyphs, one per value of the `icon` column. big.c
# resolves <icon>.svg against this directory and ships the path as `iconfile`,
# so this list and apps_table() are the only two places a new tile touches.
#
# ⚠ NAMED ONE BY ONE rather than install_subdir(), which would also ship the
# README that explains how to draw them — and, worse, would keep silently
# shipping whatever else ended up in that directory. A file list that has to be
# edited is a file list somebody looks at.
install_data(
  'data/icons/steam.svg',
  'data/icons/retroarch.svg',
  'data/icons/lutris.svg',
  'data/icons/heroic.svg',
  'data/icons/moonlight.svg',
  'data/icons/greenlight.svg',
  'data/icons/syn-gfn.svg',
  'data/icons/music.svg',
  'data/icons/kodi.svg',
  'data/icons/plex.svg',
  'data/icons/jellyfin.svg',
  'data/icons/twitch.svg',
  'data/icons/youtube.svg',
  'data/icons/spotify.svg',
  'data/icons/firefox.svg',
  'data/icons/terminal.svg',
  'data/icons/syn-arcade.svg',
  'data/icons/visualizer.svg',
  'data/icons/desktop.svg',
  'data/icons/quit.svg',
  'data/icons/sleep.svg',
  'data/icons/restart.svg',
  'data/icons/poweroff.svg',
  # Not tile glyphs: the dendrite mark beside the wordmark in the header, and
  # the gear on the System menu's Settings row — which is a page of that menu
  # rather than something `big run` can run, so apps_table() emits no row for
  # it. Both go through icon_file() like the rest, which is what lets the shell
  # draw from the source tree and from the installed tree without knowing
  # which it is.
  'data/icons/synapse.svg',
  'data/icons/settings.svg',
  install_dir: get_option('datadir') / 'syn-arcade' / 'icons')

# Scalable only: Qt renders the SVG at whatever size the dock or the menu asks
# for, and a set of PNGs would be five more files to keep in step with one
# drawing.
install_data('data/syn-arcade.svg',
             install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps')

install_data('data/syn-arcade.desktop',
             install_dir: get_option('datadir') / 'applications')

install_data('data/syn-arcade-big.desktop',
             install_dir: get_option('datadir') / 'applications')

# ⚠ The session profile is not optional garnish — it is half the mechanism.
#
# It sets MANGOHUD_CONFIGFILE, which collapses MangoHud's five-candidate
# precedence list (where /etc/MangoHud.conf outranks the user's file) down to
# one writable path, and SDL_GAMECONTROLLERCONFIG_FILE, without which no game
# reads any mapping this tool writes. It also runs `hud ensure`, because
# MangoHud's inotify watch fails permanently if the config file does not exist
# when the game starts.
install_data('data/syn-arcade.sh', install_dir: get_option('sysconfdir') / 'profile.d')

subdir('po')

# ⚠ SEPARATE FROM THE SUITE BELOW, deliberately. This one builds a locale with
# localedef and runs the binary under it; the main suite asserts English golden
# output and must not.
test('syn-arcade-i18n', find_program('tests/i18n_test.sh'),
     args: [meson.current_source_dir(), meson.current_build_dir() / 'syn-arcade'],
     timeout: 120)

test('syn-arcade', find_program('tests/syn_arcade_test.sh'),
     args: [meson.current_build_dir() / 'syn-arcade'],
     workdir: meson.current_source_dir(),
     # ⚠ meson kills a test at 30s and calls that a BUILD failure. The suite
     # runs dozens of subprocesses against fixture trees; the default is not
     # generous enough to be safe on a loaded machine.
     timeout: 120)
