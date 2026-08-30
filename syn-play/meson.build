project('syn-play', 'c',
  version: '0.1.0',
  default_options: ['c_std=gnu11', 'warning_level=2', 'werror=false'])

cc = meson.get_compiler('c')

conf = configuration_data()
conf.set_quoted('SYNPLAY_VERSION', meson.project_version())
conf.set_quoted('SYNPLAY_DATADIR', get_option('prefix') / get_option('datadir') / 'syn-play')
configure_file(output: 'config.h', configuration: conf)

inc = include_directories('include', '.')

# ⛔ NOTHING BUT libc, AND MPV IS NOT LINKED. This program opens a unix socket
# and writes JSON at it; linking libmpv would put a player's ABI between a
# frontend and its own build, for a feature set reached perfectly well through a
# documented text protocol that also works from a shell script.
engine = files('src/util.c', 'src/json.c', 'src/ipc.c', 'src/playlist.c',
               'src/history.c', 'src/open.c', 'src/watch.c', 'src/serve.c',
               'src/tui.c')

syn_play = executable('syn-play', ['src/main.c'] + engine,
                      include_directories: inc,
                      install: true)

install_data('data/syn-play.qml', install_dir: get_option('datadir') / 'syn-play')
install_data('data/syn-play.desktop',
             install_dir: get_option('datadir') / 'applications')
install_data('data/syn-play.svg',
             install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps')

# ⛔ THE SUITE RUNS INSIDE A SCRATCH SYNPLAY_HOME AND ON ITS OWN SOCKET.
#
# Two reasons, and both are structural rather than careful. A program that keeps
# a history of what somebody has watched must not be testable only by writing
# into the real one — and a test that says `stop` on the shared socket would
# stop the music the person running it was listening to. SYNPLAY_HOME moves
# every path AND the socket, so neither is reachable from here.
#
# ⚠ The parser cases are compiled and fed the real bytes. A JSON reader is
# exactly the kind of thing a grep test cannot check, and the bug it exists to
# prevent — a key matching somebody's VALUE — is invisible to reading.
json_test = executable('json_test', ['tests/json_test.c'] + engine,
                       include_directories: inc, install: false)
test('json', json_test)

test('cli', find_program('tests/cli_test.sh'),
     args: [syn_play.full_path()],
     depends: [syn_play],
     timeout: 120)

# ⛔ FILES DRAGGED IN, DRIVEN RATHER THAN READ. A `keys` list that excludes the
# drag, a url left percent-encoded, a zone test comparing the wrong way round —
# every one of those reads correctly and does nothing, and synstudio lost three
# releases to exactly that. This loads the shipped QML offscreen and asserts on
# the queue MPV IS HOLDING, not on a property the window set itself.
#
# ⚠ It found a real one: `serve` polled the file descriptor and read with
# buffered stdio, so two commands arriving in a single write lost the second —
# every multi-file drop queued one file. Driving it from a shell could not see
# that, because two printfs are two writes.
test('drop', find_program('tests/drop_test.sh'),
     args: [syn_play.full_path()],
     depends: [syn_play],
     timeout: 180)
