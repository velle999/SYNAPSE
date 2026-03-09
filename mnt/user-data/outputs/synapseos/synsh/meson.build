# meson.build — synsh
#
# Build:
#   meson setup build --buildtype=release
#   meson compile -C build

project('synsh', 'c',
  version: '0.1.0',
  default_options: ['c_std=gnu17', 'warning_level=2']
)

dep_readline = dependency('readline')
dep_threads  = dependency('threads')

add_project_arguments('-D_GNU_SOURCE', '-DSYNSH_VERSION="0.1.0-synapse"', language: 'c')

sources = files(
  'src/main.c',
  'src/parser.c',
  'src/executor.c',
  'src/ai.c',
  'src/builtins.c',   # also contains prompt.c and history.c inline
)

inc = include_directories('include')

executable('synsh',
  sources,
  include_directories: inc,
  dependencies: [dep_readline, dep_threads],
  install: true,
  install_dir: get_option('bindir'),
)

# Set as login shell option
install_data('synsh.conf', install_dir: '/etc/synsh')
