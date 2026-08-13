project('synfiles', 'c',
  version: '0.1.0',
  license: 'GPL-2.0-or-later',
  default_options: ['c_std=c11', 'warning_level=2', 'werror=false'])

conf = configuration_data()
conf.set_quoted('SYNFILES_VERSION', meson.project_version())
conf.set_quoted('SYNFILES_DATADIR',
                get_option('prefix') / get_option('datadir') / 'synfiles')
configure_file(output: 'config.h', configuration: conf)

# No external dependencies on purpose. The file type database is read as data
# (/usr/share/mime/globs2), mounting is delegated to udisks2/gvfs through their
# own command-line tools, and the icon theme is resolved by the front-end that
# already has it loaded. Nothing here needs to link glib to list a directory.
sources = files(
  'src/main.c',
  'src/util.c',
  'src/mime.c',
  'src/listing.c',
  'src/resolution.c',
  'src/find.c',
  'src/peek.c',
  'src/places.c',
  'src/recent.c',
  'src/volumes.c',
  'src/fileops.c',
  'src/trash.c',
  'src/config.c',
  'src/tui.c',
  'src/about.c',
  'src/actions.c',
  'src/undo.c',
  'src/archive.c',
)

executable('synfiles', sources,
  include_directories: include_directories('include', '.'),
  install: true)

install_data('data/synfiles.qml', install_dir: get_option('datadir') / 'synfiles')

# The application icon. Scalable only: Qt renders the SVG at whatever size the
# dock or the menu asks for, and a set of PNGs would be five more files to keep
# in step with one drawing.
install_data('data/synfiles.svg',
             install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps')

install_data('data/synfiles.desktop',
             install_dir: get_option('datadir') / 'applications')

# ...and the distribution's answer to "which of the candidates opens a folder".
# The .desktop only makes synfiles a candidate; this is what makes it the one
# that runs. It sits at the bottom of the mime-apps precedence list, so a user's
# own choice and an admin's /etc/xdg override both still win. See the file.
install_data('data/mimeapps.list',
             install_dir: get_option('datadir') / 'applications')

test('synfiles', find_program('tests/synfiles_test.sh'),
     args: [meson.current_build_dir() / 'synfiles'],
     workdir: meson.current_source_dir())
