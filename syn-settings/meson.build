project('syn-settings', 'c',
  version: '0.1.0',
  license: 'GPL-2.0-or-later',
  default_options: ['c_std=c11', 'warning_level=2', 'werror=false'])

# No dependencies on purpose. Every answer this app gives comes from a tool
# that already owns it — localectl, timedatectl, systemctl, synctl — or from
# sysfs. Linking a library to ask systemd what it already prints would buy a
# build dependency and a second source of truth to disagree with.

conf = configuration_data()
conf.set_quoted('SYNSETTINGS_VERSION', meson.project_version())
conf.set_quoted('SYNSETTINGS_DATADIR',
                get_option('prefix') / get_option('datadir') / 'syn-settings')
configure_file(output: 'config.h', configuration: conf)

sources = files(
  'src/main.c',
  'src/util.c',
  'src/display.c',
  'src/region.c',
  'src/power.c',
  'src/system.c',
  'src/set.c',
  'src/network.c',
  'src/bluetooth.c',
  'src/fprint.c',
  'src/assistant.c',
  'src/probe.c',
  'src/kernel.c',
  'src/mode.c',
  'src/pkg.c',
  'src/boot.c',
  'src/apps.c',
  'src/time.c',
  'src/ai.c',
  'src/speech.c',
)

executable('syn-settings', sources,
  include_directories: include_directories('include', '.'),
  install: true)

install_data('data/syn-settings.qml',
             install_dir: get_option('datadir') / 'syn-settings')

# The translation singleton and its module declaration. ⚠ In a SUBDIRECTORY: a
# qmldir makes its directory an explicit QML module, and syn-settings.qml is the
# file quickshell is pointed at, so one beside it would change how that file
# itself resolves. po/ installs the catalogs into qml/i18n/ alongside these.
install_data('data/qml/I18n.qml', 'data/qml/qmldir',
             install_dir: get_option('datadir') / 'syn-settings' / 'qml')

i18n_test = find_program('tests/i18n_test.sh')
test('i18n', i18n_test, args : [meson.current_source_dir()], timeout : 60)

subdir('po')
# The application icon. Scalable only: Qt renders the SVG at whatever size the
# dock or the menu asks for, and a set of PNGs would be five more files to keep
# in step with one drawing.
install_data('data/syn-settings.svg',
             install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps')

install_data('data/syn-settings.desktop',
             install_dir: get_option('datadir') / 'applications')

# meson's default test timeout is THIRTY SECONDS, and this suite spends 21 of
# them on a fast native box — 130 checks, most of which fork the binary, and a
# good few of which wait on pacman. That left no margin at all: the first
# machine slower than this one turns a passing suite into
#
#     1/1 syn-settings:syn-settings TIMEOUT 30.00s killed by signal 15 SIGTERM
#     ==> ERROR: A failure occurred in check().
#
# which is a BUILD failure, not a test failure — makepkg aborts, syn-update
# reports "build failed", and nothing says the suite was still passing when it
# was killed. That is exactly what a 4-CPU QEMU guest did, 2026-08-13.
#
# 300 rather than "off": a suite that genuinely hangs must still fail, and ten
# times the native runtime is slack enough for a VM without being no limit.
test('syn-settings', find_program('tests/syn_settings_test.sh'),
     args: [meson.current_build_dir() / 'syn-settings'],
     workdir: meson.current_source_dir(),
     timeout: 300)
