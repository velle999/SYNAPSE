project('synui', 'c',
  version : '0.1.0',
  default_options : ['warning_level=1', 'c_std=gnu17'])

wlroots   = dependency('wlroots-0.20')
# scenefx-0.5: the wlr_scene fork adding corner radius / backdrop blur / shadows.
# synui includes <scenefx/types/wlr_scene.h> (a superset of wlroots' wlr_scene.h)
# everywhere, and creates fx_renderer_create() instead of wlr_renderer_autocreate.
# The two are versioned in lockstep: 0.4.x = wlroots 0.19, 0.5.x = wlroots 0.20.
# Built from SYNAPSE/scenefx0.5/, which is a FORK — it ports six wlroots scene
# TUs in so client surfaces are scenefx-native nodes. Bumping wlroots means
# re-porting those from the matching wlroots release; see that PKGBUILD.
scenefx   = dependency('scenefx-0.5')
wayland   = dependency('wayland-server')
wayland_protos = dependency('wayland-protocols')
xkbcommon = dependency('xkbcommon')
pixman    = dependency('pixman-1')
threads   = dependency('threads')
cairo     = dependency('cairo')
rsvg      = dependency('librsvg-2.0')  # icons.c rasterizes SVG-only app icons
libinput  = dependency('libinput')
# sound.c watches udev so a device being plugged in can make a noise. Already
# installed as a wlroots dependency, so this adds no package to the ISO.
libudev   = dependency('libudev')
# screensaver.c: sd-bus, to own org.freedesktop.ScreenSaver
libsystemd = dependency('libsystemd')
libjpeg   = dependency('libjpeg')  # provided by libjpeg-turbo; wallpaper.c JPEG decode
libcurl   = dependency('libcurl')  # news.c fetches the feeds (on its own thread)
# dispcfg.c parses the connector EDID for HDR/colorimetry, which wlroots reads
# with this same library but does not re-export. Already installed as a wlroots
# dependency, so this adds no new package to the ISO.
libdisplay_info = dependency('libdisplay-info')

# effects.c drives the GLES2 renderer directly for the post-process pass
egl       = dependency('egl')
glesv2    = dependency('glesv2')

# XWayland pulls xcb headers in via wlr/xwayland.h
xcb       = dependency('xcb')
xcb_icccm = dependency('xcb-icccm')
xcb_ewmh  = dependency('xcb-ewmh')
xcb_randr = dependency('xcb-randr')   # xwayland.c marks the primary output

# nightlight.c's blackbody approximation uses log()/pow(). libm arrives
# transitively via cairo today; naming it means that staying true is not load
# bearing.
cc = meson.get_compiler('c')

# giflib has no pkg-config file, so it is found as a plain library + header.
# Needed because 62 of the Workshop previews here are .gif — without it
# nearly half the wallpaper picker previews blank (wpthumb.c).
giflib    = cc.find_library('gif', required : true)
libm = cc.find_library('m', required : false)

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

# kde-blur (org_kde_kwin_blur): vendored because plasma-wayland-protocols is not
# a dependency worth taking for one 40-line XML — and unlike the wlr protocols,
# no wlroots header needs it, so only src/kde_blur.c sees the generated code.
kde_blur_xml = join_paths(meson.project_source_root(),
  'protocols/kde-blur.xml')

kde_blur_h = custom_target('kde-blur-protocol.h',
  output  : 'kde-blur-protocol.h',
  input   : kde_blur_xml,
  command : [wl_scanner, 'server-header', '@INPUT@', '@OUTPUT@'])

