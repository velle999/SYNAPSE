project('synsh', 'c', version: '0.1.0',
  default_options: ['c_std=gnu17', 'warning_level=1'])

dep_readline = dependency('readline')
dep_threads  = dependency('threads')

add_project_arguments('-D_GNU_SOURCE', language: 'c')

# readline was listed as a dependency but HAVE_READLINE was never defined, so
# the #ifdef'd block compiled out, --as-needed dropped the library, and the
# hand-rolled termios editor ran instead (no arrow keys, no Ctrl-A/E, ASCII
# only). Define it so the real thing is used; the fallback stays for builds
# without readline.
add_project_arguments('-DHAVE_READLINE', language: 'c')

synsh_bin = executable('synsh',
  files('src/main.c','src/classify.c','src/exec.c','src/builtins.c','src/intents.c',
        'src/ipc.c','src/readline_synsh.c','src/init.c','src/color.c',
        'src/i18n.c','src/phrases.c','src/expand.c'),
  include_directories: include_directories('include'),
  dependencies: [dep_readline, dep_threads],
  install: true)

# ⛔ THESE TWO SUITES EXISTED AND NOTHING RAN THEM. `meson test` answered "No
# tests defined" while 53 shell assertions and every language assertion sat in
# tests/ passing when somebody remembered to type them — which is not a gate,
# it is a habit. Wired in now, so a build runs them.
test('shell', find_program('tests/shell_test.sh'),
     args: [synsh_bin.full_path()], depends: [synsh_bin], timeout: 180)

# ⚠ AND THIS ONE READS THE SOURCES AS WELL AS RUNNING THE BINARY — the gate
# that every printed word goes through T() is a grep over src/*.c, so it needs
# the source root and not just the executable.
test('lang', find_program('tests/lang_test.sh'),
     args: [synsh_bin.full_path()], depends: [synsh_bin], timeout: 180)
