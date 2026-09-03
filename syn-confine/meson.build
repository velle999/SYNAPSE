project('syn-confine', 'c',
  version: '0.1.0',
  license: 'GPL-2.0-or-later',
  default_options: ['c_std=c11', 'warning_level=2', 'werror=false'])

# No dependencies but libc, and no library for Landlock either: glibc ships no
# wrappers for the three landlock syscalls, so they are called through
# syscall(2) directly. That also means this builds on a machine whose headers
# predate a right it uses — <linux/landlock.h> is the only requirement.
sc_exe = executable('syn-confine', files('src/syn-confine.c'),
  c_args: ['-DSYNCONFINE_VERSION="' + meson.project_version() + '"',
           '-DSYNCONFINE_LOCALEDIR="' + get_option('prefix') / get_option('localedir') + '"'],
  install: true)

test('syn-confine', find_program('tests/syn_confine_test.sh'),
     args: [meson.current_build_dir() / 'syn-confine'],
     workdir: meson.current_source_dir(),
     timeout: 120)

# ⛔ VIA meson's i18n MODULE, NOT A custom_target: libintl looks for
# <localedir>/<lang>/LC_MESSAGES/**syn-confine**.mo — named after the DOMAIN —
# and a loop over the languages can only name its output de.mo, which installs
# to the right directory under a name nothing ever looks for.
i18n = import('i18n')
i18n.gettext('syn-confine', preset : 'glib', install : true)

# ⛔ REGENERATE WITH `ninja -C <build> syn-confine-template`, NOT
# `syn-confine-pot`: i18n.gettext() defines that second name itself and drives
# it from po/POTFILES, which this component does not keep.
run_target('syn-confine-template',
  command : [find_program('po/pot.sh'), meson.project_source_root()])

# ⛔ THE ONE THAT PROVES THE EXIT STATUS AND THE SUITE'S OWN PIN. 78 is how a
# caller tells "the sandbox could not be built" from "the command failed", and
# it must be a number in every language; and the main suite greps the CONFINED
# child's strerror, which is why it pins LC_ALL before anything runs.
test('i18n', find_program('tests/i18n_test.sh'),
     args : [meson.current_source_dir(), sc_exe.full_path()],
     depends : [sc_exe],
     timeout : 120)
