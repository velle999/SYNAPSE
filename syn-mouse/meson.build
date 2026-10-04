project('syn-mouse', 'c',
  version: '0.1.0',
  default_options: ['c_std=gnu11', 'warning_level=2', 'werror=false'])

conf = configuration_data()
conf.set_quoted('SYNMOUSE_VERSION', meson.project_version())
conf.set_quoted('SYNMOUSE_DATADIR', get_option('prefix') / get_option('datadir') / 'syn-mouse')
conf.set_quoted('SYNMOUSE_LOCALEDIR', get_option('prefix') / get_option('localedir'))
configure_file(output: 'config.h', configuration: conf)

inc = include_directories('include', '.')

# Nothing but libc. The mouse is read with read(), the twins are written with
# write() to /dev/uinput, and everything else is a socket or inotify — a library
# between the hand and the cursor would be latency and a dependency on the one
# path this program must not get wrong.
#
# ⚠ THE ENGINE IS ITS OWN LIBRARY so tests/engine_test.c links the same object
# the daemon does. It has no device, file or clock of its own; the test drives
# it with a fake one.
engine = static_library('synmouse-engine',
  ['src/engine.c', 'src/keys.c', 'src/config.c', 'src/util.c'],
  include_directories: inc)

syn_mouse = executable('syn-mouse', ['src/main.c', 'src/daemon.c'],
  include_directories: inc,
  link_with: engine,
  install: true)

install_data('data/syn-mouse.qml', install_dir: get_option('datadir') / 'syn-mouse')
install_data('data/syn-mouse.desktop',
             install_dir: get_option('datadir') / 'applications')
install_data('data/syn-mouse.svg',
             install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps')

# ⛔ THE SINGLETON AND ITS qmldir, AS A MODULE DIRECTORY. quickshell is pointed
# at syn-mouse.qml; a qmldir beside that file would change how the file itself
# is resolved, so the translation singleton lives one level down and the
# generated catalogs land beside it (qml/i18n/*.json), which is the only place
# Qt.resolvedUrl("i18n") from inside I18n.qml will look.
install_data('data/qml/I18n.qml', 'data/qml/qmldir',
             install_dir: get_option('datadir') / 'syn-mouse' / 'qml')

# A USER unit: the mice and /dev/uinput are reached through the `input` group
# the installer puts every account in, and the focus it follows is that user's
# compositor. Nothing here needs root.
install_data('systemd/syn-mouse.service',
             install_dir: get_option('prefix') / 'lib' / 'systemd' / 'user')

subdir('po')

engine_test = executable('engine_test', 'tests/engine_test.c',
  include_directories: inc,
  link_with: engine)
test('engine', engine_test)

# ⛔ THE DAEMON SUITE NEVER TOUCHES A REAL DEVICE. Its mouse is a FIFO
# (SYNMOUSE_INPUT), its output is a text file (SYNMOUSE_OUTPUT), its synui is a
# socket the suite serves, and its config and control socket live in a scratch
# directory. A suite that made a real uinput device would be typing into
# whatever window the person running it has focused.
test('daemon', find_program('tests/daemon_test.sh'),
     args: [syn_mouse.full_path()],
     depends: [syn_mouse],
     is_parallel: false,
     timeout: 120)

test('cli', find_program('tests/cli_test.sh'),
     args: [syn_mouse.full_path()],
     depends: [syn_mouse],
     timeout: 60)

test('qml', find_program('tests/qml_test.sh'),
     args: [meson.current_source_dir() / 'data' / 'syn-mouse.qml'],
     timeout: 60)

# ⛔ THE ONE THAT PROVES THE RECORDS DID NOT MOVE. It builds a catalog from the
# TEMPLATE with every msgstr marked and runs every --rec command under it: a
# record that changes has a string reaching gettext, whether or not any real
# catalog carries that entry today.
test('i18n', find_program('tests/i18n_test.sh'),
     args: [meson.current_source_dir(), syn_mouse.full_path()],
     depends: [syn_mouse],
     timeout: 120)
