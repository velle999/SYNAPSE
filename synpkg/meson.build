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
  'src/progress.c',
  'src/alpmctx.c',
  'src/query.c',
  'src/provides.c',
  'src/trans.c',
  'src/curated.c',
  'src/news.c',
  'src/kernel.c',
  'src/arsenal.c',
  'src/cachyos.c',
  'src/ext.c',
  'src/appstream.c',
  'src/ignore.c',
  'src/settings.c',
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

# The CachyOS bootstrap, beside it and for the same reason. It deliberately does
# NOT run upstream's cachyos-repo.sh — that installs a pacman fork and adds the
# v3/v4 whole-system repositories ahead of [core]. See the script's header.
install_data('data/synpkg-enable-cachyos.sh',
             install_dir: get_option('prefix') / 'lib' / 'synpkg',
             rename: 'synpkg-enable-cachyos',
             install_mode: 'rwxr-xr-x')

install_data('data/synpkg.qml',    install_dir: get_option('datadir') / 'synpkg')

# The translation singleton and its module declaration. ⚠ In a SUBDIRECTORY: a
# qmldir makes its directory an explicit QML module, and synpkg.qml is the file
# quickshell is pointed at, so one beside it would change how that file itself
# resolves. po/ installs the generated catalogs into qml/i18n/ alongside these.
install_data('data/qml/I18n.qml', 'data/qml/qmldir',
             install_dir: get_option('datadir') / 'synpkg' / 'qml')

# ⚠ A UI STRING IN QML THAT NO TRANSLATOR CAN REACH IS SILENTLY ENGLISH FOREVER,
# with one extra way in that C does not have: qsTr() looks exactly like the
# marking and is not — quickshell has no translator to load a .qm.
i18n_test = find_program('tests/i18n_test.sh')
test('i18n', i18n_test, args : [meson.current_source_dir()], timeout : 60)

subdir('po')

# The application icon. Scalable only: Qt renders the SVG at whatever size the
# dock asks for, and shipping rasterised copies would mean four files that can
# disagree with each other.
install_data('data/synpkg.svg',
             install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps')
install_data('data/synpkg.desktop',
             install_dir: get_option('datadir') / 'applications')
install_data('data/org.synapseos.synpkg.policy',
             install_dir: get_option('datadir') / 'polkit-1' / 'actions')

# timeout: meson's default is 30 SECONDS, and a test it kills is a FAILED test,
# which fails the build. This suite already takes ~15s of forks against a real
# ALPM database on a fast machine; a loaded build box or a slower disk clears 30
# without anything being wrong. A generous ceiling still catches a genuine hang.
test('synpkg', find_program('tests/synpkg_test.sh'),
     args: [meson.current_build_dir() / 'synpkg'],
     workdir: meson.current_source_dir(),
     timeout: 300)

# The progress bar, driven directly. Its own binary because the one branch that
# decides whether anything is drawn is isatty(stderr), and reaching that from
# the real synpkg means a real transaction against a real mirror — see the
# driver's header. It links the three files it actually needs rather than the
# whole suite, so a compile error in a verb cannot make this look broken.
progress_test = executable('progress_test',
  ['tests/progress_test.c', 'src/progress.c', 'src/pconf.c', 'src/util.c'],
  include_directories: include_directories('include', '.'),
  dependencies: [alpm],
  install: false)
test('progress', find_program('tests/progress_test.sh'),
     args: [progress_test],
     workdir: meson.current_source_dir(),
     timeout: 120)
