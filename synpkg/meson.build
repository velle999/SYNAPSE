project('synpkg', 'c',
  version: '0.1.0',
  license: 'GPL-2.0-or-later',
  default_options: ['c_std=c11', 'warning_level=2', 'werror=false'])

cc = meson.get_compiler('c')

# libalpm ships no .pc file in some pacman releases; fall back to the bare
# library plus a header check so a missing pacman-devel fails loudly at
# configure time rather than with a wall of implicit-declaration warnings.
alpm = dependency('libalpm', required: false)
if not alpm.found()
  alpm = cc.find_library('alpm', required: true)
  if not cc.has_header('alpm.h')
    error('alpm.h not found — install pacman (it ships the libalpm headers)')
  endif
endif

conf = configuration_data()
conf.set_quoted('SYNPKG_VERSION', meson.project_version())
conf.set_quoted('SYNPKG_DATADIR', get_option('prefix') / get_option('datadir') / 'synpkg')
configure_file(output: 'config.h', configuration: conf)

sources = files(
  'src/main.c',
  'src/util.c',
  'src/pconf.c',
  'src/alpmctx.c',
  'src/query.c',
  'src/trans.c',
  'src/curated.c',
  'src/arsenal.c',
  'src/ext.c',
  'src/about.c',
  'src/tui.c',
)

executable('synpkg', sources,
  include_directories: include_directories('include', '.'),
  dependencies: [alpm],
  install: true)

install_data('data/curated.tsv',   install_dir: get_option('datadir') / 'synpkg')

# The BlackArch bootstrap stays shell: it is orchestration of curl, gpg, tar and
# pacman, and its mirror-redundancy pass is the fix for a real outage. Under
# /usr/lib because it is implementation, not a command to find on PATH.
install_data('data/synpkg-enable-blackarch.sh',
             install_dir: get_option('prefix') / 'lib' / 'synpkg',
             rename: 'synpkg-enable-blackarch',
             install_mode: 'rwxr-xr-x')

install_data('data/synpkg.qml',    install_dir: get_option('datadir') / 'synpkg')
install_data('data/synpkg.desktop',
             install_dir: get_option('datadir') / 'applications')
install_data('data/org.synapseos.synpkg.policy',
             install_dir: get_option('datadir') / 'polkit-1' / 'actions')

test('synpkg', find_program('tests/synpkg_test.sh'),
     args: [meson.current_build_dir() / 'synpkg'],
     workdir: meson.current_source_dir())
