project('syn-edit', 'c',
  version: '0.1.0',
  license: 'GPL-2.0-or-later',
  default_options: ['c_std=c11', 'warning_level=2', 'werror=false'])

conf = configuration_data()
conf.set_quoted('SYNEDIT_VERSION', meson.project_version())
conf.set_quoted('SYNEDIT_DATADIR',
                get_option('prefix') / get_option('datadir') / 'syn-edit')
configure_file(output: 'config.h', configuration: conf)

# No dependencies but libc, on purpose — the same rule the rest of the suite
# follows.
#
# The one that would have been tempting is a regular expression library. There
# is no need: regcomp/regexec are in libc and are POSIX, and compiled as BASIC
# regular expressions they behave close enough to vim's default magic level
# that a pattern typed out of habit does what it looks like. Linking PCRE would
# buy lookarounds and cost a dependency on every machine that runs the editor.
#
# Syntax highlighting is a table in syntax.c rather than a parser per language,
# and the clipboard is delegated to wl-copy/wl-paste, which is how everything
# else on this desktop reaches it.
sources = files(
  'src/main.c',
  'src/util.c',
  'src/buffer.c',
  'src/syntax.c',
  'src/vim.c',
  'src/ex.c',
  'src/config.c',
  'src/drivers.c',
  'src/serve.c',
  'src/tui.c',
  'src/about.c',
)

executable('syn-edit', sources,
  include_directories: include_directories('include', '.'),
  install: true)

install_data('data/syn-edit.qml', install_dir: get_option('datadir') / 'syn-edit')

# The translation singleton, as its own QML module directory.
#
# ⚠ A qmldir makes its directory an explicit module, and syn-edit.qml is the
# file quickshell is pointed AT — putting one beside it changes how that file
# itself resolves. po/ installs the catalogs into qml/i18n/ alongside these.
install_data('data/qml/I18n.qml', 'data/qml/qmldir',
             install_dir: get_option('datadir') / 'syn-edit' / 'qml')

# ⚠ A UI STRING NO TRANSLATOR CAN REACH IS SILENTLY ENGLISH FOREVER, and in
# QML there is one extra way in: qsTr() looks exactly like the marking and is
# not — quickshell has no translator to install one into.
i18n_test = find_program('tests/i18n_test.sh')
test('i18n', i18n_test, args : [meson.current_source_dir()], timeout : 60)

# Scalable only: Qt renders the SVG at whatever size the dock or the menu asks
# for, and a set of PNGs would be five more files to keep in step with one
# drawing.
install_data('data/syn-edit.svg',
             install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps')

install_data('data/syn-edit.desktop',
             install_dir: get_option('datadir') / 'applications')

test('syn-edit', find_program('tests/syn_edit_test.sh'),
     args: [meson.current_build_dir() / 'syn-edit'],
     workdir: meson.current_source_dir(),
     timeout: 120)

subdir('po')
