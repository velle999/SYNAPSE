project('synui', 'c',
  version : '0.1.0',
  default_options : ['warning_level=1', 'c_std=gnu17'])

wlroots   = dependency('wlroots-0.19')
wayland   = dependency('wayland-server')
wayland_protos = dependency('wayland-protocols')
xkbcommon = dependency('xkbcommon')
pixman    = dependency('pixman-1')
threads   = dependency('threads')
cairo     = dependency('cairo')
rsvg      = dependency('librsvg-2.0')  # icons.c rasterizes SVG-only app icons
libinput  = dependency('libinput')
# screensaver.c: sd-bus, to own org.freedesktop.ScreenSaver
libsystemd = dependency('libsystemd')
libjpeg   = dependency('libjpeg')  # provided by libjpeg-turbo; wallpaper.c JPEG decode
libcurl   = dependency('libcurl')  # news.c fetches the feeds (on its own thread)

# effects.c drives the GLES2 renderer directly for the post-process pass
egl       = dependency('egl')
glesv2    = dependency('glesv2')

# XWayland pulls xcb headers in via wlr/xwayland.h
xcb       = dependency('xcb')
xcb_icccm = dependency('xcb-icccm')
xcb_ewmh  = dependency('xcb-ewmh')
xcb_randr = dependency('xcb-randr')   # xwayland.c marks the primary output

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

# wlr-layer-shell is an out-of-tree protocol whose generated code wlroots does
# not export, so we vendor the XML (protocols/) and generate it ourselves.
layer_shell_xml = join_paths(meson.project_source_root(),
  'protocols/wlr-layer-shell-unstable-v1.xml')

layer_shell_h = custom_target('wlr-layer-shell-unstable-v1-protocol.h',
  output  : 'wlr-layer-shell-unstable-v1-protocol.h',
  input   : layer_shell_xml,
  command : [wl_scanner, 'server-header', '@INPUT@', '@OUTPUT@'])

