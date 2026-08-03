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
    'src/hit.c',
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
    'src/aimodel.c',
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
    'src/record.c',
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
synctl_exe = executable('synctl',
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

# Sanitizer environment for EVERY test, not just the ones that remember to set
# it. smoke.sh and resize_heal.sh exported LSAN_OPTIONS themselves (they have
# to — they are also run by hand, outside meson), and nothing else got it at
# all: the C-binary tests ran with no suppression file, so cat_render aborted
# on the very libfontconfig/libcairo entries tests/lsan.supp already listed.
# A suppression file that only some tests load is worse than none, because the
# entries look covered.
#
# fast_unwind_on_malloc=0 buys the frames the suppressions need. The default
# frame-pointer unwinder truncates a leak stack inside whichever library did
# the allocating, which for the NVIDIA driver's D-Bus connection means the
# stack ends in libdbus-1 and for its GBM backend means it ends at
# "<unknown module>" — in both cases with the actual owner, several frames
# further out, never recorded. Suppressing what a truncated stack DOES show
# means suppressing by allocator instead of by owner. The slow unwinder costs
# this suite a few seconds and is what lets tests/lsan.supp name the driver.
#
# protect_shadow_gap=0 is smoke.sh's, for the same reason it gives there: a
# real graphics driver JITs into the shadow gap. Kept in sync with the values
# the two scripts export; theirs win when they are run standalone.
add_test_setup('sanitize',
  is_default : true,
  env : [
    'LSAN_OPTIONS=suppressions=' + meson.current_source_dir() /
      'tests/lsan.supp' + ':print_suppressions=0',
    'ASAN_OPTIONS=protect_shadow_gap=0:fast_unwind_on_malloc=0:' +
      'halt_on_error=1:abort_on_error=1:print_summary=1',
  ])

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

  # An override-redirect window that moves itself AFTER mapping — a Steam menu
  # correcting its own placement. Its test photographs the screen either side of
  # the move, so it needs grim and python3+PIL as well as X11 (it skips without
  # any of them).
  x11_or_move_test = executable('x11_or_move_test',
    'tests/x11_or_move_test.c',
    dependencies : [x11],
    build_by_default : true)

  x11_or_move = find_program('tests/x11_or_move.sh')
  test('x11_or_move', x11_or_move,
       args    : [synui_exe.full_path(), x11_or_move_test.full_path()],
       depends : [synui_exe, x11_or_move_test],
       timeout : 120)
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
# The row/column arithmetic every panel's pointer handler sits on. Pure
# geometry, so it needs no compositor at all — but it does need synui.h, which
# drags in wlroots and cairo for their types.
hit_test = executable('hit_test',
  ['tests/hit_test.c', 'src/hit.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, wlroots, wayland, pixman],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('hit', hit_test)

# The panel pointer contract, on a real panel. There is no way to synthesise a
# click into a headless synui — no virtual-pointer protocol, no input devices on
# the headless backend, and uinput would be picked up by the LIVE session — so
# the handlers are called directly, exactly as input.c's chain calls them.
# Stubs are lid_test's; see that file's header for why power.c links alone.
panel_pointer_test = executable('panel_pointer_test',
  ['tests/panel_pointer_test.c', 'src/power.c', 'src/hit.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, wlroots, wayland, pixman],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('panel_pointer', panel_pointer_test)

# The control panel's CHOICE row (System ▸ AI model). Driven by calling
# ctlpanel_key() with keysyms for the same reason panel_pointer_test calls the
# pointer handlers: nothing can synthesise input into a headless synui. What it
# pins is mostly the NOT-loading — a row whose values are multi-gigabyte models
# must not act on a keypress you were only passing through.
# AIMODEL_DIR is redirected at a directory of stub files the test writes, the
# way lid_test redirects SYNUI_POWER_SUPPLY_DIR.
ctlpanel_choice_test = executable('ctlpanel_choice_test',
  ['tests/ctlpanel_choice_test.c', 'src/ctlpanel.c', 'src/aimodel.c', 'src/hit.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, wlroots, wayland, pixman, xkbcommon, libcurl],
  c_args : ['-DWLR_USE_UNSTABLE',
            '-DAIMODEL_DIR="' + meson.current_build_dir() +
            '/ctlpanel_choice_models"'],
  build_by_default : false)
test('ctlpanel_choice', ctlpanel_choice_test)

# The download catalogue: what synui believes a Hugging Face answer says, and
# what it will agree to send on the strength of it. Pure functions only — the
# fetch itself is a thread and a socket, and neither belongs in a unit test.
# The refusals here are one half of a privilege boundary whose other half is
# syn-model's tests/fetch_validate_test.sh; both are meant to hold alone.
aimodel_catalog_test = executable('aimodel_catalog_test',
  ['tests/aimodel_catalog_test.c', 'src/aimodel.c', 'src/hit.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, wlroots, wayland, pixman, xkbcommon, libcurl],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('aimodel_catalog', aimodel_catalog_test)

edid_hdr_test = executable('edid_hdr_test',
  ['tests/edid_hdr_test.c', 'src/edid.c'],
  include_directories : include_directories('src'),
  dependencies : [libdisplay_info],
  build_by_default : false)
test('edid_hdr', edid_hdr_test)

# Laptop lid actions. Links power.c against stubs for the compositor, because
# the lid cannot be closed headless — wlroots' headless backend has no input
# devices, and the only real alternative is a uinput switch, which the *live*
# session's synui would pick up and act on. See the test's header.
# SYNUI_POWER_SUPPLY_DIR aims power_on_ac() at a scratch tree the test builds,
# since there is no other way to exercise the on-battery branch on a machine
# that is plugged in. Only this target defines it; the shipped binary keeps the
# real /sys/class/power_supply.
lid_test = executable('lid_test',
  # hit.c is linked in rather than stubbed: it is pure geometry with no wlroots
  # in it, so the pointer handlers power.c grew are exercised for real.
  ['tests/lid_test.c', 'src/power.c', 'src/hit.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, wlroots, wayland, pixman],
  c_args : ['-DWLR_USE_UNSTABLE',
            '-DSYNUI_POWER_SUPPLY_DIR="' + meson.current_build_dir() +
            '/lid_test_power_supply"'],
  build_by_default : false)
test('lid', lid_test)

# Re-sending a configure the client never took (view_heal_size, synui_main.c).
# The client is a real xdg-shell client that drops one configure, which is what
# left a maximized Firefox rendering into 552x304 of its own full-screen frame;
# see tests/stubborn_client.c. It needs the CLIENT half of xdg-shell, where the
# compositor uses the server half generated above.
xdg_shell_client_h = custom_target('xdg-shell-client-protocol.h',
  output  : 'xdg-shell-client-protocol.h',
  input   : xdg_shell_xml,
  command : [wl_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

stubborn_client = executable('stubborn_client',
  ['tests/stubborn_client.c', xdg_shell_client_h, xdg_shell_c],
  dependencies : [wayland_client],
  build_by_default : false)

resize_heal = find_program('tests/resize_heal.sh')
test('resize_heal', resize_heal,
     args    : [synui_exe.full_path(), stubborn_client.full_path(),
                synctl_exe.full_path()],
     depends : [synui_exe, stubborn_client, synctl_exe],
     timeout : 60)

# Whether windows.conf gets a say on the desktop that is opening the window
# (layout_restore_geometry, layout.c). Reuses the same client: it only has to be
# a real xdg-shell toplevel with a stable app_id to look up. Four layouts, one
# window each, so the timeout is the roomier one.
geom_layout = find_program('tests/geom_layout.sh')
test('geom_layout', geom_layout,
     args    : [synui_exe.full_path(), stubborn_client.full_path(),
                synctl_exe.full_path()],
     depends : [synui_exe, stubborn_client, synctl_exe],
     timeout : 120)

# The case geom_layout cannot cover, because it seeds windows.conf on purpose:
# an app the desk has NEVER seen, opening on a floating desktop, where
# layout_apply is a no-op and there is no remembered box to fall back on. Same
# client, three opens.
float_first_open = find_program('tests/float_first_open.sh')
test('float_first_open', float_first_open,
     args    : [synui_exe.full_path(), stubborn_client.full_path(),
                synctl_exe.full_path()],
     depends : [synui_exe, stubborn_client, synctl_exe],
     timeout : 120)

# What Alt+Tab can reach (input.c alttab_candidates) and, more importantly, WHEN
# it acts on it (alttab_reveal, at commit only). Three windows across three
# desktops with one minimized, so every case is live at once; same client again.
# Whether a tiling desktop can get its windows back (layout_reclaim, layout.c):
# the `retile` action, the reclaim on selecting a tiling layout, and the
# maximize/un-maximize fixed point that made the bug read as unfixable.
retile = find_program('tests/retile.sh')
test('retile', retile,
     args    : [synui_exe.full_path(), stubborn_client.full_path(),
                synctl_exe.full_path()],
     depends : [synui_exe, stubborn_client, synctl_exe],
     timeout : 120)

# Whether the window you focus on a MONOCLE desktop is the window you can see
# (layout_monocle + the reflow in focus_view). Three windows on one headless
# output, focus moved by both cycles, plus the floating-window exemption. Same
# client again.
monocle_focus = find_program('tests/monocle_focus.sh')
test('monocle_focus', monocle_focus,
     args    : [synui_exe.full_path(), stubborn_client.full_path(),
                synctl_exe.full_path()],
     depends : [synui_exe, stubborn_client, synctl_exe],
     timeout : 120)

# Whether the layout you chose is still there next login (layout_state_save /
# _load, layout.c): two desktops changed, the compositor restarted on the same
# XDG_CONFIG_HOME, then a junk state file. No client — a desktop's layout is a
# property of the desktop, so this one needs no windows at all.
layout_persist = find_program('tests/layout_persist.sh')
test('layout_persist', layout_persist,
     args    : [synui_exe.full_path(), synctl_exe.full_path()],
     depends : [synui_exe, synctl_exe],
     timeout : 120)

alttab_scope = find_program('tests/alttab_scope.sh')
test('alttab_scope', alttab_scope,
     args    : [synui_exe.full_path(), stubborn_client.full_path(),
                synctl_exe.full_path()],
     depends : [synui_exe, stubborn_client, synctl_exe],
     timeout : 120)
