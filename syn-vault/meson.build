project('syn-vault', 'c',
  version: '0.1.0',
  default_options: ['c_std=gnu11', 'warning_level=2', 'werror=false'])

cc = meson.get_compiler('c')

conf = configuration_data()
conf.set_quoted('SYNVAULT_VERSION', meson.project_version())
conf.set_quoted('SYNVAULT_DATADIR', get_option('prefix') / get_option('datadir') / 'syn-vault')
conf.set_quoted('SYNVAULT_LOCALEDIR', get_option('prefix') / get_option('localedir'))
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

# ⛔ THE SINGLETON AND ITS qmldir, AS A MODULE DIRECTORY. quickshell is pointed
# at syn-vault.qml; a qmldir beside that file would change how the file itself
# is resolved, so the translation singleton lives one level down and the
# generated catalogs land beside it (qml/i18n/*.json), which is the only place
# Qt.resolvedUrl("i18n") from inside I18n.qml will look.
install_data('data/qml/I18n.qml', 'data/qml/qmldir',
             install_dir: get_option('datadir') / 'syn-vault' / 'qml')

subdir('po')

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

# ⛔ THE ONE THAT PROVES THE RECORDS DID NOT MOVE. It builds a catalog from the
# TEMPLATE with every msgstr marked and runs the offline --rec commands under
# it: a record that changes has a string reaching gettext, whether or not any
# real catalog carries that entry today. A marked field name — or a marked
# `open` — is invisible to a real-catalog diff until somebody translates it,
# and by then it is a window that thinks a locked vault is mounted.
test('i18n', find_program('tests/i18n_test.sh'),
     args: [meson.current_source_dir(), syn_vault.full_path()],
     depends: [syn_vault],
     timeout: 120)
