project('syn-disks', 'c',
  version: '0.1.0',
  license: 'GPL-2.0-or-later',
  default_options: ['c_std=c11', 'warning_level=2', 'werror=false'])

conf = configuration_data()
conf.set_quoted('SYNDISKS_VERSION', meson.project_version())
conf.set_quoted('SYNDISKS_DATADIR',
                get_option('prefix') / get_option('datadir') / 'syn-disks')
# Where the .mo files land, and the domain they are named after. ⚠ The QML
# window does NOT read these — quickshell has no translator, so it reads JSON
# compiled from the same .po by po/meson.build. See include/i18n.h.
conf.set_quoted('SYNDISKS_LOCALEDIR',
                get_option('prefix') / get_option('datadir') / 'locale')
configure_file(output: 'config.h', configuration: conf)

# No dependencies but libc, on purpose. The shape of the storage tree is read
# out of /sys/class/block, which is the kernel's own account of it and needs no
# daemon; filesystem labels come from lsblk, which already reads the udev
# database; mounting and safe removal are delegated to udisks2, which owns the
# polkit rules. Linking libudev or libblkid would buy a build dependency and a
# second source of truth to disagree with.
sources = files(
  'src/main.c',
  'src/util.c',
  'src/sysfs.c',
  'src/mounts.c',
  'src/lsblk.c',
  'src/disks.c',
  'src/smart.c',
  'src/guard.c',
  'src/actions.c',
  'src/table.c',
  'src/partition.c',
  'src/copy.c',
  'src/about.c',
)

executable('syn-disks', sources,
  include_directories: include_directories('include', '.'),
  install: true)

install_data('data/syn-disks.qml', install_dir: get_option('datadir') / 'syn-disks')

# ⛔ THE TRANSLATION SINGLETON, IN ITS OWN MODULE DIRECTORY. Not beside the .qml
# above: a qmldir makes its directory an explicit QML module, and that is the
# file quickshell is pointed AT. The generated catalogs land beside it
# (po/meson.build installs qml/i18n/<lang>.json), because I18n.qml resolves them
# against its OWN file.
install_data('data/qml/I18n.qml', 'data/qml/qmldir',
             install_dir: get_option('datadir') / 'syn-disks' / 'qml')

# Scalable only: Qt renders the SVG at whatever size the dock or the menu asks
# for, and a set of PNGs would be five more files to keep in step with one
# drawing.
install_data('data/syn-disks.svg',
             install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps')

install_data('data/syn-disks.desktop',
             install_dir: get_option('datadir') / 'applications')

# ⚠ A TIMEOUT, because meson's default is 30 SECONDS and a test it kills is a
# BUILD failure, not a test failure. The suite ran in four seconds until it grew
# the wallpaper-accent check, which starts a real QML engine three times.
subdir('po')

# ⚠ SEPARATE FROM THE SUITE BELOW, deliberately. This one builds a locale with
# localedef and runs the binary under it; the main suite asserts English golden
# output and must not.
test('syn-disks-i18n', find_program('tests/i18n_test.sh'),
     args: [meson.current_source_dir(), meson.current_build_dir() / 'syn-disks'],
     timeout: 120)

test('syn-disks', find_program('tests/syn_disks_test.sh'),
     args: [meson.current_build_dir() / 'syn-disks'],
     workdir: meson.current_source_dir(),
     timeout: 120)
