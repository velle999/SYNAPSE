project('syn-clean', 'c',
  version: '0.1.0',
  default_options: ['c_std=gnu11', 'warning_level=2', 'werror=false'])

cc = meson.get_compiler('c')

conf = configuration_data()
conf.set_quoted('SYNCLEAN_VERSION', meson.project_version())
conf.set_quoted('SYNCLEAN_DATADIR', get_option('prefix') / get_option('datadir') / 'syn-clean')
configure_file(output: 'config.h', configuration: conf)

inc = include_directories('include', '.')

# Nothing but libc. This program walks directories, sums block counts and
# writes zeroes; every one of those is a syscall, and a dependency that wrapped
# them would be a dependency between the user's files and a bug somebody else
# wrote.
engine = files('src/util.c', 'src/scan.c', 'src/shred.c')

syn_clean = executable('syn-clean', ['src/main.c'] + engine,
                       include_directories: inc,
                       install: true)

install_data('data/syn-clean.qml', install_dir: get_option('datadir') / 'syn-clean')
install_data('data/syn-clean.desktop',
             install_dir: get_option('datadir') / 'applications')
install_data('data/syn-clean.svg',
             install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps')

# ⛔ THE SUITE RUNS ENTIRELY INSIDE A SCRATCH SYNCLEAN_HOME. A test for a
# program that deletes directory trees must never be able to reach the real
# one, and `SYNCLEAN_HOME` exists for exactly that: it is the only way the
# roots are composed, so a suite that sets it cannot touch $HOME even if a
# category is wrong.
clean_test = find_program('tests/clean_test.sh')
test('clean', clean_test,
     args: [syn_clean.full_path()],
     depends: [syn_clean],
     timeout: 120)

qml_test = find_program('tests/qml_test.sh')
test('qml', qml_test,
     args: [meson.current_source_dir() / 'data' / 'syn-clean.qml'],
     timeout: 60)
