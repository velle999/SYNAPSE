project('syn-scan', 'c',
  version: '0.1.0',
  default_options: ['c_std=gnu11', 'warning_level=2', 'werror=false'])

cc = meson.get_compiler('c')

conf = configuration_data()
conf.set_quoted('SYNSCAN_VERSION', meson.project_version())
conf.set_quoted('SYNSCAN_DATADIR', get_option('prefix') / get_option('datadir') / 'syn-scan')
conf.set_quoted('SYNSCAN_LOCALEDIR', get_option('prefix') / get_option('localedir'))
conf.set_quoted('SYNSCAN_STATEDIR', get_option('localstatedir') / 'lib' / 'syn-scan')
conf.set_quoted('SYNSCAN_QUARANTINE', get_option('localstatedir') / 'lib' / 'syn-scan' / 'quarantine')
configure_file(output: 'config.h', configuration: conf)

inc = include_directories('include', '.')

# Nothing but libc. The detecting is done by clamscan, rkhunter and chkrootkit,
# each run as a child process with an argv — never through a shell, because a
# filename is attacker-controlled input on the one machine this program exists
# to look at.
engine = files('src/util.c', 'src/finding.c', 'src/engine.c',
               'src/scan.c', 'src/quarantine.c')

syn_scan = executable('syn-scan', ['src/main.c'] + engine,
                      include_directories: inc,
                      install: true)

install_data('data/syn-scan.qml', install_dir: get_option('datadir') / 'syn-scan')
install_data('data/syn-scan.desktop',
             install_dir: get_option('datadir') / 'applications')
install_data('data/syn-scan.svg',
             install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps')

install_data('systemd/syn-scan.service', install_dir: get_option('prefix') / 'lib/systemd/system')
install_data('systemd/syn-scan.timer',   install_dir: get_option('prefix') / 'lib/systemd/system')

subdir('po')

test('cli', find_program('tests/scan_test.sh'),
     args: [syn_scan.full_path()],
     env: ['SYNSCAN_TESTROOT=' + meson.current_build_dir() / 'testroot'],
     timeout: 120)