kde_blur_c = custom_target('kde-blur-protocol.c',
  output  : 'kde-blur-protocol.c',
  input   : kde_blur_xml,
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
    'src/deskmenu.c',
    'src/geom_persist.c',
    'src/output_persist.c',
    'src/dispcfg.c',
    'src/edid.c',
    'src/power.c',
    'src/game.c',
    'src/filters.c',
    'src/uifx.c',
  'src/widgets.c',
  'src/sound.c',
    'src/clock.c',
    'src/greeter.c',
    'src/ctlpanel.c',
    'src/theme.c',
    'src/bt.c',
    'src/notif.c',
    'src/logind.c',
    'src/nightlight.c',
    'src/clipboard.c',
    'src/gpu.c',
    'src/taskmgr.c',
    'src/news.c',
    'src/screensaver.c',
    'src/session.c',
    'src/lock.c',
    'src/ai_interface.c',
    'src/secfeed.c',
    'src/synapd_mon.c',
    'src/config.c',
    'src/render.c',
    'src/effects.c',
    'src/wallpaper.c',
    'src/matrix.c',
    'src/imgdec.c',
    'src/wppick.c',
    'src/wpthumb.c',
    'src/cursor.c',
    'src/icons.c',
    'src/dock.c',
    'src/launcher.c',
    'src/cat.c',
    'src/cat_draw.c',
    'src/kde_blur.c',
    xdg_shell_h,
    xdg_shell_c,
    layer_shell_h,
    layer_shell_c,
    output_power_h,
    output_power_c,
    kde_blur_h,
    kde_blur_c,
    pointer_constraints_h,
    cursor_shape_h,
  ],
  include_directories : include_directories('src'),
  # scenefx BEFORE wlroots: both export the wlr_scene_* symbols, and DT_NEEDED
  # order decides resolution. scenefx must win so the surface/xdg/layer/subsurface
  # /drag/output-layout helpers build scenefx-native (type-3) buffer nodes that can
  # carry blur/corners/shadow. wlroots-first gives a mixed tree and dead clicks.
  dependencies : [scenefx, wlroots, wayland, xkbcommon, pixman, threads, cairo, rsvg,
                  libinput, libudev, libjpeg, giflib, libcurl, libdisplay_info,
                  xcb, xcb_icccm, xcb_ewmh, xcb_randr,
                  egl, glesv2, libsystemd, libm],
  c_args : ['-DWLR_USE_UNSTABLE',
            '-DSYNUI_DATADIR="' + get_option('prefix') / get_option('datadir') / 'synui' + '"'],
  install : true)

# synctl: the control-socket client (hyprctl equivalent). Talks to the socket
# synui exports as SYNUI_SOCKET; no Wayland connection of its own.
executable('synctl',
  sources : ['src/synctl.c'],
  install : true)

