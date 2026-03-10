project('synsh', 'c', version: '0.1.0',
  default_options: ['c_std=gnu17', 'warning_level=1'])

dep_readline = dependency('readline')
dep_threads  = dependency('threads')

add_project_arguments('-D_GNU_SOURCE', language: 'c')

executable('synsh',
  files('src/main.c','src/classify.c','src/exec.c','src/builtins.c',
        'src/ipc.c','src/readline_synsh.c','src/init.c'),
  include_directories: include_directories('include'),
  dependencies: [dep_readline, dep_threads],
  install: true)
