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
  'src/probe.c',
  'src/kernel.c',
  'src/mode.c',
  'src/pkg.c',
  'src/boot.c',
)

executable('syn-settings', sources,
  include_directories: include_directories('include', '.'),
  install: true)

install_data('data/syn-settings.qml',
             install_dir: get_option('datadir') / 'syn-settings')
install_data('data/syn-settings.desktop',
             install_dir: get_option('datadir') / 'applications')

test('syn-settings', find_program('tests/syn_settings_test.sh'),
     args: [meson.current_build_dir() / 'syn-settings'],
     workdir: meson.current_source_dir())
