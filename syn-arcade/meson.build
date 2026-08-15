project('syn-arcade', 'c',
  version: '0.1.0',
  license: 'GPL-2.0-or-later',
  default_options: ['c_std=c11', 'warning_level=2', 'werror=false'])

conf = configuration_data()
conf.set_quoted('SYNARCADE_VERSION', meson.project_version())
conf.set_quoted('SYNARCADE_DATADIR',
                get_option('prefix') / get_option('datadir') / 'syn-arcade')
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
#   libudev     the same list, behind a library and a daemon, with nothing
#               here needing hotplug events.
#   libevdev    a wrapper over the ioctls in pad.c. Six of them are used.
wayland_client = dependency('wayland-client')
wayland_scanner = find_program('wayland-scanner')

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
  'src/big.c',
  'src/vptr.c',
)

executable('syn-arcade',
  sources + [virtual_pointer_header, virtual_pointer_code],
  dependencies: [wayland_client],
  include_directories: include_directories('include', '.'),
  install: true)

install_data('data/syn-arcade.qml', install_dir: get_option('datadir') / 'syn-arcade')

# Big screen mode's shell — a separate QML file, not a tab of the one above,
# because it is a different SURFACE: a full-screen layer-shell window with
# exclusive keyboard focus, where the other is an ordinary desktop window. They
# share the record format and nothing else.
install_data('data/syn-arcade-big.qml',
             install_dir: get_option('datadir') / 'syn-arcade')

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

test('syn-arcade', find_program('tests/syn_arcade_test.sh'),
     args: [meson.current_build_dir() / 'syn-arcade'],
     workdir: meson.current_source_dir(),
     # ⚠ meson kills a test at 30s and calls that a BUILD failure. The suite
     # runs dozens of subprocesses against fixture trees; the default is not
     # generous enough to be safe on a loaded machine.
     timeout: 120)
