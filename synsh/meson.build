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

executable('synsh',
  files('src/main.c','src/classify.c','src/exec.c','src/builtins.c',
        'src/ipc.c','src/readline_synsh.c','src/init.c','src/color.c'),
  include_directories: include_directories('include'),
  dependencies: [dep_readline, dep_threads],
  install: true)
