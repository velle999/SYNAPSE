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
# text.c asks fontconfig which installed font can draw a character the UI font
# cannot, then loads it through cairo-ft. Without this every panel is limited to
# the single face the toy font API resolves to, which is how CJK and emoji used
# to come out as question marks. Already a synui depends= (and cairo links it
# anyway), so this adds no package to the ISO.
fontconfig = dependency('fontconfig')
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
    'src/spawntoggle.c',
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
    'src/deskdrop.c',
    'src/deskdrag.c',
    'src/geom_persist.c',
    'src/output_persist.c',
    'src/dispcfg.c',
    'src/edid.c',
    'src/power.c',
    'src/game.c',
    'src/filters.c',
    'src/aimodel.c',
    'src/gguf.c',
    'src/uifx.c',
  'src/widgets.c',
  'src/sound.c',
    'src/clock.c',
    'src/greeter.c',
    'src/ctlpanel.c',
    'src/keys.c',
    'src/overview.c',
    'src/theme.c',
    'src/bt.c',
    'src/notif.c',
    'src/logind.c',
    'src/nightlight.c',
    'src/clipboard.c',
    'src/gpu.c',
    'src/taskmgr.c',
    'src/news.c',
    'src/saver.c',
    'src/saver_state.c',
    'src/screensaver.c',
    'src/session.c',
    'src/lock.c',
    'src/ai_interface.c',
    'src/secfeed.c',
    'src/synapd_mon.c',
    'src/config.c',
    'src/settings.c',
    'src/render.c',
    'src/cairo_shapes.c',
    'src/text.c',
    'src/contrast.c',
    'src/effects.c',
    'src/wallpaper.c',
    'src/matrix.c',
    'src/imgdec.c',
    'src/wppick.c',
    'src/wpthumb.c',
    'src/cursor.c',
    'src/fontpick.c',
    'src/emoji.c',
    'src/emoji_data.c',
    'src/calc.c',
    'src/calceval.c',
    'src/panel.c',
    'src/eq.c',
    'src/crop.c',
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
  dependencies : [scenefx, wlroots, wayland, xkbcommon, pixman, threads, cairo, fontconfig, rsvg,
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

# synui-lock-fprint: the same trick for the fingerprint reader, and needed even
# more — pam_fprintd does not return until a finger is presented or it times
# out, so in-process it would freeze the desktop for the length of a swipe. Runs
# for as long as the screen is locked. Links only libpam: fprintd is reached
# through pam_fprintd.so, which is an optdepend, so a machine without it builds
# and runs exactly the same and the helper reports "unavailable" at runtime.
# On $PATH, like the password helper, because lock.c execlp's it by name.
executable('synui-lock-fprint',
  sources : ['src/synui-lock-fprint.c'],
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

# syn-calc — the calculator's parser with a main() around it.
#
# Deliberately NOT linked against wlroots, wayland or cairo: calceval.c was
# split out of calc.c precisely so that answering "1440 * 0.8" needs no
# compositor, and the dependency list is what enforces that. If this target
# ever needs another dependency, something has leaked back across the line.
syn_calc = executable('syn-calc',
  ['tools/syn-calc.c', 'src/calceval.c'],
  include_directories : include_directories('src'),
  dependencies : [libm],
  install : true)

# The calculator, as a launchable entry.
#
# Super+X has opened it for a while, but a keybind is not discoverable and it
# was in neither the start menu nor rofi — both of which read
# /usr/share/applications and nothing else. One file serves both. It is a
# compositor panel rather than a program, so its Exec asks synui to open the
# same thing the keybind does.
install_data('data/synui-calc.desktop',
             install_dir : get_option('prefix') / get_option('datadir') / 'applications')

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
  # text.c comes along wherever cat_draw.c goes: it draws its label through
  # syn_show_text() now, like everything else that puts text on screen.
  ['tests/cat_render_test.c', 'src/cat_draw.c', 'src/text.c'],
  include_directories : include_directories('src'),
  # scenefx too: synui.h includes <scenefx/types/wlr_scene.h> since the glass
  # migration, so this target stopped compiling without the header path even
  # though it draws nothing scenefx-y itself.
  dependencies : [scenefx, cairo, fontconfig, wlroots, wayland, pixman],
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

# Dragging a dock icon to a new place in the row: which gesture a release turns
# out to have been, and the rule that decides where a drop lands in the PIN
# LIST. Same shape as the test above and for the same reason — the headless
# backend has no pointer, so dock.c is linked against stubs and driven through
# the entry points input.c uses. syn_config_path() is stubbed to REFUSE, so
# dock_state_save() can be counted without any path to a real dock.state.
dock_reorder_test = executable('dock_reorder_test',
  ['tests/dock_reorder_test.c', 'src/dock.c', 'src/contrast.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, wlroots, wayland, pixman],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('dock_reorder', dock_reorder_test)

# The text/uri-list a drag-and-drop hands the desktop. INCLUDES deskdrop.c
# rather than linking it: the percent-decoding, the list walk and the
# collision-renaming are all static, and there is no way to drive the wire side
# of a drop headless (no client, no seat, no pointer). Touches only its own
# mkdtemp scratch dir.
deskdrop_test = executable('deskdrop_test',
  ['tests/deskdrop_test.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, wlroots, wayland, pixman],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('deskdrop', deskdrop_test)

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

# Which titlebar button sits where, per chrome style — the painter in deco.c and
# the hit test beside it read one set of functions in synui.h, and this is what
# says they still agree. The Mac styles moved the buttons to the LEFT, so the
# failure mode is a click that closes a window somebody meant to minimize:
# silent, invisible in a screenshot, and not caught by anything else here.
chrome_layout_test = executable('chrome_layout_test',
  ['tests/chrome_layout_test.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, wlroots, wayland, pixman],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('chrome_layout', chrome_layout_test)

# The config parser and settings.state. config.c's key chain was lifted whole
# out of synui_config_load() into config_parse_kv() so the control panel's
# persistence could read it back through the very same code; this is the guard
# on that move, and on the two staying in step afterwards. Links config.c and
# settings.c with the seven older state loaders stubbed — no compositor.
settings_test = executable('settings_test',
  # cat_draw.c comes along for cat_breed_names[]: config.c resolves `cat_breed =`
  # against the coat table's own spellings rather than keeping a second list.
  ['tests/settings_test.c', 'src/config.c', 'src/settings.c', 'src/cat_draw.c',
   'src/text.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, fontconfig, wlroots, wayland, pixman, xkbcommon],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('settings', settings_test)

# The state files a config RELOAD must not throw away. Links the REAL readers in
# filters.c and uifx.c against config.c, with an isolated XDG_CONFIG_HOME
# carrying a synuirc, a settings.state and a filters.state that deliberately
# disagree — so a load that skips the last of them cannot land on the right
# answer by accident. Guards the "Ctrl+Shift+R switched CRT effects on" bug.
state_reload_test = executable('state_reload_test',
  # saver_state.c is linked for the same reason filters.c and uifx.c are: it is
  # the REAL saver.state reader, and a stub here would let the exact regression
  # this test exists to catch (a .state file dropped by a config reload) pass.
  ['tests/state_reload_test.c', 'src/config.c', 'src/settings.c',
   'src/filters.c', 'src/uifx.c', 'src/cat_draw.c', 'src/text.c',
   'src/saver_state.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, fontconfig, wlroots, wayland, pixman, xkbcommon],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('state_reload', state_reload_test)

# The control panel's item table against the config parser. Every row that names
# a synuirc key is adjusted, persisted, reloaded from disk and reset — so a
# mistyped key or a copied offset fails here rather than as "that setting
# doesn't stick" months later. Links ctlpanel.c with config.c and settings.c.
ctlpanel_table_test = executable('ctlpanel_table_test',
  ['tests/ctlpanel_table_test.c', 'src/ctlpanel.c', 'src/config.c',
   'src/settings.c', 'src/aimodel.c', 'src/gguf.c', 'src/hit.c',
   'src/panel.c', 'src/cat_draw.c', 'src/text.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, fontconfig, wlroots, wayland, pixman, xkbcommon, libcurl],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('ctlpanel_table', ctlpanel_table_test)

# Per-glyph font fallback. Links text.c alone — it needs cairo and fontconfig
# and nothing else, which is the point of it being its own file: the fallback
# is testable without a compositor, an output or a scene graph.
#
# The load-bearing assertion is that syn_text_extents() measures the string
# syn_show_text() paints; every panel lays out by measuring first, so the two
# disagreeing is a layout bug in exactly the cases the fallback exists for.
# Coverage assertions SKIP rather than fail where the box has no font for a
# script — see the test's header.
text_fallback_test = executable('text_fallback_test',
  ['tests/text_fallback_test.c', 'src/text.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, fontconfig, wlroots, wayland, pixman],
  c_args : ['-DWLR_USE_UNSTABLE'],   # synui.h pulls wlroots headers in
  build_by_default : false)
test('text_fallback', text_fallback_test,
  args : [meson.current_build_dir() / 'text_fallback.png'])

# The emoji picker's logic, driven through emoji_key() the way input.c calls it
# — nothing can synthesise input into a headless synui. text.c comes along for
# syn_utf8_copy() (the recents file is on disk and can be edited by hand), hit.c
# for the grid geometry. Writes only into its own mkdtemp scratch home.
emoji_test = executable('emoji_test',
  ['tests/emoji_test.c', 'src/emoji.c', 'src/emoji_data.c', 'src/text.c',
   'src/hit.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, fontconfig, wlroots, wayland, pixman, xkbcommon],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('emoji', emoji_test)

# The calculator. Two halves worth pinning for different reasons: the panel is
# driven through calc_key()/calc_click() as input.c calls them (nothing can
# synthesise input into a headless synui), and the EVALUATOR is the half that
# can be quietly wrong — a parser that gets -2^2 or 2^3^2 the wrong way round
# answers confidently and incorrectly, and nobody checks a calculator by hand.
# hit.c comes along for the keypad geometry; libm because the evaluator is
# mostly libm. Writes only into its own mkdtemp scratch home.
calc_test = executable('calc_test',
  ['tests/calc_test.c', 'src/calc.c', 'src/calceval.c', 'src/hit.c', 'src/panel.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, fontconfig, wlroots, wayland, pixman, xkbcommon, libm],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('calc', calc_test)

# …and the PROGRAM around it: arguments, stdin, and the exit status a script
# depends on. calc_test cannot see any of that.
test('syncalc', find_program('tests/syncalc.sh'), args : [syn_calc])

# The clipboard history CAPTURES. Not the panel, not the keys — that a copy
# reaches items[0] at all, which is what pkgrel 311 shipped not doing while
# looking entirely healthy from outside (the selection is wlroots' job and went
# on working, so only the panel was ever empty).
#
# Drives the real wl_event_loop against a real pipe, because the bug WAS the
# event loop: a client writes and closes together, so READABLE and HANGUP land
# in one event, and the old code returned on HANGUP without reading. A test that
# stubbed the transfer would have passed against the broken code.
#
# hit.c for the panel's row geometry, as in calc_test. No display, no backend —
# wlr_seat_create needs neither.
clip_test = executable('clip_test',
  ['tests/clip_test.c', 'src/clipboard.c', 'src/hit.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, fontconfig, wlroots, wayland, pixman, xkbcommon],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('clip', clip_test)

# The cropper's geometry and its output rules. Two things here can destroy
# something — the image/screen coordinate mapping (get it wrong and the tool
# writes a different rectangle from the one you dragged, silently) and the
# never-overwrite rule (cropping has no undo). text.c comes along for
# syn_utf8_copy(); the compositor half is stubbed, as in hit_test.
# hit.c since the recent-images list is a rows panel and hit-tests like every
# other one. Real rather than stubbed: it is pure arithmetic over a struct with
# no compositor behind it (that is what hit_test asserts), so linking it keeps
# the picker's click path honest instead of testing a stub of it.
crop_test = executable('crop_test',
  ['tests/crop_test.c', 'src/crop.c', 'src/text.c', 'src/hit.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, fontconfig, wlroots, wayland, pixman, xkbcommon],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('crop', crop_test)

# Panel legibility, against every shipped theme. Two claims that pull opposite
# ways: on a PALE surface every accent and status colour clears 4.5:1, and on a
# DARK one nothing moves at all — several rice reds are already under target on
# their own backgrounds because that is the palette their author shipped, and
# "correcting" those would repaint seven working themes to fix three. Pure
# arithmetic, so it links contrast.c alone and needs no compositor.
# The cairo half of the corner radius: it rounds by r²(4 - π), r <= 0 and NaN
# degenerate to exactly cairo_rectangle(), and a radius past half the shorter
# side clamps to the capsule instead of turning the arcs inside out. Pure cairo,
# so it links cairo_shapes.c alone and needs no compositor — which is the reason
# that function is its own file. The WIRING (the two right-click menus passing
# chrome_corner_radius() in) needs a pointer to open a menu with and so cannot be
# tested here; see the file's header.
rounded_rect_test = executable('rounded_rect_test',
  ['tests/rounded_rect_test.c', 'src/cairo_shapes.c'],
  include_directories : include_directories('src'),
  dependencies : [cairo, cc.find_library('m', required : false)],
  build_by_default : false)
test('rounded_rect', rounded_rect_test)

panel_contrast_test = executable('panel_contrast_test',
  ['tests/panel_contrast_test.c', 'src/contrast.c'],
  include_directories : include_directories('src'),
  dependencies : [cc.find_library('m', required : false)],
  build_by_default : false)
test('panel_contrast', panel_contrast_test)

# The other half of contrast.c, and the one with a failure mode a screenshot
# cannot show: a bar with NO background of its own (macOS 26) is drawn on the
# wallpaper, so its ink is chosen from the wallpaper — and there is a band of
# mid-tone backdrops where neither black nor white clears AA and the honest
# answer is to keep the background. Same arithmetic-only link as above.
bar_ink_test = executable('bar_ink_test',
  ['tests/bar_ink_test.c', 'src/contrast.c'],
  include_directories : include_directories('src'),
  dependencies : [cc.find_library('m', required : false)],
  build_by_default : false)
test('bar_ink', bar_ink_test)

# The panel pointer contract, on a real panel. There is no way to synthesise a
# click into a headless synui — no virtual-pointer protocol, no input devices on
# the headless backend, and uinput would be picked up by the LIVE session — so
# the handlers are called directly, exactly as input.c's chain calls them.
# Stubs are lid_test's; see that file's header for why power.c links alone.
# The shortcut palette (Super+/). Driven by keysym, like ctlpanel_choice_test
# and for the same reason: nothing can synthesise input into a headless synui.
# config.c is NOT linked — the bind table is seeded by hand, so "the palette
# lists whatever is in the table" is a real assertion rather than a restatement
# of the defaults.
# The mission-control grid. Links overview.c alone — the layout is pure
# arithmetic over an output box, and it is what both the renderer and the hit
# test read, so "what you click is what you see" rests entirely on it.
overview_layout_test = executable('overview_layout_test',
  ['tests/overview_layout_test.c', 'src/overview.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, wlroots, wayland, pixman, xkbcommon],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('overview_layout', overview_layout_test)

keys_test = executable('keys_test',
  ['tests/keys_test.c', 'src/keys.c', 'src/ctlpanel.c', 'src/aimodel.c',
   'src/gguf.c', 'src/hit.c', 'src/panel.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, wlroots, wayland, pixman, xkbcommon, libcurl],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('keys', keys_test)

panel_pointer_test = executable('panel_pointer_test',
  ['tests/panel_pointer_test.c', 'src/power.c', 'src/hit.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, wlroots, wayland, pixman],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('panel_pointer', panel_pointer_test)

# The same contract's windowed clause: the wheel over a windowed panel is the
# panel's, and the wheel anywhere else is the client's. Leaving windowed panels
# out of panel_pointer_active() took the wheel away from them entirely, so they
# did not scroll at all in window mode. calc.c is the cheapest of the three
# panels that HAS a mode; the guard is the same in all three.
panel_wheel_test = executable('panel_wheel_test',
  ['tests/panel_wheel_test.c', 'src/calc.c', 'src/panel.c', 'src/hit.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, wlroots, wayland, pixman, xkbcommon],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('panel_wheel', panel_wheel_test)

# The control panel's CHOICE row (System ▸ AI model). Driven by calling
# ctlpanel_key() with keysyms for the same reason panel_pointer_test calls the
# pointer handlers: nothing can synthesise input into a headless synui. What it
# pins is mostly the NOT-loading — a row whose values are multi-gigabyte models
# must not act on a keypress you were only passing through.
# AIMODEL_DIR is redirected at a directory of stub files the test writes, the
# way lid_test redirects SYNUI_POWER_SUPPLY_DIR.
ctlpanel_choice_test = executable('ctlpanel_choice_test',
  ['tests/ctlpanel_choice_test.c', 'src/panel.c', 'src/ctlpanel.c', 'src/aimodel.c', 'src/gguf.c',
   'src/hit.c'],
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
  ['tests/aimodel_catalog_test.c', 'src/aimodel.c', 'src/gguf.c', 'src/hit.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, wlroots, wayland, pixman, xkbcommon, libcurl],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('aimodel_catalog', aimodel_catalog_test)

# The GGUF header reader, against a file the test writes. It links gguf.c ALONE
# — no wlroots, no cairo, no compositor state — which is the property that lets
# it also be pointed at a real multi-GB model on the box:
#   ./build/gguf_test /var/lib/synapd/models/synapse.gguf
# See the test's header for the three header shapes it pins.
gguf_test = executable('gguf_test',
  ['tests/gguf_test.c', 'src/gguf.c'],
  include_directories : include_directories('src'),
  build_by_default : false)
test('gguf', gguf_test)

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

# The niri (scrollable tiling) strip: columns that do not shrink when another
# window opens, a strip that scrolls to whatever has the focus, and the two
# column moves (Super+, / Super+.). Three windows on one headless output, so
# the strip is deliberately longer than the monitor. Same client again.
# Every default bind has a row in the README's hotkey table. Pure text — no
# compositor, no output, no client — so it runs anywhere and costs nothing.
# It exists because the table went stale in a way only a human reading two
# files side by side could see: Super+Space moved to rofi and the command bar
# moved to Super+=, and the README described neither.
readme_binds = find_program('tests/readme_binds.sh')
test('readme_binds', readme_binds,
     args    : [meson.current_source_dir() / 'src/config.c',
                meson.current_source_dir() / '../README.md'],
     timeout : 30)

# The start menu's Settings page against the control panel's categories. Same
# shape as readme_binds above and there for the same reason: a hand-written QML
# list of things C defines, with nothing keeping the two in step. It had already
# drifted — Windows and Input were categories with no row, so they could not be
# reached from the menu at all — and it drifts SILENTLY, because an arg that
# matches no category makes ctlpanel_show_cat fall back to the plain front door
# instead of failing. Source-only: no compositor, no QML engine, runs anywhere.
menu_cats = find_program('tests/menu_cats.sh')
test('menu_cats', menu_cats,
     args    : [meson.current_source_dir() / 'src/ctlpanel.c',
                meson.current_source_dir() / 'quickshell/StartMenu.qml'],
     timeout : 30)

# What synui-record hands wf-recorder. Source-only: recording needs a seat and
# an output, so this asserts the command line rather than the capture. The
# constant-rate flag in particular has no visible symptom when it goes missing
# — the recording still works, it just becomes unusable to every editor.
# Game mode borrows the machine from the desktop; this asserts it gives every
# piece back, including on the compositor-shutdown path where the external
# processes it stopped would otherwise stay stopped.
# "AI backend: off" has to survive a reboot, which means masking the units and
# recording the choice off the tmpfs. Runs the helper itself against a fake
# systemctl and a scratch SYNUI_AI_ROOT, so it cannot stop the daemon on the
# machine running the suite.
# The `spawn_toggle` bind action, driven with real children: the second press of
# a launcher's key has to close it, a slot whose child died has to open rather
# than signal a recycled pid, and the close has to reach the whole process group.
# Links spawntoggle.c alone — it needs no compositor, which is why it is its own
# file.
spawn_toggle_test = executable('spawn_toggle_test',
  ['tests/spawn_toggle_test.c', 'src/spawntoggle.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, fontconfig, wlroots, wayland, pixman, xkbcommon],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('spawn_toggle', spawn_toggle_test, timeout : 30)

ai_backend_off = find_program('tests/ai_backend_off.sh')
test('ai_backend_off', ai_backend_off,
     args    : [meson.current_source_dir() / 'systemd/synui-ai-backend.sh'],
     timeout : 30)

game_actions = find_program('tests/game_actions.sh')
test('game_actions', game_actions,
     args    : [meson.current_source_dir() / 'src/game.c',
                meson.current_source_dir() / 'src/config.c',
                meson.current_source_dir() / 'src/ctlpanel.c',
                meson.current_source_dir() / 'src/layout.c'],
     timeout : 30)

record_args = find_program('tests/record_args.sh')
test('record_args', record_args,
     args    : [meson.current_source_dir() / 'systemd/synui-record.sh',
                meson.current_source_dir() / 'systemd/synui-record-status.sh',
                meson.current_source_dir() / 'src/input.c',
                meson.current_source_dir() / 'src/record.c',
                meson.current_source_dir() / 'src/config.c',
                meson.current_source_dir() / 'config/synuirc'],
     timeout : 30)

niri_strip = find_program('tests/niri_strip.sh')
test('niri_strip', niri_strip,
     args    : [synui_exe.full_path(), stubborn_client.full_path(),
                synctl_exe.full_path()],
     depends : [synui_exe, stubborn_client, synctl_exe],
     timeout : 120)

# The niri SLIDE (layout_scroll_tick): that the strip is observed part-way to
# its new scroll offset rather than teleporting, that it arrives, and that
# animation_ms = 0 still lands in one step. niri_strip.sh runs with the
# animation off precisely so it can measure geometry; this is the other half.
niri_slide = find_program('tests/niri_slide.sh')
test('niri_slide', niri_slide,
     args    : [synui_exe.full_path(), stubborn_client.full_path(),
                synctl_exe.full_path()],
     depends : [synui_exe, stubborn_client, synctl_exe],
     timeout : 120)

# The DESKTOP switch (anim.c): that a slide is observed part-way — the window is
# drawn away from its logical position — that it arrives, and that a style of
# `none` never displaces anything. The offset is only visible because
# `synctl clients` reports it; "at" deliberately does not move during a slide.
ws_slide = find_program('tests/ws_slide.sh')
test('ws_slide', ws_slide,
     args    : [synui_exe.full_path(), stubborn_client.full_path(),
                synctl_exe.full_path()],
     depends : [synui_exe, stubborn_client, synctl_exe],
     timeout : 120)

# The BAR's panels follow corner_radius, and a retro chrome squares them again.
# Reads rendered pixels (grim + PIL) rather than a property, because the whole
# bug was a setting that reported itself applied and moved nothing: it asserts
# the four corners of an open start menu carve at 14, come back square under
# `square_chrome`, and that the panel's body never repaints either way.
bar_radius = find_program('tests/bar_radius.sh')
test('bar_radius', bar_radius,
     args    : [synui_exe.full_path(), meson.current_source_dir() / 'quickshell'],
     depends : [synui_exe],
     timeout : 180)

# The other half of that rule, for the windows synui does NOT decorate. Firefox
# never binds xdg-decoration, so a Win95 desktop kept its rounded corners no
# matter what corner_radius said; the chrome style now reaches GTK, where every
# self-decorating client reads its radius. No compositor needed — the helper's
# job is a file, so the file is what is asserted.
apply_theme_square = find_program('tests/apply_theme_square_chrome.sh')
test('apply_theme_square_chrome', apply_theme_square,
     args    : [meson.current_source_dir() / 'systemd' / 'synui-apply-theme.sh'],
     timeout : 60)

# The same helper's OTHER half: whether it tells the session the desktop is
# light. It used to say `default` (= no preference) for every light theme, which
# left Firefox's System theme guessing and made 95's chrome flip to dark grey at
# random. See the header of the script for the measured portal values.
apply_theme_scheme = find_program('tests/apply_theme_color_scheme.sh')
test('apply_theme_color_scheme', apply_theme_scheme,
     args    : [meson.current_source_dir() / 'systemd' / 'synui-apply-theme.sh'],
     timeout : 60)

# And its THIRD: the bar alpha a theme brings with it, which reaches theme.json
# and deliberately not the waybar CSS — only the quickshell bar can pick an ink
# that survives having no background (backdrop.state). Also pins that a
# malformed value is refused rather than pasted into JSON, where it would take
# the bar's whole palette down rather than one colour.
apply_theme_alpha = find_program('tests/apply_theme_bar_alpha.sh')
test('apply_theme_bar_alpha', apply_theme_alpha,
     args    : [meson.current_source_dir() / 'systemd' / 'synui-apply-theme.sh'],
     timeout : 60)

# The two ways round src/contrast.c, which panel_contrast_test.c already covers.
# The calendar's day numbers were a hardcoded near-white — 1.31:1 on 95's silver,
# and a literal is not a rung, so the corrector never saw them. kitty's light
# ANSI half was measured on SOLID silver while being drawn on silver composited
# at the glass floor. Both are numbers in a file, so no compositor is needed.
theme_contrast = find_program('tests/theme_contrast.py')
test('theme_contrast', theme_contrast,
     args    : [meson.current_source_dir() / 'src' / 'render.c',
                meson.current_source_dir() / 'systemd' / 'synui-apply-theme.sh'],
     timeout : 60)

# `bar_shape`: the strip's own shape, where bar_radius covers its popups'. Four
# captures on one compositor — full-width, rounded-ends, floating-pill, and the
# pill again at corner_radius = 0, which must come back identical to the first.
# That last one is the contract the setting is sold on: the shape does nothing
# with the corners off.
bar_shape = find_program('tests/bar_shape.sh')
test('bar_shape', bar_shape,
     args    : [synui_exe.full_path(), meson.current_source_dir() / 'quickshell'],
     depends : [synui_exe],
     timeout : 180)

# What a bar with NO background is drawn on. macOS 26's menu bar is clear, so its
# ink comes from the wallpaper rather than the palette, and the compositor writes
# which ink survives to backdrop.state. bar_ink_test.c pins the arithmetic; this
# pins that the number fed to it is the strip the BAR covers — measured off the
# painted buffer, at the edge the bar is on — and not the picture as a whole.
# No bar needed: the state file is the interface.
bar_backdrop = find_program('tests/bar_backdrop.sh')
test('bar_backdrop', bar_backdrop,
     args    : [synui_exe.full_path()],
     depends : [synui_exe],
     timeout : 180)

# The fibonacci layout (layout_spiral): five windows laid out by BOTH tilers,
# asserting the spiral's smallest window beats master-stack's — the claim the
# layout exists to make — plus the no-overlap invariant its remainder
# arithmetic is the sharp end of.
spiral_layout = find_program('tests/spiral_layout.sh')
test('spiral_layout', spiral_layout,
     args    : [synui_exe.full_path(), stubborn_client.full_path(),
                synctl_exe.full_path()],
     depends : [synui_exe, stubborn_client, synctl_exe],
     timeout : 120)

# The cascade (layout_cascade): that its cards stay SMALL — a third of the
# screen wide, half of it tall — that the piles are dealt into a grid that
# reaches the far edges rather than leaving a third of the desk bare, and that
# past the grid's slots the piles deepen again instead of the cards growing.
# The first two are the 2026-08-07 screenshot; the third is the layout's point.
cascade_grid = find_program('tests/cascade_grid.sh')
test('cascade_grid', cascade_grid,
     args    : [synui_exe.full_path(), stubborn_client.full_path(),
                synctl_exe.full_path()],
     depends : [synui_exe, stubborn_client, synctl_exe],
     timeout : 120)

# Cascade's OTHER half: its arrangement IS its stacking order, so the window the
# user focused has to survive a reflow it did not ask for. velle, 2026-08-12 —
# the volume OSD mapping reflowed the desktop and re-buried the focused window
# twice per keypress.
cascade_focus_top = find_program('tests/cascade_focus_top.sh')
test('cascade_focus_top', cascade_focus_top,
     args    : [synui_exe.full_path(), stubborn_client.full_path(),
                synctl_exe.full_path()],
     depends : [synui_exe, stubborn_client, synctl_exe],
     timeout : 120)

# The same rule for the windows the cascade does NOT arrange. velle, 2026-08-12:
# double-clicking a titlebar maximized Firefox and put the other two windows in
# front of it — a maximized window is floating by construction, so the cascade
# placed nothing for it and then raised every card it did place over the top.
cascade_maximize_top = find_program('tests/cascade_maximize_top.sh')
test('cascade_maximize_top', cascade_maximize_top,
     args    : [synui_exe.full_path(), stubborn_client.full_path(),
                synctl_exe.full_path()],
     depends : [synui_exe, stubborn_client, synctl_exe],
     timeout : 120)

# The floating desktop's own tiler (layout_float_arrange): that untouched
# windows are arranged into an inset grid, that the grid leaves the wallpaper
# showing rather than filling the screen, that a window the user has placed is
# left alone forever after, and that Super+Shift+G takes it back.
float_arrange = find_program('tests/float_arrange.sh')
test('float_arrange', float_arrange,
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

notif_sh = find_program('tests/notif.sh')
test('notif', notif_sh,
     args    : [synui_exe.full_path()],
     depends : [synui_exe],
     timeout : 120)

dnd_sh = find_program('tests/dnd.sh')
test('dnd', dnd_sh,
     args    : [synui_exe.full_path(), synctl_exe.full_path()],
     depends : [synui_exe, synctl_exe],
     timeout : 120)

overview_sh = find_program('tests/overview.sh')
test('overview', overview_sh,
     args    : [synui_exe.full_path(), stubborn_client.full_path(),
                synctl_exe.full_path()],
     depends : [synui_exe, stubborn_client, synctl_exe],
     timeout : 120)

alttab_scope = find_program('tests/alttab_scope.sh')
test('alttab_scope', alttab_scope,
     args    : [synui_exe.full_path(), stubborn_client.full_path(),
                synctl_exe.full_path()],
     depends : [synui_exe, stubborn_client, synctl_exe],
     timeout : 120)

# The Super+Z screensaver panel answers the keyboard. It shipped drawing a
# footer that advertised five keys and wired none of them: saver_key() existed,
# complete, and input.c's hand-kept key chain never called it, so the panel took
# the mouse (it IS in SYN_PANEL_LIST) and ignored every key it told you about.
# Nothing in a build sees an uncalled non-static function, so the keys have to
# be pressed. wtype-driven; SKIPs (77) without it.
saver_keys = find_program('tests/saver_keys.sh')
test('saver_keys', saver_keys,
     args    : [synui_exe.full_path()],
     depends : [synui_exe],
     timeout : 120)

# A synui with no seat must not push the theme to the desktop's apps.
# synui-apply-theme writes $HOME and gsettings literally, so every headless
# instance re-themed the session it was started from — and this suite starts
# about twenty of them, which is how running the tests turned velle's Firefox
# chrome dark grey. The rig stubs the helpers, so it is also the seatbelt.
theme_seat_guard = find_program('tests/theme_seat_guard.sh')
test('theme_seat_guard', theme_seat_guard,
     args    : [synui_exe.full_path()],
     depends : [synui_exe],
     timeout : 60)
