project('synui', 'c',
  version : '0.1.0',
  default_options : ['warning_level=1', 'c_std=gnu17'])

# The compositor's own words, in the user's language. libintl is part of glibc,
# so this adds nothing to the ISO; `gettext` is a makedepends, for msgfmt.
i18n = import('i18n')

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
# Shaping and bidi, also text.c. Choosing a face is not enough for Arabic or
# Devanagari — the letter forms depend on their neighbours and the reading order
# is not the storage order — and po/ has carried ar and hi catalogs since 575.
# HarfBuzz joins and reorders, FriBidi resolves the embedding levels.
#
# ⚠ NEITHER ADDS A PACKAGE TO THE ISO. harfbuzz is already pulled in by
# librsvg, which is a synui dependency two lines down, and fribidi by pango and
# gtk3/gtk4, which every GTK application on this desktop needs. Both are also
# listed in the PKGBUILD's depends= rather than left implicit, because "some
# other package happens to want it" is not a dependency declaration.
harfbuzz  = dependency('harfbuzz')
fribidi   = dependency('fribidi')
rsvg      = dependency('librsvg-2.0')  # icons.c rasterizes SVG-only app icons
libinput  = dependency('libinput')
# sound.c watches udev so a device being plugged in can make a noise. Already
# installed as a wlroots dependency, so this adds no package to the ISO.
libudev   = dependency('libudev')
# screensaver.c: sd-bus, to own org.freedesktop.ScreenSaver; mpris.c is the
# other side of the same library — a CLIENT of the players on the session bus,
# so the lock and login screens can say what is playing and stop it.
libsystemd = dependency('libsystemd')
libjpeg   = dependency('libjpeg')  # provided by libjpeg-turbo; wallpaper.c JPEG decode
libcurl   = dependency('libcurl')  # news.c fetches the feeds, weather.c the
                                  # forecast — both on their own threads, so a
                                  # slow host never stalls the event loop
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

