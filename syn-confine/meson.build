project('syn-confine', 'c',
  version: '0.1.0',
  license: 'GPL-2.0-or-later',
  default_options: ['c_std=c11', 'warning_level=2', 'werror=false'])

# No dependencies but libc, and no library for Landlock either: glibc ships no
# wrappers for the three landlock syscalls, so they are called through
# syscall(2) directly. That also means this builds on a machine whose headers
# predate a right it uses — <linux/landlock.h> is the only requirement.
executable('syn-confine', files('src/syn-confine.c'),
  c_args: ['-DSYNCONFINE_VERSION="' + meson.project_version() + '"'],
  install: true)

test('syn-confine', find_program('tests/syn_confine_test.sh'),
     args: [meson.current_build_dir() / 'syn-confine'],
     workdir: meson.current_source_dir(),
     timeout: 120)
