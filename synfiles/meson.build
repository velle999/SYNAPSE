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
  'src/places.c',
  'src/recent.c',
  'src/volumes.c',
  'src/fileops.c',
  'src/trash.c',
  'src/about.c',
  'src/actions.c',
  'src/undo.c',
)

executable('synfiles', sources,
  include_directories: include_directories('include', '.'),
  install: true)

install_data('data/synfiles.qml', install_dir: get_option('datadir') / 'synfiles')

# Shipped so the browser can be LAUNCHED, deliberately without a MimeType= line.
# Registering inode/directory here would make synfiles the default file manager
# on install, which is exactly the decision this is not allowed to make for the
# user. Until it is chosen deliberately, nothing routes to it.
install_data('data/synfiles.desktop',
             install_dir: get_option('datadir') / 'applications')

test('synfiles', find_program('tests/synfiles_test.sh'),
     args: [meson.current_build_dir() / 'synfiles'],
     workdir: meson.current_source_dir())
