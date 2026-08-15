project('syn-arcade', 'c',
  version: '0.1.0',
  license: 'GPL-2.0-or-later',
  default_options: ['c_std=c11', 'warning_level=2', 'werror=false'])

conf = configuration_data()
conf.set_quoted('SYNARCADE_VERSION', meson.project_version())
conf.set_quoted('SYNARCADE_DATADIR',
                get_option('prefix') / get_option('datadir') / 'syn-arcade')
configure_file(output: 'config.h', configuration: conf)

# No dependencies but libc, on purpose, and it is worth saying why for each of
# the three obvious ones this does NOT link:
#
#   libSDL      would be the natural way to read a gamepad, and would pull a
#               graphics stack into a tool that has to work over SSH. The
#               kernel already describes every attached device in
#               /sys/class/input, which is also what makes the discovery path
#               testable against a fixture.
#   libudev     the same list, behind a library and a daemon, with nothing
#               here needing hotplug events.
#   libevdev    a wrapper over the ioctls in pad.c. Six of them are used.
sources = files(
  'src/main.c',
  'src/util.c',
  'src/hud.c',
  'src/pad.c',
  'src/binds.c',
  'src/sdlmap.c',
)

executable('syn-arcade', sources,
  include_directories: include_directories('include', '.'),
  install: true)

install_data('data/syn-arcade.qml', install_dir: get_option('datadir') / 'syn-arcade')

# Scalable only: Qt renders the SVG at whatever size the dock or the menu asks
# for, and a set of PNGs would be five more files to keep in step with one
# drawing.
install_data('data/syn-arcade.svg',
             install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps')

install_data('data/syn-arcade.desktop',
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

test('syn-arcade', find_program('tests/syn_arcade_test.sh'),
     args: [meson.current_build_dir() / 'syn-arcade'],
     workdir: meson.current_source_dir(),
     # ⚠ meson kills a test at 30s and calls that a BUILD failure. The suite
     # runs dozens of subprocesses against fixture trees; the default is not
     # generous enough to be safe on a loaded machine.
     timeout: 120)
