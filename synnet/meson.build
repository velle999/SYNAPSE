project('synnet', 'c',
  version : '0.1.0',
  default_options : ['warning_level=2', 'c_std=gnu17'])

add_project_arguments(['-D_GNU_SOURCE', '-D_DEFAULT_SOURCE'], language: 'c')

executable('synnet',
  sources : [
    'src/main.c',
    'src/monitor.c',
  ],
  include_directories : include_directories('include'),
  install : true)