# synui-lock-auth: the native lock's PAM helper. The compositor forks it and
# reads a verdict off a pipe, so pam_unix's fail delay never stalls the event
# loop (see src/lock.c). Not setuid — pam_unix reaches shadow through its own
# unix_chkpwd. Installed on $PATH because lock.c execlp's it by name.
libpam = cc.find_library('pam')
executable('synui-lock-auth',
  sources : ['src/synui-lock-auth.c'],
  dependencies : [libpam],
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
install_data('data/logo-bold.svg',    install_dir : synui_datadir)

# Extra wallpapers, into /usr/share/backgrounds rather than synui's own datadir:
# that is one of the directories the picker (Super+W) already scans, so they show
# up as rows with no code change and no per-machine copying — on the live ISO too,
# whose root session has no ~/Pictures to scan. The picker labels a scanned image
# by its basename, hence the synapse- prefix: /usr/share/backgrounds is shared
# with every other package, and a bare "logo.png" there would be both a collision
# waiting to happen and a meaningless row in the list.
install_data('data/synapse-logo.png',
             install_dir : get_option('prefix') / get_option('datadir') / 'backgrounds')
install_data('data/synapse-logo-electrified.png',
             install_dir : get_option('prefix') / get_option('datadir') / 'backgrounds')

# Headless smoke test: boots the compositor on the headless backend, checks
# the advertised globals, maps a client (if foot is installed), screenshots
# (if grim is installed) and asserts a clean SIGTERM teardown. Run with
# `meson test`; add -Db_sanitize=address to a build dir for a leak-checked
# run (tests/lsan.supp covers library-internal static caches).
# An X11 client that maps/unmaps one toplevel repeatedly — the close-to-tray
# cycle Steam does, and the path every other test misses by mapping once and
# exiting. smoke.sh test 13 uses it to assert no frame is left behind per cycle;
# it skips when libX11 isn't available to build against.
x11 = dependency('x11', required : false)
smoke_args = [synui_exe]
if x11.found()
  x11_remap_test = executable('x11_remap_test',
    'tests/x11_remap_test.c',
    dependencies : [x11],
    build_by_default : true)
  smoke_args += [x11_remap_test]
endif

# security-context client bindings, for the test below. Client-side this time:
# every other generated protocol here is server-side, because synui is the
# compositor — this one has to act like an application to prove anything.
security_context_xml = join_paths(
  wayland_protos.get_variable('pkgdatadir'),
  'staging/security-context/security-context-v1.xml')

security_context_client_h = custom_target('security-context-v1-client-protocol.h',
  output  : 'security-context-v1-client-protocol.h',
  input   : security_context_xml,
  command : [wl_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

security_context_client_c = custom_target('security-context-v1-client-protocol.c',
  output  : 'security-context-v1-client-protocol.c',
  input   : security_context_xml,
  command : [wl_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

# Proves the RESTRICTING half of the global filter: that a client holding a
# security context really is refused screen capture, input interception and
# clipboard snooping. The smoke suite's other tests only show that ordinary
# clients still work — a filter that never matches would pass those too.
#
# smoke.sh finds this next to the synui binary rather than taking it as another
# positional argument, since $2 is already optional.
security_context_test = executable('security_context_test',
  ['tests/security_context_test.c',
   security_context_client_h, security_context_client_c],
  dependencies : [wayland_client],
  build_by_default : true)

test('smoke',
  find_program('tests/smoke.sh'),
  args : smoke_args,
  timeout : 180)

# The kitty (cat.c). Links only the drawing half — cat_draw.c is pure cairo, no
# wlroots — and renders every pose to a contact sheet. As a test it can only
# assert the mechanical parts (each pose drew something, inside its canvas, and
# the poses differ); the actual question, "does that read as a cat", is answered
# by looking at the PNG it writes.
cat_render_test = executable('cat_render_test',
  ['tests/cat_render_test.c', 'src/cat_draw.c'],
  include_directories : include_directories('src'),
  # scenefx too: synui.h includes <scenefx/types/wlr_scene.h> since the glass
  # migration, so this target stopped compiling without the header path even
  # though it draws nothing scenefx-y itself.
  dependencies : [scenefx, cairo, wlroots, wayland, pixman],
  c_args : ['-DWLR_USE_UNSTABLE'],   # synui.h pulls wlroots headers in
  build_by_default : false)
test('cat_render', cat_render_test, args : [meson.current_build_dir() / 'cat.png'])

# Every preview image the picker can be asked to draw, through the real
# decoders. The preview pane fails silently by design (it draws "Preview
# unavailable" and moves on), so nothing at runtime would report a decoder
# that had stopped working — 62 of the previews here are GIFs handled by code
# that exists for no other purpose.
#
# With no argument it runs a synthesised suite (valid PNG, corrupt file,
# missing file, cache re-hit) so it is meaningful without Wallpaper Engine
# installed. Pass a Workshop content tree to check the real mix:
#   ./wpthumb_test ~/.local/share/Steam/steamapps/workshop/content/431960
wpthumb_test = executable('wpthumb_test',
  ['tests/wpthumb_test.c', 'src/wpthumb.c', 'src/imgdec.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, wlroots, wayland, pixman, libjpeg, giflib],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('wpthumb', wpthumb_test)

# Desktop-icon grid, drag-to-move and deskicons.state. Links deskmenu.c against
# stubs for the compositor half (outputs, render, config paths) because there is
# no way to synthesize a pointer drag on the headless backend — see the test's
# header. It writes only into its own mkdtemp scratch $HOME.
deskicon_drag_test = executable('deskicon_drag_test',
  ['tests/deskicon_drag_test.c', 'src/deskmenu.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, wlroots, wayland, pixman],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('deskicon_drag', deskicon_drag_test)

# EDID HDR detection. edid.c deliberately depends on nothing but
# libdisplay-info, so this links it directly — no stubs, no wlroots, and the
# fixtures are the two real monitors on the dev desk (one HDR10, one SDR) so
# the test pins the distinction the old 10-bit-framebuffer probe could not make.
edid_hdr_test = executable('edid_hdr_test',
  ['tests/edid_hdr_test.c', 'src/edid.c'],
  include_directories : include_directories('src'),
  dependencies : [libdisplay_info],
  build_by_default : false)
test('edid_hdr', edid_hdr_test)
