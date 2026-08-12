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
