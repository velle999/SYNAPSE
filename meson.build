# meson.build — synui
project('synui', 'c',
  version: '0.1.0',
  default_options: ['c_std=gnu17', 'warning_level=2', 'buildtype=debugoptimized']
)

dep_wayland  = dependency('wayland-server', version: '>=1.21')
dep_wlroots  = dependency('wlroots-0.18', version: '>=0.18')
dep_xkb      = dependency('xkbcommon')
dep_pixman   = dependency('pixman-1')
dep_threads  = dependency('threads')
dep_cairo    = dependency('cairo',  required: false)
dep_pango    = dependency('pango',  required: false)

add_project_arguments(
  '-D_GNU_SOURCE',
  '-DSYNUI_VERSION="@0@"'.format(meson.project_version()),
  '-DWLR_USE_UNSTABLE',
  language: 'c'
)

sources = files(
  'src/synui_main.c',
  'src/layout.c',
  'src/input.c',
  'src/ai_interface.c',
)

inc = include_directories('include')

deps = [dep_wayland, dep_wlroots, dep_xkb, dep_pixman, dep_threads]

if dep_cairo.found() and dep_pango.found()
  deps += [dep_cairo, dep_pango]
  add_project_arguments('-DHAVE_CAIRO_PANGO', language: 'c')
  message('synui: building with cairo+pango overlay text rendering')
else
  message('synui: cairo/pango not found — overlay text rendering disabled')
endif

executable('synui', sources,
  include_directories: inc,
  dependencies: deps,
  install: true,
  install_dir: get_option('bindir'),
)

# Wayland session
install_data('config/synui.desktop',
  install_dir: '/usr/share/wayland-sessions')

install_data('systemd/synui.service',
  install_dir: '/etc/systemd/user')

message('synui @0@ configured.'.format(meson.project_version()))