layer_shell_c = custom_target('wlr-layer-shell-unstable-v1-protocol.c',
  output  : 'wlr-layer-shell-unstable-v1-protocol.c',
  input   : layer_shell_xml,
  command : [wl_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

# wlr-output-power-management: its wlroots header exposes the mode enum from the
# generated protocol header, so we vendor + generate it too.
output_power_xml = join_paths(meson.project_source_root(),
  'protocols/wlr-output-power-management-unstable-v1.xml')

output_power_h = custom_target('wlr-output-power-management-unstable-v1-protocol.h',
  output  : 'wlr-output-power-management-unstable-v1-protocol.h',
  input   : output_power_xml,
  command : [wl_scanner, 'server-header', '@INPUT@', '@OUTPUT@'])

output_power_c = custom_target('wlr-output-power-management-unstable-v1-protocol.c',
  output  : 'wlr-output-power-management-unstable-v1-protocol.c',
  input   : output_power_xml,
  command : [wl_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

# pointer-constraints: wlroots' wlr_pointer_constraints_v1.h includes the
# generated protocol header (for the lifetime enum). Ships with
# wayland-protocols, so generate straight from pkgdatadir.
pointer_constraints_xml = join_paths(
  wayland_protos.get_variable('pkgdatadir'),
  'unstable/pointer-constraints/pointer-constraints-unstable-v1.xml')

pointer_constraints_h = custom_target('pointer-constraints-unstable-v1-protocol.h',
  output  : 'pointer-constraints-unstable-v1-protocol.h',
  input   : pointer_constraints_xml,
  command : [wl_scanner, 'server-header', '@INPUT@', '@OUTPUT@'])

# cursor-shape: same deal — wlroots' wlr_cursor_shape_v1.h includes the generated
# header for the wp_cursor_shape_device_v1_shape enum.
cursor_shape_xml = join_paths(
  wayland_protos.get_variable('pkgdatadir'),
  'staging/cursor-shape/cursor-shape-v1.xml')

cursor_shape_h = custom_target('cursor-shape-v1-protocol.h',
  output  : 'cursor-shape-v1-protocol.h',
  input   : cursor_shape_xml,
  command : [wl_scanner, 'server-header', '@INPUT@', '@OUTPUT@'])

# idle-inhibit: the synui-idle-inhibit helper is a Wayland *client* (unlike the
# compositor, which gets its server-side idle-inhibit code from wlroots), so it
# needs the client-side glue generated from the stock wayland-protocols XML.
idle_inhibit_client_xml = join_paths(
  wayland_protos.get_variable('pkgdatadir'),
  'unstable/idle-inhibit/idle-inhibit-unstable-v1.xml')

idle_inhibit_client_h = custom_target('idle-inhibit-unstable-v1-client-protocol.h',
  output  : 'idle-inhibit-unstable-v1-client-protocol.h',
  input   : idle_inhibit_client_xml,
  command : [wl_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

idle_inhibit_client_c = custom_target('idle-inhibit-unstable-v1-protocol.c',
  output  : 'idle-inhibit-unstable-v1-protocol.c',
  input   : idle_inhibit_client_xml,
  command : [wl_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

wayland_client = dependency('wayland-client')

synui_exe = executable('synui',
  sources : [
    'src/synui_main.c',
    'src/input.c',
    'src/constraints.c',
    'src/deco.c',
    'src/snap.c',
    'src/anim.c',
    'src/ime.c',
    'src/ipc.c',
    'src/foreign_toplevel.c',
    'src/layout.c',
    'src/layer.c',
    'src/xwayland.c',
    'src/output_mgmt.c',
    'src/output_persist.c',
    'src/dispcfg.c',
    'src/power.c',
    'src/game.c',
    'src/filters.c',
    'src/ctlpanel.c',
    'src/gpu.c',
    'src/taskmgr.c',
    'src/news.c',
    'src/screensaver.c',
    'src/session.c',
    'src/ai_interface.c',
    'src/secfeed.c',
    'src/synapd_mon.c',
    'src/config.c',
    'src/render.c',
    'src/effects.c',
    'src/wallpaper.c',
    'src/matrix.c',
    'src/wppick.c',
    'src/icons.c',
    'src/dock.c',
    'src/cat.c',
    'src/cat_draw.c',
    xdg_shell_h,
    xdg_shell_c,
    layer_shell_h,
    layer_shell_c,
    output_power_h,
    output_power_c,
    pointer_constraints_h,
    cursor_shape_h,
  ],
  include_directories : include_directories('src'),
  dependencies : [wlroots, wayland, xkbcommon, pixman, threads, cairo, rsvg,
                  libinput, libjpeg, libcurl, xcb, xcb_icccm, xcb_ewmh, xcb_randr,
                  egl, glesv2, libsystemd],
  c_args : ['-DWLR_USE_UNSTABLE',
            '-DSYNUI_DATADIR="' + get_option('prefix') / get_option('datadir') / 'synui' + '"'],
  install : true)

# synctl: the control-socket client (hyprctl equivalent). Talks to the socket
# synui exports as SYNUI_SOCKET; no Wayland connection of its own.
executable('synctl',
  sources : ['src/synctl.c'],
  install : true)

# synui-idle-inhibit: standalone helper that holds a real Wayland idle inhibitor
# on demand (fed by the synui-media-inhibit script). Installed under libdir/synui
# alongside synui-idle-wrapper.
executable('synui-idle-inhibit',
  sources : [
    'src/idle_inhibit.c',
    idle_inhibit_client_h,
    idle_inhibit_client_c,
  ],
  dependencies : [wayland_client],
  install : true,
  install_dir : get_option('libdir') / 'synui')

# Built-in wallpaper assets (wallpaper selector: matrix.c / wppick.c).
# SYNUI_DATADIR points the compositor at these at runtime.
synui_datadir = get_option('prefix') / get_option('datadir') / 'synui'
install_data('data/wallpaper.png',    install_dir : synui_datadir)
install_data('data/kanji_atlas.png',  install_dir : synui_datadir)
install_data('data/logo.svg',         install_dir : synui_datadir)

# Headless smoke test: boots the compositor on the headless backend, checks
# the advertised globals, maps a client (if foot is installed), screenshots
# (if grim is installed) and asserts a clean SIGTERM teardown. Run with
# `meson test`; add -Db_sanitize=address to a build dir for a leak-checked
# run (tests/lsan.supp covers library-internal static caches).
test('smoke',
  find_program('tests/smoke.sh'),
  args : [synui_exe],
  timeout : 120)

# The kitty (cat.c). Links only the drawing half — cat_draw.c is pure cairo, no
# wlroots — and renders every pose to a contact sheet. As a test it can only
# assert the mechanical parts (each pose drew something, inside its canvas, and
# the poses differ); the actual question, "does that read as a cat", is answered
# by looking at the PNG it writes.
cat_render_test = executable('cat_render_test',
  ['tests/cat_render_test.c', 'src/cat_draw.c'],
  include_directories : include_directories('src'),
  dependencies : [cairo, wlroots, wayland, pixman],
  c_args : ['-DWLR_USE_UNSTABLE'],   # synui.h pulls wlroots headers in
  build_by_default : false)
test('cat_render', cat_render_test, args : [meson.current_build_dir() / 'cat.png'])
