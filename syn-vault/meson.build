project('syn-vault', 'c',
  version: '0.1.0',
  default_options: ['c_std=gnu11', 'warning_level=2', 'werror=false'])

cc = meson.get_compiler('c')

conf = configuration_data()
conf.set_quoted('SYNVAULT_VERSION', meson.project_version())
conf.set_quoted('SYNVAULT_DATADIR', get_option('prefix') / get_option('datadir') / 'syn-vault')
configure_file(output: 'config.h', configuration: conf)

inc = include_directories('include', '.')

# ⛔ NO CRYPTO LIBRARY, AND THAT IS THE DESIGN. gocryptfs is a program this one
# runs, not a library it links: see synvault.h. The only dependency here is libc,
# which is what a component that composes paths and forks a process should need.
engine = files('src/util.c', 'src/vault.c')

syn_vault = executable('syn-vault', ['src/main.c'] + engine,
                       include_directories: inc,
                       install: true)

install_data('data/syn-vault.qml', install_dir: get_option('datadir') / 'syn-vault')
install_data('data/syn-vault.desktop',
             install_dir: get_option('datadir') / 'applications')
install_data('data/syn-vault.svg',
             install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps')

# The vault logic against a STUB gocryptfs. The real one is a hard dependency of
# the package and not of the test: a suite that needed a FUSE mount would not run
# in a build container, on a machine without /dev/fuse, or as any user who is not
# the one running the desktop — which is three ways of saying it would not run.
# What is testable here is everything this program actually decides: the names it
# refuses, the paths it composes, that the password never reaches a command line,
# and that it will not mount over files somebody would then lose.
vault_test = find_program('tests/vault_test.sh')
test('vault', vault_test,
     args: [syn_vault.full_path(), meson.current_source_dir()],
     depends: [syn_vault],
     timeout: 60)

qml_test = find_program('tests/qml_test.sh')
test('qml', qml_test,
     args: [meson.current_source_dir() / 'data' / 'syn-vault.qml'],
     timeout: 60)
