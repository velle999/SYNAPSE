project('syntty', 'c',
  version : '0.1.0',
  license : 'GPL-2.0-or-later',
  default_options : [
    'c_std=c11',
    'warning_level=3',
    'werror=false',
    'b_ndebug=if-release',
  ])

cc = meson.get_compiler('c')

# util is the only library dependency, and it is for forkpty. Nothing here
# links GL, and that is a design decision rather than an omission: of kitty's
# 264 MB resident, 188 MB is the graphics stack it maps by creating a context.
# See the file header in include/syntty.h.
util_dep = cc.find_library('util')

# Glyphs. freetype rasterises and fontconfig answers "which file is
# Monospace" — the second one ONCE, on a cold cache, because the answer is
# written to $XDG_CACHE_HOME/syntty/fonts.v1 and a later start hands the path
# straight to freetype. That lookup is one of the three things kitty pays 230 ms
# for; see the file header in src/font.c.
freetype_dep   = dependency('freetype2')
fontconfig_dep = dependency('fontconfig')

# The window. wayland-client and xkbcommon, and NOTHING ELSE — no EGL, no
# toolkit, no cairo. The buffer render.c paints is already in the format wl_shm
# wants, so it is handed to the compositor with no conversion and no library in
# between.
wayland_client_dep = dependency('wayland-client')
xkbcommon_dep      = dependency('xkbcommon')

# xdg-shell has no C library: the protocol XML ships with wayland-protocols and
# every client generates its own glue. Generated at BUILD time rather than
# checked in, so a wayland-protocols upgrade cannot leave a stale copy behind.
wl_protocols  = dependency('wayland-protocols')
wl_scanner    = find_program('wayland-scanner')
protocol_dir  = wl_protocols.get_variable(pkgconfig : 'pkgdatadir')

xdg_shell_xml = protocol_dir / 'stable/xdg-shell/xdg-shell.xml'

xdg_shell_h = custom_target('xdg-shell-client-protocol.h',
  input   : xdg_shell_xml,
  output  : 'xdg-shell-client-protocol.h',
  command : [wl_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

xdg_shell_c = custom_target('xdg-shell-protocol.c',
  input   : xdg_shell_xml,
  output  : 'xdg-shell-protocol.c',
  command : [wl_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

syntty_sources = files(
  'src/font.c',
  'src/grid.c',
  'src/main.c',
  'src/pty.c',
  'src/render.c',
  'src/util.c',
  'src/vt.c',
  'src/win.c',
)

syntty = executable('syntty',
  syntty_sources, xdg_shell_h, xdg_shell_c,
  include_directories : include_directories('include'),
  dependencies : [util_dep, freetype_dep, fontconfig_dep,
                  wayland_client_dep, xkbcommon_dep],
  install : true)

# ⚠ The suite runs a pty and a benchmark, so it is slower than a pure unit
# test — but meson kills a test at 30 seconds by DEFAULT and reports the kill
# as a BUILD failure, which looks like a broken program rather than a slow one.
# The timeout is stated here so a passing suite cannot fail the build.
test('syntty', find_program('tests/syntty_test.sh'),
  args : [syntty.full_path()],
  timeout : 120)
