# meson.build — synguard
project('synguard', 'c',
  version: '0.1.0',
  default_options: ['c_std=gnu17', 'warning_level=2', 'buildtype=debugoptimized']
)

dep_threads = dependency('threads')

add_project_arguments('-D_GNU_SOURCE', '-DSYNGUARD_VERSION="@0@"'.format(meson.project_version()), language: 'c')

sources = files(
  'src/synguard_main.c',
  'src/rule_engine.c',
  'src/event_processor.c',
  'src/ai_classifier.c',
  'src/action_engine.c',
  'src/core.c',
)

inc = include_directories('include')

executable('synguard', sources,
  include_directories: inc,
  dependencies: [dep_threads],
  install: true,
  install_dir: get_option('bindir'),
)

install_data('systemd/synguard.service', install_dir: '/etc/systemd/system')

install_subdir('rules', install_dir: '/etc/synguard', strip_directory: false)

message('synguard @0@ configured.'.format(meson.project_version()))
