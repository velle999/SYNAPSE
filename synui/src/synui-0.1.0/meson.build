project('synui', 'c',
  version : '0.1.0',
  default_options : ['warning_level=1', 'c_std=gnu17'])

wlroots   = dependency('wlroots-0.19')
wayland   = dependency('wayland-server')
wayland_protos = dependency('wayland-protocols')
xkbcommon = dependency('xkbcommon')
pixman    = dependency('pixman-1')

wl_scanner = find_program('wayland-scanner')

xdg_shell_xml = join_paths(
  wayland_protos.get_variable('pkgdatadir'),
  'stable/xdg-shell/xdg-shell.xml')

xdg_shell_h = custom_target('xdg-shell-protocol.h',
  output  : 'xdg-shell-protocol.h',
  input   : xdg_shell_xml,
  command : [wl_scanner, 'server-header', '@INPUT@', '@OUTPUT@'])

xdg_shell_c = custom_target('xdg-shell-protocol.c',
  output  : 'xdg-shell-protocol.c',
  input   : xdg_shell_xml,
  command : [wl_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

executable('synui',
  sources : ['src/main.c', 'src/server.c', xdg_shell_h, xdg_shell_c],
  include_directories : [include_directories('include'), include_directories('.')],
  dependencies : [wlroots, wayland, xkbcommon, pixman],
  c_args : ['-DWLR_USE_UNSTABLE'],
  install : true)