# The client side of the same protocol, for tests/wp_live_gl_client.c — the
# stand-in live wallpaper that paints with the GPU. wlroots gives the compositor
# the server half only.
layer_shell_client_h = custom_target('wlr-layer-shell-unstable-v1-client-protocol.h',
  output  : 'wlr-layer-shell-unstable-v1-client-protocol.h',
  input   : layer_shell_xml,
  command : [wl_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

# The CLIENT side of wlr-virtual-pointer, for tests/vpointer_click.c — the only
# way to send synui a real pointer double-click, which is what the edge-expand
# gesture is made of. synui itself uses the server half straight out of wlroots
# (input.c), so only the client header is generated here. XML vendored for the
# same reason the layer-shell one is; syn-arcade vendors its own copy and says
# why in its meson.build.
virtual_pointer_xml = join_paths(meson.project_source_root(),
  'protocols/wlr-virtual-pointer-unstable-v1.xml')

virtual_pointer_client_h = custom_target('wlr-virtual-pointer-unstable-v1-client-protocol.h',
  output  : 'wlr-virtual-pointer-unstable-v1-client-protocol.h',
  input   : virtual_pointer_xml,
  command : [wl_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

virtual_pointer_c = custom_target('wlr-virtual-pointer-unstable-v1-protocol.c',
  output  : 'wlr-virtual-pointer-unstable-v1-protocol.c',
  input   : virtual_pointer_xml,
  command : [wl_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

# …and linux-dmabuf, for that client's `--linear` mode: the one way to hand the
# compositor a buffer it imports as GL_TEXTURE_EXTERNAL_OES on purpose.
linux_dmabuf_client_xml = join_paths(
  wayland_protos.get_variable('pkgdatadir'),
  'stable/linux-dmabuf/linux-dmabuf-v1.xml')

linux_dmabuf_client_h = custom_target('linux-dmabuf-v1-client-protocol.h',
  output  : 'linux-dmabuf-v1-client-protocol.h',
  input   : linux_dmabuf_client_xml,
  command : [wl_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

linux_dmabuf_client_c = custom_target('linux-dmabuf-v1-protocol.c',
  output  : 'linux-dmabuf-v1-protocol.c',
  input   : linux_dmabuf_client_xml,
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
wayland_egl    = dependency('wayland-egl')   # tests/wp_live_gl_client.c only
gbm_dep        = dependency('gbm')           # ditto: its --linear DMA-BUF mode
libdrm_dep     = dependency('libdrm')        # ditto: drm_fourcc.h

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
  'src/hdrprobe.c',
  'src/hdr.c',
    'src/edid.c',
    'src/power.c',
    'src/game.c',
    'src/filters.c',
    'src/aimodel.c',
    'src/gguf.c',
    'src/uifx.c',
  'src/widgets.c',
  'src/sound.c',
    'src/calevents.c',
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
    'src/greeterbg.c',
    'src/mpris.c',
    'src/weather.c',
    'src/kbdlayout.c',
    'src/ai_interface.c',
    'src/secfeed.c',
    'src/synapd_mon.c',
    'src/config.c',
    'src/settings.c',
    'src/render.c',
    'src/cairo_shapes.c',
    'src/i18n.c',
    'src/text.c',
    'src/contrast.c',
    'src/effects.c',
    'src/cube.c',
    'src/wallpaper.c',
    'src/barscan.c',
  'src/palette.c',
    'src/matrix.c',
    'src/imgdec.c',
    'src/wppick.c',
    'src/wpthumb.c',
    'src/cursor.c',
    'src/fontpick.c',
    'src/appgrid.c',
    'src/emoji.c',
    'src/emoji_data.c',
    'src/calc.c',
    'src/calceval.c',
    'src/panel.c',
    'src/eq.c',
    'src/crop.c',
    'src/icons.c',
    'src/iconhue.c',
    'src/recent.c',
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
                  harfbuzz, fribidi,
                  libinput, libudev, libjpeg, giflib, libcurl, libdisplay_info,
                  xcb, xcb_icccm, xcb_ewmh, xcb_randr,
                  egl, glesv2, libsystemd, libm],
  c_args : ['-DWLR_USE_UNSTABLE',
            '-DSYNUI_DATADIR="' + get_option('prefix') / get_option('datadir') / 'synui' + '"',
            # Where bindtextdomain() looks. Composed from the same options as
            # DATADIR above so a --prefix build finds its own catalogs rather
            # than the system's — which is what makes a test build testable.
            '-DSYNUI_LOCALEDIR="' + get_option('prefix') / get_option('localedir') + '"'],
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
install_data('data/logo-ink.svg',     install_dir : synui_datadir)
install_data('data/logo-bold-ink.svg', install_dir : synui_datadir)

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
# The compositor's own panels, as menu entries.
#
# Reported as "no control panel access under KDE or GNOME" — which was true in
# both directions. Under synui the panels existed only as keybinds, so nothing
# in a searchable menu led to them; under KDE or GNOME the one entry that DID
# exist (the calculator) was a dead button, because `synctl dispatch` needs a
# synui listening on the other end.
#
# Every one of these carries OnlyShowIn=synui;SynapseOS; — see the note in the
# files. They make the panels findable by name and by keyword inside synui, and
# they do not appear at all on a desktop that cannot open them.
#
# ⚠ AI IS NOT IN THIS LIST BY DESIGN. `aimodel` is the model chooser, which is
# a synui panel like the rest — but managing the BACKEND (off/gpu/cpu, and the
# units that restart it) has to work where synui is not running at all. That
# lives in syn-settings' AI pane, which is a normal program and shows up
# everywhere.
install_data(['data/synui-calc.desktop',
              'data/synui-ctlpanel.desktop',
              'data/synui-theme.desktop',
              'data/synui-taskmgr.desktop',
              'data/synui-sound.desktop',
              'data/synui-sounds.desktop',
              'data/synui-keys.desktop',
              # ⚠ THE ODD ONE OUT: `Exec=synui-welcome toggle`, a program,
              # where every other entry here dispatches a compositor panel. The
              # welcome guide stopped being a panel at 497 — it is a second
              # quickshell on the bar's QML tree — so there is no action for
              # `synctl dispatch` to name. It is in this list for the reason all
              # of them are: a thing reachable only by a chord is a thing nobody
              # finds, and Super+Escape is not a discoverable key.
              'data/synui-welcome.desktop',
              'data/synui-widgets.desktop',
              'data/synui-aimodel.desktop',
              'data/synui-filters.desktop',
              'data/synui-clipboard.desktop',
              # The image viewer and the cropper — two faces of one panel
              # (src/crop.c), and two entries because they are two things to
              # ask for. The viewer also DECLARES the image MIME types, which
              # is what makes it an Open With candidate and what synfiles'
              # vendor mimeapps.list points the system default at; before it
              # there was no image viewer on SynapseOS at all and the system
              # answer to "open this PNG" was a browser.
              'data/synui-view.desktop',
              'data/synui-crop.desktop',
              # ⛔ A PANEL REACHABLE ONLY BY A CHORD IS NOT A FEATURE ANYBODY
              # CAN FIND. Eleven entries went in at 392 and the rest of the
              # panels stayed keybind-only, which is the same bug in a smaller
              # font: news was Super+R and nothing else, so the only way to
              # learn it exists was to read config.c or the shortcut palette —
              # and nobody opens a shortcut palette to discover an app they do
              # not know about. Every panel a person would call an APP now has
              # an entry; tests/menu_apps.sh fails the build when a new one
              # does not.
              'data/synui-news.desktop',
              'data/synui-apps.desktop',
              'data/synui-emoji.desktop',
              'data/synui-wallpaper.desktop',
              'data/synui-cursor.desktop',
              'data/synui-displays.desktop',
              'data/synui-saver.desktop',
              'data/synui-overview.desktop',
              'data/synui-equalizer.desktop',
              'data/synui-bluetooth.desktop',
              'data/synui-power.desktop',
              'data/synui-record.desktop',
              'data/synui-screenshot.desktop',
              'data/synui-network.desktop',
              'data/synui-printers.desktop'],
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
# ⚠ NOT a theme. It is a wallpaper, and on Prism that is the whole of it:
# `wallpaper_accent = auto` is already ON for Prism, so the desktop takes its
# accent off whatever picture is up. Picking this one IS the "Hannah Montana
# theme" — Prism, wearing it, re-tinted from it, live.
#
# A SYN_THEME_ entry would have been a byte-for-byte copy of Prism's palette
# with a different name: a second table to keep in step, for no colour of its
# own. Platinum and XP make the same call in theme.c — each names its signature
# desktop in a comment and leaves it to Super+W, because a wallpaper is not
# chrome.
install_data('data/synapse-hannah-montana.png',
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
  dependencies : [scenefx, cairo, fontconfig, harfbuzz, fribidi, wlroots, wayland, pixman],
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

# The dock's four switches — magnify, clock, on-top, and the right-click menu
# that carries all of them plus auto-hide. Same stub rig as the reorder test with
# ONE difference that is the reason it is a second binary: create_cairo_buf()
# hands back a REAL cairo context on a throwaway image surface, because the
# numbers under test (the buffer size, the tree's position) are written on the
# far side of the early return a NULL context takes. Nothing here resolves a
# config path, so no dock.state can be reached.
dock_options_test = executable('dock_options_test',
  ['tests/dock_options_test.c', 'src/dock.c', 'src/contrast.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, wlroots, wayland, pixman],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('dock_options', dock_options_test)

# Does the clock FIT the cell the layout gives it? A separate rig from the one
# above because that one stubs syn_text_extents() to zero on purpose — which is
# right for asserting the layout's floor and is exactly why a clock that ran off
# both edges of a vertical dock never failed a test. This one draws with cairo's
# real text and reads the pixels back. Same stub set otherwise.
dock_clock_fit_test = executable('dock_clock_fit_test',
  ['tests/dock_clock_fit_test.c', 'src/dock.c', 'src/contrast.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, wlroots, wayland, pixman],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('dock_clock_fit', dock_clock_fit_test)

# ⛔ IS THE FINGERPRINT PROMPT ON THE LOGIN SCREEN'S PIXELS? Six releases chased
# a reader that was working: lock_draw_core() returns early on the greeter's
# two-field block, and the fingerprint row was written after that return, so the
# words arrived, parsed, were copied into nlock.fp_msg — and only ever appeared
# on the session lock. Every test watching that path was a grep, and a grep can
# see that lock.c draws fp_msg but not whether the greeter ever reaches it. This
# one renders the real panel through lock_render() and reads the band back.
lock_fp_row_test = executable('lock_fp_row_test',
  ['tests/lock_fp_row_test.c', 'src/lock.c', 'src/contrast.c', 'src/cairo_shapes.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, wlroots, wayland, pixman],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('lock_fp_row', lock_fp_row_test)

# The clock cell's own drag: it can be picked up and carried into any gap in the
# icon row (dock_clock_slot, persisted as clock_slot in dock.state). Same stub
# rig as the reorder test — no pointer exists headless, so dock.c is driven
# through the exact entry points input.c calls. The cell is LOCATED by sweeping
# dock_clock_at() rather than by re-deriving its rect, so a bug in the layout
# walk cannot be copied into the test that is supposed to catch it.
dock_clock_drag_test = executable('dock_clock_drag_test',
  ['tests/dock_clock_drag_test.c', 'src/dock.c', 'src/contrast.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, wlroots, wayland, pixman],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('dock_clock_drag', dock_clock_drag_test)

# The clock, the all-apps button and the power button as ONE model: three cells
# parked in a gap of the icon row, each draggable and each with a position
# setting that writes the very field the drag commits to. The assertion that
# matters most is the dullest — a stock dock, with all three defaulting to the
# end, must lay out exactly as it did when the tail was hard-coded, and nothing
# in a build sees it move.
dock_cell_slots_test = executable('dock_cell_slots_test',
  ['tests/dock_cell_slots_test.c', 'src/dock.c', 'src/contrast.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, wlroots, wayland, pixman],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('dock_cell_slots', dock_cell_slots_test)

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
# A rect's clipped_region is a RENDER-time cutout, and barscan.c's walk had no
# way to know it: deco.c's border is one rect the size of the whole frame with
# the content clipped out, so it answered for every point inside every window
# and the bar inked itself off its own chrome. Pure geometry, same as hit_test —
# synui.h carries the function, this says it still agrees with the paint.
clipbox_test = executable('clipbox_test',
  ['tests/clipbox_test.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, wlroots, wayland, pixman],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('clipbox', clipbox_test)

# A confined pointer stays in the surface AND can reach its edges. Both are
# one wlroots contract: wlr_region_confine() confines a MOVEMENT, so a pointer
# already outside the region has no ray to clip and the call answers false.
# Passing the delta through on that answer is a confinement that does nothing
# in the only case it exists for. Pure arithmetic over a struct constraint and
# a pixman region — no compositor, same as hit_test.
pointer_confine_test = executable('pointer_confine_test',
  ['tests/pointer_confine_test.c', 'src/constraints.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, wlroots, wayland, pixman],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('pointer_confine', pointer_confine_test)

# Game mode holds the pointer on the game's screen when the CLIENT never asks
# for a constraint — which, measured, is every game so far. Drives the real
# gate and clamp out of game.c against stubs; no compositor.
game_confine_test = executable('game_confine_test',
  ['tests/game_confine_test.c', 'src/game.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, wlroots, wayland, pixman],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('game_confine', game_confine_test)

# barscan reads the right BYTE for each channel. A wrong index reports a wrong
# COLOUR rather than failing, and an unhandled format declines silently into
# the wallpaper fallback — which is how BGR888 went unnoticed on a whole
# monitor. Pure arithmetic over drm_fourcc's own bit layout; no compositor.
# ⛔ THE PARSER THIS BOX CANNOT OTHERWISE EXERCISE. The VRAM column reads DRM
# fdinfo, and this machine's NVIDIA driver publishes none of those keys — so
# without fixtures the parser would ship having never seen its own input.
gpu_fdinfo_test = executable('gpu_fdinfo_test',
  ['tests/gpu_fdinfo_test.c', 'src/gpu.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, wlroots, wayland, pixman],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('gpu_fdinfo', gpu_fdinfo_test)

barscan_format_test = executable('barscan_format_test',
  ['tests/barscan_format_test.c', 'src/barscan.c', 'src/contrast.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, wlroots, wayland, pixman],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('barscan_format', barscan_format_test)

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
  dependencies : [scenefx, cairo, fontconfig, harfbuzz, fribidi, wlroots, wayland, pixman, xkbcommon],
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
  dependencies : [scenefx, cairo, fontconfig, harfbuzz, fribidi, wlroots, wayland, pixman, xkbcommon],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('state_reload', state_reload_test)

# The desktop's recently-opened list. recent.c is linked rather than
# reimplemented for the same reason saver_state.c is above: the format IS the
# thing under test, and a test that wrote its own copy of it would pass while
# the compositor read something else. config.c is NOT linked — it drags in a
# dozen unrelated state readers — so the test stubs the two path helpers and
# says so; the real resolution is covered by syn-arcade's bigscreen_rig.sh.
recent_apps_test = executable('recent_apps_test',
  ['tests/recent_apps_test.c', 'src/recent.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, fontconfig, wlroots, wayland, pixman, xkbcommon],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('recent_apps', recent_apps_test)

# One slider for the whole desktop's glass, and the rows allowed to walk away
# from it. config.c's resolution against the pin set — no compositor and no
# panel, because the decision is syn_glass_drives() and that is where it lives.
# Guards the pair of bugs the sync was written for: a Glass row that moved three
# of five surfaces and could be overruled on none of them, and an Auto that
# stopped writing the alphas without giving them back.
# Only config.c and settings.c for real. The state-file readers config.c calls
# on the way past are stubbed in the test: what is under test is the RESOLUTION
# against the pin set, and state_reload_test is what links those readers for
# real to prove a load does not drop them.
glass_sync_test = executable('glass_sync_test',
  ['tests/glass_sync_test.c', 'src/config.c', 'src/settings.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, fontconfig, wlroots, wayland, pixman, xkbcommon],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('glass_sync', glass_sync_test)

# The control panel's item table against the config parser. Every row that names
# a synuirc key is adjusted, persisted, reloaded from disk and reset — so a
# mistyped key or a copied offset fails here rather than as "that setting
# doesn't stick" months later. Links ctlpanel.c with config.c and settings.c.
ctlpanel_table_test = executable('ctlpanel_table_test',
  ['tests/ctlpanel_table_test.c', 'src/ctlpanel.c', 'src/config.c',
   'src/settings.c', 'src/aimodel.c', 'src/gguf.c', 'src/hit.c',
   'src/panel.c', 'src/cat_draw.c', 'src/text.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, fontconfig, harfbuzz, fribidi, wlroots, wayland, pixman, xkbcommon, libcurl],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('ctlpanel_table', ctlpanel_table_test)

# How see-through synui's own thirty panels are on a glass theme. The case it
# exists for is glass_level UNSET: only a fresh install writes that key, so a
# machine that reached Prism through the theme manager had none, and keying the
# panels off the level alone left the house glass theme drawing solid slabs.
# Pure functions of a syn_config_t, so it links nothing.
panel_glass_test = executable('panel_glass_test',
  ['tests/panel_glass_test.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, fontconfig, wlroots, wayland, pixman, xkbcommon],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('panel_glass', panel_glass_test)

# What Super+Return actually runs. The fallback chain used to be
# `syntty || kitty || …`, and `||` fires on a NON-ZERO exit — which for a
# terminal is the exit status of the shell inside it, so closing the window
# opened another terminal. This links nothing: synui_terminal_cmd() is a policy
# inline in synui.h, and the test executes the string it returns against stub
# terminals on PATH, because the bug was in what /bin/sh does with the operators
# rather than in the words.
terminal_chain_test = executable('terminal_chain_test',
  ['tests/terminal_chain_test.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, fontconfig, wlroots, wayland, pixman, xkbcommon, libcurl],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('terminal_chain', terminal_chain_test)

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
  dependencies : [scenefx, cairo, fontconfig, harfbuzz, fribidi, wlroots, wayland, pixman],
  c_args : ['-DWLR_USE_UNSTABLE'],   # synui.h pulls wlroots headers in
  build_by_default : false)
test('text_fallback', text_fallback_test,
  args : [meson.current_build_dir() / 'text_fallback.png'])

# Shaping and bidi. A separate binary from text_fallback because it asks the
# question AFTER that one: not "did the character reach a font" but "is what
# came out the right shape, in the right order".
#
# ⚠ ITS ASSERTIONS ARE RELATIONS BETWEEN TWO DRAWS, never glyph indices — an
# index is a property of whichever font is installed, and this has to pass on a
# CI container as well as on a desktop. Where a script has no font at all the
# cases SKIP; read the report rather than the exit status if you want to know
# what was actually exercised. libm because the checks compare distances.
text_shape_test = executable('text_shape_test',
  ['tests/text_shape_test.c', 'src/text.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, fontconfig, harfbuzz, fribidi,
                  wlroots, wayland, pixman, libm],
  c_args : ['-DWLR_USE_UNSTABLE'],   # synui.h pulls wlroots headers in
  build_by_default : false)
test('text_shape', text_shape_test,
  args : [meson.current_build_dir() / 'text_shape.png'])

# The application grid's scan and filters, driven against a mkdtemp sandbox with
# XDG_DATA_HOME and XDG_DATA_DIRS pointed at it — so the counts do not depend on
# what the machine running the test happens to have installed. hit.c comes along
# for the grid geometry the pointer entry points read.
# Matching a .desktop file's OWN translations — `Name[de]`, `Name[pt_BR]`,
# `Name[sr@latin]` — against the locale gettext is already answering in. A pure
# function taking the locale as an argument, so every case is asserted on a
# machine where none of those locales is installed.
desktop_locale_test = executable('desktop_locale_test',
  ['tests/desktop_locale_test.c', 'src/i18n.c'],
  include_directories : include_directories('src'),
  build_by_default : false)
test('desktop_locale', desktop_locale_test)

appgrid_test = executable('appgrid_test',
  ['tests/appgrid_test.c', 'src/appgrid.c', 'src/hit.c', 'src/i18n.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, fontconfig, wlroots, wayland, pixman, xkbcommon],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('appgrid', appgrid_test)

# Where icons.c looks for an icon file, driven against a mkdtemp sandbox with
# HOME, XDG_DATA_HOME and XDG_DATA_DIRS pointed into it — so the answers do not
# depend on which themes the machine running the test has installed. Every
# failure this pins is SILENT on a real desktop (a missing icon is a monogram,
# not an error), which is why it is worth a test of its own. iconhue.c and
# text.c come along because icons.c tints its own icons and draws the monogram.
icon_lookup_test = executable('icon_lookup_test',
  ['tests/icon_lookup_test.c', 'src/icons.c', 'src/iconhue.c', 'src/text.c',
   'src/i18n.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, fontconfig, harfbuzz, fribidi, wlroots, wayland, pixman,
                  xkbcommon, rsvg],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('icon_lookup', icon_lookup_test)

# The emoji picker's logic, driven through emoji_key() the way input.c calls it
# — nothing can synthesise input into a headless synui. text.c comes along for
# syn_utf8_copy() (the recents file is on disk and can be edited by hand), hit.c
# for the grid geometry. Writes only into its own mkdtemp scratch home.
emoji_test = executable('emoji_test',
  ['tests/emoji_test.c', 'src/emoji.c', 'src/emoji_data.c', 'src/text.c',
   'src/hit.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, fontconfig, harfbuzz, fribidi, wlroots, wayland, pixman, xkbcommon],
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

# The desktop's accent, on the RGB hardware. ⛔ No real device is touched: the
# test puts a stub `openrgb` on the path that writes down what it was asked
# for, because a test that changed the colour of the machine running it is not
# one anybody would run twice.
#
# ⚠ Two of its assertions are SLOW on purpose (pkgrel 522): the wait for a bus
# that is still enumerating is defined in whole seconds of real time, so there
# is nothing in them to speed up. ~9s.
test('synrgb', find_program('tests/syn_rgb.sh'),
     args : [meson.current_source_dir() / 'tools' / 'syn-rgb'],
     timeout : 60)

# And that a BURST of wallpapers does not kill the watch (pkgrel 437). A
# path-triggered service that trips systemd's default 5-in-10s start limit
# fails the PATH unit too, permanently — so clicking through a folder froze
# the lights until the next login.
#
# ⛔ Drives scratch units under $XDG_RUNTIME_DIR, never syn-rgb's own: stopping
# the real path unit would leave the machine running the test with no bridge.
# SKIPs itself where there is no user manager (a build chroot), keeping the
# static half of the assertion.
#
# ⚠ Slower than the rest because the burst has to be REAL — the limit is
# defined in wall-clock seconds, so there is nothing here to speed up. ~7s.
test('synrgbburst', find_program('tests/syn_rgb_burst.sh'),
     args : [meson.current_source_dir() / 'systemd' / 'syn-rgb.service'],
     timeout : 60)

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
  dependencies : [scenefx, cairo, fontconfig, harfbuzz, fribidi, wlroots, wayland, pixman, xkbcommon],
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

# The grid the bar's strip grew into, once every surface that is not the bar
# had to ask what it was drawn on. Links contrast.c and nothing else, for the
# same reason panel_contrast does: it is arithmetic over a grid and two
# luminances, and a test that needed a compositor to reach it would not be run.
# The colour the DOCK's own marks are drawn in — its clock, apps grid, power
# mark and running dots. They are drawn in `panel_ink`, which is the colour the
# theme picked to read on `panel_bg`; that was the same surface the mark landed
# on only for as long as every PALE preset drew a SOLID dock. Prism Light is the
# first that is pale AND glass, and at the house dock alpha its near-black ink
# was landing straight on a night photograph at 1.90:1 — reported as the clock,
# the apps button and the power icon being washed out on the light Prism and
# fine on the dark one.
#
# The claim that needs a test is not the fix, it is the OTHER half: fourteen
# presets look right today, some of them at contrasts that are low by choice,
# and a legibility rescue that repaints them is worse than the bug. Every preset
# that still reads must come back bit-for-bit. Pure arithmetic, so it links
# contrast.c alone and needs no compositor.
# Can you actually READ a panel — on every theme, over everything a panel can
# open on top of. panel_contrast_test asks the same question of the theme's OWN
# panel surface and could not have caught this: every panel is glass now, and a
# glass panel's surface is the theme's colour composited over whatever it opened
# over, which on a desktop is a window far more often than the wallpaper.
#
# Reported as the emoji picker's categories and the keyboard shortcuts panel
# being hard to read. Measured off the screenshot on stock Prism over a white
# web page, the panel composites to L=0.135 and the accent lands at 1.49:1 —
# because syn_contrast_fix() and syn_ink_floor() both open with
# `if (lum <= SURFACE_PALE) return`, and a mid-tone is neither pale nor the
# theme's own surface. Pure arithmetic; contrast.c alone.
panel_legibility_test = executable('panel_legibility_test',
  ['tests/panel_legibility_test.c', 'src/contrast.c'],
  include_directories : include_directories('src'),
  dependencies : [cc.find_library('m', required : false)],
  build_by_default : false)
test('panel_legibility', panel_legibility_test)

dock_ink_test = executable('dock_ink_test',
  ['tests/dock_ink_test.c', 'src/contrast.c'],
  include_directories : include_directories('src'),
  dependencies : [cc.find_library('m', required : false)],
  build_by_default : false)
test('dock_ink', dock_ink_test)

backdrop_test = executable('backdrop_test',
  ['tests/backdrop_test.c', 'src/contrast.c'],
  include_directories : include_directories('src'),
  dependencies : [cc.find_library('m', required : false)],
  build_by_default : false)
test('backdrop', backdrop_test)

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

# The palette SYNAPSE Prism takes off the wallpaper. Pure — pixels in, colours
# out — so it links contrast.c and nothing else, and its images are built in
# the test rather than shipped as fixtures nobody can read the intent of.
palette_test = executable('palette_test',
  ['tests/palette_test.c', 'src/palette.c', 'src/contrast.c'],
  include_directories : include_directories('src'),
  dependencies : [cc.find_library('m', required : false)],
  build_by_default : false)
test('palette', palette_test)

# The recolour that makes our own app icons follow the theme. Pure — pixels in,
# pixels out — so it links iconhue.c and nothing else, and like palette_test its
# images are built in the test rather than shipped as fixtures nobody can read
# the intent of.
iconhue_test = executable('iconhue_test',
  ['tests/iconhue_test.c', 'src/iconhue.c'],
  include_directories : include_directories('src'),
  dependencies : [cc.find_library('m', required : false)],
  build_by_default : false)
test('iconhue', iconhue_test)

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

# The [w] button in the wallpaper picker — where more wallpapers come from, and
# the compositor half of a key that flips between the picker and the browser.
# wppick.c linked alone, the way panel_pointer_test links power.c: the spawn is
# COUNTED, because the failure this shape has is a keypress that both acts and
# falls through, which toggles the browser open and shut again.
wppick_wallhaven_test = executable('wppick_wallhaven_test',
  ['tests/wppick_wallhaven_test.c', 'src/wppick.c', 'src/hit.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, wlroots, wayland, pixman, xkbcommon],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('wppick_wallhaven', wppick_wallhaven_test)

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

# Night light's LUT length. The one input that decides whether a colour
# transform is accepted at all is the CRTC's gamma size, and it is not
# reachable from a test — a headless output reports none — so
# wlr_output_get_gamma_size() is stubbed in the test executable, whose
# definition wins over the shared library's. See the test's header: a
# hard-coded 1024 matched the NVIDIA desktop and nothing else.
nightlight_test = executable('nightlight_test',
  ['tests/nightlight_test.c', 'src/nightlight.c'],
  include_directories : include_directories('src'),
  dependencies : [wlroots, wayland, pixman, libm, scenefx, xkbcommon,
                  cairo, fontconfig],
  c_args : ['-DWLR_USE_UNSTABLE'],   # synui.h pulls wlroots headers in
  build_by_default : false)
test('nightlight', nightlight_test)

# The SDR->PQ curve, the fold that keeps night light working inside it, and the
# cache both commit paths lean on. Links nightlight.c because the two share the
# output's single colour-transform slot — the fold is checked against night
# light's own answer, not against a constant. wlr_output_get_gamma_size() is
# stubbed for the same reason it is in the night light test.
hdr_test = executable('hdr_test',
  ['tests/hdr_test.c', 'src/hdr.c', 'src/nightlight.c'],
  include_directories : include_directories('src'),
  dependencies : [wlroots, wayland, pixman, libm, scenefx, xkbcommon,
                  cairo, fontconfig],
  c_args : ['-DWLR_USE_UNSTABLE'],   # synui.h pulls wlroots headers in
  build_by_default : false)
test('hdr', hdr_test)

# `synctl hdr` on a nested headless synui — the REFUSAL path, which is the one a
# real desk hits on most of its monitors. A headless output cannot be driven in
# HDR10, so every enable here must come back as an error rather than a
# successful-looking no-op, and the compositor must survive being asked.
hdr_cli = find_program('tests/hdr_cli.sh')
test('hdr_cli', hdr_cli,
     args : [synui_exe.full_path(), synctl_exe.full_path()],
     depends : [synui_exe, synctl_exe],
     timeout : 60)

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

# The pointer that sends the double-click. Down here rather than beside its
# custom_targets above because `wayland_client` is not defined until later in
# this file and meson reads it top to bottom.
vpointer_click = executable('vpointer_click',
  ['tests/vpointer_click.c', virtual_pointer_client_h, virtual_pointer_c],
  dependencies : [wayland_client],
  build_by_default : false)

stubborn_client = executable('stubborn_client',
  ['tests/stubborn_client.c', xdg_shell_client_h, xdg_shell_c],
  dependencies : [wayland_client],
  build_by_default : false)

# The other half of the bar's backdrop, and the half bar_backdrop.sh cannot
# reach: what is under the bar when it is not the wallpaper. Needs a real client
# with real pixels, because the measurement it pins is a GPU readback of one —
# so it takes the same stubborn_client the layout tests use.
#
# ⚠ meson kills a test at 30s and calls that a BUILD failure, and this one waits
# on a 400ms poll four times over. 120s is the roomy figure the other
# client-driven tests use.
bar_scene_strip = find_program('tests/bar_scene_strip.sh')
test('bar_scene_strip', bar_scene_strip,
     args    : [synui_exe.full_path(), stubborn_client.full_path(),
                synctl_exe.full_path()],
     depends : [synui_exe, stubborn_client, synctl_exe],
     timeout : 120)

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
# `edge_expand`: double-clicking a window border fills the usable box along that
# border's axis (velle, 2026-08-20). Driven two ways on purpose — the geometry
# through `synctl dispatch`, where each failure is one readable step, and then
# the gesture itself through a real pointer (tests/vpointer_click.c), because
# "the dispatch works" says nothing about whether the border ever sees a press.
# `imgview_mouse`: the image viewer is a full-screen panel with no window
# chrome, so until it grew a close X and a chevron at each side, everything it
# could do was on the keyboard — a picture opened by double-clicking a file
# could not be dismissed by the hand that opened it. Driven with a real pointer,
# because the failure being guarded against (testing the buttons AFTER the press
# has already started a pan drag) draws and highlights them perfectly and leaves
# them dead.
imgview_mouse = find_program('tests/imgview_mouse.sh')
test('imgview_mouse', imgview_mouse,
     args    : [synui_exe.full_path(), synctl_exe.full_path(),
                vpointer_click.full_path()],
     depends : [synui_exe, synctl_exe, vpointer_click],
     timeout : 120)

# `deskmenu_widgets`: the desktop right-click menu's widget-manager row. The
# row is put on screen by an n++ walk, so deleting it builds and runs clean and
# just draws a menu with nothing about widgets in it — only a pointer landing
# where the row should be can see that. Driven with a real pointer and wtype;
# the ledger is a fake synui-widgets on PATH.
# Bar plugins, in Omarchy's shell-plugin format. Their desktop is quickshell and
# so is this bar, which makes their format the only one already describing "a
# QML widget you can drop into a quickshell bar" — so synui reads it rather than
# growing a second directory layout for the same idea.
#
# ⛔ WHAT THIS GUARDS IS THE REFUSALS. An Omarchy widget that imports qs.Commons
# or Quickshell.Hyprland cannot run here — their own singletons, and a
# compositor socket synui does not have — and enabling one anyway leaves a state
# file claiming something is on with an empty space where it should be. Every
# refusal is asserted by name, including the one that must NOT fire: qs.Ui is
# provided (it is where BarWidget lives) and is asked of the filesystem rather
# than of a hardcoded list.
#
# Runs entirely inside a scratch HOME: the real plugin directories are the
# user's, and a test that could enable a plugin on the machine running it is one
# bad path away from changing the desktop it is testing.
# ⛔ AND THAT ONE RUNS NOTHING. plugins.sh is a shell script reading files: it
# proves synui will OFFER a plugin and nothing about whether one runs. qmllint
# does not close that gap either — "the types resolve" is not "it instantiates".
# plugin_load.sh starts a headless synui, starts the real quickshell tree against
# it with a plugin enabled, and reads the bar's own log. The fixture is
# OMARCHY'S OWN Spacer widget verbatim (MIT, © DHH), because a plugin written to
# pass proves the loader works and nothing about the compatibility claim.
plugin_load_sh = find_program('tests/plugin_load.sh')
test('plugin_load', plugin_load_sh,
     args    : [synui_exe.full_path(),
                meson.current_source_dir() / 'quickshell'],
     depends : [synui_exe],
     timeout : 120)

# ⛔ AND plugin_load.sh STOPS AT THE BAR WIDGET. A plugin declaring a `panel` or
# a `service` delegates its whole behaviour to an entry point the bar never
# touches, and every call into the host is guarded — so a missing member is a
# button that does nothing and logs nothing. plugin_host.sh mounts both, drives
# the panel over the `plugin` IPC handler (a headless session has nothing to
# click with) and asserts the settings write-back lands on disk and comes back
# to the widget.
# ⚠ AND THE LAUNCHER A PLUGIN CALLS BY NAME. Its name is hardcoded in somebody
# else's QML and reached through `bar.run`, which detaches — so a wrong branch,
# a dead shim or a flag syntty does not know all look identical from the widget:
# a Play button that does nothing. Stubs on PATH, because the real thing would
# open a game window on the seat running the suite.
launch_tui_sh = find_program('tests/launch_tui.sh')
test('launch_tui', launch_tui_sh,
     args    : [meson.current_source_dir() / 'systemd/omarchy-launch-or-focus-tui.sh'],
     timeout : 60)

# The other commands their QML names. Same contract, same silence: the
# weather widgets write their location through omarchy-weather-location and
# read it back out of a JSON file, and announce storms through
# omarchy-notification-send — so a shim that writes an unparseable file or
# forwards a flag notify-send rejects looks exactly like weather that is fine.
#
# ⛔ AND omarchy-shell, WHICH IS QUIETER STILL. The others are spawned as a
# Process, so Quickshell logs a missing binary. That one is reached through
# `bar.run` — `sh -c`, exit 127, nothing written anywhere — and it is how a
# plugin opens its own panel.
omarchy_shims_sh = find_program('tests/omarchy_shims.sh')
test('omarchy_shims', omarchy_shims_sh,
     args    : [meson.current_source_dir() / 'systemd/omarchy-weather-location.sh',
                meson.current_source_dir() / 'systemd/omarchy-notification-send.sh',
                meson.current_source_dir() / 'systemd/omarchy-shell.sh'],
     timeout : 60)

plugin_host_sh = find_program('tests/plugin_host.sh')
test('plugin_host', plugin_host_sh,
     args    : [synui_exe.full_path(),
                meson.current_source_dir() / 'quickshell'],
     depends : [synui_exe],
     timeout : 120)

# ⛔ AND NEITHER OF THOSE SEES WHAT A REORDER COSTS. The arrows on the bar's
# right-click menu used to spend two full scans per click — one to work out the
# current order, one to find out what the write had done — and the second one
# reloaded every plugin widget on every bar as a side effect. All of it silent,
# none of it an error. plugin_reorder.sh moves a plugin over IPC and counts what
# the bar spends doing it: one call to synui-plugins, no scan, and no widget
# rebuilt.
plugin_reorder_sh = find_program('tests/plugin_reorder.sh')
test('plugin_reorder', plugin_reorder_sh,
     args    : [synui_exe.full_path(),
                meson.current_source_dir() / 'quickshell'],
     depends : [synui_exe],
     timeout : 120)

plugins_sh = find_program('tests/plugins.sh')
test('plugins', plugins_sh,
     args    : [meson.current_source_dir() / 'systemd/synui-plugins.sh'],
     env     : ['SYNUI_BAR=' + meson.current_source_dir() / 'quickshell'],
     timeout : 60)

# The bar's own row order for plugin widgets — separate from plugins.sh's
# refusal/enable coverage the way apply_theme_dock_alpha.sh sits beside
# apply_theme_bar_alpha.sh: one file, one question, kept small enough that a
# failure names exactly what broke.
# ⛔ WHETHER MANGOHUD'S LAYER LOADS IN EVERYTHING, which is a session-wide
# decision written in three separate places — the ISO's profile.d and the two
# session paths the installer writes. They are three texts of one rule, and the
# old default (export MANGOHUD=1 for every process) segfaulted three non-games
# on AMD before it was noticed. This checks the rule AND that the three agree.
mangohud_session_sh = find_program('tests/mangohud_session.sh')
test('mangohud_session', mangohud_session_sh,
     args    : [meson.current_source_dir() / '../archiso/airootfs/etc/profile.d/synapseos.sh',
                meson.current_source_dir() / '../syn-install/syn-install.sh',
                meson.current_source_dir() / 'systemd/synui-game-run.sh'],
     timeout : 60)

plugin_order_sh = find_program('tests/plugin_order.sh')
test('plugin_order', plugin_order_sh,
     args    : [meson.current_source_dir() / 'systemd/synui-plugins.sh'],
     env     : ['SYNUI_BAR=' + meson.current_source_dir() / 'quickshell'],
     timeout : 60)

# ⛔ THE MODULE A PLUGIN IMPORTS, CHECKED AS A UNIT. plugins.sh proves the
# manager will OFFER a widget and plugin_load.sh proves one RUNS; neither says
# anything about qs.Ui / qs.Commons themselves — and a typo in Style.qml is not
# a QML compile error, it is a binding that silently evaluates to undefined for
# every widget on the desktop. It also catches the qmldir gap in both
# directions: a type written but never listed does not exist to an importer,
# however complete its file is. Skips (77) where Qt 6's qmllint is absent.
qs_module_sh = find_program('tests/qs_module.sh')
test('qs_module', qs_module_sh,
     args    : [meson.current_source_dir() / 'quickshell'],
     timeout : 60)

# `wallhaven_key`: the keys that open the wallhaven browser, PRESSED. The bug
# this closes had every layer reporting success — the bind existed, the dispatch
# was accepted, the spawn happened — and the launcher refusing onto a stderr
# nobody was reading, so the ledger here is the launcher actually being run.
wallhaven_key = find_program('tests/wallhaven_key.sh')
test('wallhaven_key', wallhaven_key,
     args    : [synui_exe.full_path(), synctl_exe.full_path()],
     depends : [synui_exe, synctl_exe],
     timeout : 120)

# `wallhaven_window`: the browser's OWN keyboard, which shipped dead in 590 —
# Keys.onPressed on a PanelWindow is delivered to nothing. Four presses against
# a real headless compositor, and the switch's promise (nothing leaves the
# machine before consent) asserted from the ledger rather than from reading.
wallhaven_window = find_program('tests/wallhaven_window.sh')
test('wallhaven_window', wallhaven_window,
     args    : [synui_exe.full_path(),
                meson.current_source_dir() / 'quickshell' / 'wallhaven.qml'],
     depends : [synui_exe],
     timeout : 300)

deskmenu_widgets = find_program('tests/deskmenu_widgets.sh')
test('deskmenu_widgets', deskmenu_widgets,
     args    : [synui_exe.full_path(), synctl_exe.full_path(),
                vpointer_click.full_path()],
     depends : [synui_exe, synctl_exe, vpointer_click],
     timeout : 120)

# `pointer_smoothing`: the filter must cost latency and NOT travel. Driven
# through the relative motion path, which is the only one it applies to.
pointer_smoothing = find_program('tests/pointer_smoothing.sh')
test('pointer_smoothing', pointer_smoothing,
     args    : [synui_exe.full_path(), synctl_exe.full_path(),
                vpointer_click.full_path()],
     depends : [synui_exe, synctl_exe, vpointer_click],
     timeout : 180)

edge_expand = find_program('tests/edge_expand.sh')
test('edge_expand', edge_expand,
     args    : [synui_exe.full_path(), stubborn_client.full_path(),
                synctl_exe.full_path(), vpointer_click.full_path()],
     depends : [synui_exe, stubborn_client, synctl_exe, vpointer_click],
     timeout : 120)

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
# The LOGIN screen shows the LOCK screen's background — one setting, across a
# permission boundary the greeter cannot cross (it runs as another account and a
# home is 0700, so the picture is copied, not named).
greeter_bg_t = find_program('tests/greeter_bg.sh')
test('greeter_bg', greeter_bg_t,
     args    : [synui_exe.full_path()],
     depends : [synui_exe],
     timeout : 120)

# The keyboard-layout selector, which exists for the LOGIN screen: a password
# typed in the wrong layout is rejected exactly like a wrong password, and
# nothing on that screen could say which had happened. The rig drives the CLI
# half (`synctl layout`) and pins the two things a selector can get wrong —
# offering a layout the KEYMAP does not have, and losing the group when no
# keyboard is on the seat, which is what a keyboard plugged in later adopts.
# ⛔ THE LOGIN SCREEN'S FINGERPRINT, WHICH THREE RELEASES "FIXED" WITHOUT IT
# EVER BEING REACHABLE. greetd owns PAM and only runs a conversation while it is
# creating a session, and this greeter did not ask for one until Enter had been
# pressed with a password already typed — so pam_fprintd in /etc/pam.d/greetd
# was armed for the seconds AFTER you finished typing, and the prompt text was
# discarded so nothing said so. Source-level, because the alternative is a real
# greetd and a real reader, and getting this wrong means a laptop nobody can log
# into.
greeter_fp_t = find_program('tests/greeter_fprint.sh')
test('greeter_fprint', greeter_fp_t, timeout : 60)

# ⛔ The LOCK's reader, which is a different helper and a different fault. The
# verdict letter is the whole retry policy and it was wrong inside a switch
# nothing reached: `waited` — did this attempt take longer than a person takes
# to swipe? — was consulted only on PAM_AUTH_ERR, while this hardware answers
# PAM_AUTHINFO_UNAVAIL after its full 30s wait. Four of those retire the reader
# for the lock, so nobody being in the room was charged to the device and every
# lock lost its fingerprint about two minutes in. Compiles fp_verdict() out of
# the real file and asks it; a grep cannot tell a conditional R from a bare one.
lock_fp_t = find_program('tests/lock_fprint_verdict.sh')
test('lock_fprint_verdict', lock_fp_t, timeout : 60)

lock_layout_t = find_program('tests/lock_layout.sh')
test('lock_layout', lock_layout_t,
     args    : [synui_exe.full_path(), synctl_exe.full_path()],
     depends : [synui_exe, synctl_exe],
     timeout : 120)

# `battery_surfaces`: the battery shows on a laptop and hides on a desktop.
# ⛔ THE DEV BOX HAS NO BATTERY, so every laptop-only path in the shell is
# unreachable on the only machine anybody looks at it from — and both surfaces
# that draw a battery hide themselves when there is none, which makes "correctly
# hidden" and "broken" identical from a chair. tests/mock_upower.py is a fake
# org.freedesktop.UPower on a PRIVATE bus; the real system bus is never touched
# and the mock refuses to start without DBUS_SYSTEM_BUS_ADDRESS. The control is
# a DESKTOP-shaped UPower with this machine's own measured numbers, not a
# missing service — otherwise it would pass on modules keyed to the service
# existing rather than to the device being a battery.
# `analog_face`: the analog clock's numerals carry the accent, and the other
# three faces' hour marks do not. Source-level, because the thing it guards is a
# choice of colour SOURCE — accentInk against the wallpaper rather than the raw
# accent — and that is visible in the file and invisible in a screenshot taken
# over a backdrop where the two happen to agree.
analog_face_t = find_program('tests/analog_face.sh')
test('analog_face', analog_face_t,
     args    : [meson.current_source_dir() / 'quickshell'],
     timeout : 60)

battery_surfaces_t = find_program('tests/battery_surfaces.sh')
test('battery_surfaces', battery_surfaces_t,
     args    : [synui_exe.full_path(),
                meson.current_source_dir() / 'quickshell',
                meson.current_source_dir() / 'tests'],
     depends : [synui_exe],
     timeout : 180)

# `bar_module_menu`: a module's right-click menu opens UNDER THE MODULE. The
# menu anchored on `root.x` — a module's position inside the bar's Row — while
# a PopupWindow anchors against the bar WINDOW, so it opened at the far left of
# the screen with every row present and working. Only WHERE was wrong, which no
# load test and no "did a menu appear" check can see.
# ⚠ Two earlier versions of this rig passed on the bug: one right-clicked the
# centred CLOCK (whose module declines the button, so the click fell through to
# the bar's own menu, which IS anchored on the pointer), the next found the
# compositor's "◢ SYNAPSE" corner, which is not a bar module and no setting
# hides. Every other module is switched off so the badge is the only one left.
bar_module_menu_t = find_program('tests/bar_module_menu.sh')
test('bar_module_menu', bar_module_menu_t,
     args    : [synui_exe.full_path(),
                meson.current_source_dir() / 'quickshell',
                vpointer_click.full_path()],
     depends : [synui_exe, vpointer_click],
     timeout : 120)

# `menu_uninstall`: right-clicking an application in the START MENU opens a
# context menu, that menu is at the POINTER, and left-click still launches.
# ⚠ THREE EARLIER VERSIONS OF THIS RIG PASSED ON AN ABSENT FEATURE, and each
# one is written down in the file: counting changed pixels passed when
# right-click merely DISMISSED the whole menu (155,000 of them); the row it
# picked was a category rather than an application, which is supposed to open
# nothing; and `vpointer_click X Y left` clicked ZERO times, because there is no
# `left` keyword — the third argument is a count and atoi("left") is 0. That one
# is now refused by the tool itself.
menu_uninstall_t = find_program('tests/menu_uninstall.sh')
test('menu_uninstall', menu_uninstall_t,
     args    : [synui_exe.full_path(),
                meson.current_source_dir() / 'quickshell',
                vpointer_click.full_path()],
     depends : [synui_exe, vpointer_click],
     timeout : 180)

# `bar_palette_keys`: every `pal.<key>` a bar module reads is one the palette
# actually defines. Updates.qml asked for `pal.yellow` — a THEME property, never
# a key of the per-strip object the modules read — on its error branch alone, so
# nothing exercised it. ⚠ An undefined assignment does not blank a QML property,
# it leaves the previous value: the errored badge was drawn in ordinary bar ink,
# identical to the working state, with `Unable to assign [undefined] to QColor`
# in a log per monitor as the only trace. Unreachable by any pixel test (it needs
# a machine whose update check is failing), so the guard is mechanical. Reads
# files only — no compositor, no GPU, never skips.
bar_palette_keys_t = find_program('tests/bar_palette_keys.sh')
test('bar_palette_keys', bar_palette_keys_t,
     args    : [meson.current_source_dir() / 'quickshell'],
     timeout : 30)

# `weather_surfaces`: one fetch, three surfaces, and the FILE between them. The
# lock screen, the bar module and the desktop widget all show the weather and
# none of them fetches it — src/weather.c publishes weather.state and everything
# else reads it. A file seam breaks silently, so this pins the two halves that
# have no other symptom: switching the weather OFF removes the published
# reading (a temperature left on the bar afterwards is the desktop insisting on
# a fact nobody asked it to keep checking), and `stale_after` crosses the
# process boundary rather than being hardcoded on both sides. Never touches the
# network: the readings are written by hand into the same file the fetch thread
# writes, which is the whole point of there being one.
weather_surfaces_t = find_program('tests/weather_surfaces.sh')
test('weather_surfaces', weather_surfaces_t,
     args    : [synui_exe.full_path(), synctl_exe.full_path(),
                meson.current_source_dir() / 'quickshell'],
     depends : [synui_exe, synctl_exe],
     timeout : 180)

# The accessibility control: ONE setting that makes the whole desktop bigger —
# the compositor's panels, every application and the cursor together. Two
# headless outputs, because "every screen" and "the focused screen" are
# indistinguishable with one. Also holds the refusal that stops a scale from
# hiding the very panels used to undo it.
display_scale_t = find_program('tests/display_scale.sh')
test('display_scale', display_scale_t,
     args    : [synui_exe.full_path(), synctl_exe.full_path()],
     depends : [synui_exe, synctl_exe],
     timeout : 120)

# Every panel with a background rect gets the theme's corner radius and the
# frosted glass behind it. A hand-written roster in render.c against the set of
# `*_ui.bg` rects in the same file — `saver` was missing from it for its whole
# life, which is invisible from inside the panel and obvious beside another one.
panel_chrome = find_program('tests/panel_chrome.sh')
test('panel_chrome', panel_chrome,
     args    : [meson.current_source_dir() / 'src/render.c'],
     timeout : 30)

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
# Walking back out of a category with the arrows kept dropping the highlight
# at the top of the menu, because the rebuild that follows a page change reset
# the selection to row 0 unconditionally. A text check, and deliberately so:
# the start menu is a quickshell panel that talks to the RUNNING compositor,
# and loading it to press arrow keys at it would drive the live session.
# The task manager can end a process, and the right-click menu is a second way
# to reach that. What this guards is that it is a second way to reach the SAME
# path — not a second place that calls kill(), which would look identical in
# use and have none of the confirmation, the pid-1 refusal or the pinned name.
# The CPU column's scale. Not a shape check like the menu test below it: this
# is arithmetic, and the bug was that it was RIGHT on a scale nothing else in
# the panel used — a per-core percent printed six lines under a meter that
# means percent of the machine. Every case names a core count, because the
# whole bug is a factor of ncpu and is invisible on a one-core box.
taskmgr_cpu_test = executable('taskmgr_cpu_test',
  ['tests/taskmgr_cpu_test.c', 'src/taskmgr.c', 'src/hit.c', 'src/panel.c',
   'src/gpu.c'],
  include_directories : include_directories('src'),
  dependencies : [scenefx, cairo, fontconfig, wlroots, wayland, pixman, xkbcommon, libm],
  c_args : ['-DWLR_USE_UNSTABLE'],
  build_by_default : false)
test('taskmgr_cpu', taskmgr_cpu_test)

taskmgr_menu = find_program('tests/taskmgr_menu.sh')
test('taskmgr_menu', taskmgr_menu,
     args    : [meson.current_source_dir() / 'src/taskmgr.c',
                meson.current_source_dir() / 'src/render.c'],
     timeout : 30)

menu_back = find_program('tests/menu_back.sh')
test('menu_back', menu_back,
     args    : [meson.current_source_dir() / 'quickshell/StartMenu.qml'],
     timeout : 30)

menu_cats = find_program('tests/menu_cats.sh')
test('menu_cats', menu_cats,
     args    : [meson.current_source_dir() / 'src/ctlpanel.c',
                meson.current_source_dir() / 'quickshell/StartMenu.qml'],
     timeout : 30)

# Every row of the music widget's drawer against the verbs syn-arcade answers
# with. Same shape as menu_cats above and there for the same reason: a
# hand-written QML dispatch over a list another program owns, with nothing
# keeping the two in step — and it had already drifted the whole way. Spotify's
# row came back `action=setup` and the shell had no branch that ran `big music
# setup`, so the drawer said "press to sign in" and had nowhere to sign in; the
# YouTube page's three errand rows were drawn and then refused on a stdin
# argument that stopped being true when `yt find` and `yt login` learned to open
# their own terminal. Neither is visible from a build, a run or the journal —
# the row is simply inert. Source-only: the widget is a quickshell surface on
# the running compositor and MusicLibrary shells out to a real Plex server and a
# real YouTube session, so loading it to press rows would drive the live desktop
# and start the music.
music_errands = find_program('tests/music_errands.sh')
test('music_errands', music_errands,
     args    : [meson.current_source_dir() / 'quickshell/MusicLibrary.qml',
                meson.current_source_dir() / 'quickshell/widgets/MusicPlayer.qml'],
     timeout : 30)

# A queued track is drawn by its NAME, on the card and in the bar. cliamp
# reports the path it was handed as the title, which on YouTube reduces to the
# word `watch` before MPRIS ever sees it — so the widget's own "a title that is
# really a path" rule had nothing left to strip and every song in a playlist was
# drawn with the same non-name. The names live in syn-arcade's titles cache;
# this asserts both surfaces ask for them and that neither grew a second copy of
# the C's key rule. Source-only, same as above.
music_title = find_program('tests/music_title.sh')
test('music_title', music_title,
     args    : [meson.current_source_dir() / 'quickshell/MusicLibrary.qml',
                meson.current_source_dir() / 'quickshell/widgets/MusicPlayer.qml',
                meson.current_source_dir() / 'quickshell/modules/Media.qml'],
     timeout : 30)

# The windows synui opens to run ONE command, and the flag that keeps them
# open. Three terminals spell --hold and two of them take the command
# differently; syntty handed the trailing form dies at argument parsing, which
# is a row that was clicked and a window that never opened, with nothing on
# screen or in the journal. Source-only, same as above.
term_hold = find_program('tests/term_hold.sh')
test('term_hold', term_hold,
     args    : [meson.current_source_dir() / 'quickshell/StartMenu.qml',
                meson.current_source_dir() / 'src/ai_interface.c'],
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

# The image viewer's Exec and the association it is half of. Runs the wrapper
# against fakes on PATH — the recursion guard is the case that cannot be caught
# any other way, since the failure is a handler that calls xdg-open which calls
# the handler.
# Every panel is in the menus, not only on a chord. Derived from
# SYN_PANEL_LIST — the roster the pointer and keyboard chains already walk — so
# a panel added to the compositor fails this until somebody decides, in
# writing, whether it is an app or not.
# Network printer discovery, against fake CUPS tools. It must not be tested any
# other way: the real lpadmin would add queues to the machine running the build.
printers = find_program('tests/printers.sh')
test('printers', printers,
     args    : [meson.current_source_dir() / 'systemd/synui-printers.sh'],
     timeout : 60)

menu_apps = find_program('tests/menu_apps.sh')
test('menu_apps', menu_apps,
     args    : [meson.current_source_dir() / 'src/input.c',
                meson.current_source_dir() / 'data'],
     timeout : 30)

view_image = find_program('tests/view_image.sh')
test('view_image', view_image,
     args    : [meson.current_source_dir() / 'systemd/synui-view-image.sh',
                meson.current_source_dir() / 'data/synui-view.desktop',
                meson.current_source_dir() / 'src/crop.c'],
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

# The OTHER axis of a desktop switch: how many monitors it moves
# (workspace_mode). Two headless outputs and a window on each, because the
# failure this catches is not "the switch does nothing" but an unfocused monitor
# quietly following along — which on a real desk looks like the feature working
# until you glance at the second screen. The shared half is the control.
ws_per_monitor = find_program('tests/ws_per_monitor.sh')
test('ws_per_monitor', ws_per_monitor,
     args    : [synui_exe.full_path(), stubborn_client.full_path(),
                synctl_exe.full_path()],
     depends : [synui_exe, stubborn_client, synctl_exe],
     timeout : 180)

# The cube (cube.c), which ws_slide.sh is blind to by construction: it is not a
# per-window animation, so there is no draw offset and no alpha to read. What is
# asserted instead is that it DREW (cube.c's per-turn DEBUG line counts frames;
# zero means the driver refused and the switch fell back, which is a skip and not
# a failure), that the outgoing desktop is disabled while still fully opaque, and
# that a full turn each way lands PIXEL-IDENTICAL to where it started.
ws_cube = find_program('tests/ws_cube.sh')
test('ws_cube', ws_cube,
     args    : [synui_exe.full_path(), stubborn_client.full_path(),
                synctl_exe.full_path()],
     depends : [synui_exe, stubborn_client, synctl_exe],
     timeout : 180)

# ...and the surface the user actually looks at: the bar's desktop pills, which
# must light PER MONITOR. `synctl workspaces` marks a desktop visible when it is
# up on ANY screen, so a bar reading that field lights every screen's desktop on
# every screen — and the delegate is one stale binding away from lighting two at
# once even with the right data. Neither failure logs anything, so this reads
# rendered pixels off two headless bars.
ws_bar_pills = find_program('tests/ws_bar_pills.sh')
test('ws_bar_pills', ws_bar_pills,
     args    : [synui_exe.full_path(), synctl_exe.full_path(),
                meson.current_source_dir() / 'quickshell'],
     depends : [synui_exe, synctl_exe],
     timeout : 180)

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

# And its FOURTH: the dock alpha a theme brings with it, one positional
# argument later, for the desktop widgets — quickshell's own process, which
# had no theme-aware fallback for `dock_opacity = auto` at all before this and
# fell back to a flat 0.72 beside a dock the compositor had already drawn at
# Prism's 0.05 in-process.
apply_theme_dock_alpha = find_program('tests/apply_theme_dock_alpha.sh')
test('apply_theme_dock_alpha', apply_theme_dock_alpha,
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

# `welcome_guide`: the welcome guide is a SECOND quickshell entry point on the
# bar's tree, and every way it can fail is silent — a qmldir line missing makes
# "Guide is not a type", an uninstalled pages.js makes every page `undefined`,
# and both come from a package that built cleanly. Loads it on a headless synui
# and asks the PIXELS: did a card draw, do two pages differ, does the IPC answer,
# and does it get out of the way when a window opens.
welcome_guide = find_program('tests/welcome_guide.sh')
test('welcome_guide', welcome_guide,
     args    : [synui_exe.full_path(), meson.current_source_dir() / 'quickshell'],
     depends : [synui_exe],
     timeout : 180)

# `popup_hover_dismiss`: the bar menu and the mixer must not dismiss themselves
# while the pointer is resting on them. Qt gives the hover enter/exit pair to
# exactly ONE item, so a MouseArea filling a popup is `exited` the instant the
# pointer reaches a row or a slider INSIDE it — which armed the dismissal timer
# with the pointer still on the popup. Static, and cheap: the real pointer path
# through the identical hover graph is driven by
# synfiles/tests/ctx_flyout_hover.qml.
popup_hover_dismiss = find_program('tests/popup_hover_dismiss.sh')
test('popup_hover_dismiss', popup_hover_dismiss,
     args    : [meson.current_source_dir() / 'quickshell'],
     timeout : 30)

# `bar_enabled`: turning the bar off turns off THE BAR. It used to be a
# `pkill -x quickshell`, and that process is also every desktop widget, the OSD,
# the start menu, the mixer and the notes — so the switch cleared the desktop.
# Three captures: bar+widget up, then bar_enabled = off (the top edge back to
# bare wallpaper, the widget's corner UNCHANGED — that is the assertion a stop
# command cannot pass), then on again from the watch alone.
# Double-clicking a day in the bar's calendar opens syn-cal on it. A source
# test, not a capture: this click spawns an application, and a rig that proved
# it would open a real window on whoever ran the suite. See the file's own note.
# The HDR probe must never commit output state — see the test's own note.
hdr_probe_ro = find_program('tests/hdr_probe_is_read_only.sh')
test('hdr_probe_is_read_only', hdr_probe_ro,
     args : [meson.current_source_dir() / 'src'])

calendar_double_click = find_program('tests/calendar_double_click.sh')
test('calendar_double_click', calendar_double_click,
     args : [meson.current_source_dir() / 'src',
             meson.current_source_dir() / '..' / 'syn-cal'])

bar_enabled = find_program('tests/bar_enabled.sh')
test('bar_enabled', bar_enabled,
     args    : [synui_exe.full_path(), meson.current_source_dir() / 'quickshell'],
     depends : [synui_exe],
     timeout : 180)

# `bar_opacity`: the user's alpha for the bar, over the theme's. ctlpanel_table
# proves the row stores and restores; this proves it ARRIVES — three captures
# over a magenta desktop, asserting that 0.00 leaves most of the strip showing
# the wallpaper (no background at all, not a faint one), that 1.00 leaves none
# of it, and that `auto` comes back pixel-identical to the theme's own bar.
bar_opacity = find_program('tests/bar_opacity.sh')
test('bar_opacity', bar_opacity,
     args    : [synui_exe.full_path(), meson.current_source_dir() / 'quickshell'],
     depends : [synui_exe],
     timeout : 180)

# `wallpaper_accent`: whether the desktop's accent is MEASURED off the picture
# or taken from the theme — `auto|off|on`, auto being Prism and nothing else.
# The assertion is palette.state's `use=`, because that is the one decision both
# halves of the desktop read: theme.c substitutes it into synui's own panels,
# and quickshell takes it out of the file. They disagreed before it existed.
wallpaper_accent = find_program('tests/wallpaper_accent.sh')
test('wallpaper_accent', wallpaper_accent,
     args    : [synui_exe.full_path()],
     depends : [synui_exe],
     timeout : 180)

# `wallpaper_mono`: what the desktop draws with when the picture has NO colour
# in it. The refusal always existed and always resolved to the theme's own
# accent, so a black-and-white or too-dark wallpaper produced a Prism desktop in
# the house cyan — the one colour nowhere near the screen. Two wallpapers in one
# run, because "it went white" proves nothing without the coloured one beside it
# still measuring a hue.
wallpaper_mono = find_program('tests/wallpaper_mono.sh')
test('wallpaper_mono', wallpaper_mono,
     args    : [synui_exe.full_path()],
     depends : [synui_exe],
     timeout : 180)

# `wallpaper_accent_widgets`: WHERE the measured colours land. The accent used
# to substitute into `magenta` alone, so the underline followed the picture
# while every module glyph kept the preset's cyan and the clock its yellow. Two
# widgets in one capture: LAUNCH must come out the measured ACCENT and CLOCK the
# measured SECONDARY. Both were #00D6E5 while it was half-applied, and
# `secondary` was published but read by nothing at all.
wallpaper_accent_widgets = find_program('tests/wallpaper_accent_widgets.sh')
test('wallpaper_accent_widgets', wallpaper_accent_widgets,
     args    : [synui_exe.full_path(), meson.current_source_dir() / 'quickshell'],
     depends : [synui_exe],
     timeout : 180)

# `wallpaper_live_accent`: WHICH picture the accent is measured off. A live
# wallpaper is a BACKGROUND layer client covering synui's own painted buffer, so
# the image that used to answer was the one underneath it — invisible, and
# usually a leftover. swaybg stands in for linux-wallpaperengine; the assertion
# is that a green client over a magenta wallpaper makes the published accent
# GREEN, and magenta again when it unmaps. "Is there an accent" passed
# throughout the bug.
wallpaper_live_accent = find_program('tests/wallpaper_live_accent.sh')
test('wallpaper_live_accent', wallpaper_live_accent,
     args    : [synui_exe.full_path()],
     depends : [synui_exe],
     timeout : 180)

# `wallpaper_live_gpu_accent`: the same question asked with a GPU client, which
# is the only kind a real live wallpaper is. swaybg commits wl_shm, imported as
# a plain GL_TEXTURE_2D that reads back; linux-wallpaperengine commits a
# DMA-BUF, which on NVIDIA imports as GL_TEXTURE_EXTERNAL_OES — and scenefx
# refuses to bind those to an FBO, so BOTH wlr_texture_preferred_read_format()
# and wlr_texture_read_pixels() fail without a word. The swaybg test passed
# through all of that. This one runs the measurement against a client that
# paints with EGL/GLES, so the accent has to come off a texture the compositor
# can only sample, never read.
wp_live_gl_client = executable('wp_live_gl_client',
  sources : [
    'tests/wp_live_gl_client.c',
    layer_shell_client_h,
    layer_shell_c,
    linux_dmabuf_client_h,
    linux_dmabuf_client_c,
    # layer-shell's generated code names xdg_popup_interface (get_popup), so a
    # client linking it needs xdg-shell's glue too — the compositor never
    # noticed because wlroots hands it that.
    xdg_shell_c,
  ],
  dependencies : [wayland_client, wayland_egl, egl, glesv2, gbm_dep, libdrm_dep],
  build_by_default : false,
  install : false)

wallpaper_live_gpu_accent = find_program('tests/wallpaper_live_gpu_accent.sh')
test('wallpaper_live_gpu_accent', wallpaper_live_gpu_accent,
     args    : [synui_exe.full_path(), wp_live_gl_client.full_path()],
     depends : [synui_exe, wp_live_gl_client],
     timeout : 180)

# `wallpaper_live_ink`: the same substitution, asked of the OTHER measurement
# taken off the same picture. 388 moved the accent onto the live wallpaper and
# left the luminance grid on the painted one, so every surface with no
# background of its own — the bar, the start menu, the panels, the widgets —
# went on choosing its ink from an image the engine covers completely. A pale
# Workshop wallpaper therefore got light text on it. Near-black static picture,
# near-white live one, and the assertion is which way the ink goes.
wallpaper_live_ink = find_program('tests/wallpaper_live_ink.sh')
test('wallpaper_live_ink', wallpaper_live_ink,
     args    : [synui_exe.full_path(), wp_live_gl_client.full_path()],
     depends : [synui_exe, wp_live_gl_client],
     timeout : 180)

# `wpengine_assets_dir`: the live wallpaper never played on a machine that does
# not own Wallpaper Engine on Steam — the ISO and every fresh install, which is
# every machine the four wallpapers in synapse-wallpapers were packaged for.
# synui-wpengine omitted `--assets-dir` when Steam's assets tree was missing,
# because a video wallpaper does not read it. The WALLPAPER does not; the ENGINE
# does — with no such flag it hunts for Steam's copy of the app, fails, and
# exits. It commits one buffer before it goes, so the dead layer surface keeps
# that frame and the desktop shows a still picture that never moves: the picker
# reports success and the only contrary evidence is a log under /run.
#
# Needs no compositor — the script is SOURCED and start_output() called with a
# stub engine, deliberately, since every VERB runs `synctl outputs` against the
# ambient WAYLAND_DISPLAY, which is the developer's live seat.
wpengine_assets_dir = find_program('tests/wpengine_assets_dir.sh')
test('wpengine_assets_dir', wpengine_assets_dir,
     timeout : 60)

# `wpengine_mangohud`: the second way the live wallpaper dies looking applied.
# The session exports MANGOHUD=1 so games get the overlay without a per-game
# wrapper, and MangoHud's Vulkan manifest makes that one variable load its layer
# into EVERY Vulkan client — including the wallpaper engine, whose mpv asks
# libavutil for a Vulkan hwdec device. On AMD that segfaults in MangoHud's own
# vkCreateDevice hook before a frame is painted. NVIDIA picks another hwdec and
# never calls it, so the development desktop cannot reproduce it — the same
# blind spot as the assets-dir bug. Sourced and stubbed for the same reason.
wpengine_mangohud = find_program('tests/wpengine_mangohud.sh')
test('wpengine_mangohud', wpengine_mangohud,
     timeout : 60)

# `tux_screen`: Tuxagotchi is almost entirely a PICTURE — a pixel penguin that
# walks, blinks, eats, sleeps, sickens, ages and dies — and every other way of
# looking at that picture means running a desktop and waiting hours for the pet
# to be in the mood you wanted to check. TuxShell and TuxScreen import QtQuick
# and tuxart.js and nothing else, so this renders seventeen pets under the
# offscreen platform with the software renderer, then proves each mood DRAWS by
# grabbing every LCD on its own and finding pairs that must differ. That last
# part is the test: a misspelled sprite is `undefined`, draws nothing, and
# reports nothing anywhere — the first draft passed with the mess deleted.
tux_screen = find_program('tests/tux_screen.sh')
test('tux_screen', tux_screen,
     args    : [meson.current_source_dir() / 'quickshell'],
     timeout : 120)

# `postit_ink`: the note is the one widget whose content somebody typed, and on
# a glass desktop it has no card to be typed on — widgetAlpha follows the dock,
# and a dock at 0.00 puts 12px body text straight onto the wallpaper in an ink
# the theme chose for a surface that is not there. Captures the note over a
# near-black wallpaper on the LIGHT glass theme, which is the one arrangement
# where the two possible answers are #1d1d1f and #ffffff.
postit_ink = find_program('tests/postit_ink.sh')
test('postit_ink', postit_ink,
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

# …and that a bar MODULE is asked about the same strip. bar_backdrop pins the
# whole bar's answer, which was never wrong; this pins the per-column one, which
# fell back to a grid row a ninth of the screen deep for a bar 34 pixels tall.
# Over a wallpaper that is dark for exactly those rows and bright underneath,
# the two readings land on opposite inks — and the modules over that column came
# out black on a black bar beside neighbours that came out white (pkgrel 522).
# Phase 1 is the published measurement, phase 2 is the bar on screen.
bar_strip_band = find_program('tests/bar_strip_band.sh')
test('bar_strip_band', bar_strip_band,
     args    : [synui_exe.full_path(), meson.current_source_dir() / 'quickshell'],
     depends : [synui_exe],
     timeout : 180)

# …and the same question asked of the START MENU, which is the one surface on
# this desktop that changes SIZE as you use it. It corrected its alpha against
# the box it happened to occupy, and the luminance grid is a ninth of the screen
# deep — so a submenu four rows longer than its neighbour crossed a grid row and
# opened as frosted glass beside one that opened clear. Two runs, one compositor
# each, differing only in how many categories the menu has to list (pkgrel 524).
menu_surface_pages = find_program('tests/menu_surface_pages.sh')
test('menu_surface_pages', menu_surface_pages,
     args    : [synui_exe.full_path(), meson.current_source_dir() / 'quickshell'],
     depends : [synui_exe],
     timeout : 180)

# `transparency = off` means nothing sees through — the master switch reaching
# the surfaces that ask for their own alpha (the bar, the shell's popups, the
# widget cards), which it did not until 594: a pinned bar_opacity of 0.05
# survived it and the frost did not, so switching transparency OFF made the
# start menu MORE see-through than leaving it on. Two runs, one compositor
# each, differing in one line of theme.state.
glass_master_off = find_program('tests/glass_master_off.sh')
test('glass_master_off', glass_master_off,
     args    : [synui_exe.full_path(), meson.current_source_dir() / 'quickshell'],
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
# Closing a window must hand focus back to the one you were using, not to
# whatever heads the stacking list — see the file for the report it comes from.
# The phosphor hot core must saturate toward the phosphor's spectrum, not white
# — the cream-highlight regression. No GPU needed; see the file for the limits.
# The screen reader's SWITCH and its ANNOUNCER are two facts, and for four
# pkgrels only the first existed: `syn-speak on` enabled a unit the package did
# not ship, `unit_do`'s `|| true` swallowed the failure, and all three surfaces
# reported On while nothing was ever announced. Asserts the unit is shipped and
# installed, that the watch loop really speaks a focus change (every voice
# candidate stubbed — a real one would talk out loud on the developer's
# session), and that it resolves the display a user unit does not inherit.
speak_announcer = find_program('tests/speak_announcer.sh')
test('speak_announcer', speak_announcer,
     args    : [meson.current_source_dir() / 'tools/syn-speak',
                meson.current_source_dir() / 'systemd',
                meson.current_source_dir() / 'PKGBUILD'],
     timeout : 60)

phosphor_tint = find_program('tests/phosphor_tint.sh')
test('phosphor_tint', phosphor_tint,
     args    : [meson.current_source_dir() / 'src/effects.c',
                meson.current_source_dir() / 'tests/phosphor_replay.py'],
     timeout : 30)

close_focus_mru = find_program('tests/close_focus_mru.sh')
test('close_focus_mru', close_focus_mru,
     args    : [synui_exe.full_path(), stubborn_client.full_path(),
                synctl_exe.full_path()],
     depends : [synui_exe, stubborn_client, synctl_exe],
     timeout : 120)

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

# Super+O / Super+Shift+O (move_output): that the window CROSSES to the other
# monitor rather than just changing which monitor it claims to be on. Two
# headless outputs, and a window hand-placed through windows.conf on a floating
# desktop — the case where `v->floating` is 0, nothing lays the window out, and
# the key looked completely dead.
move_output_t = find_program('tests/move_output.sh')
test('move_output', move_output_t,
     args    : [synui_exe.full_path(), stubborn_client.full_path(),
                synctl_exe.full_path()],
     depends : [synui_exe, stubborn_client, synctl_exe],
     timeout : 120)

# Leaving fullscreen: that the window comes back on the monitor it was on, and
# at the box it had there. Two headless outputs and a windows.conf entry
# pointing at the FIRST one, which is the trap — the un-fullscreen path used to
# ask that table where the window goes, and the table answers with wherever the
# app was last CLOSED. On a laptop plugged into a television that is the
# built-in panel, so un-fullscreening a video threw it off the TV.
fullscreen_output_t = find_program('tests/fullscreen_output.sh')
test('fullscreen_output', fullscreen_output_t,
     args    : [synui_exe.full_path(), stubborn_client.full_path(),
                synctl_exe.full_path()],
     depends : [synui_exe, stubborn_client, synctl_exe],
     timeout : 120)

# Super+arrows (window_move_key): that the window MOVES rather than just
# changing its place in a list nothing on a floating desktop reads. Same trap as
# move_output above, reached from the other side — `v->floating` is the window's
# flag, and on a FLOATING desktop it is the DESKTOP that is floating, so the key
# answered {"ok":true} and moved nothing at all.
move_window_t = find_program('tests/move_window.sh')
test('move_window', move_window_t,
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

# A UI string that no translator can reach is silently English forever, and
# nothing else in the build can see it — see tests/i18n.sh.
i18n_test = find_program('tests/i18n.sh')
test('i18n', i18n_test, timeout : 60)

subdir('po')

# The bar's own catalogs. A separate domain from po/ above because the last mile
# is different — JSON read by a FileView, not a .mo read by libintl — and the
# reason is in po-bar/meson.build.
#
# ⚠ A UI STRING IN QML THAT NO TRANSLATOR CAN REACH IS SILENTLY ENGLISH FOREVER,
# the same failure po/POTFILES has, with one extra way in: qsTr() looks like the
# marking and is not. tests/i18n_bar.sh fails on a qsTr( anywhere under
# quickshell/, on a .qml that calls I18n.tr() and is not in po-bar/POTFILES, and
# on a stale template.
i18n_bar_test = find_program('tests/i18n_bar.sh')
test('i18n_bar', i18n_bar_test,
     args    : [meson.current_source_dir()],
     timeout : 60)

# ⛔ AND THE BRIDGE ITSELF, EXECUTED. i18n_bar above proves every string is
# REACHABLE by a translator; this proves a catalog on disk changes what the bar
# would draw. Until it existed nothing ran a line of quickshell/I18n.qml.
#
# ⚠ It runs under Qt's own qml with tests/qmlstubs/, NOT under quickshell —
# quickshell connects to Wayland at startup and on a developer's box that is the
# LIVE session, which `env -u WAYLAND_DISPLAY` does not prevent. Skips (77)
# where Qt 6's qml runtime is absent.
i18n_bar_runtime = find_program('tests/i18n_bar_runtime.sh')
test('i18n_bar_runtime', i18n_bar_runtime,
     args    : [meson.current_source_dir()],
     timeout : 120)

subdir('po-bar')

# The ANTIQUITY shell's catalogs and its own gate. A third domain because there
# is a third consumer — `bar_shell = antiquity` starts quickshell-antiquity/
# instead of quickshell/ — not a third mechanism; the last mile is the same
# JSON bridge, and po-antiquity/meson.build says so.
#
# ⛔ AND IT CHECKS TWO THINGS THE BAR'S SUITE CANNOT. The weather widget
# switches on words that arrive from OpenWeatherMap, and every clock in the
# tree used Qt.formatDateTime(d, "<format>") — which formats against
# QLocale::c() and drew an English date on a Japanese desktop. Both are in
# tests/i18n_antiquity.sh's header.
i18n_antiquity_test = find_program('tests/i18n_antiquity.sh')
test('i18n_antiquity', i18n_antiquity_test,
     args    : [meson.current_source_dir()],
     timeout : 120)

subdir('po-antiquity')
