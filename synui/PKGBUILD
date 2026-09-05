pkgname=synui
pkgver=0.1.0
# 367: virtual-pointer-v1. synui advertised zwp_virtual_keyboard_manager_v1 —
#   which is how wtype and the bar's start menu work — and no pointer
#   equivalent, so nothing on the system could move the cursor. syn-arcade's
#   big screen mode needs to: a web browser on a television takes pointer
#   events from a gamepad stick, and no amount of message passing is one.
#   wlr_virtual_pointer_v1 wraps a real struct wlr_pointer, so it goes to
#   wlr_cursor and behaves exactly like a mouse — same focus rules, same cursor
#   on screen. ⚠ Added to privileged_globals[] in the same commit: motion plus
#   a button drives every window on the seat, so a sandboxed client does not
#   get it. See src/input.c server_new_virtual_pointer().
# 369: the terminal dependency caught up with the default. syntty has been the
#   default terminal since 359 and is what every fresh synuirc names, but this
#   still hard-depended on kitty — so 65 MiB of terminal nothing opens by
#   default could not be uninstalled, and `synpkg remove kitty` was refused with
#   "synui: requires kitty". depends: kitty → syntty, kitty to optdepends. It is
#   still installed by the ISO and the installer, which name it directly; this
#   only stops it being load-bearing. ⚠ syntty had to move BEFORE synui in
#   build-all.sh and archiso/build.sh — both are ordered, and makepkg -s
#   resolves depends out of the local repo as it goes.
# 374: `dispatch deskicons_refresh` — rescan ~/Desktop.
#   ⚠ There is NO inotify watch on that directory (deskmenu.c says so), so a
#   .desktop file written into it by anything other than a drag-and-drop did not
#   appear until desktop icons were toggled off and on. Nothing outside the
#   compositor could ask for the rescan at all, which made "put an icon on the
#   desktop" — syn-arcade's new gamescope wrappers offer it, and so could
#   anything else — a switch that wrote the file correctly and put nothing on
#   screen until the next login. A bind action rather than a new IPC verb,
#   because every bindable action is already scriptable through
#   `synctl dispatch` and a second registry is a second thing to keep in step.
# 375: a Refresh row on the desktop's own right-click menu, using that same
#   action. The action alone was reachable only by another program; a person who
#   drops a file into ~/Desktop from a terminal had no way to ask for the
#   rescan at all. It is a plain deskicons_reload and deliberately NOT
#   deskicons_arrange — arranging clears every cell an icon has been dragged
#   into, and a "refresh" that rearranges the desktop is not a refresh.
# 377: SYNAPSE Prism — a fourteenth theme, and the one a FRESH INSTALL boots
#   into (syn-install writes `theme = prism`; the compiled default stays
#   `synapse`, because changing it would restyle every existing desktop that
#   never picked a theme, silently, on the next upgrade).
#   It is glass like macOS 26 and takes its ACCENT off the wallpaper, live, on
#   every wallpaper change: palette.c bins the image by hue, weights by
#   saturation, and returns three colours — accent, a quieter sibling, and a
#   measured second hue. Published as palette.state for the bar (NOT through
#   synui-apply-theme, which is ~20s of kwriteconfig and would run on every
#   slide of a slideshow) and pushed straight into synui's own panels.
#   ⚠ A greyscale wallpaper is answered honestly: `ok=no`, and the preset's own
#   cyan stands. Inventing a colour there is worse than not having the feature.
#   Also `glass_level` — one 0..100 slider for how much of the desktop you see
#   through, resolved onto the windows, the panels and the bar with a different
#   number each, because the same alpha is pleasant on a window and unreadable
#   on a dense panel row.
#   Plus three silent failures found while chasing a dead Super+Return:
#   an empty `terminal =` produced an EMPTY command and spawn() then did nothing
#   without logging; a `terminal =` longer than 63 bytes was truncated in
#   silence into a path that could never run; and `synctl dispatch` answered
#   {"ok":true} to an action name it had never heard of.
# 378: Prism could not see pink. A wallpaper that is 97% Sanrio pink came back
#   "no usable hue" and the desktop kept its fallback cyan, while every vivid
#   hue on the wheel answered correctly — so the fault looked hue-specific and
#   was not: the blown-highlight rule dropped anything brighter than v = 0.96
#   carrying under 0.45 saturation, and S_MIN had already made that rule
#   redundant. A pixel genuinely clipping toward white heads for s = 0 and never
#   survives S_MIN to reach a brightness test; the only pixels the rule removed
#   were bright ones chromatic enough to have passed it. That band is PASTEL.
#   ⚠ It surfaced as "pink is broken" because pink is the one colour whose
#   ordinary form IS a pastel — "blue" means the vivid one, "pink" already means
#   a pale red, and vivid pink is called magenta or fuchsia and always worked.
#   The rule is gone rather than retuned: the mid-tone bell in the weight already
#   discounts a v = 1.0 pixel to 0.47 of its saturation, so brightness was being
#   handled as a curve by the careful mechanism and as a cliff by the crude one.
#   The house wallpaper was misread the same way and nobody noticed — the
#   dendrite mark is densest at v = 1.0, s = 0.3–0.5, so its most characteristic
#   pixels were the discarded ones and it measured #7084FF for a mark whose hue
#   is 255. It reads #A080E8 now, which is what the picture is.
#   Two tests: a wall-to-wall pastel pink must be nameable, and so must every
#   pastel hue on the wheel — stating it for pink alone would let the next
#   threshold re-break the other eleven. The greyscale and smeared-chroma
#   refusals are untouched and still assert.
# 379: the glass reaches the system menus. Prism frosted the WINDOWS and left
#   every piece of chrome synui draws itself as a solid slab — the control panel,
#   the task manager, the desktop and dock menus, the clock, the calculator, and
#   the shell's own start menu, mixer and widgets. Reported as the glass stopping
#   at the window frames and never reaching the system menus.
#   Two families, two mechanisms, because they are two different kinds of
#   surface. synui's own thirty panels are a coloured scene RECT with a cairo
#   buffer of ink on top, so the blur belongs behind the rect and the alpha
#   belongs to the fill: syn_rect_backdrop_blur() is the companion-node
#   machinery the windows already use, pointed at a rect, and panel_chrome_sync()
#   drives it over the panel roster it was already walking every frame. The
#   shell's popups are layer surfaces, which synui applied NO effects to at all;
#   they now ask for glass by NAME (WlrLayershell.namespace = "synui-glass") and
#   layer.c frosts what is behind them.
#   ⚠ THE BAR IS DELIBERATELY EXCLUDED. It is a quickshell PanelWindow like the
#   start menu, so only the namespace separates them, and its opacity system
#   (bar_opacity, the clear bar, the scrim, the backdrop ink) was measured
#   WITHOUT a blur underneath — a clear bar paints nothing but its glyphs, so
#   frosting it by namespace would put a halo behind each one and invalidate the
#   contrast those numbers were chosen for. The bar's MENUS are glass, reached
#   through the popup path rather than through the bar.
#   ⚠ AND THE CASE THIS TURNS ON IS glass_level BEING UNSET. Only syn-install
#   writes that key, so a machine that reached Prism through the theme manager
#   has none — keying the panels off the level alone would have left the house
#   glass theme solid on exactly the desktops most likely to run it. Unset means
#   "nobody chose a level", never "nobody wanted glass"; the preset already
#   answered that. An explicit level still answers for any theme, matching what
#   it already does to the windows and the bar.
#   Each panel keeps the alpha it was tuned at and glass SCALES it, so the dense
#   table stays denser than the six-row menu; a floor stops the sparsest one
#   going where its ink cannot carry. panel_glass_test sweeps every level against
#   every alpha in use for both directions, because "never more solid" and "never
#   under the floor" are the kind of claim a curve change breaks in one direction
#   while looking fine in the other.
#   ⚠ AND THE UI FX BLUR SWITCH IS A GLASS SWITCH. syn_glass_active() is "the
#   theme asked AND transparency AND blur", and of those three only the first two
#   go through theme.c — which is where both the theme.state export and the layer
#   walk live. The blur row writes through uifx.c's pointer table and calls
#   uifx_apply(), so without theme_glass_refresh() there the shell would keep the
#   see-through alpha it took for a glass desktop after the blur behind it was
#   torn down: a menu with a SHARP wallpaper through it, which is the one outcome
#   worse than the slab this change set out to remove.
# 392: the live wallpaper never played on a machine that does not own Wallpaper
#   Engine on Steam — which is the ISO and every fresh install, i.e. exactly the
#   machines the four wallpapers synapse-wallpapers ships exist for.
#   synui-wpengine passed `--assets-dir` only when Steam's assets tree was there
#   and otherwise omitted the flag, on the (correct) grounds that a video
#   wallpaper never reads that tree. The wallpaper does not; the ENGINE does:
#   with no `--assets-dir` it hunts for Steam's copy, fails, and exits. It now
#   always passes one, falling back to an empty directory under $XDG_RUNTIME_DIR.
#   ⚠ THE FAILURE LOOKED LIKE SUCCESS. The engine commits one buffer before
#   dying, its dead layer surface keeps that frame, and synui does not watch the
#   process — so the desktop showed a still picture that never moved and the
#   picker reported the wallpaper applied. The one line saying otherwise was in
#   $XDG_RUNTIME_DIR, which /run clears at the next boot.
#   Also 392: the compositor's panels are menu entries now. Reported as "no
#   control panel access under KDE or GNOME", which was true in both
#   directions — under synui the panels were keybinds and nothing searchable
#   led to them, and under KDE/GNOME the one entry that did exist (the
#   calculator) was a dead button, because `synctl dispatch` needs a synui
#   listening. Eleven entries with Keywords, all OnlyShowIn=synui;SynapseOS;.
#   ⚠ BOTH NAMES ON PURPOSE: a greetd login exports XDG_CURRENT_DESKTOP=synui
#   and a display-manager login gets SynapseOS from the wayland-session file.
#   That split is a real bug of its own and is NOT fixed here; naming one of
#   them would hide the entries on the other login path.
#   ⚠ desktop-file-validate wants an X- prefix on both. Ignore it — OnlyShowIn
#   is compared literally against XDG_CURRENT_DESKTOP, so X-SynapseOS matches
#   nothing and the prefix would hide every entry everywhere.
# 405: AN IMAGE VIEWER — and the cropper in the menus beside it.
#   SynapseOS had no image viewer at all. Nothing shipped one, so the winner of
#   image/png came from mimeinfo.cache, which is only "who declared the type",
#   and the system answer to double-clicking a screenshot was a BROWSER TAB.
#   The same walkover that made a terminal the answer to "open this folder"
#   (see synfiles' mimeapps.list), and just as silent, because a browser really
#   can display a PNG.
#   The viewer is a FACE OF THE CROPPER and not a new program: crop.c already
#   decoded PNG and JPEG, already fitted a picture to an output, already kept a
#   scaled copy so a drag would not resample 24 megapixels a frame, and already
#   drew a full-screen panel. What it lacked was zoom, pan and the rest of the
#   folder. So `view` opens the picture whole, c hands the same decoded surface
#   to the cropper, Escape comes back, and nothing is read or resampled twice.
#   ⚠ THE ARROWS DO TWO THINGS, and it is not a mode: fitted there is nowhere
#   to pan, so Left and Right walk the folder; zoomed in they move the picture.
#   The footer says which. n and p always walk.
#   ⚠ The folder is sorted NATURALLY — IMG_9 before IMG_10 — because a camera's
#   own numbering is the one order a viewer must not scramble, and strcmp
#   reverses it. The recent-images list stays sorted by mtime; it answers a
#   different question ("the screenshot I just took").
#   Zoom is a MULTIPLE OF THE FIT (1.0 is the whole picture, on any monitor);
#   the CEILING is an absolute 8 screen pixels per image pixel, because
#   "as far in as it goes" should be a fixed apparent size. Past 2:1 the filter
#   is NEAREST — at that point you are looking AT the pixels.
#   Menus: Image Viewer and Image Cropper, both OnlyShowIn=synui;SynapseOS;.
#   The viewer declares image/png and image/jpeg, and synfiles' vendor
#   mimeapps.list (pkgrel 52) names it as the distribution default.
#   ⚠ Its Exec is /usr/bin/synui-view-image, NOT a bare synctl: a default
#   handler is asked to open files in sessions where synui is not running, KIO
#   skips a not-shown entry but GLib does not, so under GNOME a bare synctl
#   would be a double-click that did nothing and said nothing. The wrapper
#   dispatches when there is a synui and hands the file on when there is not —
#   and guards the recursion, since it is itself what xdg-open would call.
#   ⚠ config/synui-crop.desktop is config/synui-crop-servicemenu.desktop now.
#   The installed path is unchanged; the source file was renamed because the
#   application entry is data/synui-crop.desktop and one name in two directories
#   is a file you edit the wrong copy of.
# 406: the settings app and the control panel can each reach the other, and
#   every panel is in the MENUS — velle: "don't make apps hotkey only must be
#   in menus."
#   System ▸ System settings and Display ▸ Monitor settings are LAUNCH rows onto
#   syn-settings (the second lands on its display pane). The split between the
#   two is real — this panel configures the desktop drawing it and cannot exist
#   where synui is not running; syn-settings configures the SYSTEM and runs
#   anywhere — but neither was findable from the other, so the timezone was
#   nowhere a person looking in the obvious place would find it.
#   ⚠ A control-panel row's action is a BIND LINE now: LAUNCH/ACTION rows split
#   it on the first space, so "settings display" means on a row what it means on
#   a key. Not PANEL rows — their action is also the token the return path is
#   armed with.
#   ⚠ The `settings` action WHITELISTS its pane rather than quoting it. It is
#   reachable from the control socket and synui_spawn runs /bin/sh -c.
#   Thirteen new menu entries: News, Emoji Picker, Wallpaper, Cursor Theme,
#   Displays, Screensaver, Mission Control, Equalizer, Bluetooth, Power Saving,
#   Record Screen, Screenshot, Network. Every one of them was reachable only by
#   a chord, which means the only way to learn it existed was to read config.c.
#   ⚠ AND TWO OF THE ELEVEN FROM 392 WERE DEAD BUTTONS: synui-ctlpanel.desktop
#   dispatched `ctlpanel` and synui-sound.desktop dispatched `sound`, but the
#   actions are `control` and `sounds`. An unknown action logs one line and
#   returns, so both menu items highlighted and did nothing for fourteen
#   pkgrels. tests/menu_apps.sh checks both halves now — every panel in
#   SYN_PANEL_LIST is in the menus or exempt in writing, and every Exec here
#   names an action input.c implements.
# 407: network printers find themselves. velle: "add printer scanning for
#   network printers and auto add."
#   The Printers row opened CUPS's web UI and that was the whole of printing
#   support. It is a complete admin interface and the wrong FIRST answer to
#   "there is a printer on my network": it opens by asking which discovery
#   protocol to use and which driver to install, and for anything sold this
#   decade the answer to both is "ask the printer".
#   synui-printers(1) does that — `scan` lists what is out there and which of it
#   is already set up, `add --auto` adds the rest with -m everywhere (IPP
#   Everywhere, the printer describes its own capabilities). Network ▸ Find
#   printers, and an Add Printers menu entry.
#   ⚠ DISCOVERY IS lpinfo's, NOT avahi's: cupsd's dnssd backend already browses
#   mDNS and already knows how to turn what it finds into a device URI it will
#   accept, and assembling ipp:// URIs out of TXT records here would be a second
#   worse copy of that. avahi is consulted for one thing — telling "no printers"
#   apart from "cupsd is not browsing", which look identical and need different
#   fixes.
#   ⚠ lpinfo -l names its fields `uri` and `info`, NOT device-uri/device-info
#   (those are the IPP attribute names, and the reasonable guess). The first
#   version matched nothing, and the symptom on a network full of printers would
#   have been "no network printers found" — a discovery tool whose failure mode
#   is its normal result. Pinned in tests/printers.sh against the format strings
#   in the binary itself.
#   ⚠ Driverless ONLY. Guessing a PPD is how a printer produces forty blank
#   pages; velle's own M2020W needs the Samsung ULD driver and is packaged
#   separately. ⚠ Nothing is overwritten: an existing queue is skipped and an
#   existing default printer stays the default. lpadmin is tried unprivileged
#   first (a box whose policy allows it must not be made to authenticate) and
#   falls back to pkexec, never sudo — there is no terminal to type into.
# 408: NIGHT LIGHT DID NOTHING ON A LAPTOP. nightlight.c built ONE 1024-entry
#   colour transform for every output, on the stated reasoning that a transform
#   is resampled by whatever consumes it so the dimension was ours to pick. It
#   is not resampled: wlroots hands the LUT to the kernel at exactly the length
#   it was built, and every driver checks that length against the CRTC's gamma
#   size — i915's check_lut_size() demands an exact match, and the legacy
#   drmModeCrtcSetGamma fallback (taken when a CRTC has no GAMMA_LUT property)
#   rejects anything that is not crtc->gamma_size. 1024 was a guess that
#   happened to be right here: this desk's NVIDIA CRTCs report GAMMA_LUT_SIZE
#   1024, measured. Intel panels report 256 or 257, AMD 4096 — the test commit
#   is refused, the frame goes out untinted, and the only trace is one
#   WLR_ERROR line. The size now comes from wlr_output_get_gamma_size() per
#   output, transforms are cached BY DIMENSION (a hybrid laptop needs two at
#   once), and the committed dim is stamped beside the temperature so a
#   connector that had no CRTC when first asked is re-tested rather than
#   written off. tests/nightlight_test.c stubs the gamma size and fails on the
#   old behaviour.
# 409: THE LIVE WALLPAPER SEGFAULTED ON AMD, and looked applied. The session
#   exports MANGOHUD=1 so games get the overlay without a per-game wrapper, and
#   MangoHud's Vulkan manifest declares enable_environment MANGOHUD=1 — so that
#   one variable loads VK_LAYER_MANGOHUD_overlay into EVERY Vulkan client in the
#   session. The wallpaper engine is one: mpv asks libavutil for a Vulkan hwdec
#   device, and on an AMD Renoir laptop MangoHud's vkCreateDevice hook dies
#   there (#1 libMangoHud.so, #4 vkCreateDevice, #7 av_hwdevice_ctx_create)
#   before a frame is painted. ⚠ NVIDIA picks a different hwdec and never calls
#   vkCreateDevice, so three engines run happily on the dev desktop while every
#   AMD laptop gets a still picture and a core dump — the same blind spot that
#   hid the --assets-dir bug. synui-wpengine now launches the engine with
#   DISABLE_MANGOHUD=1 (the manifest's own disable_environment, which beats the
#   enable) and MANGOHUD=0 for the GL side. An FPS overlay on the background
#   layer was never wanted. tests/wpengine_mangohud.sh.
# 410: DOUBLE-CLICK A BORDER TO FILL THAT AXIS (velle's request). The top or
#   bottom border grows the window to the full usable HEIGHT, the left or right
#   to the full usable WIDTH, and the axis you did not touch keeps the size you
#   gave it. A second double-click on the same axis puts it back; the two axes
#   are independent bits and collapse in either order. ⚠ A CORNER NAMES TWO
#   EDGES AND MEANS NEITHER, so it still resizes both ways as it always did.
#   Shares saved_geo with maximize and snap under their existing rule — one
#   slot, mutually exclusive — so expanding clears `snapped` and maximizing
#   clears `expanded`. ⚠ THE BORDER PRESS IS NOW ARMED, like the titlebar's:
#   committing at press time un-maximized and hand-placed the window on the
#   FIRST click of every double-click, so the second could never find an axis
#   to collapse; process_cursor_resize gained the slop check and re-takes the
#   geobox after grab_release_constraints. Also: float_arrangeable() skips an
#   expanded window (the grid dragged it straight back), layout_apply re-fits
#   the expanded AXES to the current usable box the way it does maximized
#   windows, and `synctl clients` reports expand_v/expand_h. Keys:
#   Super+Ctrl+Up / Super+Ctrl+Left (expand_v_toggle / expand_h_toggle).
#   ⚠ Collapsing by MOUSE usually is not possible — a filled axis puts the
#   border on the usable edge, where the grab ring is off-screen or under the
#   bar; the keybind and a drag of a perpendicular border are the ways back.
#   tests/edge_expand.sh, driven by a real pointer (tests/vpointer_click.c).
# 411: THE IMAGE VIEWER DID NOTHING FROM THE APPLICATION MENU, and could not be
#   left with the mouse. Two faults:
#   ⚠ synui-view.desktop's Exec is `synui-view-image %f`, and %f with nothing
#   selected expands to NOTHING — the wrapper printed a usage line to a stderr
#   no launcher reads and exited 2. No window, no error, no log: precisely the
#   failure that wrapper's own header describes and was written to fix, one
#   case over. With no file it now dispatches `view` with no path, which is
#   already defined as "open the recent-images list" and is what super+shift+i
#   does.
#   ⚠ The viewer is a full-screen panel with no window chrome, so Escape was
#   the only way out and n/p the only way through the folder. It draws a close
#   X at the top right and a chevron at each side now. The X means exactly what
#   Escape means (back to the list when that is where the image came from), via
#   one shared view_escape(). ⚠ crop_click tests the chrome BEFORE it starts a
#   pan drag, or the buttons draw, highlight and do nothing. The chevrons are
#   drawn only when the folder holds more than one image, and their rects are
#   ZEROED otherwise so a stale one cannot answer for a button that is gone.
#   view_step and view_escape now log at DEBUG what they did — nothing said
#   which image was up, so "the arrows do nothing" and "the arrows moved to a
#   file that would not decode" looked identical. tests/imgview_mouse.sh.
# 412: "Volume Mixer" in the menu opened EVENT SOUNDS. The real mixer — outputs,
#   inputs, a slider per application stream — is quickshell's, a popup on the
#   bar's volume module, and was reachable ONLY by right-clicking it. So the
#   menu entry, whose Comment describes that mixer word for word, dispatched
#   `sound` (no such action, dead from 392) and was then "fixed" to `sounds`,
#   which exists and opens a different panel in a different process. A dead
#   entry became a wrong one, which is worse: a dead entry gets reported, a
#   wrong one gets lived with.
#   The bar can be asked now. MixerState + an IpcHandler target, reached by a
#   new `mixer` action through synui_bar_ipc() — the same client-ward route the
#   start menu uses, factored out of synui_start_menu_open() now it has two
#   callers. ⚠ The Connections block is SCREEN-SCOPED: Volume.qml is
#   instantiated per monitor and without the output test one request opens a
#   mixer on all three.
#   Event sounds gets data/synui-sounds.desktop, its own entry under its own
#   name — it had one by accident before. tests/menu_apps.sh caught that the
#   moment the mixer took its Exec, which is exactly what it is for.
# 413: the widgets that had no card were inked for one. WidgetFrame has carried
#   `inkOnBackdrop` since the note needed it — measure what the card is actually
#   sitting on and ink against THAT — and only the note ever opted in. At a
#   glass level that takes a card to nothing, the system monitor and the big
#   clock's date are text painted straight onto the wallpaper in a colour chosen
#   for a dark pane. On a bright wallpaper they vanish.
#   ⚠ The clock hid it for months by solving it ONE ELEMENT UP: the time is 42px
#   with a black Text.Outline and survives anything, sitting directly above a
#   12px date with neither outline nor card. Time perfectly readable, date gone
#   — which reads as the date being broken rather than as the pair being inked
#   for two different surfaces.
#   ⚠ The HUD is the worst case, not an easier one: 9-10px glyphs, most of them
#   the dim ink. Accents and load tints are deliberately NOT re-inked — red at
#   90% is a colour with a MEANING, like the note's yellow and the bar's red
#   battery.
#   Also ships data/synapse-hannah-montana.png into /usr/share/backgrounds,
#   which the Super+W picker already scans. NOT a theme: wallpaper_accent=auto
#   is already ON for Prism, so picking it IS the theme.
# 416: the icons went highlighter on a green accent. 415 rotated the house
#   violet onto the accent keeping the pixel's HSL saturation and lightness, and
#   HSL's L is a channel average rather than a brightness: green carries five
#   times the luminance of blue at the same L. #a78bfa held at L 0.76 came out
#   30 points of CIE L* ABOVE the violet it replaced on the wallpaper's
#   yellow-green, 27 above on SYNAPSE's cyan and 21 on Nord's frost — and
#   within 8 on every purple, pink and blue, which is why nine themes looked
#   right and the green one shipped a highlighter.
#   iconhue.c rotates in OKLab now, where L IS perceived lightness, so every
#   accent lands at the lightness the icon was drawn at. Chroma is scaled by the
#   accent's own over the brand violet's: Nord's frost blue is a deliberately
#   quiet colour and an icon keeping the violet's chroma on it is a poster
#   pinned to a muted desktop.
#   ⚠ COLLIDE — how far the teal detail steps aside when the accent walks into
#   it — is measured in OKLab now and is 45 deg, not 30. It was never buying
#   the 37 dE of separation 415 credited it with: most of that gap was the
#   glyph beside it being 27 L* brighter than it was ever drawn, and with the
#   glyph back where it belongs the hue step is all there is. It buys about
#   0.8 dE per degree.
# 422: the welcome menu (Super+Escape) takes the mouse, and its one preference
#   moved out of the list of doors. "Show At Startup" was the second-to-last row
#   of a menu whose every other row OPENS something — so it read as one more
#   thing to launch, and it was the only row where Enter meant "tick" rather
#   than "go". It is a stroked checkbox in the bottom-right corner now, labelled
#   "Don't show again" — the opt-OUT, because ticking a box you came here to
#   tick is the thing you are actually doing. ⚠ The sense is INVERTED against
#   the config field (welcome_at_startup) and render.c is the only place that
#   flips it. Still focusable from the keyboard as the item after the last row,
#   so nothing that worked stopped working, and Space ticks it there.
#   The pointer is the standard contract (synui.h): hover selects, a left click
#   does the row's Enter, a click off the panel closes it, the wheel moves the
#   selection. welcome_ui.shown is welcome_ui.visible now — SYN_PANEL_LIST reads
#   every panel's flag by that one name and adding a nineteenth spelling to the
#   roster is how the roster stops being one.
#   ⚠ hit.c grew SPOTS: loose clickable rects, in draw order, for the things
#   that are neither rows nor the close/drag buttons. The alternative was the
#   two panels below keeping private x/y/w/h again, which is the drift that file
#   exists to stop. hit_set_panel() blanks them, so a panel that stops drawing
#   one stops answering for it with nobody doing anything.
#   The emoji picker's CATEGORY STRIP takes the mouse too — nine tabs you could
#   read and could not click, because only the grid had ever been wired up.
#   Hover highlights, a click switches; hover deliberately does NOT switch, as
#   rebuilding the view on the way past would throw away a typed search. Tab,
#   Shift+Tab and a click all go through one emoji_set_cat().
#   ⚠ The strip is TWO LINES now. It drew as many tabs as fitted across 536px
#   and dropped the rest, which cost Symbols and Dingbats — survivable while Tab
#   was the only way to reach them, and not survivable once a tab is a click
#   target, because a tab that is not drawn is a category the mouse cannot get
#   to at all.
#
# 427 THE START MENU LOST YOUR PLACE EVERY TIME YOU LOOKED IN A CATEGORY.
#
#   Right into a category, Left back out, and the highlight was at the TOP of
#   the menu — not on the category you had just been in. Which is the one
#   moment you most want it kept, because walking a menu with the arrows is
#   exactly looking into things and stepping back out.
#
#   The rebuild that follows a page change reset the selection to row 0
#   unconditionally, and that reset is CORRECT for every other reason the rows
#   change: a stale index into a shorter list selects nothing and Enter then
#   does nothing, which reads as the menu ignoring you. So the fix is not to
#   stop resetting; it is to record the page being left and let the rebuild
#   put the selection back on the row that leads into it.
#
#   ⚠ MATCHED BY PAGE ID, NOT BY A REMEMBERED INDEX. The root list is not the
#   same list it was — a search, a renamed category or a menu that gained a
#   row all move things — so a fix that stored `selected` and put it back
#   would work in testing and land on the wrong row in use.
#
#   ⚠ ALL THREE BACK PATHS, not just the arrow. Left, Escape and the on-screen
#   Back row are the same journey by three inputs, and a mouse that lost your
#   place while an arrow key kept it is the harder pair to explain.
#
#   tests/menu_back.sh guards the shape the bug was: a path that clears the
#   page without recording it, and a rebuild that resets before it looks. A
#   TEXT check on purpose — the start menu is a quickshell panel that talks to
#   the running compositor, and loading it to press arrow keys at it would
#   drive the live session.

# 428 RIGHT-CLICK IN THE TASK MANAGER: END TASK, FORCE QUIT.
#
#   The panel could already send SIGTERM and SIGKILL — x and X, behind a
#   confirmation that names the pid it pinned and refuses init and synui
#   itself. What it could not do was be reached with the mouse:
#   taskmgr_click() opened with `(void)button;`, so a right-click was a
#   left-click and the two most useful things in the panel were keys or
#   nothing.
#
#   ⚠ ONE PATH TO A SIGNAL. Both items ARM the existing confirmation rather
#   than signalling, so the menu inherits every refusal and the pinned name
#   that path already carries. An item that called kill() itself would look
#   identical in use and have none of it — no confirmation, no pid-1 refusal,
#   and SIGKILL one click from a table that re-sorts every second.
#   tests/taskmgr_menu.sh counts the kill() calls in the file for exactly that
#   reason (and strips string literals first, or the wlr_log that reports the
#   call counts as a second one).
#
#   ⚠ IT ACTS ON THE SELECTION, not on a pid of its own. Right-clicking selects
#   the row first and sel_pid then keeps the selection on that process across
#   the re-sorts a CPU-ordered table does every second. A menu carrying its own
#   pid would be a second answer to "which process", and the two would disagree
#   exactly when the table is busiest — which is when somebody is most likely
#   reaching for kill.
#
#   ⚠ AND THE HOVER STOPS TRACKING while it is open. The pointer crosses rows
#   on its way to "Force quit", and a hover that kept moving the selection
#   would leave the menu naming a different process than the one it opened on.
#
#   "End task" is first and "Force quit" second, in the red the confirmation
#   bar already uses: the gentle choice is the one a mouse reaches without
#   travelling.
#
#   The clickable rects are hit SPOTS registered beside the drawing, and the
#   placement moved into hit.c as hit_place_popup() — the first of these will
#   not be the last, and a panel keeping private geometry for a thing it also
#   draws is the drift that file exists to stop. tests/hit_test.c covers the
#   case nobody reproduces twice by hand: opened on the LAST visible row, where
#   it would otherwise hang off the bottom edge.

# 429: the dendrite mark is two colourways now, and the surface picks. #a78bfa
#   is drawn for dark grounds — about 7.2:1 on a dark panel and about 2.4:1 on a
#   pale one, where it stops being a logo and becomes a smudge. The ink cut
#   (#5b21b6, containment triangle lifted 0.4 -> 0.65) is the reverse, about
#   8:1 pale and 2.2:1 dark. A single hardcoded file is therefore washed out on
#   half the themes we ship whichever one it is, and it was the purple half: on
#   prism-light the start button's emblem sat beside a caret and a wordmark that
#   both followed the theme, and the welcome panel's header emblem beside a
#   title that did.
#   ⚠ THE CHOICE IS THE SURFACE'S, NOT A SETTING. Each caller asks the thing it
#   already measures — render.c the same g_panel_bg and SURFACE_PALE the ink
#   ladder uses, Launcher.qml the strip's own `inkOnDark` (so a CLEAR bar
#   follows what is behind that span, not the scheme), synui-apply-theme.sh the
#   ink_for() it already runs on menu_base for rofi. No second threshold, and
#   nothing new to keep in step.
#   data/logo-ink.svg and data/logo-bold-ink.svg ship alongside the originals.
#   ⚠ The ISO's /usr/share/synapseos/logo.svg is still the purple cut alone, so
#   the rofi fallback chain ends there — a live image gets one washed-out mark
#   rather than a rasi pointing at a file that does not exist.

# 430: one press that turns off everything you can see through. Appearance ▸
#   Make it all solid, the `solid` bind action, `synctl dispatch solid`.
#   Switching the effects off by hand was three controls and the third is
#   invisible until it bites: Transparency, then Glass to Off, then Sync all
#   glass ON again to release the pins — because a row you have ever nudged
#   stops following the master, so a pinned bar or dock stayed exactly as clear
#   as it was on a desktop whose one glass control now read "Off".
#   ⚠ AND GLASS AT OFF DOES NOT MAKE THE BAR OR DOCK OPAQUE. syn_glass_bar_alpha
#   is `0.95 - 0.95t`, so the bottom of the slider hands both strips 0.95: the
#   curve was fitted to the bar's historical default at the clear end and
#   carried the last 5% all the way down. The action therefore says bar_opacity
#   and dock_opacity outright, which pins them at 1.00 — an opinion, recorded,
#   surviving a login, and handed back to the slider by Delete on the row.
#   ⚠ It is ONE-WAY. Restoring would mean remembering four numbers somewhere
#   that outlives a logout, and a switch that half-remembers is worse than one
#   that does not pretend to.
#   ctl_adjust's tail is now ctl_commit(), so setting a row outright travels the
#   identical path a keypress does — pin, release, apply and persist included. A
#   second writer that set the field and called ctl_apply would leave a pinned
#   row glassy or a settings.state that disagrees with the screen, silently.
#   tests/ctlpanel_table_test.c asserts what the label PROMISES — that the two
#   surfaces somebody pinned come out opaque — not that the pins were cleared,
#   which is only how.
#
# 431 THE DESKTOP'S ACCENT, ON THE HARDWARE THAT HAS LIGHTS IN IT.
#
#   synui already decides one colour per wallpaper: it measures the picture and
#   writes `accent` to ~/.config/synui/palette.state, and the bar, the dock and
#   the icons wear it. Everything else in the room that lights up — the RAM,
#   the board, the keyboard — was still whatever somebody else's software had
#   set it to.
#
#   `syn-rgb` is the bridge and nothing else: read the colour this desktop has
#   already chosen and hand it to OpenRGB. Follow the wallpaper's accent, the
#   THEME's accent, or a fixed colour; a brightness that scales the channels
#   rather than changing the hue; `dark` for all off; `devices` for what
#   OpenRGB can see.
#
#   ⚠ THE WATCHING IS SYSTEMD'S. syn-rgb.path fires on palette.state and
#   theme.json, so there is no daemon, nothing polling and nothing running at
#   all between one wallpaper and the next. The service is a oneshot.
#
#   ⛔ SHIPPED OFF, and enabled per user with `syn-rgb on`. It writes to
#   HARDWARE, and hardware that is already doing what its owner asked is not a
#   thing to take over on an update. Turning it off stops it following and
#   leaves the lights where they are — blanking them is `syn-rgb dark`, which
#   is its own word.
#
#   ⚠ `ok` in palette.state is the PICTURE's own answer. A greyscale wallpaper
#   offers no hue, so the lights KEEP what they have rather than snapping to
#   something invented. ⚠ `use` is deliberately NOT consulted: that setting is
#   about whether the desktop draws with the colour, and somebody who turned
#   the bar's tint off has said nothing about their keyboard.
#
#   ⚠ Not every device has a Direct mode — openrgb fails the whole call rather
#   than skipping the ones that cannot — so a refused `direct` is retried once
#   as `static`, which is what the other half of the hardware offers.
#
#   ⛔ AND THE COLOUR IS NOT A COMMENT. The state reader stripped `#` to the
#   end of a line, which turned `accent=#9B610F` into an empty string: every
#   wallpaper read as "no accent" and the lights never moved. A whole-line
#   comment never reaches that code — the key anchor does not match one.
#
#   openrgb is an OPTDEPEND: a desktop with nothing in it that glows should not
#   pull in a lighting daemon, and syn-rgb says which one is missing rather
#   than doing nothing quietly. tests/syn_rgb.sh drives the whole contract
#   against a STUB openrgb — no real device is touched, because a test that
#   changed the colour of the machine running it is not one anybody runs twice.

#
# 432 A SWITCH FOR IT, ON THE PANEL WHERE THE ACCENT ALREADY LIVES.
#
#   431 shipped syn-rgb with `syn-rgb on` as its only door. The control panel
#   is where the wallpaper accent is switched on, so it is where somebody
#   looks for the lights that follow it.
#
#   ⚠ EXTERNAL, like the font rows: the answer lives in rgb.state, which
#   syn-rgb owns and which outlives a logout. Giving the row a config key would
#   have created the second source of truth the state file exists to prevent —
#   `syn-rgb on` from a terminal has to move this row, and it does, because the
#   row reads the file on every repaint.
#
#   ⛔ AND IT IS INTERCEPTED BEFORE THE GENERIC FLIP. The table-driven toggle
#   path reads and writes the syn_config_t field the row names, and an external
#   row names none: ctl_get/ctl_put would read the top of the config struct as
#   this row's value and write a boolean back over whatever field is declared
#   first. A plausible number and a silent corruption — the exact pair the
#   `external` flag exists to prevent, which is why the font rows say so too.
#   Dispatched by ROW rather than by the flag, since sending a lighting toggle
#   to the font stepper would change the desktop's text size.
#
#   ⚠ Enter runs `syn-rgb on|off`, never a write to the file: the command also
#   enables the path unit and pushes the colour immediately, and a row that
#   only flipped a key would leave the lights unchanged until the next
#   wallpaper — which reads as the switch not working.
#
#   The table test asserts the row is a toggle with NO key rather than pressing
#   it: activating it spawns syn-rgb, and a suite that turned on the lighting
#   of the machine running it is not one anybody runs twice.
#
# 433 THE DOCK AND THE BAR NOW AGREE ABOUT WHERE THEIR SETTINGS ARE — AND THE
#     DOCK STOPPED SITTING ON TOP OF EVERYTHING.
#
#   Two pieces of furniture on one desktop had opposite answers to "how do I
#   change this". The bar put every switch on a right-click and none on the
#   control panel; the dock put auto-hide on the control panel and offered no
#   right-click at all except on an icon. Both halves are filled in.
#
#   ⛔ THE DEFAULT CHANGED: an always-visible dock is now COVERED by windows.
#   Pinning the dock on screen used to pin it above everything, so a maximized
#   window lost a 64px strip to a bar it could never be in front of.
#   `dock_on_top` (default off) is the switch, and it is only meaningful with
#   auto-hide off — an auto-hiding dock is summoned OVER whatever is there and
#   has to arrive in front of it, so the row reads "always" and the menu drops
#   the entry rather than offering a switch that would be ignored.
#
#   ⚠ AND THE HIT TESTS HAD TO LEARN ABOUT IT. dock_entry_at/dock_bar_at ran
#   before the view hit test in pointer_button() and knew nothing about
#   stacking, so a dock under a window stayed fully clickable through it — a
#   right-click on a maximized window's bottom strip opened the DOCK's menu.
#   dock_point_clear() asks surface_at(), which walks the scene graph and so
#   answers in the dock's own stacking terms; skipped when the dock floats,
#   where it would find the window the dock is covering.
#
#   ⚠ THE DOCK HAS TWO RECTS NOW. Magnification needs somewhere for an icon to
#   grow — into a 64px bar the ceiling is 64/48 = 1.33, which does not read as
#   magnification at all — so the CANVAS gains 32px of transparent headroom past
#   the BODY. Confusing the two is silent both ways: a body measured on the
#   canvas floats the slab clear of the screen edge, a canvas measured on the
#   body clips the swollen icons off. tests/dock_options_test.c asserts the
#   numbers the compositor hands the scene, which is the only place the
#   difference shows.
#
#   ⚠ The scales are sampled from each icon's FLAT position and the row is laid
#   out cumulatively, so the bar grows and re-centres rather than overlapping
#   itself. Sampling from the magnified position feeds the layout its own output
#   and the row shivers. Suppressed outright during a rearrange drag, whose
#   drop-target arithmetic is flat.
#
#   ⚠ Cell rects are per OUTPUT now — only the screen the pointer is on
#   magnifies — so they can no longer live on the (server-global) entries. Every
#   hit test derives them from dock_metrics() for the mirror it is asking about.
#
#   The dock clock reads its 12/24-hour and seconds settings out of the same
#   syn_clock_t the Clock & Time panel writes and the bar's clock follows, so
#   there is one answer to "does this desktop use a 24-hour clock". It gets a
#   1 Hz timer of its own rather than a `return true` out of dock_tick(): output
#   frames stop when nothing is moving, which is precisely the state a clock has
#   to tick through, and holding every output at its refresh rate for one string
#   a minute is the alternative.
#
#   ⚠ Bar auto-hide is PER MONITOR (bar.json), and the control-panel row is not.
#   So the row is a MASTER — on/off/mixed across the live outputs, setting them
#   all — and it ASKS the bar over quickshell's IPC rather than writing the file.
#   bar.json has exactly one writer, which is what lets the bar write it back
#   without racing its own watch; a control-panel row writing it would be the
#   second. Per-monitor control stays on the bar's right-click menu.
# 434 THE LIGHTS WERE NINE SECONDS BEHIND, AND DROPPED WHAT HAPPENED IN THE GAP.
#
#   Two symptoms, one cause, and the cause was OpenRGB's CLI.
#
#   ⚠ IT RE-DETECTS EVERY DEVICE ON EVERY INVOCATION — it walks the SMBus and
#   i2c probing for RAM, a mainboard controller and whatever else is there, once
#   per colour. Measured on this hardware: one `openrgb --mode direct --color`
#   cost 8.8 SECONDS, of which 0.04s was user time. All of it was the probe.
#
#   ⛔ AND THE PROBE WAS ALSO A NINE-SECOND HOLE IN THE WATCHING. syn-rgb.path
#   cannot re-trigger while syn-rgb.service is still running, and systemd does
#   NOT queue what it misses. Measured with a scratch path unit: three writes
#   two seconds apart during one nine-second run fired the service ONCE, on the
#   FIRST value, and dropped the other two entirely. So changing wallpaper a few
#   times in a row left the lights on the colour from the START of the burst,
#   permanently — until something else touched the file. `syn-rgb off` then `on`
#   appeared to fix it because `on` calls cmd_apply directly, never the unit.
#
#   Both are addressed, and neither fix depends on the other.
#
#   syn-rgb-server.service runs OpenRGB's own SDK server, which enumerates once
#   and stays. The same push against it is 1.03s — openrgb autoconnects to a
#   local server all by itself, so push() is unchanged and nothing asks whether
#   the server is up.
#
#   ⚠ AN OPTIMISATION, NEVER A DEPENDENCY. openrgb probes the bus when nothing
#   is serving, so syn-rgb stays correct with the server stopped, masked or
#   missing — merely slow. That is why syn-rgb.service takes After= and not
#   Wants=, and why nothing in the script tests for it.
#
#   ⛔ IT BINDS 127.0.0.1, NOT OpenRGB's DEFAULT. `--server` listens on 0.0.0.0
#   — every interface — and the SDK protocol has no authentication at all, so
#   the stock invocation hands this machine's lighting to the whole LAN.
#
#   ⛔ AND IT IS WantedBy THE PATH UNIT, NOT default.target. It holds the i2c
#   controllers open for the life of the session and syn-rgb ships off, so a
#   desktop that has never said `syn-rgb on` must not acquire an OpenRGB server
#   at login as a side effect of an update. PartOf takes it down with the bridge.
#   Skipped cleanly by ExecCondition when something is already serving on 6742 —
#   OpenRGB's own tray runs the same server, and a red unit over lighting is
#   what these units are written to avoid.
#
#   cmd_apply re-reads before it exits and pushes again if the colour moved
#   under it. Nothing outside the process can close that window: by the time the
#   run ends, the event that would have re-triggered the unit is already gone.
#   Bounded at four so a slideshow cannot pin the process open, and the ordinary
#   case is still exactly one push — asserted, because a re-check that always
#   went round twice would double the traffic to a bus this slow.
# 435 THE BOARD'S LIGHTS WENT OUT A FEW SECONDS AFTER LOGIN, AND THE RAM'S DID
#   NOT. Every device came up lit and then all but the RAM and the keyboard
#   dropped to dark — which read as the bridge failing to reach them, and was
#   the opposite: it reached them, they took the colour, and then forgot it.
#
#   ⛔ `direct` MEANS "SHOW WHAT THE HOST IS STREAMING", AND THIS IS A ONESHOT.
#   An ASUS Aura USB mainboard falls back to dark the moment the process that
#   set it exits, which for syn-rgb is a second or so after every push. The
#   Corsair DIMMs and the G512 latch their last direct frame and so looked
#   fine; the board and its addressable header did not, and so looked broken.
#   Nothing was wrong before 431 because the only thing driving these devices
#   was a resident OpenRGB, and a resident host is exactly what direct needs.
#
#   `static` is the same colour handed to the controller's OWN effect engine.
#   It holds with nothing attached, which is the only thing that can be true
#   for a program that exits.
#
#   ⚠ SO THE MODE IS PER DEVICE, and it cannot be one mode for everything: the
#   Corsair Vengeance DIMMs here have Direct and nothing else, and asking a
#   device for a mode it does not have fails the WHOLE call rather than that
#   device. syn-rgb enumerates once, writes `index<TAB>mode<TAB>name` to
#   ~/.config/synui/rgb.devices, and pushes every device in ONE invocation with
#   its own -d/-m/-c — so a push costs exactly what the old blanket one did.
#
#   ⚠ The map is CACHED because `--list-devices` is a full bus probe (8.6s
#   measured here, 1.0s against the SDK server of 434). Everything that can
#   notice it is stale rebuilds it: a failed push, `syn-rgb on`, `syn-rgb
#   devices`. A stale map cannot paint the wrong COLOUR — every device is being
#   handed the same one — only the wrong mode, and a device that has gone away
#   fails the call, which is what triggers the rebuild.
#
#   `mode=` in rgb.state is still honoured as a hand override for everything,
#   and its default is now `auto`. The old blanket direct-then-static push
#   survives as the last resort for a listing this cannot parse.
# 436 SUPER+O MOVED THE RECORD AND LEFT THE PIXELS. Both monitor keys read as
#   dead: nothing on screen changed, and pressing again only walked the
#   window's idea of its monitor further out of step with where it was.
#
#   ⛔ `v->floating` IS THE WINDOW'S FLAG, NOT THE DESKTOP'S. move_output
#   reassigns view->output and leaves the placing to layout_apply, which is
#   right for a layout that places windows; the two cases carrying their own
#   absolute geometry were spotted with `v->floating`. On a FLOATING desktop no
#   window is marked floating — the desktop is — and LAYOUT_FLOATING's pass is
#   layout_float_arrange, which deliberately steps over every window the user
#   has ever dragged (hand_placed). So a dragged window on a floating desktop
#   was placed by nobody at all.
#
#   The branch now asks whether any layout is going to place THIS window, and
#   carries it across itself when none will. tests/move_output.sh drives it on
#   two headless outputs, with hand_placed set the way float_arrange.sh does it
#   — through layout_restore_geometry reading windows.conf, no synthetic input —
#   and fails on the old condition.
# 437 CLICKING THROUGH THE WALLPAPERS KILLED THE LIGHTS FOR THE REST OF THE
#   SESSION. Not a wrong colour and not a slow one: the accent froze on some
#   picture from the middle of the run and no later wallpaper moved it again.
#   `syn-rgb apply` by hand still worked perfectly, which is the tell — the
#   push was fine, the WATCHING was gone.
#
#   ⛔ A PATH-TRIGGERED SERVICE THAT TRIPS ITS START LIMIT TAKES THE PATH UNIT
#   DOWN WITH IT. systemd's default is 5 starts in 10s; past that the service
#   fails `start-limit-hit` AND syn-rgb.path fails `unit-start-limit-hit` and
#   stays failed. Nothing re-arms it — not the next wallpaper, not a theme
#   switch, not anything short of `systemctl --user start syn-rgb.path`. The
#   journal here caught it happening TWICE in five minutes, 02:17 and 02:22,
#   both ending on that pair of lines.
#
#   ⚠ 434 IS WHAT PUT THE LIMIT IN RANGE. While a push cost 8.8s of bus probe,
#   five of them spanned ~44 seconds and the 10-second window could never fill;
#   the fix that made it 1.0s also made five wallpapers in ten seconds an
#   ordinary thing to do. The optimisation created the failure, so nothing in
#   syn-rgb itself regressed and nothing in it needed changing.
#
#   StartLimitIntervalSec=0 on syn-rgb.service. A oneshot that already refuses
#   to fail the session (SuccessExitStatus=0 1 3) has no crash loop for a rate
#   limit to arrest, and the cost of an extra run is one 1-second push.
#
#   ⚠ THE ONE-INPUT TEST PROVES NOTHING — an un-limited unit surviving a burst
#   looks the same as a limit that was never reached. tests/syn_rgb_burst.sh
#   drives a scratch path+service PAIR, identical but for that one line, with 8
#   writes a second apart: the limited arm must end `failed` and the unlimited
#   arm `active` with all 8 runs landed. Both arms are the assertion.
# 438 THE WINDOW MOVE KEYS LOSE THE SHIFT. Super+arrows move the focused
#   window now; Super+Shift+arrows, where they shipped, no longer do — moved,
#   not aliased, so the palette lists one chord for one action.
#
#   ⚠ AND SUPER+ARROWS WAS NEVER BOUND TO ANYTHING, which is the other half of
#   why this came up: it read as a key that had broken, arriving close enough
#   to 436 to look like fallout from it. `git log -S` finds no commit that ever
#   added or removed a bare `super+<arrow>` from seed_default_binds(), and
#   436's diff lives entirely inside the `move_output` branch of
#   synui_binding_execute — it changes what happens after that action is
#   chosen, never how a chord is matched. Nothing regressed; the key had
#   always done nothing, and only the timing suggested otherwise.
#
#   The Shift was deliberate and the reason no longer holds. It was reserved
#   against a keyboard snap, that being the chord most desktops snap with —
#   but snap.c is drag-to-edge and driven by the POINTER, so the reservation
#   was against a feature nothing was building, and it charged the most-used
#   window action an extra modifier to hold the seat. Super+Ctrl+arrows are
#   still the expand toggles; a keyboard snap can have those, a letter, or the
#   Shift chord this just vacated.
#
#   ⚠ FIVE SURFACES, and the table is only the first. The rationale comment
#   above the binds said in as many words that Super+arrows were left alone
#   deliberately, and the expand-toggle comment below it claimed to hold the
#   first arrow binds in the table — both false the moment the binds moved, and
#   neither is anything a compiler can see. Also src/input.c's header on
#   window_move_key, the --help block in synui_main.c, and the 0.2.9.3 release
#   notes, which describe this cycle and had already been written.
#
#   tests/readme_binds.sh is what makes the README the sixth: every combo in
#   seed_default_binds() must appear in the hotkey table, so the row moves or
#   the build fails.
# 450 EVERY ROW IN THE MUSIC WIDGET'S DRAWER WAS DRAWN AND THEN REFUSED.
#   Reported from the desktop: Spotify's row invited a sign-in and did nothing
#   when it was pressed, and the YouTube page — with a Firefox session already
#   configured and being read correctly — offered Search…, Your playlists and
#   Search inside cliamp… and acted on none of them.
#
#   ⛔ THE SECOND-ROSTER TRAP, and both halves of the report are one instance of
#   it. `syn-arcade big music` answers, for THIS machine, what each row needs —
#   an `action` column on the source picker, a `kind` column on the YouTube page
#   — and MusicLibrary.qml drew both answers faithfully and had no branch that
#   acted on either. Nothing warns: the build is green, the fetch succeeds, the
#   row appears, and pressing it does nothing at all.
#
#   · Spotify came back `action=setup`, note "press to sign in — needs Spotify
#     Premium". The note was drawn in the middle of an empty drawer and the only
#     thing to press was the source chip that had already been pressed. There
#     was no path in the shell that ran `big music setup`. It is a BUTTON now,
#     under the note, labelled from the action rather than from the note — a
#     button is its own label, and "press to sign in — needs Spotify Premium" is
#     a sentence. `install` and `browse` are the same property, because the set
#     MOVES: Spotify becomes `browse` the moment somebody finishes signing in,
#     and a widget that learned only `setup` would break at the moment it worked.
#
#   · The YouTube page's three errand rows — Search…, Your playlists, Search
#     inside cliamp… — were refused by chooseItem() on the grounds that `yt
#     find` and `yt login` READ FROM STDIN, which from a widget is a process sat
#     waiting for a line that can never arrive. ⚠ THAT WAS TRUE WHEN IT WAS
#     WRITTEN AND STOPPED BEING TRUE: both verbs ask can_be_asked() first and
#     re-run themselves inside a terminal when there is none. So the reward for
#     signing a browser in was three rows of grey text. All four ids dispatch
#     now, to the same verbs syn-arcade's own shell uses.
#
#   ⚠ `mine` IS A LIST, NOT AN ERRAND — it is the whole payoff of signing in and
#   it cannot be shown by opening a terminal. The drawer goes one level down
#   (`drill`) and back out through a row at the head of the list, which is the
#   television's menu page in the shape a 246px drawer can hold. Measured on
#   velle's own session: twelve playlists, Liked videos and Watch later
#   included, off the Firefox cookies that were already configured.
#
#   ⚠ AND THE ERRANDS ARE NOT ON THE TRANSPORT POOL. Each blocks for as long as
#   the terminal it opened is on screen; through actPool one would hold a slot
#   of three for however long a sign-in takes, and `running = true` on a busy
#   Process is a SILENT no-op — three of them and every skip is dropped with
#   nothing said. One Process, its `running` the guard, and the picker refreshed
#   when it exits: every errand exists to change big.c's answer, and a row still
#   saying "press to sign in" afterwards is the fix looking like a failure.
#
#   The delegate stopped deciding which rows are live (`kind !== "action"`) and
#   asks MusicLibrary.pressable() — one answer in one place, beside the
#   dispatch. tests/music_errands.sh is source-only for the reason menu_back.sh
#   is: the widget is a quickshell surface on the RUNNING compositor and
#   MusicLibrary shells out to a real Plex server and a real YouTube session, so
#   loading it to press rows would drive the live desktop and start the music.
#   ⚠ Needs syn-arcade 0.1.0-41, which stops these terminals fullscreening
#   themselves when there is no television to fill.
# 453 A WALLPAPER WITH NO COLOUR IN IT NOW ANSWERS IN WHITE AND GREY. The
#   accent has followed the picture since 378 and has always had one answer for
#   a picture it could not name a hue off — a greyscale photograph, or one
#   simply too dark to match: the theme's own accent. On Prism that is the house
#   cyan, which is the one colour on screen that is nowhere near the wallpaper,
#   so the feature failed in exactly the direction it exists to avoid.
#
#   Monochrome is the honest reading of a grey picture. syn_palette_monochrome()
#   (palette.c) builds the same three roles out of the greyscale: white for the
#   accent — icons, focus ring, selections — #B8B8B8 for the clock's secondary
#   and #8C8C8C for the dim, on Prism's near-black panel. It flips with the
#   surface like every other colour here does: on XP's beige it is #333333 and a
#   mid grey, because white is not there at all on a pale panel.
#
#   ⚠ THE EXTRACTOR STILL REFUSES, and that is the seam. syn_palette_from_pixels()
#   is untouched — a greyscale image still returns false, so every guard on "do
#   not invent a hue" keeps asserting exactly what it did. What changed is what
#   the DESKTOP does with the refusal, which is wallpaper_palette()'s to decide,
#   and it now has to tell two things apart that both left `ok` false: a picture
#   with no colour in it (white and grey) and no picture measured yet (the
#   theme's accent). That is the new `wp_measured` — an unreadable live buffer
#   or an output that has never been painted must not read as "there is nothing
#   to see", and an external-only DMA-BUF fails SILENTLY.
#
#   ⚠ NOT THROUGH to_ui_band(), AND THE FAILURE WOULD BE SPECTACULAR. The band
#   clamps saturation UP to 0.45 and a grey has no hue to clamp — rgb_to_hsv()
#   hands back h = 0, so a grey put through it comes out RED. "The wallpaper is
#   greyscale so the desktop went crimson" was one call away, and no existing
#   test would have seen it: none of them feeds the band an achromatic colour.
#   tests/palette_test.c asserts r == g == b to the byte, on both surfaces.
#
#   palette.state gains `mono=yes|no` beside `ok=`, and the monochrome palette
#   is published with `ok=yes` on purpose: every consumer already gates on that
#   — the bar, the widgets and the eight app windows — so all of them draw with
#   it unchanged. A second way of saying "here is what to draw with" is a second
#   way for half the desktop to miss it, which is the 386 bug exactly.
#   `ok=no` now means "no wallpaper measured", and the log has a line per state.
#   tests/wallpaper_mono.sh drives both wallpapers in one run: grey and magenta,
#   because "it went white" proves nothing without the coloured one beside it
#   still measuring a hue.
# 454 AND THE DOCK'S OWN ICONS GO WITH IT. 453 turned the panels, the bar, the
#   widgets and the eight app windows white and grey on a wallpaper with no
#   colour in it. The dock did not: our own app icons follow `panel_accent`
#   through iconhue.c, and its answer for an accent with no hue was to leave
#   them exactly as drawn — so a monochrome desktop had nine violet tiles in the
#   middle of it. That is the same one-part-ignored-the-theme complaint the file
#   was written for in 415, arriving through the one accent nobody had fed it.
#
#   ⚠ AND THE OBVIOUS FIX IS THE SPECTACULAR ONE. "Follow the accent" cannot be
#   made to apply here: rgb_to_hsl() reads a grey as h = 0, so rotating the
#   family onto a white accent turns every icon RED — the same shape as the
#   to_ui_band() trap 453 documents, one file over. What follows a colourless
#   accent is a colourless icon: chroma to zero, each pixel keeping the
#   PERCEIVED lightness it was drawn at, which once the colour is gone is the
#   whole of the drawing. The plate lands within 2 CIE L* of where it was drawn
#   on all five family members.
#
#   ⚠ MONOCHROME TAKES EVERY COLOUR IN THE ICON, not just the two palettes the
#   hue path knows by name. Leaving a foreign hue alone protects its meaning
#   while there is another hue to move it to; in monochrome it just leaves a
#   grey icon with coloured crumbs on it — synstudio's three aperture blades,
#   and a fringe of antialias ramp around every teal mark. What protects a third
#   party's icon is the GATE, where it has always been: syn-resolve-gui measures
#   0% house violet and never reaches the transform at all.
#
#   ⚠ THE TEAL DETAIL HAD TO GIVE WAY ALONG A DIFFERENT AXIS. The knobs, the pad
#   bars and the disk LED step off a colliding accent by 45 deg of OKLab hue
#   (COLLIDE) — and greyscale has no hue circle to step around. Desaturated
#   where it was drawn the detail lands 6 CIE L* from the violet plate: not a
#   detail, a smudge. So it steps in LIGHTNESS, by 0.215 of OKLab L = 25 CIE L*,
#   which is the step 453 already put between the monochrome accent (#FFFFFF)
#   and its secondary (#B8B8B8) — this desktop's own answer for how far apart
#   two greys have to be. Measured on the rasterized icons: the knob goes
#   #4ec9b0 → #e6e6e6.
#
#   ⚠ AND THE LIFT IS WEIGHTED, BECAUSE SECOND_WINDOW ASKS ABOUT HUE ALONE.
#   That was enough for a hue nudge and is not enough for a lightness move,
#   which is far louder on a picture with no other colour left: the antialias
#   ramp of synstudio's GREEN blade crosses the teal's window on its way into
#   the dark opening — 27 pixels, in an icon with no teal in it anywhere — and a
#   flat lift speckles the iris. detail_weight() scales it by hue distance AND
#   chroma, so the impostors move under 4 L* while the mark moves 17.
#
#   `meson test -C build iconhue` — pure, no compositor, and ⛔ NOT the full
#   `meson test`, which has re-themed the live desktop. Six new checks, the two
#   load-bearing ones being r == g == b to the byte (the red failure above) and
#   the dark green in the teal's hue window staying where it was drawn.
# 455: a fourth stock wallpaper — St. Louis at night, the Gateway Arch over the
#   Mississippi, Daniel Schwen's Commons photograph under CC BY-SA 4.0. It goes
#   in data/wallpapers/, which is now documented as THE third-party directory
#   (our own art is data/synapse-*.png through meson's install_data), with the
#   grant for every file in it recorded in data/wallpapers/WALLPAPERS.md.
#   ⚠ Shipped BYTE-FOR-BYTE upstream's, and that is a licence decision, not
#   laziness: CC BY-SA 4.0 makes a modified copy an ADAPTED work, which we would
#   then owe under CC BY-SA 4.0 ourselves and have to mark as changed. A
#   verbatim copy owes attribution and nothing else. The antiquity collages were
#   re-encoded because they were 23 MB of PNG; this is already a 1.35 MB JPEG at
#   3000×1929, so there was nothing to win. Crop or re-encode it and that whole
#   paragraph stops being true.
#   ⚠ Attribution has to SHIP — a repo file the user never receives does not
#   satisfy §3(a) — so WALLPAPERS.md installs to /usr/share/licenses/synui/, and
#   the package() loop now filters to image extensions so that same .md does not
#   also get dropped into /usr/share/backgrounds. wppick filters on extension
#   and would have drawn no row for it, so that would have shipped unnoticed.
# 456: the noir cut of the same photograph, velle's greyscale grade of it, as a
#   fifth stock wallpaper. ⚠ Unlike 455's it is an ADAPTED work under CC BY-SA
#   4.0, so the licence is inherited rather than chosen: §3(b) forces the grade
#   to CC BY-SA 4.0 as well, and §3(a) still wants Daniel Schwen credited for
#   the underlying photograph and the modification INDICATED — which is what the
#   new section of data/wallpapers/WALLPAPERS.md is for. 455's "ship it
#   verbatim" reasoning does NOT carry over: there was no exemption left to
#   lose, so this one is re-encoded (4.03 MB PNG → 1.21 MB JPEG, q92 4:4:4,
#   44.70 dB, max deviation 16/255 — the same band the antiquity collages
#   measured). The master PNG's alpha was uniformly opaque and carried nothing.
#   ⚠ 4:4:4 is NOT about sharpness here. Chroma subsampling on a black-and-white
#   picture introduces a faint tint, and syn_palette_from_pixels() would then
#   find a HUE in it and paint the desktop off a colour that is not in the
#   photograph. The shipped file measures HSL saturation max = 0 — still r == g
#   == b to the byte.
#   The accent answer is grey, and that is correct: the palette returns
#   ok = false ("nothing worth taking") while wallpaper.c still sets
#   wp_measured = true because it DID look, and that pair is what routes it to
#   syn_palette_monochrome() instead of the theme's own accent.
#   ⚠ It encodes as a ONE-COMPONENT greyscale JPEG — the encoder picks that by
#   itself for a grey image — so syn_decode_jpeg() is asking libjpeg for
#   JCS_EXT_BGRA from a grayscale source. Verified against the real decoder in a
#   harness (3000x1929, CAIRO_FORMAT_RGB24, stride 12000), not assumed.
# 459: SYN_GLASS_PANEL_DEFAULT 55 -> 100 — the house glass level, for the
#   desktops that reach Prism by PICKING it rather than by being installed.
#   The constant's own comment has always said it is syn-install's number
#   written twice ("one of them has to survive a synuirc that predates the
#   key"), and syn-install 93 writes 100 into a fresh install's synuirc: the
#   two Prisms would otherwise have looked like two different themes depending
#   on which door you came through. syn-install's layout_test asserts the two
#   files agree, so this cannot drift again silently.
#   ⚠ IT REACHES THE THREE GLASS PRESETS AND NOTHING ELSE. Only theme_is_glass
#   consults it (syn_glass_resolve's last arm), so the eleven others keep the
#   opacities they were tuned with. On a Prism or macOS 26 desktop with no
#   glass_level chosen, the panel ladder goes from 0.835 of each panel's
#   designed alpha to 0.70 — glassier, which is the point, and still the
#   ladder rather than an absolute so a dense table stays readable next to a
#   menu. panel_glass_test reads the constant rather than the number and needs
#   no change; the arithmetic it checks is unchanged.
# 460: the dock's clock, apps grid and power mark were washed out on Prism
#   Light and fine on Prism. Reported off two screenshots of the same desktop,
#   and they were: those marks are drawn by synui rather than pulled from an
#   icon theme, in `panel_ink` — the colour the theme chose to read on
#   `panel_bg` — and for fifteen presets that WAS the surface the mark landed
#   on, because every pale preset drew a SOLID dock.
#   ⚠ 459 IS WHAT BROKE IT, and the shape is worth keeping. Prism Light is the
#   first preset that is pale AND glass; at the house dock_opacity of 0.05
#   there is no surface left under the mark at all, so its #1A1D24 lands
#   straight on a night photograph. Measured on the stock wallpaper under a
#   centred pill: 1.40:1, against dark Prism's 5.78:1 on the same pixels. One
#   ink happened to suit the picture and neither of them ever asked. At the old
#   0.72 the same theme measures 8.66:1 — the body was carrying it.
#   syn_mark_ink() in contrast.c is the rescue, beside the ink rules the clear
#   bar already runs on: composite the surface at its alpha over the backdrop
#   at BOTH extremes (the worst cell, not the mean — render.c's alpha walk
#   learnt that one the hard way), keep the theme's ink wherever it still
#   clears, and otherwise take the two-colour answer syn_ink_for_backdrop
#   gives. The analog clock's hands go with it, mixed toward the new ink in
#   quarter-steps so #00727E keeps its hue instead of being thrown away.
#   ⚠ A RESCUE, NOT A POLICY. The trip point is AA large text (3.0) and NOT
#   CONTRAST_TARGET, deliberately: a dot grid at 3.2:1 is a mark somebody
#   chose, at 1.4:1 it is a mark nobody can find, and asking for 4.5 would
#   start repainting dark themes' docks black over a bright photograph. It also
#   refuses to act unless the replacement is strictly better, so the mid-tone
#   band where both inks fail changes nothing rather than flickering under a
#   live wallpaper. dock_ink_test drives all fifteen presets over a night and a
#   bright wallpaper: the eleven solid-dock presets are untouched on both, and
#   only the three glass ones are ever rescued — macOS 26 on a dark wallpaper
#   and Prism on a bright one are the same bug, found by writing the test.
#   ⚠ The five dock model tests gained a `wallpaper_backdrop_for_box` stub
#   answering UNMEASURED, which is the value that hands the theme's ink back
#   unchanged — so every geometry assertion in them is still made against the
#   colours it was written for.
# 461: panel text was illegible on glass, and the two correctors that exist to
#   stop that were both sitting out. Reported as the emoji picker's
#   unhighlighted categories and the keyboard shortcuts panel being hard to
#   read; measured off the screenshot, on stock Prism with the panel over a
#   white web page, the surface composites to L=0.135 and on it the accent
#   lands at 1.52:1, the hint line (INK_DIM) at 1.26:1 and the count
#   (INK_LABEL) at 1.70:1 — while the panel's own INK cleared 4.75:1.
#   ⚠ ONE ROOT CAUSE, AND IT IS A SHARED FIRST LINE. syn_contrast_fix() and
#   syn_ink_floor() both open `if (lum <= SURFACE_PALE) return`, because both
#   were written when a panel surface was the THEME's opaque colour: dark
#   (nothing to do) or pale (correct downward). A glass panel's effective
#   surface is neither — it is the theme's colour composited over whatever the
#   panel opened on top of, which over a bright window is the middle. Both
#   returned at the first line and every derived colour kept its dark-theme
#   tuning on a mid-tone surface.
#   ⚠ AND THE ALPHA WALK CANNOT SEE IT. panel_alpha_floor() raises a glass
#   panel until the FULL-STRENGTH ink clears CONTRAST_TARGET and asks about
#   nothing else, so it settles on a surface chosen to suit exactly one of the
#   colours the panel draws. That is why the descriptions read and the key
#   combos did not.
#   syn_glass_restore() + syn_ink_floor_glass() in contrast.c, on one rule:
#   EVERY COLOUR REACHES AT LEAST THE CONTRAST IT WOULD HAVE HAD ON THE THEME'S
#   OWN OPAQUE SURFACE, capped at its ordinary target. Relative and not
#   absolute, which is what makes it safe — on an opaque panel the composite IS
#   the theme's surface, so the goal is met by construction and nothing moves;
#   on glass it restores precisely what the glass took away and never asks for
#   more than the theme itself delivers. An absolute 4.5 would demand more of a
#   glass Gruvbox than Gruvbox gives on its own panels, which is the "repaint
#   four working dark themes to settle a complaint about two light ones" that
#   panel_contrast_test forbids. It also runs in whichever direction has room;
#   syn_contrast_fix() only ever darkens, which offers a dark theme nothing.
#   ⚠ syn_ink_floor_glass() SCANS UPWARD FROM ref_level AND MUST. The ladder is
#   not monotonic against a mid-tone composite: the panel's own background
#   colour has real contrast of its own there (2.87:1 on this surface), so a
#   scan from zero returns 0 and clamps nothing. The first draft did exactly
#   that and panel_legibility_test caught it — the accent was fixed and every
#   dim rung was left where it was. A bisect would be worse: it assumes one
#   crossing and can settle on either side of the dip.
#   Measured result on the reported panel: accent 1.52:1 -> 4.50:1 (and
#   #D66318 comes back #E2905B — a lighter orange, not a grey, because the
#   channels scale together), INK_DIM 1.26:1 -> 2.47:1 with the floor lifting
#   that rung from 0.44 to 0.70. The emoji picker's unhighlighted categories
#   are the same INK_DIM and are fixed by the same line.
#   panel_legibility_test is the new gate: 15 themes x 5 backdrops x 4 tuned
#   alphas, the alpha walk modelled FIRST (leaving it out is what made the
#   first draft report every glass case as broken), then every text rung, the
#   accent and the four status colours — 6000 pairs, 450 of them on glass. Its
#   second claim is the load-bearing one: on an OPAQUE panel every theme must
#   come back bit-for-bit.
# 462: the shortcut palette (Super+/) is a hotkey MANAGER now. Three gaps, one
#   cause: it listed the bind table, so the only rows it had were the chords
#   that already existed.
#   ⚠ REMOVING A SHORTCUT WAS A ONE-WAY DOOR — the reported bug. Unbind a chord
#   and the row naming it is not in the table any more, so the list it is
#   derived from cannot show it and the way back was to hand-edit synuirc. It
#   only ever happened as a SIDE EFFECT (F3 takes the chord when it puts a row
#   on the tap), which is why nobody hit it on purpose.
#   The list is "what a key could go on" now, of which the bind table is one
#   quarter (syn_sc_kind_t): BOUND, UNBOUND (every action with no chord),
#   APP (the .desktop scan the start menu already does), COMMAND (the query
#   itself). Delete frees a chord and the row STAYS as its UNBOUND self, which
#   is the only reason offering a delete is safe; F2 on any keyless row CREATES
#   rather than moves.
#   Two things fall out for free. The actions that never had a default chord —
#   printers_scan, cascade, launcher_style — are reachable for the first time.
#   And "give this app a key" and "give this command a key" stop needing a text
#   editor: type `firef` and F2, or type a whole command line and F2.
#   ⚠ ctlpanel_shortcuts_ex()'s `include_unbound` IS A PARAMETER, NOT A SECOND
#   FUNCTION. The control panel's column is read-only and wants the chords that
#   exist; the palette assigns keys and needs the ones that do not. A copy of
#   that walk with thirty extra rows is the drift ctlpanel_shortcuts() was
#   written to end. action_desc()'s table moved to file scope for the same
#   reason: it was a lookup, it is also the ROSTER, and a second roster goes
#   stale.
#   ⚠ AN OVER-LONG Exec= IS SKIPPED, NOT TRUNCATED. A .desktop Exec can be 255
#   bytes against SYN_BIND_ARG_LEN; snprintf would cut it and the key would run
#   a command line missing its tail, silently, for the few applications with
#   long ones. A row that is not offered is honest.
#   ⚠ Enter on a query that matches nothing now RUNS it, where it used to do
#   nothing. That is the command row and it is the feature, not a regression —
#   but keys_test asserted the old behaviour and now asserts the new one out
#   loud, because it is the one change here somebody could be surprised by.
#   ⚠ AND keys_test WAS ALREADY RED BEFORE ANY OF THIS: 7 of its 156 checks
#   looked for a tap row reading "Start menu", and action_desc() has rendered it
#   "Start menu (bar menu)" since start_menu_style became a setting. Stale
#   assertions, not a code bug — fixed here as TAP_DESC, spelt once, because a
#   test that hardcodes a setting-derived string in seven places goes stale in
#   seven places. 189 checks green now, from 156 with 7 failing.
# 463: the dock's right-click menu — the app's own rows moved to the BOTTOM,
#   and the menu stops hiding behind an auto-hiding dock.
#   The app block (Unpin, New Window, Close Window, Quit All Windows) was
#   first. It is one to four rows depending on whether the icon is pinned,
#   whether the app has an Exec and whether it is running, so every setting
#   below it moved by up to four positions from one icon to the next and
#   "Auto-hide Dock" was never twice in the same place. Below the settings, the
#   dock's rows start at the top of the menu on every icon and on the bar body
#   alike — and since dockmenu_place() opens the popup ABOVE the cursor on a
#   bottom dock, the LAST rows are the ones nearest the pointer, which is
#   sitting on the icon those rows are about. Quit All Windows is now the last
#   row in the menu. The separator moved with the block: left where it was it
#   would have ruled off the settings from nothing.
#   ⚠ THE SECOND HALF IS A REAL STACKING BUG, reported as the menu vanishing
#   behind the dock for a moment and coming back once the dock shrank — on an
#   AUTO-HIDING dock only. synui_render_dockmenu() raises the menu when it
#   draws, which was enough while nothing raised the dock afterwards. An
#   auto-hiding dock is always on top (dock_on_top_here), and the slide is an
#   ANIMATION: dock_apply_position() runs once per frame for its length and
#   every one of those frames put the dock back over the menu it had just
#   opened. It reappeared when the slide ended and the ticking stopped — which
#   is exactly "until the dock shrinks", and why a pinned dock never showed it.
#   Fixed by re-raising the menu at the END of dock_apply_position() rather
#   than by not raising the dock: a sliding dock does have to be over the
#   windows it is sliding across. The rule is the ORDER of the two, and that
#   function is the one place the dock's depth is set.
#   ⚠ The stacking test needs the menu a TREE OF ITS OWN. dock_options_test
#   stubs wlr_scene_tree_create() to hand every caller the same fake_tree, so
#   with the real plumbing the dock's node and the menu's would be one pointer
#   and "which was raised last" would be unobservable — the assertion would
#   have passed against the bug. Proven by reverting the fix: the two stacking
#   checks fail, and pass again with it back.
#   Eleven new checks in dock_options_test, all about ORDER, because reordering
#   four lines is a one-minute edit and the reason they are where they are does
#   not survive in a diff. ⚠ Two of them needed menu_last_at(): an icon menu
#   carries TWO separators now — one above the dock's placement rows, one above
#   the app block — and menu_at() answers with the wrong one.
# 464: the bar takes third-party widgets, in OMARCHY'S shell-plugin format.
#   Their desktop is one long-lived quickshell process in which the bar, the
#   panels and the overlays are all plugins; this bar is quickshell too. That
#   makes their format the only one already describing "a QML widget you can
#   drop into a quickshell bar", so synui reads it rather than growing a second
#   directory layout for the same idea — a widget written once loads on either.
#   A plugin is a directory with a manifest.json and some QML. Searched in
#   order: ~/.config/omarchy/plugins (theirs, FIRST, so a plugin installed with
#   `omarchy plugin add` is found without copying it), ~/.config/synui/plugins,
#   /usr/share/synui/plugins.
#   ⛔ AN ARBITRARY OMARCHY WIDGET WILL NOT RUN HERE, and the whole design is
#   about saying so rather than showing an empty space. Their widgets root at
#   BarWidget — 45 lines of contract, portable, reimplemented (not copied: it is
#   somebody else's project under somebody else's licence) as
#   quickshell/Ui/BarWidget.qml — but they also `import qs.Commons`, whose
#   Style.qml alone is 23KB of API, and several `import Quickshell.Hyprland`,
#   which speaks to a compositor socket synui does not have. synui-plugins
#   refuses those BEFORE the bar sees them, naming the import, and refuses to
#   write `on` for one: a state file claiming a plugin is enabled with nothing
#   on the bar is the failure this exists to prevent.
#   ⚠ THE qs.* CHECK IS ASKED OF THE FILESYSTEM, NOT A HARDCODED LIST.
#   quickshell resolves `import qs.Foo` to <shell root>/Foo, so whether an
#   import can be satisfied is a question about which module directories this
#   bar ships. A list would have refused qs.Ui — which IS provided, and is where
#   BarWidget lives — or kept allowing a module after it was deleted.
#   ⚠ Ui/ NEEDS ITS OWN qmldir OR NOTHING IMPORTS IT. Same trap the top-level
#   qmldir's note describes, one directory down: without it every plugin fails
#   with "BarWidget is not a type" from a package that installed cleanly.
#   ⚠ Peer lookup goes through the Plugins SINGLETON because a Bar cannot see
#   its siblings — the Variants that creates them is in shell.qml, so there is
#   no id to reach and no parent to walk. That is what makes BarWidget's
#   broadcast() mean anything on a multi-monitor desk; without it a widget
#   refreshes one screen and leaves the others stale. Registration builds a NEW
#   array rather than push()ing: reassigning the same object notifies no QML
#   binding.
#   Each plugin is instantiated in its own Loader, first in the right-hand run
#   so it never displaces the clock or the tray. Third-party QML in the bar's
#   own process: a Loader isolates a failure to that widget, where instantiating
#   inline would take down the surface you would use to fix it.
#   data/plugins/synapse.uptime is the worked example, shipped OFF like every
#   plugin — a plugin system whose list is empty until you have already written
#   one teaches nobody the format. 21 checks in tests/plugins.sh, all inside a
#   scratch HOME: the real plugin directories are the user's, and a test that
#   could enable one on the machine running it would change the desktop it is
#   testing.
# 465: cliamp would not open from the start menu. It is a terminal application,
#   so its .desktop carries Terminal=true and a launcher owes it a terminal —
#   and the menu left that to quickshell's DesktopEntry.execute(), which looks
#   for one the way GLib does: down a list compiled into libgio (xterm, konsole,
#   gnome-terminal, …) that syntty, kitty and foot are not on and never will be.
#   That list is the whole reason /usr/bin/xdg-terminal-exec exists in this
#   tree; execute() does not consult it.
#   ⛔ AND IT FAILED SILENTLY, which is why it went unnoticed on every other
#   Terminal=true entry too: a menu row that launches nothing looks exactly like
#   one whose program crashed on startup.
#   StartMenu.qml handles runInTerminal itself now, through syntty — which is
#   what every static row in that same file already hardcodes, and what
#   appgrid_launch() has always done, so all three doors open a CLI program the
#   same way. Uses quickshell's parsed `command` argv, so it stays
#   argv-not-a-shell-string.
# 466: the analog clock widget is frameless, and resizable by its corner.
#   Frameless first: `chrome: false` drops the card, the header rule and the
#   "CLOCK" label, so what is on the desktop is the dial and nothing else. A
#   clock face is already a bounded, self-evident object — round, obviously a
#   clock, with no reading that needs a caption — so the frame was doing what a
#   frame is for on the widgets that need it (a column of numbers has to be told
#   where it ends) and nothing at all here. `inkOnBackdrop` matters MORE with the
#   card gone: the hands are hairlines straight onto the wallpaper now.
#   ⚠ THE DIAL IS THE ONLY WIDGET THAT CAN HONESTLY RESIZE, which is why
#   `resizable` is opt-in on WidgetFrame rather than on by default. Every part of
#   a dial is a fraction of its own box, so a bigger one is the same drawing at a
#   bigger size; a reporting widget has a FONT SIZE rather than a dimension, and
#   stretching its card would put the same glyphs in more space.
#   ⚠ EVERY STROKE WIDTH IN onPaint HAD TO BE SCALED. The radii were already
#   fractions of `r` and scaled for free; the widths were absolute pixels — a 3px
#   hour mark and a 10px neon glow are right at the designed 148 and are
#   hairlines at 420, a blob at 96. One `k` (the ratio to the designed radius)
#   through a `px()` floored at one device pixel, so every literal keeps meaning
#   exactly what it did and nothing was re-tuned. Left alone the resize would
#   have produced a bigger clock that looked progressively worse, which reads as
#   the resize being broken rather than as line widths being fixed.
#   ⚠ WidgetLayout.place() NOW MERGES INSTEAD OF REPLACING, and that is the
#   fragile part of this change. It built the entry from scratch, so dragging a
#   clock you had resized threw the size away mid-gesture and the card snapped
#   back to its designed one. resize() takes the same care in the other
#   direction. Verified by hand both ways — resize-then-drag keeps the size,
#   drag-then-resize keeps the position — and there is no automated cover for it,
#   because testing QML's own JS would mean a JS engine in synui's test suite for
#   ten lines, which is a build dependency this distro should not grow for that.
#   ⚠ The resize grip is BOTTOM-LEFT while the drag grip is top-right, so the two
#   18px targets can never be adjacent however small the card gets — and it is in
#   the input mask, because a Region that omits it is a corner that lights up on
#   hover and cannot be pressed.
#   Size is stored in widgets.pos beside the position, and a widget that has
#   never been resized has no `size` and takes its designed one — the same rule
#   the position takes, so a later change to a default reaches everyone who never
#   touched it.
# 467: `auto` was not auto, which is the only reason 459 had to write a glass
#   level into a fresh install's synuirc. Two halves were missing, and the
#   desktop that came out of them was three quarters of a design.
#   ⚠ THE PANELS FELL THROUGH TO A LADDER WHERE AN EXPLICIT LEVEL RETURNED AN
#   ABSOLUTE. syn_glass_resolve()'s auto arm handed back a multiplier on each
#   panel's designed alpha; the explicit arm handed back syn_glass_bar_alpha().
#   So `auto` and `glass_level = 100` produced visibly different desktops on the
#   same theme. Auto is now defined as the curve at SYN_GLASS_PANEL_DEFAULT and
#   nothing else — one call, shared with the explicit arm through
#   syn_glass_bar_alpha_at(), because a second copy of that arithmetic is how
#   they came to disagree in the first place.
#   ⚠ AND THE DOCK HAD NO WAY TO ASK ITS THEME AT ALL. Every other surface
#   resolves one — the bar through theme_bar_alpha(), the windows through the
#   preset's own opacity pair, the panels through syn_glass_resolve() — and the
#   dock kept its compiled 0.72 whatever was on screen. theme_dock_alpha() is
#   the question it was missing and dock_opacity takes -1 = "the theme decides"
#   like bar_opacity always has; SYN_DOCK_ALPHA_DEFAULT is that same 0.72 for
#   the eleven presets that are not glass.
#   ⚠ THE TWO PRISMS ONLY, NOT EVERY GLASS THEME. macOS 26's BAR asks for
#   nothing at all, but a dock BODY at 0.00 does not read as a clear dock: the
#   icons are painted over it at full opacity, so the backdrop blur masks to the
#   ICONS and frosts each one instead of the slab. Tahoe's dock stays a pane.
#   theme.state now carries the dock's resolved alpha when the row is genuinely
#   unset, because the widgets are quickshell and take the dock's number
#   verbatim — without it they sat at their built-in 0.72 beside a dock the
#   compositor had drawn at 0.05. Guarded on `< 0` so it can never override a
#   number the user put in settings.state, which theme.state is read before.
#   Verified with a hermetic probe against the real parser: auto and
#   glass_level = 100 now resolve identically on Prism (bar 0.05, dock 0.05,
#   panels 0.05) while Gruvbox keeps 0.72 and no glass at all. syn-install 94
#   puts the fresh-install synuirc back to `glass_level = auto` and drops both
#   the window opacity and the pin it needed — with no level set, both Prisms'
#   own 0.90/0.84 preset pair stands on its own.
# 468: ⛔ THE BAR DID NOT LOAD AT ALL, from 464. The plugin registration added a
#   second `Component.onCompleted` to Bar.qml, which already had one, and QML
#   refuses a property assigned twice by failing the WHOLE type:
#   `Property value set multiple times` -> `Type Bar unavailable` -> no bar and
#   no start menu, from a package that built and installed cleanly.
#   Folded into the handler that was already there. ⚠ CAUGHT BY bar_shape.sh,
#   which loads the real QML tree — and missed when 464 shipped because the
#   tests run for it (plugins, dock_options, keys) are all pure C and none of
#   them loads a line of QML. Any change under quickshell/ needs one of the
#   bar_*.sh tests run against it, not the unit suite.
# 469: Omarchy's own bar widgets actually run now, and there is something to
#   browse and install them with.
#   468 shipped the FORMAT and refused anything that reached past BarWidget,
#   because their widgets also import qs.Commons and several import
#   Quickshell.Hyprland. Measured rather than assumed this time: across the eight
#   bar widgets they ship, the whole qs.Commons surface is ELEVEN Style
#   properties, two Color and two Util — and five of the eight fail on one shared
#   type, WidgetButton.
#   ⚠ A SHIM, NOT A VENDORED COPY, AND THAT IS A DESIGN DECISION RATHER THAN A
#   LICENCE ONE. Omarchy is MIT so copying Style.qml would be perfectly legal; it
#   is 23KB carrying THEIR spacing scale, THEIR font tokens and THEIR palette, so
#   a widget dropped on this desktop would look like a piece of Omarchy sitting
#   on SynapseOS. What a widget wants from Style is "how big is body text here" —
#   a question this desktop already answers. Same names, synui's answers.
#   ⚠ Color takes Theme.barFg/barAccent and NOT fg/magenta: those are the FOLDED
#   answers that already resolve the clear-bar case to ink measured off the
#   wallpaper. Reading fg directly would hand a widget the palette's colour on a
#   clear bar — the unreadable text that measurement exists to prevent. The bar
#   hands over its own per-strip ink through `bar.barForeground`, which a
#   singleton cannot express: it has one value for a desktop with one answer per
#   monitor and per module.
#   ⚠ WidgetButton is REIMPLEMENTED and that one is practical, not courteous:
#   theirs calls into THEIR host for tooltips, click registration and an
#   indicator reveal, so a verbatim copy would be a file full of calls this bar
#   does not have. Every one of those calls is guarded in their own file, which
#   is what makes a host that offers none of them a supported case.
#   ⛔ THE REFUSAL NEEDED NO EDIT. It asks the filesystem — quickshell resolves
#   `import qs.Foo` to <shell root>/Foo — so qs.Commons became hostable the
#   moment the directory existed. That is the whole reason it was not a
#   hardcoded list, and plugins.sh now asserts the rule against a module nothing
#   provides instead.
#   Verified at RUNTIME, not by lint: tests/plugin_load.sh starts a headless
#   synui, starts the real quickshell tree with a plugin enabled, and reads the
#   bar's log. The fixture is Omarchy's own Spacer verbatim (MIT, © DHH, notice
#   inline) because a widget written to pass proves nothing about compatibility.
#   ⚠ Its ERROR filter drops ONE line by name — pipewire has no daemon in a
#   headless session — rather than the whole service, so every other error a
#   plugin can cause is still fatal. The needle is `ERROR` and not `Error:`.
#   `synui-plugins browse|add|remove` is the manager half. `add` takes a git URL
#   or a catalogue id; a SHIPPED id is one widget out of a repository holding
#   many, so it is a partial + sparse checkout of that one path rather than a
#   clone of their whole desktop. (A REGISTRY id — see pkgrel 473 — is a
#   repository that IS one plugin, and that one is a plain shallow clone.)
#   ⚠ THEIR MANIFEST IS <Base>.manifest.json, not manifest.json — their bar
#   widgets share a directory so each carries its own. The installer renames on
#   the way in, and that is the only shape difference between "a widget in their
#   repo" and "a plugin".
#   ⚠ THE LICENCE IS IN THE SPARSE-CHECKOUT SET. A checkout of the widget path
#   alone leaves the repo root empty, so the LICENSE the copy loop looks for is
#   not on disk and the copy silently does nothing — which would put somebody
#   else's MIT code on a user's machine with no notice. Caught by installing one
#   and looking.
#   ⛔ `remove` only deletes out of ~/.config/synui/plugins. The other two search
#   paths are Omarchy's (theirs, with their own command) and the package's
#   (pacman's); a remove reaching either deletes a file somebody else owns, and
#   on the packaged one the next upgrade puts it back.
# 470: the plugin manager gets its other two faces. Every SynapseOS app ships a
#   GUI and a CLI, and a TUI where it fits — synpkg, synfiles and syn-edit all
#   do, and a tool with one face is one half the desktop cannot reach.
#   `synui-plugins gui` is a quickshell window like synpkg's: FloatingWindow as
#   the ROOT rather than a ShellRoot holding one, because `onClosed` is the
#   window's signal — on a ShellRoot it matches nothing, the process outlives the
#   window and every later launch exits 0 having drawn nothing. Reads theme.json
#   AND palette.state, so it wears the wallpaper's accent like the bar does
#   rather than the preset's; ⚠ `ok` and `use` both have to hold before that
#   colour is used, which is the check the bar needed in 386.
#   ⚠ QS_APP_ID=synui-plugins on the exec, or the window inherits the identity of
#   whatever spawned it and gets no dock entry — every one of these apps hands
#   its whole environment to what it starts.
#   `synui-plugins tui` is the same list with arrow keys. ⛔ Its cleanup restores
#   the cursor and leaves the alternate screen on EVERY exit path including
#   SIGINT: a TUI that dies with the cursor hidden leaves the terminal unusable
#   and the person fixing it cannot see what they type. It refuses outright
#   without a tty rather than silently doing nothing.
#   ⚠ `catalogue` is a SEPARATE subcommand from `browse`, not a --tsv flag on it,
#   for the reason `scan` is separate from `list`: one is laid out for a person
#   and the other is parsed, and a formatter doing both does neither well.
#   Both new faces render the same two lists the window does — what is on disk
#   and what the catalogue offers — merged by id, so a plugin cloned from a git
#   URL and a catalogue entry are one row rather than two.
#   The window is verified by RUNNING it headless and reading the log for
#   "Configuration Loaded"; the TUI by driving it through a pty and reading what
#   it drew. Neither is proven by qmllint or by `bash -n`.
# 471: config.c documents all 187 synuirc keys. It documented 84.
#   ⚠ OVER HALF THE PARSER WAS UNDISCOVERABLE, and not the obscure half: the
#   whole of xkb, every animation, every blur and shadow knob, the lock screen,
#   the screensaver, most of the pointer, the power timeouts and the lid. A key
#   with no line in that header is a key nobody outside this file can find —
#   the panels reach some of them and nothing reaches the rest.
#   Written from EVIDENCE rather than from memory: each key's own parser arm
#   (which is the authority on its values and clamps), its struct field comment,
#   its control-panel label and help text, and its compiled default. Nothing was
#   described that could not be sourced from one of those four.
#   ⚠ CHECKED BOTH DIRECTIONS. Every documented enum value is asserted against
#   the values the parser actually accepts, and every documented clamp against
#   the numbers it actually clamps to — and, the half that matters more, nothing
#   is documented that is NOT a real key. A reference that invents a setting is
#   worse than one that omits it.
#   ⛔ super_space is documented as OBSOLETE AND IGNORED, because that is what it
#   is: the arm logs a line and does nothing. A key that silently does nothing is
#   the one thing a config reference must never leave unsaid.
#   The wiki's new Configuration page is derived from this header and names all
#   187 as well. If the two ever disagree, this file is right.
# 473: the bar-plugin browser browsed five widgets. That was every row in the
#   catalogue this package ships, and the catalogue was hand-written — so the
#   window, the TUI and `browse` were all faithfully showing a list nobody had
#   any way to grow. The format has around nine hundred community bar widgets
#   written for it, listed at omarchyplugins.com, and none of them were reachable
#   from here. Two dozen of those are GAMES — Tetris, Snake, Minesweeper, 2048,
#   Wordle, solitaire — which is most of what a person opens a widget browser
#   hoping to find.
#   `synui-plugins refresh` fetches their catalogue; data/plugins/registry.py
#   reduces it to the twelve columns catalogue.tsv already had, so a row is a row
#   whichever list it came from and the shipped one wins on a collision. Cached
#   under ~/.cache/synui/plugins, refetched at a week old, fetched on first use.
#   ⛔ EVERYTHING KNOWABLE FROM A LISTING IS DROPPED AND NO MORE: not a bar
#   widget, their own harness says it fails, the repository has its own
#   installer, it is a monorepo the installer cannot address. 869 of 1,229
#   survive. Whether one can actually RUN here is not a listing's business — that
#   is answered at install time, by name, by the refusal check.
#   ⚠ THE `trust` COLUMN EXISTS SO THE WINDOW CANNOT LIE. `shipped` means loaded
#   into a real bar before it was listed; `verified` is THEIR word about THEIR
#   desktop. One badge for both would be this project vouching for code it has
#   never run.
#   A soft check joins the hard one: `import qs.Ui` passes because synui ships a
#   qs.Ui, but theirs has thirty-odd types and ours has two, so a widget can
#   clear the refusal and still name `BarIconButton`. `add` now runs Qt 6's
#   qmllint against the entry point with the bar's modules on the import path and
#   NAMES what it could not resolve — a warning, not a refusal, because a type
#   can be named on a path nothing runs.
#   ⛔ /usr/lib/qt6/bin/qmllint, never /usr/bin/qmllint: the one on PATH is
#   another Qt's, prints nothing and exits 0 — a check that always passes.
#   ⚠ AND IT NEEDS A `qs/` SYMLINK FARM. quickshell resolves `import qs.Foo` to
#   <shell root>/Foo, which no import path reproduces; without it our OWN shipped
#   example fails the check, which is the setup being wrong and not the widget.
#   ⛔ `IFS=$'\t' read -r a b c` IS WRONG ON THIS DATA and was the bug that broke
#   the first install. Tab is IFS WHITESPACE, so a run of tabs collapses to one
#   delimiter and every EMPTY field vanishes, shifting the rest left. A registry
#   row is three empty columns in the middle; the read handed the category to
#   `ref`, `add` believed it had a sub-path, and a clone tried to be a sparse
#   checkout of a directory that does not exist. `cut -f` keeps empty fields.
#   ⚠ ONE `scan` PER RUN, cached in a temp file. It walks every search directory
#   and reads every manifest, and the browsers ask it a question per row — at
#   five rows that was invisible, at nine hundred it is a window that looks hung.
#   ⛔ THE TESTS READ THE CHECKOUT, NOT /usr/share. Both the catalogue and the
#   filter default to the installed copies, and an installed copy is the LAST
#   RELEASE: without the overrides the suite proves things about the package
#   already on the machine and passes green over a broken tree. It bit during
#   this change. 40 checks now, and not one byte over the network — the registry
#   URL is an override and curl reads file://.
# 474: three widgets installed off the new browser went on and never appeared,
#   and the browser had said "installed and on" for every one of them. The cause
#   was not the browser. synui's qs.Ui had TWO types — BarWidget and
#   WidgetButton — and Omarchy's has thirty-two, so most of the nine hundred
#   widgets the registry lists were written against a module this desktop did
#   not have. So qs.Ui and qs.Commons are now IMPLEMENTED, over this desktop's
#   theme: the same type names and contracts, drawing SynapseOS's font, spacing
#   and ink rather than theirs.
#   Measured against 40 of the most-installed community widgets — 9 of 40
#   resolved before, 39 of 40 do now. The staging came off the measurement and
#   not off taste: +BarIconButton took it to 18, +the Panel layer to 22, +Button
#   and the toggles to 32, +Dropdown and the fields to 40 (entry points; 39 of
#   40 whole plugins). tools/plugin-compat.sh is that measurement, and it is
#   deliberately NOT a meson test — it clones off the network.
#   ⛔ A MISSING TYPE IS LOUD AND A MISSING PROPERTY IS SILENT, which is why
#   Style.qml and Color.qml were built from a COUNT over the real corpus rather
#   than written to taste. `import qs.Ui` naming a type nothing provides refuses
#   to load and says which; `Style.spacing.md` where there is no `md` is
#   undefined, lays out as zero, and draws a widget that is present, running,
#   enabled and invisible. That IS the bug this release is fixing — two of the
#   three dead widgets sized themselves `implicitWidth: button.implicitWidth`
#   against a BarIconButton that did not exist.
#   ⛔ BarIconButton ALONE DOUBLED THE NUMBER. 21 of the 40 name it.
#   ⚠ KeyboardPanel is a LAYER SURFACE, not an xdg-popup: an xdg-popup only gets
#   keys after a click has routed focus through its parent, so a
#   keyboard-summoned panel would ignore every key. Its focus is primed
#   Exclusive→OnDemand because OnDemand is granted on first map but NOT to a
#   still-mapped fade-out surface reopening, nor over a client holding a pointer
#   constraint; and focus follows `open`, never `visible`, or the keyboard is
#   locked out for the length of the fade.
#   ⚠ The bar's host contract gained `position` — a panel handed nothing opens
#   downwards off the bottom of a bottom bar.
#   Border is a shim: theirs is 242 lines because most of it reads a user token
#   file synui has no counterpart for, so this is those defaults behind the same
#   function names.
#   ⚠ tests/qs_module.sh guards the module as a unit — every file resolves every
#   name, and qmldir agrees with the directory in BOTH directions (a type
#   written but never listed does not exist to an importer, however complete its
#   file is). ⛔ It needs a `qs/` SYMLINK FARM: quickshell resolves `import
#   qs.Foo` to <shell root>/Foo and no import path reproduces that. Without it
#   our OWN shipped example fails, which is the tell that the harness is wrong
#   rather than the code. ⛔ /usr/lib/qt6/bin/qmllint, never /usr/bin/qmllint.
#   `synui-plugins check` reports the same thing for what is already installed,
#   and a degraded install now exits 3 rather than 0 — the window only showed a
#   message on a non-zero code, which is why it reported three dead widgets as
#   installed and on.

# 483: `bar_opacity`/`dock_opacity = auto` are real now, and the ISO's synuirc
#   and the installer's heredoc were switched to them from a hardcoded 0.05 —
#   a number that read as a CHOICE in the control panel for a row nobody had
#   touched. The bar's half already worked (theme.json's barAlpha, written on
#   every login regardless of theme.state);
#   the dock's did not — BarConfig.dockOpacity had no theme-aware fallback at
#   all, just a flat compiled 0.72, so a genuinely `auto` dock_opacity would
#   have left the desktop WIDGETS solid beside a dock the compositor already
#   draws at Prism's 0.05 in-process. theme.c now hands synui-apply-theme
#   theme_dock_alpha() the same way it already hands it theme_bar_alpha(); the
#   helper writes it into theme.json's new `dockAlpha`; Theme.widgetAlpha
#   resolves it exactly the way barAlphaAsked already resolves barAlpha —
#   BarConfig.dockOpacity's own default moved from 0.72 to the same -1
#   sentinel bar_opacity uses, so "unset" can be told apart from "chosen
#   0.72" for the first time. Pinned by a new apply_theme_dock_alpha.sh,
#   mirroring apply_theme_bar_alpha.sh.

# 484: the bar's own right-click menu grew a Plugins section — a checkbox per
#   hostable plugin, plus ▲▼ to reorder it, without opening the separate
#   Plugin Manager window.
#   `synui-plugins <id> up|down` is new: it writes plugins-order.state, one id
#   per line, and scan() now reorders every row to match before Plugins.qml
#   ever sees them — scan_dirs' own order was a directory listing (effectively
#   alphabetical by plugin folder name) and nobody had chosen it. An id never
#   reordered is appended after the ones that have been, in scan_dirs' own
#   order; an id removed from disk is silently dropped rather than leaving a
#   gap, and reinstalling it under the same id returns it to where it was.
#   Plugins.qml gained setEnabled()/moveUp()/moveDown(), all execDetached
#   (never a shared Process — PostItState.qml and PluginHost.qml both carry
#   the same note: `running = true` on an already-running Process is a silent
#   no-op, not a queue, and clicking two rows before the first exits would
#   drop the second) — and a FileView on plugins-order.state beside the
#   existing plugins.state one, so a reorder reaches the bar with no IPC.
#   ⚠ ONE LIST FOR THE WHOLE DESK, NOT PER OUTPUT. Every row above it in the
#   menu is per-monitor (bar.json, keyed on the output), but plugins.state has
#   no notion of an output and every bar reads the same file — a plugin on for
#   one monitor and off for another was never a distinction anyone asked this
#   menu for, so the new rows write and read the SAME state regardless of
#   which monitor's menu is open.
#   ▲▼ rather than a drag handle: a popup that closes on a click outside it
#   and re-anchors on every reopen is a bad home for a drag gesture. Refused
#   (unhostable) plugins are not offered a row at all — nothing a checkbox
#   here could turn on would ever draw.
#   Pinned by a new plugin_order.sh, beside plugins.sh's own refusal/enable
#   coverage the way apply_theme_dock_alpha.sh sits beside apply_theme_bar_
#   alpha.sh — one file, one question.

# 485: the bar's right-click menu (and the mixer popup) were closing
#   themselves during ordinary use — open, glance away to read it, gone a
#   bit over a second later. Both hold a real xdg_popup grab (click outside
#   closes them; that part was fine) but each also carries a pointer-leave
#   Timer meant only as a BACKSTOP for a refused grab. The timer does not
#   know whether the grab succeeded, so it closed the menu itself on any
#   ordinary pause with the pointer resting off the popup — 1200ms on
#   BarMenu, 1600ms on Mixer, both far short of how long a person reads a
#   menu or nudges a slider before checking back. Reported as "closes
#   itself before I can read it," confirmed live (two open/close cycles in
#   well under 18 seconds, nothing in the compositor log at either close),
#   and reproduced with a real xdg_popup grab via tests/vpointer_click.c in
#   a headless instance: the menu stayed open 15-20s with no interaction
#   once actually grabbed, so the backstop firing on ordinary hover-away —
#   not a broken grab — was the whole bug. Both timers moved to 8000ms:
#   long enough that no ordinary pause trips it, short enough to still
#   recover if a grab really is ever refused.
# 487: reordering a plugin from the bar's right-click menu was slow — a click
#   on ▲ or ▼ took about half a second to show, and flickered the whole row of
#   plugin widgets while it did. Two full scans per click and a rebuild of
#   every widget, for a swap of two rows the bar was already holding:
#   `synui-plugins <id> up` had to scan (a walk of every plugin directory, a
#   read of every manifest, a grep of every entry point) to work out what the
#   current order even was, wrote the file, and the bar's own watch on that
#   file then ran the WHOLE SCAN AGAIN to find out what the write had done.
#   Three things, none of which changes what a reorder MEANS:
#   * Plugins.move() reorders `all` in the frame of the click and tells the
#     script the finished order, through a new `synui-plugins order <id>...`
#     that writes the file and nothing else — 6ms against 240ms, and no
#     read-modify-write to race a second click. `up|down` stay exactly as they
#     were: they are a MOVE within an order the script has to work out, and
#     the CLI has no list in hand to state. The write is coalesced (80ms) so a
#     run of clicks is one child, not one per arrow.
#   * The bar's plugin Repeater draws a ListModel of ids (Plugins.activeModel)
#     rather than the `active` array. ⛔ A Repeater OVER A JS ARRAY REBUILDS
#     EVERY DELEGATE WHEN THE ARRAY IS REASSIGNED — it has no per-entry
#     identity, so "one widget moved" and "thirteen different widgets" look
#     alike to it. Every scan therefore reloaded every plugin on every bar:
#     the weather widget refetched, vitals' timer restarted, tetris forgot its
#     board. syncModel() moves the ITEM instead, and a scan that changes
#     nothing now costs no model operations at all — which is most of them,
#     since every file watch asks for one.
#   * And the bar no longer rescans on a write that only says what it is
#     already showing. The test is the FILE against `all`, not a flag saying
#     "that write was mine": an `up` typed in a terminal, a hand-edited file,
#     or our own write landing after something else reordered `all` all
#     disagree with it, and all of them still rescan.
#   ⚠ The neighbour a row swaps with is the one the MENU shows. `all` carries
#   plugins this bar cannot host, which the menu filters out and the bar never
#   draws, so a plain swap with i-1 could trade places with a row nobody can
#   see — one click, nothing moves. Unsupported rows are stepped over and keep
#   their own place in the file.
#   Pinned by a new tests/plugin_reorder.sh, which moves a plugin over the
#   `plugin` IPC target (new `up`/`down` there — a headless session has
#   nothing to click with, the same reason `toggle` is on it) and counts what
#   the bar spends: one call to synui-plugins, no scan, no widget reloaded,
#   and an order written from a terminal still reaching the bar.
# 488: MANGOHUD=1 IS NO LONGER EXPORTED FOR THE WHOLE SESSION (velle: "mangohud
#   is always causing issues it seems… make that configuration smarter so it's
#   not an issue but still there in games when it needs to be"). MangoHud's
#   Vulkan manifest declares enable_environment MANGOHUD=1, so that one
#   variable loaded VK_LAYER_MANGOHUD_overlay into EVERY Vulkan client in the
#   session — a game, a browser, a video player, a QML app that touched
#   QtMultimedia, a test. On AMD the layer segfaults the client inside its own
#   vkCreateDevice hook and on NVIDIA it never does, so it had already taken
#   the live wallpaper (409), synstudio (0.1.0-16) and synstudio's own test
#   suite down on the ThinkPad while every run on the dev desktop passed, and
#   each fix was another DISABLE_MANGOHUD=1 in another launcher. The next
#   Vulkan client to arrive would have been the fourth.
#   The overlay now comes from the launcher, which is the only place it can be
#   turned on anyway — nothing can inject an overlay into a process that is
#   already up. synui-game-run already wrapped with mangohud + gamemoderun, and
#   everything it starts inherits the variable, so ONE wrap covers a library:
#   `syn game steam`, `syn game -- wine foo.exe`, `syn game -- ./game.x86_64`.
#   `syn game` is a new front door on it (syn 0.1.0-24) that supplies the `--`.
#   ⚠ AND THE OLD BEHAVIOUR IS ONE LINE AWAY, because on a machine that has
#   never seen the crash it is genuinely convenient: `syn game hud on` writes
#   MANGOHUD_EVERYWHERE=1 to ~/.config/synapseos/mangohud.conf, read at the
#   next login. The machine's own default lives in the new
#   /etc/synapseos/mangohud.conf (shipped saying 0, in backup=), and a user's
#   answer beats it in both directions.
#   ⛔ THE RULE IS WRITTEN IN THREE PLACES — the ISO's profile.d and the two
#   session paths syn-install writes — and the installer's own comment already
#   said "this path must stay in step with it". tests/mangohud_session.sh
#   checks the rule and that all three agree; DISABLE_MANGOHUD=1 stays in
#   wpengine and synstudio, so opting back in cannot re-break them.
# 497 THE WELCOME SCREEN IS A GUIDE NOW, AND IT IS QML.
#   What it replaces: render.c's synui_render_welcome() — a 513px column of
#   nineteen labels and their chords, drawn in cairo by the compositor, with
#   ~330 lines of drawing here and ~150 more of arrow/click/wheel handling in
#   input.c. Everything wrong with it was structural:
#   ⚠ IT WAS A LIST, NOT A GUIDE. Nineteen doors in one scroll with no room to
#     say what any of them was for, so "Neural Overlay" and "Cat Mode" arrived
#     side by side unexplained and the only way to find out was to press them.
#     Someone meeting the desktop for the first time met a menu.
#   ⚠ ITS KEY COLUMN WAS A HARDCODED rgba(0.45, 0.45, 0.55) — a fixed blue-grey
#     no theme could move, under 3:1 on the panel this desktop actually draws.
#     Present and unreadable, which is what "the secondary colour is too dark"
#     was. The chip reads Theme.fgDim now: the ink 65% of the way from the
#     surface to the foreground, recomputed per theme.
#   The guide is quickshell/welcome.qml + welcome/: seven pages (welcome, the
#   keys, layouts, make it yours, the AI, everything else, you are set — the
#   layouts page arrived in 593), a rail that doubles as the contents page, a
#   description under every row, and the "Don't show again" checkbox in the
#   footer of every page rather than stranded on the last one.
#   ⚠ IT IS ITS OWN QUICKSHELL, NOT A WINDOW IN THE BAR, and that is the whole
#   design. TWO BARS SHIP — `bar_shell = synapse|antiquity` — and a guide inside
#   the SYNAPSE bar would simply not exist for anyone running the other one; the
#   panel it replaces was drawn by the compositor, so every configuration had
#   it. As a second ENTRY POINT into the same QML tree it still gets Theme.qml,
#   the picked UI font and the glass namespace with `import ".."` and copies
#   none of them. It also costs nothing when closed (dismissing it quits) and
#   does not reappear every time game mode restarts the bar.
#   ⚠ AND IT CLOSES ITSELF WHEN A WINDOW OPENS. synui_main.c used to hide the
#   panel on the first map; the guide is full-screen, so one left standing
#   covers the window it just opened. Guide.qml watches ToplevelManager, behind
#   a short arm — every window that was already open arrives as an insertion one
#   event-loop turn after startup and would otherwise close it before it drew.
#   (497 shipped that arm at 1.5s with the wrong reason attached; see 499.)
#   ⚠ THE CHORDS COME FROM `synctl binds`, which is new (ipc.c). The old menu
#   carried a hand-typed chord per row and said so at length: the command bar
#   has been on Super+Space, on Super+=, and back, and each move left that
#   column naming the old one. Rendered compositor-side by ctlpanel_combo_str(),
#   so the guide, the shortcuts palette and the control panel print a chord the
#   same way and a rebound key needs no edit anywhere.
#   welcome.state did NOT move: synui still owns the setting (`welcome_at_startup`
#   is a synuirc key with a control-panel row), the guide only READS the file and
#   asks for a change with `synctl dispatch welcome_startup`. One writer.
#   synui-welcome(1) is the launcher and the CLI: toggle/show/hide/page N, with
#   the toggle done by asking a running instance first and starting one only
#   when nothing answers — closing quits, so "closed" and "not running" are the
#   same state and there is no third one to get wrong.
# 498: the three comments that say a quickshell PanelWindow with
#   `focusable: true` takes EXCLUSIVE keyboard focus. It does not —
#   WlrKeyboardFocus is None=0/Exclusive=1/OnDemand=2 and it reads back 2, so it
#   is ON-DEMAND. Measured on the live seat 2026-08-15; PostIt.qml and
#   WidgetFrame.qml were corrected then and StartMenu.qml, input.c (×3) and
#   synui_main.c were not.
#   ⚠ IT WORKS ANYWAY, AND THE REASON IS THE POINT: layer_surface_map() grants
#   the keyboard to ANY interactivity that is not NONE, at map and nowhere else.
#   That is also the waybar failure those comments are about (NONE set once at
#   startup and never revised) and it is still the first thing to check when a
#   layer surface is deaf.
#   ⛔ AND 497's WELCOME GUIDE ARGUED FROM THE WRONG FACT. Its comments said the
#   guide must close when a window maps because it holds the keyboard and would
#   starve that window. The opposite is true: focus_view() notifies the new
#   toplevel unconditionally, so the GUIDE goes deaf. The behaviour is unchanged
#   and correct — a full-screen panel covering the window you just asked for,
#   which you cannot type into either — but the reason in the file was wrong,
#   and a wrong reason is what gets deleted by the next person who checks it.
# 499 THE WELCOME GUIDE'S ARM TIMER, WITH THE RIGHT REASON ON IT.
#   497 held the ToplevelManager watch for 1.5s and said it was for "the login
#   burst", because `autostart` defaults to a terminal. IT DOES NOT. config.c's
#   compiled-in `syntty` is the fallback for finding NO config file at all, and
#   opening any synuirc zeroes the list before parsing ("Config file found —
#   reset autostart"). Every install ships /etc/synui/synuirc, so on a real
#   desktop nothing autostarts unless somebody asked for it. The fallback bites
#   in exactly one place — a hermetic test rig with no synuirc — which is the
#   trap tests/bar_radius.sh already documents.
#   ⛔ REMOVING THE TIMER ON THAT READING BROKE IT, and the check that said it
#   was safe was a bad control: `interval: 1` is not "no timer", it is a timer
#   that still covers the same event-loop turn. The real behaviour, probed:
#       PROBE completed t=0 count=0
#       PROBE active    t=1 -> syntty
#       PROBE insert    t=1 appId=syntty
#   `ToplevelManager.toplevels` is EMPTY at Component.onCompleted and the
#   windows that were ALREADY OPEN are inserted a turn later — they look exactly
#   like new ones. Unguarded, the guide closes instantly on any desktop that is
#   not empty, which is most of them.
#   So the timer stays, at 400ms: slack over one event-loop turn rather than a
#   number chosen to outlast something imaginary. Every millisecond of it is
#   also a window in which a genuinely new window is ignored, which is the
#   reason to keep it short.
#   ⚠ tests/welcome_guide.sh NOW ASKS BOTH HALVES. It opened a window only
#   AFTER the guide, so it could see the watch failing to fire and not the guard
#   failing to hold — it passed the whole time the guard was gone. It opens one
#   BEFORE as well now (ok 5a), and fails without the arm.
# 500 LEAVING FULLSCREEN STOPPED THROWING THE WINDOW AT ANOTHER MONITOR.
#   velle, laptop plugged into a television: "when it's unfullsized it's
#   jumping the window back to the main display for no reason."
#   ⛔ THE UN-FULLSCREEN PATH ASKED A QUESTION ABOUT *OPENING* A WINDOW.
#   view_apply_fullscreen's else-branch re-derived the placement from scratch —
#   layout_float_place → layout_restore_geometry → windows.conf. That table
#   records where an app was WHEN IT LAST CLOSED, in ABSOLUTE layout
#   coordinates, so it names a monitor as much as a position, and
#   layout_restore_geometry re-homes the view onto the monitor that box's centre
#   lands on. Right for a window that is opening. For a window that has been on
#   screen for an hour and merely came out of fullscreen it is a teleport to
#   wherever that app was last shut down — on a laptop, the built-in panel,
#   which is precisely the screen the video had been moved off.
#   view->fs_geo now records the box (and therefore the SCREEN) on the way in
#   and the way out puts it back. Its own slot, not a fourth tenant of
#   saved_geo: fullscreen is the one state that NESTS — a maximized or snapped
#   window can go fullscreen and has to come back to both boxes in turn — and
#   view_apply_maximized refuses to run at all while fullscreen, so it cannot
#   hand its slot over the way maximize/snap/expand hand it to each other.
#   THREE WAYS OUT, AND THEY FAILED DIFFERENTLY — tests/fullscreen_output.sh
#   asserts all three, two headless outputs and a windows.conf seeded at the
#   FIRST one (the seed is the trap, and it is also what makes the window
#   hand_placed without synthetic input, the same seam move_output.sh uses):
#     1. a FLOATED window — the reported jump, straight through
#        layout_float_place.
#     2. hand-placed on a FLOATING desktop — view->floating is 0 there (the
#        DESKTOP is floating) and layout_float_arrange steps over hand-placed
#        windows, so NOTHING resized it: it silently kept the full-output box.
#        Same family as 436/439 — "does a LAYOUT own this window's position?"
#     3. a TILED window — the layout still owns the box, but it does not own
#        the SCREEN, so it is re-homed before layout_apply rather than after.
#   layout_restore_geometry's own re-home/clamp is now the shared
#   view_place_saved_box() it always was, one copy instead of two.
#   AND THE LAPTOP'S SCREEN KEY WORKS: XF86Display (Fn+F7 on a ThinkPad) cycles
#   display_mode — extend / duplicate / built-in off. `external` has existed
#   since 376 and re-applies itself on every hotplug, but the only ways to reach
#   it were the control panel and Super+D — a menu you have to open ON the
#   screen you are trying to switch off. `display_mode` was also missing from
#   ctlpanel.c's action table, so it appeared in neither the shortcut palette
#   nor the control panel's Shortcuts column: a real action nobody could find.
# 501 THE SCREENSAVER PANEL WAS THE ONE PANEL WITH NO CHROME.
#   velle: "screensaver screen isn't following the theme quite the same, more
#   transparent than the rest." Both halves of that are right, and neither is a
#   colour.
#   synui_ui_apply_chrome() walks a hand-written roster of panels, setting the
#   theme's corner radius on each background rect and hanging a backdrop blur
#   behind it. `saver` was not on that roster and never had been — so the
#   screensaver settings panel came up SQUARE among rounded ones, with nothing
#   frosted behind it, and at the 0.94 alpha every panel shares that means you
#   read the window underneath straight through it. The alpha was never the
#   problem; it is the same 0.94 the control panel uses.
#   ⚠ IT IS INVISIBLE FROM INSIDE THE PANEL. Nothing fails, nothing logs, and
#   the panel looks perfectly reasonable on its own — it only reads as wrong
#   beside another one, which is why it survived from 352 to here.
#   tests/panel_chrome.sh diffs the roster against every `*_ui.bg` in render.c.
#   Two panels are legitimately absent, `overview` and `appgrid`: both are a
#   full-screen dim the size of the output, so rounding one would cut a
#   transparent notch out of each corner of the SCREEN and the blur would be
#   sampling the very thing it is drawn over. That is the ONLY excuse, the test
#   holds both of them to it (the rect must actually be ob.width × ob.height),
#   and appgrid's exclusion is now written down instead of merely true.
#   ALSO: the bar-plugin manager window (data/plugins-gui.qml) ignored the
#   desktop font. uiFont was the literal "monospace" and all nineteen
#   pixelSizes were literals, so it kept whatever Qt resolved at startup while
#   every other window in the suite followed ~/.config/synui/font.state — one
#   window in the middle of the settings, off on its own. It watches the file
#   now, with the family named on every Text and every size through root.ui().
#   ⚠ BOTH HALVES OR NEITHER: Qt resolves an application's default font ONCE at
#   startup and QML cannot change it afterwards, so each has to be a BINDING.
#   Three checks in tests/plugins.sh, including "no pixelSize escapes ui()" —
#   the failure is silent, a window drawn perfectly at the wrong size.
# 502 THE UPTIME BAR PLUGIN DREW IN THE WRONG FACE. It named no font family,
#   so it kept whatever Qt resolved at startup while every module beside it in
#   the same strip followed the picked font — a plugin that looks foreign for
#   no reason a plugin author could see, in the file that is also the worked
#   EXAMPLE for writing one.
#   ⚠ IT ASKS THE HOST, and does not read font.state itself. Bar.qml already
#   exposes `fontFamily: Theme.fontFamily` right beside the `vertical` and
#   `barSize` a widget reads off the bar; the plugin simply never used it. That
#   keeps one reader of the file and costs the plugin contract nothing — the
#   read is guarded, so an Omarchy host with no such property yields "" and the
#   widget still loads there.
#   ⚠ AND IT DOES NOT TAKE THE TEXT SCALE. A bar widget's size comes off the
#   BAR's thickness so it fits the strip on any desktop; a percentage meant for
#   window text would push it out of the bar. tools/preflight.sh gates the
#   desktop font repo-wide now and holds bar plugins to the family only.
# 503 THE DESKTOP SCALES. One setting makes ALL of it bigger — the
#   compositor's own panels, every application, the cursor — together, and
#   drawn at the larger size rather than magnified.
#   velle, 2026-08-26: "it's for disabled, i need the entire os to scale
#   together. however other systems handle it is fine." Other systems handle it
#   with an OUTPUT SCALE (GNOME's Display ▸ Scale, macOS's Displays), and so
#   does this.
#   ⚠ THE MECHANISM WAS NEVER MISSING, WHICH IS WHY THIS LOOKED LIKE A BIG JOB
#   AND WAS NOT. wlroots has scaled since the port; output_persist.c has read
#   and written a `scale` key the whole time; wlr_fractional_scale_manager_v1
#   is created in synui_main.c, so 125% and 150% are as crisp as 200% instead
#   of being drawn at 1x and stretched. Verified before writing anything, by
#   setting scale 2 with wlr-randr in a nested synui: the whole desktop came
#   back at 2x and SHARP. What did not exist was any way for a person to set
#   it — no config key, no panel row, no dispatch verb, reachable only by
#   installing wlr-randr and typing a command, which for the setting that
#   decides whether the desktop is legible is the same as not having it.
#   So: `display_scale` (bare steps, `+`/`-` step, a number sets),
#   Super+Ctrl+= / Super+Ctrl+- / Super+Ctrl+0, a Control Panel ▸ Display row
#   "Scale everything", and `-`/`+` in Super+D for ONE monitor. The panel shows
#   each screen's scale beside its logical size, accented when it is not 100%.
#   ⚠ EVERY SCREEN, deliberately. "Make the desktop bigger" is one intent, and
#   a desk where the shortcut grew one monitor of three has not done it — it
#   has made the desk inconsistent and left the user to find the other two.
#   ⛔ AND IT REFUSES TO CLOSE ITS OWN ESCAPE HATCH. The Displays panel is 990px
#   wide and the control panel 860, both laid out in columns at fixed offsets,
#   so below ~1010 logical they do not clip — they put their values and their
#   key legend off the screen. Those two are exactly what somebody uses to undo
#   a scale they regret, and the person most likely to reach for a large scale
#   is the least able to read a tiny screen to escape one. A step that would go
#   under 1010x620 is refused and SAYS SO, because a key that silently stops
#   working reads as broken. That floor is the panels' size and moves when they
#   learn to reflow, not before.
#   ⚠ NOT font.state's `scale`, and the control panel now says so in both rows.
#   That one sizes text inside the suite's own QML windows and can reach
#   neither a cairo panel nor Firefox. Two real settings, neither a spelling of
#   the other.
#   ⚠ SYN_BINDS_MAX WAS 96 AND THE DEFAULTS ARE 78. The four new binds took the
#   table to within a handful of full and settings_test caught it — over the
#   cap config.c logs "bind table full" and a user's synuirc line silently does
#   nothing. Raised to 192; headroom is the point of the number.
#   tests/display_scale.sh: two headless outputs (with one, "every screen" and
#   "the focused screen" are indistinguishable), the logical size actually
#   shrinking by the factor rather than merely the number being recorded,
#   outputs.conf carrying it, the refusal, and the way back.
# 504 THE LOGIN SCREEN SHOWS THE LOCK SCREEN'S BACKGROUND. velle: "login
#   screen should be the same setting as lock screen for background." It is one
#   setting — lock_background / lock_image / lock_dim / lock_blur — and it now
#   decides both screens. There is NO greeter_background key and there must not
#   be one; the published file below is a cache of an answer, not a second
#   question.
#   ⛔ THE REASON IT WAS NOT ALREADY IS A PERMISSION BOUNDARY, NOT AN OVERSIGHT,
#   and that is what decides the whole design. greetd runs `synui --greeter` as
#   the unprivileged `greeter` account, whose home is `/`:
#     - syn_config_path() resolves to `//.config/synui/…`, so config load falls
#       through to /etc/synui/synuirc, which carries no lock_* lines at all —
#       which is exactly why the login screen has always been black while the
#       lock screen showed a wallpaper;
#     - and pointing it at the user's config would NOT help, because a home
#       directory is 0700. Neither the synuirc nor a wallpaper under it is
#       readable by uid 963 — and the shipped default IS
#       ~/.config/synui/wallpaper.png.
#   ⚠ SO THE PATH IS NOT ENOUGH; THE PICTURE IS COPIED. Publishing only the
#   settings would work for the wallpapers in /usr/share and fail silently for
#   every picture a user actually chose — the worst kind of bug, one that looks
#   fixed on the developer's box.
#   The session writes /var/lib/synui/greeter/<uid>/{background.conf,.img}; the
#   greeter reads the directory belonging to the account it is about to log in.
#   The root is 1777 (sticky, like /tmp) from a tmpfiles.d rule, so a session
#   writes it with no privilege and no prompt — a background that needed
#   authenticating is a background nobody keeps in step, and the setting would
#   quietly become two settings again. Same shape GNOME uses for login-screen
#   avatars (/var/lib/AccountsService/icons), for the same reason.
#   ⚠ THE READER IS PRE-LOGIN CODE AND IS WRITTEN LIKE IT. It stats the
#   directory and requires it to be owned by that account (sticky makes a
#   mismatch unlikely, not impossible, and "cannot happen" is not a check); the
#   `image` value is a BARE NAME resolved against that directory, so a doctored
#   file cannot aim a pre-auth image decoder at a path of somebody's choosing;
#   the copy is capped at 64 MiB and lands by rename, because a half-written
#   JPEG is a decode failure that shows as a black login screen with nothing in
#   any log a user will read.
#   ⚠ THE PUBLISH HOOK WAS WRONG FIRST, and silently: output_layout_changed()
#   looked like the obvious choke point and does NOT fire at startup on a
#   default desktop — a new output reaches dispcfg_outputs_changed(), which
#   returns immediately when the arrangement is EXTEND and the panel is closed.
#   It is wallpaper_output_created() now: an output that has just been painted
#   is the earliest moment the background resolves. Also on wallpaper_reload()
#   (lock_background=desktop follows the wallpaper) and on a Super+Z row change.
#   Re-copying is compared by path+size+mtime, or plugging in a monitor would
#   move megabytes.
#   tests/greeter_bg.sh puts the wallpaper inside a 0700 directory ON PURPOSE —
#   a rig using /usr/share would pass on a build that only published the path.
# 505 THE TASK MANAGER'S PERCENTAGES WERE TWO SCALES UNDER ONE WORD.
#   velle: "percentages never seem accurate", with a panel reading
#     CPU  13%  (12 cores)        and        synui   82.1
#   ⚠ NOTHING WAS ARITHMETICALLY WRONG, which is why this survived: measured
#   against /proc at the moment of the screenshot, synui really was using 72%
#   of ONE CORE and the machine really was 11.7% busy. The meter is percent of
#   the WHOLE MACHINE (0-100). The column multiplied by ncpu(), which is top's
#   convention — 100% means one core saturated, so on this desk the column can
#   add to 1200. Two numbers labelled the same way, six lines apart, answering
#   different questions.
#   The meter cannot move (percent of the machine is what a CPU meter means),
#   so the column did: it is percent of the machine now, and the rows sum to
#   roughly what the meter shows. GNOME's system monitor and Windows' task
#   manager both read this way for the same reason.
#   ⚠ WHAT THAT LOSES, THE COLOUR NOW CARRIES. "This process is pegging a core"
#   is real information and it is 8.3 here, 50 on a two-core laptop — so a
#   threshold on the printed number would mean something different on every
#   machine and on a big one would never fire. render.c scales the warn/crit
#   hues by ONE CORE'S WORTH (100/ncpu) instead of by 100.
#   ⛔ AND THE FIRST FRAME WAS A THIRD ANSWER AGAIN. taskmgr_show() clears the
#   baselines on purpose, so the opening sample had nothing to difference and
#   fell back to the LIFETIME AVERAGE — total jiffies over the process's whole
#   life, which is what ps prints and is not a rate at all. The list therefore
#   opened sorted by "who has burned the most CPU since it started", reliably
#   synui since login, and rearranged itself a second later into what was
#   actually busy. The first frame is the one a person reads and screenshots.
#   Now the priming sample measures nothing and SAYS so (-1 draws a dash), and
#   the panel re-polls after TASKMGR_PRIME_MS = 150ms instead of a full second.
#   ⛔ NOT a sleep: this is the compositor's main thread, and blocking it to
#   make a task manager look tidy is how the app grid froze the desktop (457).
#   tests/taskmgr_cpu_test.c pins the SCALE — every case names a core count,
#   because the whole bug is a factor of ncpu and is invisible on a one-core
#   box. Verified to fail on the old line: "all twelve cores" came back 1200%.
#   It links taskmgr.c whole rather than copying the formula; a copy would go
#   on passing after the shipped line changed, which for a scale bug is exactly
#   the failure mode.
# 506 THE BAR SHOWS WAITING UPDATES. A new module beside the tray: a count and
#   an icon while SynapseOS updates are pending, one click to the Updates
#   window, and nothing at all when the machine is current — a row that reads
#   "0 updates" all day is a row that gets ignored on the day it says something
#   else, which is why Recording and GameMode are not permanent furniture
#   either.
#   ⚠ IT DOES NO CHECKING OF ITS OWN, and that is the design. A git fetch per
#   bar is a git fetch per MONITOR (this module is instantiated once per
#   screen), running inside the shell process, where a network stall is a
#   stalled bar. `syn-update ping` does the work from a systemd user timer and
#   leaves the answer in ~/.cache/syn-update/pending; this reads that file, and
#   WATCHES it rather than polling — the writer renames into place, which is
#   what makes watching safe (a FileView on a path written in place sees it
#   empty for a frame, and the badge would blink to nothing every time the
#   timer fired).
#   ⚠ TWO SWITCHES, AND THEY ARE NOT DUPLICATES. The timer is network traffic —
#   how often the machine asks upstream, `syn-update ping --every 6h` /
#   `--off`. This module is furniture — whether the bar shows the answer, an
#   ordinary BarConfig key in the bar's right-click menu, per monitor, beside
#   the tray and the clock. Neither is a copy of the other and neither belongs
#   in the other's file.
#   `held` is counted separately and only in the tooltip: a component the user
#   pinned is not work waiting to be done, and putting it in the badge would
#   make the bar insist on something nobody is going to do.
#   ⚠ THE STATE PARSE WAS DOUBLE-ESCAPED AND FAILED SILENTLY on the first
#   build: `\\s` in the QML source is a literal backslash in the pattern, not
#   whitespace, so every field came back 0, `pending` was 0, and the module
#   simply stayed hidden — a feature that looks like it was never wired up.
#   Caught only by rendering a bar against a seeded state file and looking.
# 507 A POINTER THAT WILL NOT HOLD STILL. Two settings on Control panel ▸ Input
#   ▸ Pointer, for a cheap 1000 DPI wireless mouse whose sensor rattles enough
#   that the cursor never quite sits where it is put.
#   `accel_profile = default|flat|adaptive` is libinput's acceleration CURVE,
#   which synui had no control for at all. ⚠ IT IS NOT `accel_speed`, and that
#   is the whole reason it had to exist: accel_speed scales whichever curve the
#   device is already on, so turning the speed up never turns acceleration on
#   and turning it down never turns it off — somebody asking for "acceleration"
#   and finding only a speed slider is looking at the wrong control. Adaptive
#   raises the gain with hand speed, which is what makes a slow movement land
#   precisely and a fast one still cross the screen. The request is checked
#   against `accel_get_profiles()` and not against `accel_is_available()`: a
#   device can offer a speed and still support only one curve, and libinput
#   answers an impossible profile with UNSUPPORTED rather than by picking
#   something — a config line that silently does nothing.
#   `pointer_smoothing = 0..10` is synui's own, because LIBINPUT HAS NO
#   SMOOTHING TO ASK FOR. A low-pass filter over the cursor's path, in
#   input.c's relative motion handler.
#   ⚠ A LEAKY BUCKET, NOT AN AVERAGE OVER THE LAST N REPORTS. An average feels
#   smooth and quietly eats the tail of every movement: the pointer lands a
#   little short, every time, and the complaint that comes back is "it doesn't
#   go where I aim" — the exact thing smoothing was turned on to fix. Here every
#   delta goes into a pending total and each event pays out a fraction of it,
#   so nothing is ever discarded; a settle timer applies the remainder one frame
#   after the reports stop, because otherwise the last fraction has no event to
#   ride out on and the bias is permanent.
#   ⚠ THE FRACTION COMES FROM ELAPSED TIME, NOT PER EVENT. A fixed per-event
#   fraction makes the setting mean different things on different hardware — a
#   heavy filter on a 125 Hz office mouse and no filter at all on a 1000 Hz
#   gaming one, on the same desktop, from the same number. Solving for a time
#   constant (strength x 5 ms) makes one row mean one thing on both, which
#   matters here because this machine has one of each.
#   ⚠ THE CURSOR ONLY. The relative-pointer broadcast above it stays raw, so a
#   game holding a locked pointer still reads unfiltered motion — filtering a
#   player's aim from a row on the desktop's pointer page would be a symptom
#   with no discoverable cause. Absolute devices (tablets, a VM's passed-through
#   pointer) are exempt too: their "delta" is the distance to a position, and
#   damping it makes the cursor trail the stylus.
#   `synctl cursor` is new, and reports the pointer's layout position — the
#   readback tests/pointer_smoothing.sh needs to assert the property that
#   matters, which is that a smoothed stroke arrives LATE and never SHORT.
# 510 THE MOUSE STAYS IN THE GAME. A fullscreen game now keeps the pointer on
#   its own monitor, and the setting for it is Control panel ▸ Power ▸ "Keep the
#   mouse on the game's screen" (`game_confine_pointer = on|off`, default ON).
#   Capturing the pointer is normally the CLIENT's job: a game asks for a
#   zwp_locked_pointer or zwp_confined_pointer and constraints.c honours it.
#   Measured on Cyberpunk 2077 under proton-cachyos across three outputs, the
#   request never arrives — 16010 cursor samples over 59 s with the game holding
#   focus ranged over x 0..3639, y 75..2602, which is all three monitors. There
#   was nothing for constraints.c to honour, so no amount of work there could
#   have helped; game mode already knows which output the game is on and answers
#   for itself instead.
#   ⚠ HELD ONLY WHILE THE GAME HAS FOCUS. Alt-Tab, or any bind that moves focus,
#   frees the pointer at once and clicking back into the game takes it again —
#   the mouse can never end up stuck on one screen with no way off it. That is
#   also why this is safe to default on.
#   ⚠ The clamp is to the game's OUTPUT, not its window: a sub-native game is
#   letterboxed inside a larger frame by view_fullscreen_rescale(), and confining
#   to the window would stop the cursor short of the edge of the screen.
#   Two constraint bugs found alongside it, both able to drop a lock that a
#   better-behaved client HAD asked for: panel_pointer_claim_cursor() cleared
#   pointer focus without telling constraints.c, leaving active_constraint naming
#   a surface that no longer held focus (the same shape 508 guarded
#   pointer_rebase against); it now says so. tests/game_confine_test.c drives the
#   real gate and clamp against stubs — 18 assertions, no compositor.
# 511 THE BAR'S PIXEL SCAN WORKS ON A 24-BIT DISPLAY, AND STOPS ENTIRELY ON A
#   GAME'S SCREEN. Two faults in barscan.c, found while looking at something
#   else: the journal was carrying 16 "declined: unhandled read format
#   0x34324742" lines per scan, 2.5 scans a second, for a whole session.
#   ⚠ 0x34324742 is BGR888, and barscan handled only the 32-bit orders — so on
#   the 2560x1440 DP output that asked for it, EVERY read had been declining
#   for as long as the file has existed. Declining is silent by design: the
#   cell stays -1 and each consumer falls back to the wallpaper, which looks
#   perfectly fine, so the bar and every panel on that monitor had been inking
#   themselves against a picture nobody could see. A fallback that works is
#   what hides a lookup that does not. BGR888 and RGB888 are read now.
#   ⚠ AND ROWS ARE 4-BYTE ALIGNED, which only a 3-byte format can notice: GL
#   pads every row up to the next multiple of 4, so a stride of width*3 reads
#   each row at a creeping offset — an image that slides sideways as it goes
#   down, with no error anywhere. The stride is padded.
#   The scan is also skipped for an output a fullscreen game is on. Nothing
#   translucent is visible there to ink, so every read was a GPU readback
#   nobody looks at. Per-OUTPUT, not a global switch — a game on one monitor
#   says nothing about the bar on the other two.
#   ⚠ game_owns_output() is NOT game_pointer_box(): a game the user has tabbed
#   away from still COVERS its screen (barscan keeps skipping it) but has
#   released the pointer (the confine lets go). The two read almost the same at
#   a call site and mean different things; tests/game_confine_test.c asserts
#   the pair. tests/barscan_format_test.c checks each channel index against
#   drm_fourcc's own bit layout — a reversed one reports a wrong COLOUR rather
#   than failing, which is how this went unnoticed.
# 513 A GAME'S LETTERBOX BARS BELONG TO THE GAME, AND A ONE-PIXEL BAR STOPS
#   BEING LEFT THERE AT ALL. Cyberpunk 2077 lost the mouse seconds after every
#   launch — the camera moved an inch and stuck, and only tabbing out and back
#   freed it, for a while.
#   ⚠ MEASURED WITH THE HAND, NOT ONLY THE CURSOR. A cursor sample cannot tell
#   a working pointer lock from nobody touching the mouse: both read as a
#   frozen cursor, and 510 and 512 each concluded from one. Reading the mouse's
#   own evdev node alongside it — 643 samples with the game focused — the
#   cursor moved on every single sample the hand moved. There was no pointer
#   lock at any moment. That CORRECTS 512's "Cyberpunk does take a lock": the
#   19-second hold it measured was a loading screen.
#   ⚠ THE PICTURE WAS 2560x1438 INSIDE A 2560x1440 SCREEN. The buffer's aspect
#   is a shade wider than the monitor's, so view_fullscreen_rescale()'s fit
#   left a ONE PIXEL bar top and bottom. A bar belongs to no surface, so
#   surface_at() answers NULL on it, pointer_update_focus() clears pointer
#   focus, and the client stops receiving motion at all. Worse for an Xwayland
#   game: Xwayland only asks for a pointer lock while its surface holds pointer
#   focus, so a bar the cursor can reach is a lock the game can never take —
#   and looking down parks the cursor on the bottom edge of the picture, which
#   is exactly where that bar is. A bar nobody can see cost the whole pointer.
#   Fixed at both ends, because a real letterbox (4:3 on a 16:9 screen) is not
#   going away: a fit landing within 4px of the box now fills the box, a fifth
#   of a percent of stretch nobody can see; and surface_at() answers a
#   fullscreen client's own surface for a point on its bars, at the nearest
#   point of its picture — the same coordinate the confine would have clamped
#   to. Nothing else can be under them, and anything drawn above still wins the
#   scene walk first.
#   ⚠ THE CONSTRAINT LIFECYCLE WAS LOGGED AT WLR_DEBUG, which no shipped
#   session prints, so "did the client ever ask for the pointer" — the first
#   fork in every mouse-capture report — has never been answerable from the
#   journal. At INFO now: locked vs confined, oneshot vs persistent, activation,
#   deactivation with what it costs, and a line for a constraint that arrives
#   while its surface holds no pointer focus.
#   `synctl pointer` reports the live state instead of leaving it to be
#   inferred: every constraint on file, which one is active, whether it is
#   BOUND (an active constraint whose surface lost pointer focus is a lock that
#   has silently stopped locking), and game mode's own confine box beside the
#   game's drawn rectangle and its output — the gap between those two IS the
#   bar. view_scaled_content_box() (xwayland.c) is now the one owner of a
#   measurement that had grown three private copies.
#   tests/game_confine_test.c gains the letterbox cases and states the drawn
#   rectangle outright rather than building a scene to imply it.
# 514 ALT-TAB OUT OF A GAME AND IT IS STILL THERE. An exclusive-fullscreen X11
#   title UNMAPS its window every time it loses focus — that is what iconify is
#   in X11, and Cyberpunk 2077 does it on every single Alt-Tab: one
#   `X11 window mapped` line in the journal per focus flip, measured.
#   ⚠ TWO THINGS FALL OFF THE DESK WHEN IT DOES.
#   The view leaves its workspace list (those lists hold only mapped views, and
#   56 call sites across the tree rely on that), so from everything that lists
#   windows the game has ceased to exist — no dock entry, no Alt+Tab tile,
#   nothing in `synctl clients`. Steam is the only route back, which is what
#   "it does not come back when I Alt-Tab out" means. xw_unmap() now RECORDS
#   the unmap of a live fullscreen window as a minimise, and
#   alttab_candidates() reaches for those through s->xw_views — the one list an
#   unmapped X11 view is still on — on the same terms as any other minimised
#   window. alttab_reveal() then asks the client to restore (WM_STATE, activate,
#   restack) rather than trying to focus a surface that is not mapped, and
#   xw_map() clears the flag when the window really comes back. ⚠ The flag is
#   deliberately NOT cleared at the moment of asking: a client that ignores the
#   restore has to stay reachable, and unreachable-for-good is the bug.
#   And game_find_view() answered NULL for the same reason, so the 508 grace
#   timer ran out and game mode LEFT: synapd restarted, kmod events restored,
#   THE BAR RELAUNCHED — a whole desktop rebuilt because somebody looked at
#   their browser for six seconds, then torn down again on the way back. Twice
#   in three and a half minutes on the measured session. game_minimized_view()
#   counts an unmapped-but-alive fullscreen X11 view as the game still being
#   there. ⚠ Never an ENTRY condition — a window nobody can see keeps game mode
#   on but must not turn it on — and keyed on the X surface still existing, so
#   a game that really quit stops counting; xw_destroy() asks again for exactly
#   that, which it never used to do at all.
#   ⚠ CONSEQUENCE, deliberate: the bar now stays down while a game sits
#   minimised, the same as it already did for a game merely tabbed away and
#   still mapped. Game mode has always been about a game RUNNING rather than
#   focused; the six-second restore-then-kill was the accident.
#   Restricted to fullscreen windows throughout, so a client that merely closes
#   a window (Steam to its tray) behaves exactly as before.
#   tests/game_confine_test.c: 7 more assertions on the minimise lookup.
# 516: kitty is described as what it now is — something you ADD. It came off
#   the base set and the ISO in syn-install 100, so neither optdepend here is
#   installed with SynapseOS any more, and the line that recommended kitty read
#   as though it were still lying around. foot's line said it "works where
#   kitty needs OpenGL", which stopped being its reason the day syntty became
#   the default: syntty links no GL either. foot is a SECOND IMPLEMENTATION,
#   and that is what it is for.
# 517: the LOCK AND LOGIN SCREEN grew three things: what is playing, the
#   weather, and a keyboard-layout chip. They are one screen (greeter.c calls
#   synui_lock), so all three reach the login prompt too — and for the layout
#   that is the entire point.
#   ⚠ THE LAYOUT CHIP IS THE ONE THAT FIXES A BUG RATHER THAN ADDING A FEATURE.
#   `xkb_layout = us,no` has always compiled a two-group keymap, but nothing
#   said which group was typing — and at a login prompt a password typed in the
#   wrong layout is rejected exactly like a wrong password. Worse, the greeter
#   falls through to /etc/synui/synuirc and only ever had the SYSTEM layout, so
#   a password needing the second one could not be entered at all. The layouts
#   now cross the same permission boundary the lock wallpaper does
#   (greeterbg.c), and greeter.c re-applies the keymap after adopting them —
#   writing them into the config is half the job, since the keyboards were
#   attached with the keymap the config had at backend start.
#   Also fixed on the way past: a keyboard attached AFTER a layout switch
#   arrived on group 0, because wlr_keyboard_set_keymap resets the locked layout
#   exactly as it resets NumLock — so a laptop and its external keyboard typed
#   different letters. syn_server::kbd_layout is the session's memory of the
#   group and keyboard_apply_config re-applies it, right beside the NumLock
#   re-lock that is there for the same reason.
# 518: the lock/login panel's header is re-laid-out. The WEATHER is centred over
#   the clock instead of sitting in the top-left corner, and the KEYBOARD-LAYOUT
#   CHIP moves out of the header entirely to sit centred under the password
#   entry — which is where the eye already is when the thing it answers happens.
#   A chip in a header corner is furthest from the dots on a screen whose whole
#   point is that you look at it AFTER a rejection.
#   ⚠ The weather block is MEASURED BEFORE ANY OF IT IS DRAWN. Its left edge is
#   a function of its own width, so `-4°C` and `102°F · Thunderstorms` would
#   otherwise pin the icon to one spot and let the text run off to the side —
#   a block that drifts with the weather rather than one that is centred.
#   The core band is 40px taller to hold the chip below the fingerprint row, and
#   the chip's hit rect gains the band's offset the way the transport buttons'
#   already did: the caller translates the context, the mouse has none.
# 519: THE WEATHER LEAVES THE LOCK SCREEN. The engine 517 added for the lock and
#   login screens now feeds two more surfaces: a bar module (the temperature,
#   invisible until there is a reading) and a desktop widget (the same reading
#   with the condition, the place and its age around it).
#   ⚠ NEITHER OF THEM FETCHES ANYTHING, and that is the design. A bar module is
#   instantiated once per MONITOR, inside the shell process — three bars would
#   be three requests, and a connect stalling behind a captive portal would be a
#   stalled desktop. src/weather.c stays the only fetcher and publishes
#   ~/.config/synui/weather.state; WeatherState.qml watches it. Same arrangement
#   as Updates.qml and `syn-update ping`, for the same reasons.
#   ⚠ THE FILE IS WRITTEN TEMP+RENAME NOW. A FileView watching a path rewritten
#   in place sees it empty for a frame, so the bar's temperature would blink to
#   nothing every twenty minutes. It also carries `cond`, `icon` and
#   `stale_after` — all DERIVED by the compositor, because wx_describe() is the
#   one table mapping a WMO code to a word and a picture, and a second copy of
#   it in QML is how a sun ends up drawn over the word "Snow". `stale_after`
#   crossing the boundary is what stops the bar and the lock screen disagreeing
#   about whether a reading is old.
#   `lock_weather` is `weather` (and `lock_weather_unit` is `weather_unit`): the
#   key is the NETWORK switch for three surfaces now and the old name would be a
#   lie. Both spellings are read, in synuirc and in saver.state, so no machine
#   that ever turned the weather on loses it — only the new one is documented.
#   Turning it OFF removes the published reading, so a temperature does not stay
#   on the bar after the feature was switched off.
#   `synctl weather [on|off|refresh]`, a `weather` row in the bar's right-click
#   menu (per monitor), `synui-widgets weather`, row 10 of Super+Shift+A.
#   QuickLaunch hops below the card when both are on, as the pet does for the
#   monitor. tests/weather_surfaces.sh (5 phases; it never touches the network).
# 520: the update notifier drew its own FAILURE in ordinary bar ink.
#   Updates.qml asked for `pal.yellow` for its error state. `yellow` is a THEME
#   property; the per-strip palette a bar module reads is a different object
#   with its own vocabulary, and it has never had that key — its warm slot is
#   `orange`. So the binding evaluated to undefined on the errored branch, and
#   only there, which is why it shipped.
#   ⚠ AND UNDEFINED DOES NOT BLANK A QML PROPERTY, IT LEAVES THE OLD VALUE. The
#   badge kept BarModule's own `pal.glyph`/`pal.fg`, so "the update check is
#   failing" was drawn exactly like "an update is waiting" — right in every way
#   except the one thing the colour existed to say. The only trace was
#   `Unable to assign [undefined] to QColor` in a log, once per monitor.
#   `orange` rather than `clock` (what Battery/Cpu/Memory use for their warm
#   tier): `clock` collapses to plain ink on a clear bar, and this badge is a
#   bare "!" with no number beside it, so its colour carries the whole message.
#   tests/bar_palette_keys.sh checks every `pal.<key>` in the bar against both
#   palette literals — the error state needs a machine whose update check is
#   failing, so no pixel test can reach it and the guard is mechanical instead.
# 521: a right-click menu for a bar module, and a battery that can be TESTED.
#   components/ModuleMenu.qml is the second menu shape: BarMenu is the bar's own
#   per-monitor furniture switches, this is a short list of ACTIONS belonging to
#   whatever module was right-clicked. A component and not a popup written
#   inside a module, because everything that makes a popup dismiss correctly
#   here was learned the hard way — the grab, the long backstop timer, the
#   HoverHandler that must not be a MouseArea, the anchor window that cannot be
#   discovered from inside a PopupWindow — and a second copy would be a menu
#   that dismisses correctly until the day it does not.
#   Updates.qml takes it: Open Updates, Check now (with when it last did),
#   Apply in a terminal (greyed with nothing pending), Held back (with a count).
#   Every row runs a COMMAND, as Network and Bluetooth already right-click into
#   nmtui and bluetoothctl; apply goes through `syntty --hold` because it is a
#   long sudo build that has to be able to ask for a password.
#   ⚠ AND THE ANCHOR IS MAPPED, NOT `root.x`. A module's own x is its position
#   inside the bar's Row while a PopupWindow anchors against the bar WINDOW, so
#   the menu opened at the far left of the screen — every row present, every row
#   working, only WHERE was wrong. tests/bar_module_menu.sh pins it, and two
#   earlier versions of that rig PASSED on the bug: one right-clicked the
#   centred clock (whose module declines the button, so the click fell through
#   to the bar's own menu, which IS anchored on the pointer) and the next found
#   the compositor's "◢ SYNAPSE" corner, which is not a bar module at all.
#   ⛔ AND THE BATTERY IS TESTABLE AT LAST. This box is a desktop, so every
#   laptop-only path in the shell is unreachable on the only machine anybody
#   looks at it from — and both surfaces that draw a battery hide themselves
#   when there is none, which makes "correctly hidden" and "broken" identical.
#   tests/mock_upower.py is a fake org.freedesktop.UPower on a PRIVATE bus
#   (DBUS_SYSTEM_BUS_ADDRESS; it refuses to start without it, so the real system
#   bus can never be taken). tests/battery_surfaces.sh runs the shell against a
#   laptop-shaped and a desktop-shaped one and requires the bar module and the
#   monitor's BAT row to appear only for the first. The control is a desktop
#   UPower with this machine's own measured numbers rather than a MISSING
#   service — otherwise it would pass on modules keyed to the service existing
#   rather than to the device being a battery.
#   ⚠ Both new rigs, and weather_surfaces, run quickshell under dbus-run-session
#   now: the bar reads the LIVE session bus otherwise, so MPRIS put whatever the
#   real desktop was playing into the frames being compared — a track title
#   changing between two shots is 1800px of diff and can carry an assertion on
#   its own. And a private session bus activates xdg-document-portal, which
#   fuse-mounts $XDG_RUNTIME_DIR/doc and outlives every process the rig started;
#   unmounted in cleanup or every run leaks both the mount and its directory.
#   weather_surfaces waits for the repaint instead of sleeping through it — it
#   flaked once under a parallel `meson test` and passed alone every time, which
#   is the signature of a timed guess rather than a wrong result.
#   mpris.c: an sd-bus CLIENT of the session's players — a reader and three
#   verbs. Every call is async; a synchronous property read against a wedged
#   Electron app would stall every window on the desktop. The media keys reach
#   it while locked, so pausing what you locked the machine on no longer means
#   unlocking it first.
#   weather.c: OFF by default — the only part of that screen that touches the
#   network. The PLACE is the file every weather widget here already reads
#   (~/.local/state/omarchy/settings/weather.json), not a new setting. Fetching
#   is a thread on the news.c pattern, and the stop flag doubles as the libcurl
#   progress callback so logout never waits out a connect timeout.
#   ⚠ The transport buttons and the weather icons are cairo PATHS, not glyphs:
#   ⏮ ⏯ ⏭ and most of the weather block are not in DejaVu, and the ISO ships a
#   small font set — a missing glyph is a button you cannot see and can still
#   press.
#   Four rows in Super+Z, `synctl layout [next|prev|<name>]`, and a `kbd_layout`
#   bind action (NOT `layout_cycle`, which is the tiling layout).
#   tests/lock_layout.sh (5 phases, incl. a layout xkb cannot resolve being
#   refused rather than offered), tests/greeter_bg.sh phase 6, and four more
#   assertions in tests/state_reload_test.c.
#   ⛔ AND A LIVE-MACHINE HAZARD CLOSED ON THE WAY PAST: greeterbg_publish()
#   wrote to /var/lib/synui/greeter/<uid> from ANY instance. That root is 1777
#   and keyed on the uid, so every headless rig published over the real
#   session's — with a hermetic HOME and so no wallpaper, i.e. it replaced the
#   login-screen background with a published "plain". `meson test` starts ~20
#   compositors; the symptom is a black login screen at the next boot and
#   nothing connecting the two. Same shape as the nested-synui theme bug fixed
#   in 357, and now the same guard: publish only when synui_owns_seat(), with
#   SYNUI_GREETER_BG_DIR lifting it because a rig that has redirected the root
#   has no real root to protect.
# 522: THE LIGHTS FOLLOWED THE WALLPAPER ON THE RAM AND NOTHING ELSE. Changing
#   wallpapers moved the two Corsair DIMMs and left the board and the keyboard on
#   whatever colour they happened to be holding, for the rest of the session.
#   The MAP is the bug. syn-rgb caches `index<TAB>mode<TAB>name` because a
#   listing with no SDK server up is a full 8.6s bus probe — and that cache can
#   only ever be SMALLER than the truth, in the one direction that is silent. A
#   device that has GONE fails the whole openrgb call, and that failure is what
#   rebuilds the map; a device that has ARRIVED fails nothing at all, because the
#   push still succeeds on every device the map does know. Nothing ever asked
#   again.
#   ⛔ AND AT LOGIN THE MAP IS ALWAYS BUILT SHORT. Measured from the SDK server's
#   start: no devices at all at 0.3s, the two i2c DIMMs at 0.6s, and the USB
#   mainboard and the G512 only at 7.8s — while syn-rgb.service runs about a
#   second after the server. So the map cached at login named the RAM, and every
#   push for the rest of the session pushed the RAM. `syn-rgb apply` by hand
#   hours later looked perfect, which is the same tell as 437: the push works,
#   what it is pushing TO is wrong.
#   ⚠ "Wait until the count stops changing" answers the wrong question — it is
#   perfectly still at two for those seven seconds. The only thing on this
#   machine that knows how many devices it has is the map from the last time they
#   were all counted, so a listing SHORTER than the map is not believed until it
#   has been asked again. Bounded, and what it finds at the end of the wait is
#   the answer: a device really unplugged has to be let go of, or the map would
#   only ever grow. With no map at all — the first run ever — it waits for a long
#   quiet instead, which is the one case where paying ten seconds once is right.
#   ⚠ THE WAIT IS THE SDK SERVER'S PROBLEM ALONE. With no server, openrgb
#   enumerates the bus itself before it prints a word — that IS the 8.6 seconds —
#   so a short listing cannot happen there and nothing waits. The partial answer
#   belongs to the server: it serves the controllers it has registered so far and
#   says nothing about the ones it is still looking for.
#   With a server up a listing is a socket round-trip, 0.03s against a 1s push,
#   so the map is re-read on EVERY push now and the cache can no longer hide
#   anything. A mode set by hand survives that — merged by index and name — and
#   `syn-rgb on` no longer deletes the map, because it STARTS the server, which
#   is the worst possible moment to throw away the only record of the bus.
#   tests/syn_rgb.sh: +7 assertions, and both halves are asked for BY NAME
#   (SYN_RGB_SDK), because on a developer's own box a server is listening and the
#   no-server path would otherwise quietly stop being covered.
# 523: HALF THE BAR STOPPED INVERTING. The weather and the volume in white, the
#   CPU meter, the Bluetooth glyph and the network glyph in BLACK, side by side
#   on one strip — identical code reading identical properties, one of them
#   backwards.
#   Nothing was wrong with the ink rule. What was wrong was the picture it was
#   asked about. The bar decides per MODULE (Theme.qml barPaletteAt), and for a
#   column no window covers, the only per-column answer it had was
#   wp_lum_grid's TOP ROW. A grid row is SYN_LUM_ROWS deep — a ninth of the
#   screen, 160 pixels on this box, standing in for a bar 34 logical pixels
#   tall. Four fifths of that cell is picture the bar is not on.
#   ⛔ AND ON A PHOTOGRAPH THAT IS NOT A ROUNDING ERROR. Measured off the
#   screenshot this came from — a dark canopy along the top edge over lit leaves
#   below it — the cell read 0.29 for a column whose top 34 rows read 0.08, and
#   those two numbers are on opposite sides of the flip: dark ink clears 5.3:1
#   on the first and 2:1 on the second. The modules over that column inked for a
#   backdrop they were nowhere near.
#   ⚠ THE WHOLE-BAR ANSWER WAS RIGHT THE WHOLE TIME, which is what made it read
#   as an inversion bug rather than a measurement one: `bar_ink` has always been
#   taken off the strip's own rows (wallpaper_strip_lum), so every module with
#   no per-column answer of its own — and the clock, whose span straddles two
#   disagreeing columns and vetoes — came out correctly white beside them.
#   wp_strip_lum[SYN_LUM_COLS]: the same rows wp_top_lum measures, on the same
#   edge, binned into the same columns as the grid, for the painted wallpaper
#   and for a live one. Published as `wp_strip.<output>`, and it is what
#   `bar_strip.<output>`'s -1 columns now fall back to — in Theme.qml's
#   barStripAt() and in barscan.c, which was compositing see-through windows
#   against the same wrong cell. The grid row stays as the last resort, for a
#   backdrop.state written by a synui that does not publish the new one.
#   ⚠ A SECOND CASE IN THE SAME FUNCTION, reasoned rather than seen: a span with
#   a safe ink of its own returned `scrim: false` and decided against the
#   UNSCRIMMED wallpaper, while the bar had already laid its wash across the
#   whole width. On a scrimmed bar a cell at 0.29 reads 0.12 under a 34% black
#   wash — dark ink 2:1, white 8.5:1 — so that span could be handed exactly the
#   wrong direction. The scrim is composited into the span's luminance now
#   (lumOver, the one the popups walk their alpha with) and carried in the
#   palette it returns. Only the scrim: a clear bar's own barAlphaAsked is
#   ignored on purpose, because at or under barThinAlpha it is too thin to
#   carry its own ink and so too thin to decide anybody else's.
#   tests/bar_strip_band.sh, two phases over a wallpaper that is dark for
#   exactly the rows the bar covers and bright underneath: the published
#   measurement (the strip and the cell must disagree, and disagree onto
#   opposite inks), then the bar on screen (no pixel of the strip may be
#   #1D1D1F). Against the old Theme.qml that second phase finds 361px of it,
#   every one in the right half.
# 524: two things the bar and its menu were measuring wrong, and a scene node
#   nobody could see.
#   ⛔ A SCENE BUFFER'S PIXELS OUTLIVE ITS `buffer`, AND EVERY SURFACE SYNUI
#   DRAWS FOR ITSELF IS IN THAT STATE. wlroots uploads a buffer that is not a
#   client's to a texture the moment it is set and then lets the buffer go:
#   `scene_buffer->buffer` reads NULL from then on and the pixels live in the
#   private `texture`, sized by `buffer_width`/`buffer_height`. barscan.c asked
#   `sb->buffer` for both the geometry and the pixels, so every one of synui's
#   own scene buffers was ZERO BY ZERO — never hit by leaf_at(), and declined by
#   the read even if it had been.
#   The one that shows is the TITLEBAR, and leaf_lum_run()'s own comment had it
#   backwards for as long as it has existed: a decoration is not a rect. The
#   BORDER is a rect, and it is clipped out over the content; the titlebar is a
#   cairo buffer. So a window under an auto-hiding bar was measured as nothing
#   at all whenever its title sat in the strip, and the bar inked itself off a
#   wallpaper that window was covering. Hiding titlebars "fixed" it, because
#   that moves the client's own surface — a client buffer, and kept — into the
#   strip, which is why only a DECORATED window ever showed it.
#   ⚠ tests/bar_scene_strip.sh has been failing three assertions since it was
#   written, and they are the three that describe exactly this. They pass now.
#   ⚠ `WLR_PRIVATE` is reached deliberately: there is no public accessor, and
#   the alternative is a second map from node to buffer for surfaces synui
#   already owns. A wlroots that moves those fields fails to COMPILE, which is
#   the failure mode to want — the one it replaces was silent for the life of
#   the file.
#   ⛔ AND THE START MENU'S MATERIAL WAS DECIDED BY WHICHEVER PAGE WAS OPEN.
#   `Accessories` opened clear, with the desktop clock legible through it;
#   `Internet`, four rows longer, opened as frosted glass. Same menu, same
#   corner, seconds apart. The panel corrects its own alpha against what is
#   behind it (Theme.alphaWalkOn) and asked that question about the box it
#   happened to occupy — and the luminance grid is a ninth of the screen deep,
#   so the answer moves in STEPS as a page grows. Measured off the report's
#   screenshots: the clear pages end at y=181, 229 and 277 and the frosted ones
#   at 325, 583 and 1423 — the flip is the panel crossing from grid row 1 into
#   row 2.
#   A page is not a material. StartMenu.qml asks once now, about the strip from
#   the bar to the far edge, which is where that panel lives whatever is open.
#   ⚠ It can only ever make the menu MORE opaque: the walk takes the worst cell
#   it is given and a larger region has the same worst cell or a worse one.
#   ⚠ AND THE "MENUS FOLLOW THE BAR" RULE WAS NEVER BROKEN — measured, not
#   assumed: at `bar_opacity = 1.00` the menu comes up at #0B0B14 with no
#   wallpaper in it at all, because popupAlpha resolves to barAlphaAsked and the
#   correction has nothing left to walk. What varied was only the corrected end
#   of that range, which is where a clear bar leaves it.
#   tests/menu_surface_pages.sh: two runs of one compositor each, differing only
#   in how many CATEGORIES the menu has to list (⚠ not how many apps — two
#   hundred entries in Utility is one row, and a first draft of that file varied
#   the count, got two identical menus and passed against the bug). Against the
#   old StartMenu the short page reads as the wallpaper and the long one as
#   glass, which is the report.
# 525: an AI button on the bar, and the assistant behind it.
#   modules/Assistant.qml — a speech bubble left of the readouts that opens
#   `vibe gui`, the chat window vibe now ships. It is a thing you press rather
#   than a thing you read, which is why it sits with the launcher's kind and not
#   among the meters.
#   ⚠ THE BUTTON OWNS THE WINDOW'S PROCESS, and that is what makes it a toggle
#   rather than a launcher: quickshell gives a client no way to raise another
#   client's window, so a click that only spawned would open a second chat
#   window every time somebody pressed it looking for the one already on screen.
#   The cost is that a bar reload closes the assistant, which is the right trade
#   for a chat box and the wrong one for a document — said out loud in the file.
#   ⛔ `running = true` on an already-running Process is a SILENT NO-OP, so the
#   click asks which way it is going rather than assuming.
#   ⚠ THE GLYPH IS A SPEECH BUBBLE AND NOT A ROBOT. F544 (robot) and F5DC
#   (brain) are the obvious icons for this and NEITHER IS IN Symbols Nerd Font
#   Mono — the font Icons.qml's codepoints come from and the one synui depends
#   on. Nothing else covers them: they are Nerd Font Private Use Area
#   codepoints, so a box with a dozen other families installed still draws them
#   as nothing. Checked against the font rather than assumed, the same way the
#   transport buttons were. (⚠ An earlier version of this note said Nerd Font
#   Symbols was the only font the ISO ships. It is not — terminus-font,
#   ttf-dejavu and noto-fonts are all on it. The glyph choice was right; the
#   reason given for it was overstated.)
#   Hidden where vibe is not installed, so a desktop without it is unchanged;
#   BarConfig gains an `assistant` row so it can be switched off per monitor
#   like every other module.
# 526: the assistant glyph's note was overstated, and is corrected in place.
#   525 said Symbols Nerd Font Mono was "the only icon font the ISO ships". It
#   is not — terminus-font, ttf-dejavu and noto-fonts are all on it, and this
#   box has a dozen more. The GLYPH CHOICE WAS STILL RIGHT and for a reason
#   that survives the correction: F544 and F5DC are Nerd Font PRIVATE USE AREA
#   codepoints, so no other family can cover them however many are installed.
#   Reported by velle, who reads these notes.
# 527: syn-speak ships, and the desktop has one voice.
#   `syn-speak` — the desktop out loud: it speaks the focused window as focus
#   moves, the highlighted text, or any line handed to it. Written in 525 and
#   left INERT in the tree (not installed, not wired to anything); installed
#   now.
#   ⛔ IT DOES NOT OWN A SPEECH ENGINE. It speaks through `vibe voice say`
#   where vibe is installed, which is chibi's piper — so the screen reader and
#   the assistant are the same voice and there is one place that works out
#   which engine this box has. espeak-ng is what is left when vibe is absent,
#   not a second opinion.
#   ⚠ IT IS AN ANNOUNCER, NOT A FULL SCREEN READER, and says so in its own
#   header. Reading every widget under the cursor needs AT-SPI support in each
#   toolkit; Orca remains the answer for that. What this does is the half that
#   is useful today and honest: what has FOCUS, and what is asked for.
#   ⚠ NOT ON A KEYBIND YET — `syn-speak on` from a terminal or the assistant.
# 528: the voice gets keys, because a screen reader you can only start from a
#   terminal is not one — the person who needs it is the person who cannot read
#   the terminal to start it.
#     Super+Shift+U   announce the focused window as focus moves, or stop
#     Super+U         speak whatever is highlighted
#     Super+Shift+V   dictate: listen once, type it where the cursor is
#   ⚠ ONE CHORD TOGGLES the reader rather than a pair. A reader talking over
#   you has to be stoppable with the key you just pressed, not with a different
#   one you have to remember while it talks.
#   ⚠ All three SPAWN rather than dispatch: the speech stack lives in vibe and
#   chibi, and a compositor that grew its own would be a third voice on the
#   machine. A box without them spawns a command that is not there, which costs
#   a log line and nothing else.
# 529: the bar says when the microphone is open.
#   The assistant can be armed to answer to its name, and that leaves a
#   microphone listening until it is turned off — with the chat window closed,
#   minimised, or on another workspace. modules/Assistant.qml watches
#   ~/.config/synui/assistant.state and, while wake is on, the button becomes a
#   MICROPHONE in the warning colour with a tooltip that says so.
#   ⛔ A DISCLOSURE THAT LIVES ONLY IN A WINDOW NOBODY IS LOOKING AT IS NOT ONE.
#   That is the whole reason this is in the bar rather than in the assistant.
#   ⚠ A FILE AND NOT AN IPC CALL: the engine is a child of this button on ONE
#   monitor, and on a second monitor there is no child at all. A file every bar
#   watches says the same thing on all of them — and an engine that exits
#   clears it, so a crash cannot leave a bar claiming a microphone is open.
# 530: the speech switches reach the control panel.
#   Sound ▸ Speech gains two rows: "Screen reader" (syn-speak) and "Answer to
#   its name" (vibe's wake word). Until now the keys, a state file and a CLI
#   were the only surfaces, which is a poor place to leave the two settings an
#   accessibility user needs FIRST — the person who wants a screen reader is
#   the person who cannot read the terminal to start one.
#   ⛔ BOTH ARE `.external` ROWS and dispatch by ROW before the generic toggle
#   flip. An external row names no config field, so ctl_get/ctl_put would read
#   the top of syn_config_t as its value and write a bool back over whatever is
#   declared first — a plausible number and a silent corruption. Same trap the
#   RGB row documents; same answer.
#   ⚠ THE WAKE ROW READS `assistant.state`, NOT systemd. The unit can be active
#   with its listener failing to open the device, and what matters to somebody
#   looking at this row is whether a MICROPHONE IS OPEN. The engine writes that
#   file when the loop is really running and clears it on the way out, and the
#   bar's microphone indicator watches the same one — one fact, two surfaces.
#   ⚠ It draws "Listening", not "On". "On" describes a setting; what is true is
#   that a device is open right now, and the status line says so both ways.
# 548: HDR10 output, on a compositor that cannot render HDR.
#   Shift+D in the display panel (Super+D) drives the selected monitor in
#   HDR10; `[` and `]` move where SDR white sits. `synctl hdr on|off <output>`
#   and `synctl hdr white <output> <cd/m2>` are the same two controls from a
#   script. Persisted per connector in outputs.conf as hdr= / hdr_white=.
#   ⛔ THE OBVIOUS ROUTE IS CLOSED AND CLOSED SILENTLY. scenefx's fx_renderer
#   declares features.output_color_transform = false and ignores the field, so
#   a colour transform handed to the SCENE is dropped with no error — which is
#   what nightlight.c found and documents. This goes on the OUTPUT STATE, where
#   the DRM backend programs it into CRTC hardware after blending. synui keeps
#   compositing 8-bit sRGB with every effect intact.
#   ⛔ A CRTC IS A GAMMA LUT — ONE CURVE PER CHANNEL. Measured, not assumed:
#   546/547's probe reported tf_curve=no, matrix=no, pipeline=no, pq_lut=yes on
#   DP-3. An inverse-EOTF object, a 3x3 matrix and a pipeline of them are not
#   expressible in that hardware, so the backend falls back to the renderer and
#   the renderer refuses. A transfer function is per-channel, so sRGB→PQ bakes
#   into a 1D LUT and goes in.
#   ⚠ SO THE PRIMARIES ARE NOT CONVERTED, and the panel says so rather than
#   leaving it to be discovered. sRGB primaries are asked for FIRST and BT.2020
#   only as a fallback — HDR10 is entered by the PQ transfer function, not by
#   the colour volume — because sending sRGB-primaried pixels in a BT.2020
#   container is what makes a desktop come out over-saturated.
#   ⛔ THERE IS ONE COLOUR-TRANSFORM SLOT PER OUTPUT AND NIGHT LIGHT WAS IN IT.
#   wlr_output_state.color_transform is a single pointer, so HDR cannot set its
#   own beside night light's: the second call replaces the first, silently. The
#   warmth is folded INTO the PQ curve instead (in the encoded domain, where
#   nightlight.c applies it), and both commit paths now go through one
#   hdr_commit() rather than two copies of the same body.
#   ⚠ AN OUTPUT PUT INTO HDR IS TAKEN BACK OUT EXPLICITLY. A state that leaves
#   the image description unset leaves the connector carrying the last one — the
#   same trap as never committing a NULL colour transform. A connector that will
#   not accept a plain sRGB description back is never offered the mode at all.
#   ⚠ 10-bit scanout is requested with it, best-effort and remembered: PQ spends
#   most of its range on the bottom two stops, which is what makes 8-bit PQ band
#   visibly in shadows. Switching HDR off puts back what HDR switched on.
#   ⚠ Two SDR monitors on this desk report capable=no, and that is the common
#   case — so every refusal is an answer, never a successful-looking no-op:
#   `synctl hdr on` on a link that will not carry PQ returns an error.
# 549: the way out of HDR is a NULL image description, so the mode can be
#   entered at all.
#   ⛔ 548 SHIPPED AN HDR MODE NO MACHINE COULD TURN ON. It gated hdr_capable
#   behind hdr_sdr_ok — "will this connector take a plain sRGB description back"
#   — and asked that question with an image description carrying
#   WLR_COLOR_TRANSFER_FUNCTION_SRGB. wlroots 0.20.2 raises
#   wlr_output.supported_transfer_functions in exactly one place (backend/drm/
#   util.c, `|= ST2084_PQ`) and supported_primaries in exactly one (`|= BT2020`),
#   and output_basic_test() refuses any image description whose transfer
#   function or primaries are not advertised. No backend advertises sRGB in
#   either, so that description is refused on EVERY connector and EVERY GPU
#   before the panel is asked. DP-3 logged "takes an HDR10 signal but will not
#   take a plain sRGB one back" — which reads as one monitor's quirk and was
#   universal.
#   ⛔ THE EXIT IS wlr_output_state_set_image_description(st, NULL), COMMITTED.
#   That sets the committed bit with a NULL pointer: the basic test skips
#   validation for it, and the atomic backend turns it into Colorspace=Default
#   with HDR_OUTPUT_METADATA cleared. ⚠ Not the same as leaving the field
#   alone, which is the trap that strands a display in PQ — so it is a named
#   function, state_leave_hdr(), and the probe asks with the same one the leave
#   path sends. Asking about a state no commit will ever carry is the whole bug.
#   ⚠ THE sRGB PRIMARIES RUNG NEVER WINS ON THIS wlroots, and the file says so
#   now rather than implying the gamut was left alone. The ladder is kept — it
#   is the right policy and costs one test commit — but BT.2020 is the only
#   reachable container, so the desktop IS over-saturated in HDR and the panel's
#   detail line is the honest one.
#   ⚠ THE PICK IS A PURE FUNCTION NOW, hdr_pick_primaries(supported), because
#   the decision that failed lived inside hdr_probe() where nothing could reach
#   it: the probe needs a CRTC, so the headless rig answers "not capable" for
#   its own reasons and looked correct while the mode was unreachable
#   everywhere. tests/hdr_test.c pins it against the bitfield a real DRM
#   connector actually hands it, and hdr_probe_is_read_only.sh fails on 548's
#   source.
#   ⚠ `synctl hdr` reports sup_prim= and sup_tf= — wlr_output's advertised
#   bitfields, verbatim. Every derived column was already telling the truth
#   (pq=yes, image_description=yes, pq_lut=yes, sdr=no); the one fact that
#   explained all four was the one not on the line.
# 563: the desktop cube, and desktops that can differ per monitor.
#   Two things velle asked for together, and they are independent — one is how a
#   switch LOOKS, the other is how many screens it moves.
#
#   `anim_workspace = cube` (Windows -> Desktop switch -> Cube). The desk turns
#   about a vertical axis: the desktop you leave and the one you arrive at are
#   two faces of a cube meeting at a right angle, on black, shaded so the corner
#   between them reads, pulled back far enough that the turn fits on screen.
#   ⚠ IT IS NOT A PER-WINDOW ANIMATION AND COULD NOT BE. Every other style moves
#   scene nodes; a scene node translates and cannot rotate in perspective, so
#   there is no arrangement of them that draws this. src/cube.c takes the frame
#   instead, the way effects.c does for the CRT pass: it renders the scene into a
#   private offscreen swapchain and draws it itself through a GLES2 program with
#   a perspective vertex shader and two quads.
#   The outgoing face is a PHOTOGRAPH — one scene render taken by cube_begin()
#   before the switch hides anything — because only one desktop can be live in
#   the scene graph at a time. Across a few hundred milliseconds of rotation a
#   still is indistinguishable from a live face, and it costs one extra render
#   for the whole turn instead of a second full one every frame. The order is
#   load-bearing: cube_begin() is ws_switch_core()'s FIRST act, since a desktop
#   whose windows are already disabled photographs as bare wallpaper.
#   The projection is the identity at theta = 0 (f = CUBE_DIST - 1, and the
#   pull-back's sin(2|theta|) is zero at both ends), so the first and last frames
#   of the turn are pixel-identical to no animation at all — which is what allows
#   it to be armed on every switch. tests/ws_cube.sh asserts exactly that with
#   grim: a full turn each way comes back to the same pixels.
#   It costs the CRT pass for the length of the turn — the two cannot both have
#   the frame — and any failure (no shader, no buffer) ends the turn and falls
#   through to the ordinary commit, so the worst case is a switch that did not
#   animate, never a black screen. Wants 400-600ms; 140 is too short to read.
#
#   ⛔ AND IT FOUND A LATENT BUG IN anim.c. anim_start's zero-length branch
#   disabled the node only `if (hide_when_done && alpha_to <= 0.0f)` — correct
#   for the fade, the one style whose end state is transparent. The cube's
#   windows must stay SOLID (they are inside the photograph being turned), so
#   they fell through it and the desktop you had just left stayed drawn on top of
#   the one you switched to. Already latent for `slide` at anim_workspace_ms = 0,
#   which would leave a window enabled AND shoved a screen-width sideways.
#   anim_tick's completion path has said "regardless of the alpha it ended on"
#   since the slide landed; the zero-length case is that path with no frames in
#   it, and now says the same thing.
#   ⚠ The turn is about the VIEWER'S vertical axis, which on a 90/270 monitor is
#   not the buffer's: the scene renders with the output transform already baked
#   in, so a cube built straight out of buffer coordinates rolls like a drum on a
#   portrait screen. The shader works in screen coordinates and maps back on its
#   last line — the same test effects.c makes for its scanlines. Verified on a
#   headless output with transform=1.
#
#   `workspace_mode = per-monitor` (Windows -> Desktops span). Each screen
#   remembers the desktop it is showing and Super+2 moves only the monitor the
#   focus is on. Default stays `shared`.
#   ⚠ THIS IS NOT THE OLD PER-OUTPUT MODEL COMING BACK. That one (ROADMAP phase
#   K, removed at pkgrel 70) had each workspace OWNED by a monitor, which on a
#   three-monitor desk permanently claimed desktops 1/2/3 — one per screen — so
#   Super+1 on the screen already showing 1 hit an early-out and did nothing and
#   Super+2 only warped the cursor. The three numbers a user reaches for first
#   were the three that could not work.
#   Here a workspace is still a desk-wide window list that nothing owns; an
#   output merely CHOOSES an index to look at, and two monitors showing desktop 2
#   at once is legal — each draws its own share of it. So `switch this monitor to
#   i` always does what it says, and SHARED is the same model with every output
#   pinned to one index rather than a different model.
#   The invariant that carries it is `view_workspace_shown()`: a window is on
#   screen iff the monitor IT lives on is showing ITS desktop. `ws->visible` is
#   not that question and every site that asked it has been moved over — under
#   per-monitor, desktop 2 can be visible next door while the window in front of
#   you is on this screen's share of it.
#   `synctl outputs` reports each screen's "workspace", which is the only
#   external view of the split (`activeworkspace` answers about the focused
#   screen alone and cannot tell a monitor that moved from one that did not).
#   tests/ws_per_monitor.sh runs two headless outputs with a window on each; its
#   assertions are about the output that was NOT switched, because the failure
#   here is not "the switch does nothing" but an unfocused monitor quietly
#   following along — which looks like the feature working until you glance at
#   the second screen.
#
#   ⚠ AND THE BAR'S PILLS ARE A SECOND, SEPARATE ANSWER. `visible` means "up on
#   SOME monitor", so under per-monitor both bars lit both screens' desktops. The
#   row carries `outputs` now — the monitors showing it, by name — and BOTH
#   shipped bars read it: quickshell/modules/Workspaces.qml and Antiquity's
#   SynWorkspaces.focusedIdOn(). Under `shared` that is every monitor for exactly
#   one desktop, so the two modes read identically and there is no branch.
#   ⛔ AND THE OBVIOUS SPELLING IS A STALE BINDING. `active: shownHere(modelData)`
#   reads nothing QML can watch — modelData is a plain JS object — so each pill
#   kept the first answer it was given and the screen that moved lit its old
#   desktop AND its new one. Fixed by resolving a `litId` property that reads
#   root.workspaces (which the poll reassigns) and comparing ids in the delegate.
#   It renders and does not log, so tests/ws_bar_pills.sh reads PIXELS off two
#   headless bars: how many pills are filled, and where. Verified to fail on the
#   `visible` spelling before being kept.
#   ⛔ That rig also found how easily one leaks: SYNUI_SOCKET is set in this
#   shell and points at the LIVE desktop, and quickshell's `synctl` children
#   inherit it — the bar under test was polling the real machine and reporting
#   its desktops. Every rig here now unsets it and exports the rig's own.
# 566: the reader said the first syllable of a title, over and over.
#   ⛔ A TITLE IS NOT AN IDENTITY. watch_loop compared the whole spoken line and
#   took any difference for a focus change. Anything with a spinner in its title
#   — Claude Code in syntty, "◑ Building" then "◐ Building" — rewrites it several
#   times a second, so every 0.5s poll looked like a new window: speak() stopped
#   the sentence it had just started and began it again. With the pointer parked
#   over such a window the reader said the first syllable on a loop and never
#   finished anything. Reported from the live desktop.
#   ⚠ IT TOOK 565 AND vibe 28 TO BECOME AUDIBLE. Before those, speak_stop killed
#   nothing and `vibe voice say` never spoke, so the loop restarted an utterance
#   that was not playing. Two fixes landing made a third bug the visible one.
#   The key is the WINDOW now — the pid, already in synui's activewindow reply
#   and stable for its whole life — which is the granularity this thing is
#   documented to have: it speaks what has FOCUS when focus MOVES, and a title
#   changing under a window that did not move is neither. window_probe() emits
#   "<key>\t<line>" so one round trip answers both questions and the two cannot
#   disagree about which window they describe; window_line() is the sentence half
#   and is what `syn-speak window` still speaks. Falls back to the line where
#   there is no pid, so an older reply keeps the old behaviour instead of going
#   silent. Test phase 6b drives a stub whose title flips every call under one
#   pid; it counts 9 announcements in 2s against 565 and 1 against this.
#
# 565: stopping the reader left it talking.
#   ⛔ `speak_stop` HAS NEVER STOPPED ANYTHING. speak() started its speaker as
#   `setsid ... &` and recorded `$!` — but setsid(1) forks whenever it is already
#   a process-group leader, so the number written to $RUN/pid belonged to a
#   wrapper that had exited before anybody read it. Every kill went to a corpse
#   and returned success. It stayed invisible because there was nothing to stop:
#   `vibe voice say` queued the words onto a daemon thread and exited in
#   milliseconds (fixed in vibe 28), so the speaker was always already gone.
#   ⚠ THE SIGNAL GOES TO THE GROUP, NOT THE PROCESS. piper does not make the
#   noise itself — it synthesises a wav and hands it to aplay, a CHILD — so even
#   with the right pid, killing the parent alone leaves aplay playing to the end
#   of the sentence. That is an `off` that keeps talking, and an announcer whose
#   next window speaks over the last. Both speakers now go through speak_spawn(),
#   where the inner shell writes its own $$ before exec'ing: one number that is
#   the speaker's pid whether or not setsid forked, and the pgid of the group
#   aplay joins. speak_stop signals `-$pid`, falling back to the bare pid.
#   The test grew a phase 7 for it, with a stub speaker shaped like the real one
#   — it outlives the call and it has a child — because the whole reason this
#   lasted was that a stop which does nothing is indistinguishable from one that
#   worked. It fails against the old script.
#
# 564: the screen reader's switch had nothing behind it.
#   ⛔ `syn-speak on` HAS NEVER STARTED THE ANNOUNCER. It writes on=yes, says
#   "Speech is on", and runs `systemctl --user enable --now syn-speak.service`
#   — a unit that was never written and never packaged. `unit_do` ends in
#   `|| true` on purpose, so a box without systemd is not a broken install, and
#   that swallowed the failure whole. Since 527 all three surfaces that offer
#   this reported On and no window was ever announced: the Super+Shift+U
#   keybind (528), synui's control panel row (530) and syn-settings ▸ Speech.
#   `syn-speak status` said `on yes` too. Found writing the wiki page for it.
#   ⚠ THE STATE FILE IS NOT THE ANNOUNCER. Everything downstream reads on=yes
#   and is right to; what was missing is the process that acts on it. The unit
#   ships now (systemd/syn-speak.service, WantedBy=default.target so the choice
#   survives a login), Restart=on-failure rather than always — the loop returns
#   0 the moment it reads on=no, and `always` would respawn a reader the user
#   has just switched off.
#   ⚠ AND A USER UNIT HAS NO WAYLAND_DISPLAY. Nothing here runs `systemctl
#   --user import-environment`, so the announcer's synctl could not have found
#   the compositor even with the unit in place — it would have polled in
#   silence, which is the same symptom for a second reason. display_name()
#   resolves $XDG_RUNTIME_DIR/synui-display, the name synui publishes and that
#   synui-foot.service and synui-media-inhibit already read; an explicit
#   WAYLAND_DISPLAY always wins, because that file names the LIVE desktop and a
#   nested synui must not read the real session's windows out loud.
#   tests/speak_announcer.sh pins both: the unit is shipped AND installed (read
#   out of the sources, with the unit name taken from the script rather than
#   typed, so the switch and the package cannot drift), and the loop actually
#   speaks a focus change, once per change, stopping when switched off, with
#   and without a display in the environment. Every voice candidate is stubbed —
#   `vibe` first on PATH as well as espeak-ng, since the positive case has to
#   stub them all or a real one speaks on the developer's session.
# 581: hi and ar are TRANSLATED AND NOW RENDER. src/text.c grows a shaper.
#   ⛔ THE CATALOGS WERE NEVER THE PROBLEM. po/ has carried complete Hindi and
#   Arabic since 572-580, correct and reviewed, and what the compositor drew
#   from them was worse than the English it replaced: text.c goes through
#   cairo's TOY font API, which resolves a family to one face and does no
#   layout, so Arabic came out as isolated letter forms in reverse order and
#   Devanagari with its matras unreordered. A row of correctly translated
#   nonsense is not a translation, and it is the failure mode nobody on this
#   side of the desk can see — the eleven Latin, Cyrillic and CJK catalogs were
#   always fine, because none of them needs shaping or bidi.
#   HarfBuzz shapes, FriBidi resolves the embedding levels and the visual order
#   of the runs, and the glyphs go out through cairo_show_glyphs() at the
#   positions HarfBuzz computed. Joining, mark positioning, Indic reordering and
#   RTL all follow, from the same two libraries pango uses — so a synui panel and
#   a GTK window now draw the same sentence the same way.
#   ⚠ NEITHER LIBRARY ADDS A PACKAGE TO THE ISO. harfbuzz is already pulled in
#   by librsvg, which synui depends on, and fribidi by pango and gtk3/gtk4.
#   Both are named in depends= anyway: "something else happens to want it" is
#   not a dependency declaration.
#   ⚠ AND IT IS A SECOND PATH, NOT A REPLACEMENT. cp_needs_shaping() decides per
#   STRING — one Arabic character anywhere shapes the whole thing, because
#   ordering runs against each other is a decision about the string. Everything
#   else keeps the character walk it has always had, so the eleven unaffected
#   languages are unaffected to the pixel; routing them through HarfBuzz would
#   have re-laid-out every panel on the desktop, across 663 call sites, to fix
#   two languages that do not use it.
#   ⛔ THE hb_font CACHE IS LOAD-BEARING, NOT A TIDY-UP. hb_ft_font_create()
#   builds an hb_face_t and the first hb_shape() against one parses that font's
#   whole OpenType layout — GSUB, GPOS, GDEF. Per run per string that measured
#   78us for an Arabic label and 162us for a Hindi one against 5us for ASCII:
#   6.5ms of a 16.7ms frame for a control-panel repaint, so a Hindi desktop
#   would have dropped frames scrolling a menu and only a Hindi one. Keyed on
#   the cairo_scaled_font_t (which pins both the face and the size the FT_Face
#   is set to) it is 11.3us and 18.1us.
#   ⛔ AND ONE BUG WORTH RECORDING, because it is exactly what this tree keeps
#   relearning: the first version resolved a character's face as "a face, or
#   NULL meaning carry on with the previous one", which collapsed "a combining
#   mark inherits its letter's face" and "the context's own face covers this"
#   into one value. The first LATIN letter after an Arabic run therefore
#   inherited the ARABIC fallback face, and since a face change is a run
#   boundary it split "HHHH" into "H" + "HHH" at two different widths. Nothing
#   errored; the string simply measured 4.9px wider than the same string with
#   the words the other way round. It was found only because the bidi test
#   compares those two orderings. The answers are an enum now.
#   tests/text_shape_test.c pins three properties, each a RELATION between two
#   draws rather than a glyph index — an index is a property of whichever font
#   is installed, and this has to pass on a CI container too: joined letters are
#   narrower than isolated ones, a conjunct is narrower than its parts, and
#   reversing a string's logical order draws the SAME line (the paragraph
#   direction flips with it, so the reordering cancels). Plus the invariant the
#   whole file rests on — syn_text_extents() measures what syn_show_text()
#   paints — re-checked through the shaped path, and that a shaped draw leaves
#   the current point where cairo_show_text() would have: cairo_show_glyphs()
#   leaves it UNDEFINED, and thirty places in this tree draw a breadcrumb or a
#   two-tone footer as consecutive syn_show_text() calls with no move_to.
#   Its contact sheet draws real msgstrs out of po/ar.po and po/hi.po, not
#   strings invented for a test.
# 582: the bar speaks, or at least it can now — the QML translation bridge.
#   ⛔ qsTr() IS A TRAP AND THAT IS WHY THIS FILE EXISTS AT ALL. Qt's
#   translation path needs someone to call QTranslator::load() and
#   installTranslator() before the QML engine starts, and quickshell 0.3.1 does
#   neither — there is no installTranslator anywhere in the binary and no way to
#   reach one from QML. So qsTr("Volume") compiles, returns "Volume", and
#   translates nothing in every language forever, while reading in a diff
#   exactly like a marked string. Marking the bar up with it would have been a
#   day's work that ships an English bar and reports success. Checked, not
#   assumed, and tests/i18n_bar.sh fails on a qsTr( anywhere under quickshell/.
#   So the catalogs are carried as JSON and looked up in quickshell/I18n.qml.
#   po-bar/ holds ordinary gettext .po files — msgmerge, msgfmt -c and
#   tools/po-fill.py all work on them, and po-fill grew a --dir so one tool
#   serves both sets rather than a second copy that drifts. Only the last mile
#   changes: tools/po2json.py compiles each to a small JSON object and a
#   blocking FileView reads it.
#   ⚠ THE READ HAS TO BE SYNCHRONOUS. A FileView is async by default and
#   onLoaded arrives a turn later — by which time every label in the bar has
#   been laid out from the English it got on the first pass, with no signal to
#   rebind to because tr() is a function and not a binding. blockLoading plus a
#   text() call inside the loader is the whole reason this works.
#   ⛔ AND xgettext CANNOT READ QML. There is no --language=QML, and
#   --language=JavaScript chokes on the object syntax and extracts a silent
#   subset — a partial .pot being the worst outcome available, since it looks
#   like a successful run. tools/qml-xgettext.py reads the tree instead, folds
#   adjacent and +-joined literals so a sentence can wrap, skips both comment
#   forms, and treats a NON-LITERAL argument as an ERROR rather than a skip:
#   I18n.tr(someVariable) is the QML shape of the N_() trap src/i18n.h
#   documents, marked-looking and English.
#   201 msgids across 35 files. What was deliberately NOT marked is the larger
#   half of the work: bar.json keys, dispatch actions, page ids, panel args,
#   connector names, SSIDs, PipeWire sink descriptions, EQ preset names, git
#   SHAs, commands a person is meant to type, and the start menu's category
#   buckets — which are the keys rowModel.apps is indexed by and which the sort
#   tests with `a === "Other"`, so they are translated only at the draw step, in
#   a new catLabel(). A translated bucket would file every application under
#   Other and open a page that does not exist.
#   ⛔ AND THE PET STAYS A PICTURE. TuxScreen and TuxShell import QtQuick and
#   the sprite table and nothing else, which is what lets tests/tux_screen.sh
#   draw sixteen moods with the `qml` tool and no compositor. The first version
#   of this reached for I18n in both and the harness exited 2 with no line
#   number. Their words arrive as a `words` property with English defaults, the
#   same route the five colours and two font families already take, and
#   Tuxagotchi.qml fills them in.
#   Two gates. tests/i18n_bar.sh is the reachability half — no qsTr, every
#   marking file listed, every argument a literal, the template current, the two
#   LINGUAS in step, and the keys untouched. tests/i18n_bar_runtime.sh is the
#   half nothing covered before: it EXECUTES quickshell/I18n.qml against stub
#   Quickshell types under Qt's own qml and checks that a catalog on disk
#   changes the answer — twenty cases including glibc's LANGUAGE precedence, the
#   C-locale veto, Russian's three plural forms at 1/3/11/100, malformed JSON,
#   and every fallback to English.
#   ⚠ NOT UNDER quickshell, and that is not a shortcut: quickshell connects to
#   Wayland at startup and on this box that is the LIVE session — `env -u
#   WAYLAND_DISPLAY` does not prevent it, it falls back to the running
#   compositor's socket. A test that has to be run carefully stops being run.
#   ⛔ TWO CATALOGS WERE BORN WRONG AND NOTHING SAID SO. `msginit -l ar` and
#   `-l zh` could not resolve those bare codes to a locale and silently fell
#   back to the template's English default, so Arabic got two plural forms
#   instead of six and Chinese two instead of one. Both are perfectly valid
#   rules that pass every property check. The gate now compares each rule
#   against po/'s for the same language — the two sets cover the same thirteen,
#   so a disagreement is a bug by construction — and runs every rule through
#   Qt's engine, which also proves `new Function` is available there.
#   The thirteen catalogs are EMPTY at this pkgrel. That is a coherent state:
#   every lookup falls through to the msgid, so the bar is exactly the English
#   bar it was, and the mechanism can be reviewed without 2,600 translations on
#   top of it. Filling them is the next pkgrel, the way po/ was filled at
#   572-580.
# 583: all thirteen bar catalogs, 201/201, and the bar speaks.
#   582 shipped the bridge with empty catalogs so the mechanism could be
#   reviewed on its own. This fills them: 201 msgids × 13 languages, complete in
#   every one, msgfmt -c clean, and checked at runtime through the real
#   quickshell/I18n.qml against the JSON the build actually generates — German,
#   Japanese, Arabic, Hindi and Russian, including Arabic's DUAL form at n=2 and
#   its "many" form at n=11, which no two-form language would have exercised.
#   ⛔ AND ONE MOJIBAKE THAT PREDATES ALL OF THIS. Updates.qml wrote its
#   separator as "\xc2\xb7" — the UTF-8 BYTES of U+00B7, which is a C idiom.
#   In QML \xNN is a UNICODE escape, so \xc2 is U+00C2, and the tooltip has read
#   "Click to open Updates Â· right-click for more" on every desktop since the
#   module was written. Nobody saw it; the extractor did, because the stray Â
#   came out in the template. Every other separator in the tree is already a
#   literal ·, and this one is now too.
#   ⛔ AND A LATENT BUG IN po-fill.py THAT BROKE ALL THIRTEEN AT ONCE. It builds
#   the replacement text with po_escape(), whose whole job is to PRODUCE
#   backslash sequences, and handed it to re.sub() as a replacement STRING —
#   which processes escapes in its template. Every "\n" it had just written was
#   turned back into a literal newline, ending the quoted string: msgfmt said
#   "end-of-line within string", then "keyword \"Klicken\" unknown" on the
#   German for the second half of the sentence. It survived this long because
#   the compositor's catalogs were filled before any multi-line msgid reached
#   them. A function replacement is used verbatim, and tests/i18n_bar.sh's
#   msgfmt -c pass is what caught it — on all thirteen, in one run.
#   ⚠ po-fill REFUSED THE RE-FILL AFTER THE RESET, correctly: the catalogs came
#   back at 582's msgids, which still had the Â in them, and it will not place a
#   translation against a msgid that does not exist. That is the check that
#   stops a list slipping onto its neighbours, doing its job.
#   What is NOT translated is unchanged and deliberate: bar.json keys, dispatch
#   actions, page ids, panel args, connector names, SSIDs, sink descriptions, EQ
#   preset names, git SHAs, and the commands a person is meant to type — a
#   translated `syn-update ping` names a binary that is not on the system.
# 584: the bar's runtime test was a FALSE GREEN, and the catalogs move beside
#   the file that reads them.
#   ⛔ TWENTY-THREE GREEN ASSERTIONS PROVING NOTHING. tests/i18n_bar_runtime.sh
#   ran each case as a QML file that exits 0 on a match and 1 on a mismatch —
#   and `qml` ALSO exits 0 when it cannot load the file at all, printing "Did
#   not load any objects, exiting." So the shell's `if qml …; then ok` reported
#   a pass for a probe that never ran a line. Found by sabotaging it: a
#   deliberately impossible comparison passed, and so did I18n.tr("Volume")
#   checked against the string NONSENSE. It was cited as evidence in 582 and
#   583; those claims about the RUNTIME half were not earned, and this is the
#   correction. Success is exit 7 now, mismatch is 1, and every other status —
#   including 0 — is reported as the harness failing rather than as a pass.
#   ⛔ AND THE STUB COULD NOT READ A FILE. With the exit code fixed, seven cases
#   failed at once: every one that needed a catalog. The stub FileView read its
#   path with a synchronous XMLHttpRequest, which returns EMPTY for a file:// URL
#   in this configuration — so those cases had silently got no catalog, fallen
#   back to English, and been compared against English. The content is injected
#   through the stub now, keyed by the path I18n.qml asks for. Reading bytes off
#   a disk is quickshell's FileView and quickshell's to test; what is ours is
#   what I18n.qml does with those bytes and WHICH path it asks for, and the path
#   is asserted separately against the string.
#   ⚠ THE SUITE IS NOW SHOWN TO FAIL, three ways, because "it passes" is exactly
#   what it said before: a wrong expectation fails one case; breaking the lookup
#   in I18n.qml fails the singular case and no plural one; breaking the plural
#   SELECTION fails four plural cases and no singular one. That split is the
#   evidence the cases are testing what they name.
#   The catalogs also move from <datadir>/synui/i18n to
#   <datadir>/synui/quickshell/i18n, beside I18n.qml, and the singleton finds
#   them with Qt.resolvedUrl("i18n") against its own file rather than a
#   hardcoded path. That is what lets ONE BYTE-IDENTICAL copy of I18n.qml serve
#   the seven other quickshell trees that still need translating — none of which
#   may depend on synui, since they install on plain Arch without it. The env
#   override is SYN_I18N_DIR, one name for every app, because the file is the
#   same file.
# 585: the SECOND bar speaks thirteen languages, and its clock stopped being
#   English on every desktop.
#   The Antiquity shell (`bar_shell = antiquity`) is its own QML tree and was
#   entirely English. 39 msgids through the same JSON bridge the SYNAPSE bar
#   uses, in a third catalog directory: po/ is the compositor's C, po-bar/ is
#   this bar's QML, po-antiquity/ is that one's. Third domain, same last mile —
#   ONE byte-identical I18n.qml, which tests/i18n_antiquity.sh now cmp's against
#   the bar's copy, because four copies of a file stay one file only if
#   something says so.
#   ⛔ Qt.formatDateTime(d, "<format>") IGNORES THE LOCALE, and every clock in
#   that tree used it. Measured under quickshell 0.3.1 on a de_DE and a ja_JP
#   session: it returns "Tuesday, 1 September 2026" on both, because the string
#   overload formats against QLocale::c() — while Qt.locale().name reads de_DE
#   and ja_JP correctly in the SAME process, which is what makes it look fine
#   in review. Date.toLocaleString(Qt.locale(), fmt) is the overload that uses
#   it: names, AM/PM and digits all follow (ja draws 午後, ar draws ١٤:٠٥). The
#   format STRING is then a msgid as well, because the locale fixes the names
#   and only a translator can fix the ORDER — ja wants yyyy年M月d日dddd, not
#   English word order with Japanese words in it. English leaves it
#   untranslated and draws exactly what it drew before. The SYNAPSE bar never
#   used that call and is unaffected.
#   ⛔ AND THE WEATHER WIDGET SWITCHES ON WORDS OFF THE NETWORK. "Snow",
#   "Rain", "Drizzle", "Thunderstorm" and "Atmosphere" are OpenWeatherMap's
#   weather[0].main values, and "metric"/"standard"/"imperial" are both what
#   getTemp() compares against and what the user types into the Unit field.
#   Translating any of them is a switch that silently stops matching on every
#   non-English desktop — so the sentence that lists the three units passes
#   them through .arg(), and the suite asserts both sets are still matched
#   literally. Same rule as ctlpanel.c's settings keys.
#   ⚠ The gate also lints the whole tree. qmllint reports a genuine parse error
#   as `Warning: … [syntax]` and exits 255, which is the signal used; a missing
#   PROPERTY it reports at exit 0 and quickshell then refuses the file, so a
#   green run means "no edit left a file that cannot be read", not "the shell
#   starts". Every check in the suite was shown to go red on a deliberate break
#   before being trusted.
#   ⚠ mktarball.sh caught its own omission: contents=() cross-checks every
#   subdir() in meson.build, so adding po-antiquity/ to the build and not to
#   the tarball failed the tarball rather than the machine that unpacks it.
# 586: the WELCOME GUIDE was English in all thirteen languages, and nothing
#   could say so.
#   ⛔ ITS EVERY WORD LIVES IN A .js. quickshell/welcome/pages.js holds the six
#   pages — nav, title, blurb, and every row's label and description — and the
#   bar's gate asked "does every .qml that marks a string appear in POTFILES?".
#   A file that marks NOTHING passes that trivially, and a file that is not a
#   .qml is not even looked at. So the first screen a new user sees stayed
#   English through eight components' worth of this work. The check reads .js
#   now, and strips comments while doing it — its first run flagged
#   GuideState.qml, whose only I18n.tr( is inside a comment.
#   ⛔ A `.pragma library` CANNOT SEE A QML SINGLETON, which is exactly why
#   pages.js is one (its header explains: importing a QML directory module
#   instantiates every singleton in it). So the table became a FUNCTION and the
#   singleton is passed in: `Pages.pages(I18n)`.
#   ⚠ AND THE PARAMETER IS NAMED I18n ON PURPOSE. Named `tr`, the call sites
#   read `tr("…")` — and tools/qml-xgettext.py keys on `I18n.tr(`, so it read
#   the file, matched none of them, and reported success. 64 strings silently
#   absent from a template that looked complete. Named I18n, every site spells
#   itself the way every other file in the tree does and the extractor needs no
#   special case. The suite now asserts the RESULT — that "Welcome to
#   SynapseOS" is in the .pot — rather than the spelling.
#   ⛔ AND EVERY ROW CARRIES WHAT `synctl dispatch` IS GIVEN BESIDE ITS LABEL.
#   `action`, `arg`, `id`, `kind` and `live` are matched on; `key` is a picture
#   of keycaps AND only a fallback, since the live chord comes from `synctl
#   binds`. All six are asserted untranslated, in .js as well as .qml.
#   265 msgids now, 265/265 in all thirteen. ⚠ Seven of the new ones are the
#   page blurbs, which gettext WRAPS — `msgid ""` plus continuation lines — so
#   a `^msgid "…"` grep skipped them and reported the catalogs full when they
#   were seven short. Joined before asking, the same rule the msgstr side
#   already learned.
# 587: eight more C files speak thirteen languages — and two mojibake that
#   have been on screen since they were written.
#   bt.c, dispcfg.c, news.c, sound.c, wppick.c, clock.c, notif.c and
#   ai_interface.c join po/POTFILES: 887 msgids now, 887/887 in all thirteen.
#   ⛔ TWO LIVE MOJIBAKE IN dispcfg.c, both drawn in the display panel's status
#   line. The scale row wrote C3 82 C2 B7 where it meant · and the rotation row
#   wrote C3 A2 C2 86 C2 92 where it meant → — UTF-8 that made a Latin-1 round
#   trip and was re-encoded, so each byte of the original became its own
#   two-byte sequence. NOT the \xNN escape mistake Updates.qml had at 584;
#   these are literal bytes. A byte-level sweep of the whole tree found exactly
#   these two and nothing else — the two hits in Updates.qml and this file are
#   the write-ups QUOTING the old bug, which is correct.
#   ⚠ The first sweep found neither: `grep -P` would not match the byte
#   sequence in either locale, and the pattern was too narrow besides — the
#   general signature is C3 xx followed by C2 xx, not just the â form.
#   ⛔ syn_display_mode_names[] IS A KEY THREE WAYS OVER — display_mode_from_name()
#   compares it, config.c parses it, input.c takes it as a dispatch argument and
#   settings_state_set() writes it to the state file. The status line gets
#   syn_display_mode_label() instead, so the value stays "mirror" and the word
#   says Spiegeln. Same shape as ctlpanel's labels beside its settings keys.
#   ⛔ AND tests/i18n.sh CAUGHT A PLURAL HACK THE MOMENT notif.c WAS LISTED:
#   `missed == 1 ? "" : "s"`, which is what P_() exists for. It is ngettext now.
#   ⚠ AND po-fill.py WAS LEAVING FIVE STRINGS FUZZY. gettext writes ONE flag
#   line — `#, fuzzy, c-format` — and the tool stripped only a line that was
#   exactly `#, fuzzy`. A fuzzy entry is one msgfmt DOES NOT USE, so five
#   correctly translated c-format strings would have shipped English. It now
#   drops the flag and keeps the rest of the line. ⚠ Swept every catalog in the
#   project afterwards: no other component was affected.
#   Verified by compiling the catalogs to .mo and calling gettext/ngettext
#   through a real locale — localedef into a scratch LOCPATH, because this box
#   has none of the thirteen generated and ⛔ LANGUAGE is vetoed under C, which
#   is the same rule I18n.qml implements on the QML side.
# 589: RIGHT-CLICK UNINSTALL, in both application lists — the start menu and
#   the full-screen "show all apps" grid. Each opens a context menu on the
#   application under the pointer with one row, and that row opens a terminal
#   running `synpkg remove --owner` (synpkg 52).
#   ⛔ NEVER --noconfirm. synpkg's remove is -Rns: it takes the unneeded
#   dependencies and the config files with it. It has to print that list, ask,
#   and authenticate through polkit — all three need somewhere to happen, and a
#   front-end that silenced the question would uninstall an application and
#   everything it dragged out on one right-click and one left click.
#   ⛔ ONE ROW, AND NOTHING BESIDE IT. A menu whose rows are Open / Pin /
#   Uninstall puts the destructive one a slot from the two anybody clicks
#   without reading. If it ever grows, Uninstall goes last and behind a
#   separator — the rule that put Quit All Windows at the bottom of the dock's
#   menu and Shut Down furthest from the hand in the power one.
#   ⚠ THE TWO MENUS DIFFER IN ONE WAY, deliberately: the start menu asks
#   `synpkg owner` while it draws and says "Not from a package" where nothing
#   owns the entry; the grid cannot, because it is the compositor and the answer
#   would have to arrive by blocking the input handler. There the row is offered
#   and synpkg refuses in the terminal, by name, before it escalates.
#   ⚠ The grid's menu is modal to the keyboard as well as the pointer. Left to
#   fall through, typing would go on editing the search box behind it — and the
#   refilter would move the application the menu is about out from under it.
#   ⚠ tests/menu_uninstall.sh is new, and three earlier versions of it PASSED
#   on an absent feature; each one is written up in the file. The last was
#   `vpointer_click X Y left`, which clicked ZERO times because there is no
#   `left` keyword — the third argument is a count and atoi("left") is 0. The
#   tool refuses that form now rather than doing nothing quietly.
# 590: WHERE MORE WALLPAPERS COME FROM — wallhaven.cc, in the picker.
#
#   Super+W lists what is already on the disk. Super+Ctrl+W, and a "Wallhaven"
#   row at the bottom of the picker's built-ins, is a grid of what is on
#   wallhaven.cc: category chips, sorted by what is popular, paged. Picking one
#   downloads it into ~/Pictures/Wallpapers — the FIRST directory wppick_scan()
#   walks — and hands it to synctl dispatch wallpaper, which lands in
#   wppick_set_path(). So a wallpaper taken from wallhaven is a local wallpaper
#   from then on, in Super+W's list, with nothing remembering where it came
#   from and one definition of "make this the wallpaper".
#
#   ⛔ OFF BY DEFAULT. This is the second thing in synui that talks to the
#   internet — weather is the other — and `synui-wallhaven on` is the whole
#   opt-in. Nothing resolves a name until it is given, the launcher checks
#   before it starts the window, and the results are pinned to wallhaven's
#   `sfw` purity, which is not a setting: the other levels need an API key,
#   and asking somebody to paste a credential into a wallpaper picker is a
#   promise this cannot keep.
#
#   ⚠ QUICKSHELL, NOT COMPOSITOR CODE, and that was decided by measurement. A
#   grid of remote thumbnails is HTTP, JSON and JPEG decoding, and this process
#   is the one place on the machine where a slow DNS lookup is a frozen
#   desktop; weather.c and news.c each pay for their network with a worker
#   thread and a stop flag wired into libcurl's progress callback. A QML Image
#   loads an https URL by itself, asynchronously, with its own cache —
#   confirmed against a real wallhaven thumbnail in a nested compositor before
#   a line of this was written.
#
#   ⛔ NO jq. This tree deliberately has none (see depends=); the JSON is read
#   by python3, which synui already depends on and synui-media-inhibit already
#   uses.
#
#   ⛔ THE PICKER APPLIES ON HIGHLIGHT, so an action row had to be DEFERRED or
#   scrolling past it would open a network browser. It reuses `pending_we` —
#   the field Workshop rows already use to mean "this row is waiting for Enter"
#   — rather than a second mechanism.
#   ⚠ AND WITHOUT THAT THE ROW WAS DEAD, which reading the apply path alone did
#   not show: Enter only commits a DEFERRED row, so a row that applied nothing
#   on highlight was never deferred and Enter had nothing to commit. The rig
#   caught it; the count of spawns is the assertion, because the whole key
#   sequence has to go in one wtype.
#
#   ⚠ Theme.wpAccent IS A STRING, not a color — `.r`/`.g`/`.b` are not on it.
#   The first chip style filled with the accent and inked with the popup
#   background, which on a dark wallpaper is dark on dark: the three chips that
#   were ON were the three nobody could read. Caught in the rig's first
#   screenshot.
# 591: THE KEY THAT DID NOTHING, AND `w` BETWEEN THE TWO WINDOWS.
#
#   Reported as Super+Ctrl+W not responding.
#
#   ⛔ THE KEY WAS FINE. It was bound, dispatched, and spawned the launcher —
#   which refused, because the switch is off by default, onto the stderr of a
#   process nobody was reading. Every layer reported success and the desktop
#   did not move. 590's own comment argued for checking there ("a key that says
#   how to turn it on rather than a panel that says turn me on"), and the key
#   cannot say anything: it has no terminal. So the WINDOW asks now — it opens
#   on its switch, says what saying yes costs, and reaches wallhaven.cc for
#   nothing until the button is pressed. `search`/`get`/`set` still refuse,
#   because those run where somebody can read the refusal, and die() posts a
#   notification whenever stderr is not a terminal.
#
#   ⛔ AND THE BROWSER'S WHOLE KEYBOARD WAS DEAD. Keys.onPressed sat on the
#   PanelWindow, and Qt delivers keys to an ITEM with active focus inside the
#   surface — layer-shell granting the surface focus is not enough, which
#   Ui/KeyboardPanel.qml already says in as many words. Escape, Enter, the
#   arrows, 1 2 3 and S were all advertised in the footer and all answered
#   nothing, silently, since 590. Found by pressing them; there is no build,
#   lint or runtime error for a key handler nobody is listening to.
#   ⚠ Keys.BeforeItem with it, or the grid's Flickable eats the arrows.
#
#   `w` flips between the two halves of picking a wallpaper: a [w] Wallhaven
#   BUTTON in the picker's header (its label taken from the row's, so the two
#   cannot drift), and a Wallpapers button in the browser's. Whichever window
#   you leave closes — both are full-screen and both want the keyboard, which
#   is the same reason the Wallhaven row closes the picker. Super+Ctrl+W now
#   puts the picker down too, which it never did.
#
#   Two rigs, because the thing that failed was reachability rather than logic:
#   wallhaven_key.sh presses Super+Ctrl+W and `w` against a real compositor and
#   counts what the launcher was ACTUALLY run with (twice for one `w` would
#   mean the picker stayed up and the second press closed the browser again),
#   and wallhaven_window.sh presses Enter/w/Esc at the window and asserts the
#   switch's promise from the ledger — nothing leaves the machine before
#   consent. wppick_wallhaven_test.c is the unit half.
# 592: THE BROWSER ON ONE MONITOR, NOT ON ALL OF THEM.
#
#   Reported after 591 went in: with the switch on, the browser opened on every
#   screen at once.
#
#   ⛔ AN UNNAMED OUTPUT MEANT "ALL OF THEM". wallhaven.qml is Variants over
#   Quickshell.screens with `visible: root.output === modelData.name ||
#   root.output === ""`, and nothing ever set root.output — so the empty case,
#   which means NOBODY SAID, was reading as a request for a window per screen.
#   On one monitor the two are the same picture, which is why it shipped twice.
#
#   ⚠ NO WAYLAND PROTOCOL TELLS A LAYER-SHELL CLIENT WHERE FOCUS IS, so the
#   window cannot work this out — the name has to come from synui, which knows
#   because it is answering the keypress that asked for the browser. That is
#   exactly the welcome guide's mechanism and it is now shared: input.c's
#   exec_named_output() runs `synui-<thing> <verb> <output>` for both.
#
#   ⚠ AND THE FIRST WINDOW CANNOT BE TOLD OVER IPC. `quickshell -p file.qml`
#   takes no positional argument and the IPC path needs a process already up,
#   so the launcher exports SYNUI_WALLHAVEN_OUTPUT for the start and calls
#   `ipc call wallhaven toggle <output>` for every toggle after it. Same split,
#   same spelling, as SYNUI_WELCOME_OUTPUT.
#
#   Started from a prompt with no name at all, it opens on the FIRST screen —
#   Osd.qml's rule. One screen's worth of not knowing is not three windows.
#
#   ⚠ BOTH RIGS RUN ON TWO HEADLESS OUTPUTS NOW. With one, "the focused
#   monitor" and "every monitor" draw the same picture and neither rig could
#   have failed: wallhaven_window.sh counts the layer surfaces synui accepted
#   (two, on the old code — confirmed by putting the old line back), and
#   wallhaven_key.sh checks the name the launcher was given against
#   `synctl outputs` rather than against "not empty".
# 593: THE GUIDE NEVER SAID WHAT A LAYOUT WAS.
#
#   The welcome guide named Super+Tab in one line of prose on its keys page —
#   "cycle layout" — and that was the whole of it. Seven layouts ship, they are
#   the thing that makes this desktop feel unlike the last one, and the first
#   window a new install shows never said what any of them does. Pressing the
#   key on an empty desktop moves nothing, so the honest reading of the guide
#   was that Super+Tab did nothing.
#
#   Page three is Layouts now: the three doors that already exist (layout_cycle,
#   retile, cascade), a fourth onto Control Panel ▸ Desktop ▸ Layout, and a line
#   per layout in CYCLE ORDER, so the list can be followed with the key it
#   documents.
#
#   ⚠ ONLY THREE LAYOUTS ARE REACHABLE BY ACTION. There is no
#   `layout_set <name>`, so the other four cannot be rows: a row Enter does
#   nothing with is the dead spot GuideRow refuses to draw. They are notes, and
#   the control panel row is the door that reaches all seven.
#
#   ⚠ AND EVERY LINE HAS TO FIT THE CARD. The card is 620px whatever the
#   monitor is, the arrows step row to row and SKIP prose, and nothing scrolls
#   the list from the keyboard — so a note past the bottom edge is reachable by
#   wheel alone. The first draft ran three notes over and lost spiral and
#   cascade off the end; the copy is one line per layout because of it, checked
#   in English, German (the longest) and Arabic.
#
#   ⚠ A ROW WITH AN ARGUMENT TAKES NO LIVE CHORD. GuideState's bind probe keeps
#   only the binds carrying NO argument, so binds[action] is the chord for the
#   BARE action — and "Pick a layout" dispatches `control Desktop`. It would
#   have shown Super+C, which opens the panel's front door instead.
#
#   All thirteen catalogs filled in the same pass; the guide's every word lives
#   in welcome/pages.js, which po-bar/POTFILES already reads.
# 594: TRANSPARENCY OFF MADE THE MENU TRANSPARENT.
#
#   Reported with two screenshots seconds apart: the start menu frosted and
#   readable with Transparency on, and the desktop clock legible straight
#   through it with Transparency off. Ironic, and exactly backwards.
#
#   ⛔ THE FLAG WAS ON THE WRONG ARM OF A TERNARY. Theme.popupAlpha resolves in
#   three steps — the bar's own alpha, then `glassSurfaces`, then the scheme's
#   0.97 — and glassSurfaces sat SECOND, which is the same as not being there:
#   the desktops that reach the second arm are the ones where nothing set a bar
#   opacity, and the flag exists for the ones where something did. A bar dialled
#   to 0.05 and pinned there took the menus down with it whatever the master
#   switch said, and with `transparency = off` synui stops frosting behind
#   them — so the switch whose name is "nothing sees through" produced the most
#   see-through menu this desktop can draw. theme.c's own comment beside the
#   glass_surfaces export describes this failure in as many words; the QML
#   asked the question one line too late to prevent it.
#
#   ⚠ THE BAR IS NOT WRONG THERE AND IS LEFT ALONE. A clear bar with no blur is
#   a supported look and a working one — its ink comes off the wallpaper
#   (barPalette, the per-module strip, the scrim) and that is what
#   `bar_opacity = 0` has always meant. The menus have no such machinery, which
#   is the whole reason the flag exists for them and not for the strip.
#
#   ⛔ AND THE FIRST FIX WENT TOO FAR, on the way to this one: gating the C-side
#   syn_bar_alpha_asked()/syn_dock_alpha_asked() on the master switch as well.
#   `transparency` DEFAULTS TO OFF (config.c), so that quietly deleted
#   `bar_opacity` on every desktop that had never switched transparency on —
#   caught by tests/bar_opacity.sh, which is the rig that says what that row
#   means.
#
#   tests/glass_master_off.sh is the regression: two compositors differing in
#   one line of theme.state, asserting that OFF draws a real surface AND that ON
#   still draws glass — the second half is what stops "make it all opaque" from
#   passing. It measures the MEDIAN colour of a strip inside the panel, because
#   a frosted surface is dithered by blur_noise and its most common single
#   triple covers a fifth of a perfectly good panel; menu_surface_pages.sh had
#   the same probe and gains the same fix, plus the theme.state it turns out it
#   always needed — without one it had been testing a solid 0.97 menu and
#   agreeing with itself.
# 595: THE APPLICATION LIST WAS THE ONE THING STILL IN ENGLISH.
#
#   Every application ships its own translations in the file that describes it —
#   `Name[de]`, `Name[pt_BR]`, `Name[sr@latin]`; Firefox ships thirty of them —
#   and both readers here took the plain key. appgrid.c said so in a comment:
#   "a real locale match belongs with the rest of i18n". So a German desktop had
#   a German compositor, a German bar, German panels, and an application grid
#   and a set of desktop icons in English. velle, 2026-09-03: the app titles are
#   largely translatable and all still in English.
#
#   synui_desktop_locale_rank() (i18n.c) scores one key against one locale on
#   the spec's own candidate list — lang_COUNTRY@MODIFIER, lang_COUNTRY,
#   lang@MODIFIER, lang, then the plain key — and the two scans keep the best
#   ranked value rather than the first or the last one they see. The localised
#   keys sit in arbitrary order around the plain one, so "first" and "last" are
#   both wrong, which is exactly why this was left undone rather than done
#   badly.
#
#   ⛔ THE ENCODING IS DROPPED ON BOTH SIDES. A desktop's locale is
#   `de_DE.UTF-8` and the key is `Name[de]`; compare them whole and nothing
#   matches, in any language, on any machine — a fix that looks right and
#   changes nothing.
#
#   ⛔ AND `Name[pt_BR]` IS NOT A CANDIDATE ON pt_PT. The spec's list is exact;
#   falling back to it because the language matched hands Portugal Brazilian
#   Portuguese, which is worse than English.
#
#   ⚠ THE LOCALE COMES FROM setlocale(), NOT THE ENVIRONMENT, so the list
#   follows the same answer gettext is giving the rest of the desktop: on a box
#   where the locale is not installed, everything stays English together rather
#   than German names appearing down an English menu. And it is read per call
#   rather than cached — a cache is what makes a matcher untestable in-process.
#
#   ⚠ Name ONLY, not Exec or Icon. A launcher whose Exec came from `Exec[de]`
#   would run a different program in Germany.
#
#   tests/desktop_locale_test.c pins the ranking (a pure function, so every case
#   is asserted where none of those locales exists); appgrid_test.c pins the
#   wiring at both ends — the C locale takes the plain key, and a de_DE desktop
#   built with localedef into a scratch LOCPATH takes `Name[de_DE]` over
#   `Name[de]` over `Name`.
#
#   ⚠ WHAT THIS DOES NOT DO: synui's own thirty .desktop entries still carry
#   English Names only, so the desktop's own launchers are the remaining English
#   ones. That is a catalog job (`xgettext --language=Desktop` + `msgfmt
#   --desktop`), not a reader job, and it is the next piece.
# 596: SOMEBODY IS LOOKING AT THIS SCREEN — the bar indicator for syn-remote(1).
#   A remote desktop is the one thing on this machine that can be running with
#   no window, no sound and no trace on screen, and `syn-remote on` survives a
#   reboot by design — so a session started for one afternoon of help is a
#   session still listening a month later and nothing about the desktop looks
#   any different. The pill is what makes being watched visible, and the click
#   ends the session (`syn-remote stop`, not `off`: turning remote access off
#   for good is a decision to make in Settings with the rest of the picture in
#   front of you, not on one click of an alert).
#
#   ⚠ IT APPEARS WHEN SOMEBODY IS CONNECTED, NOT WHEN THE SERVER IS RUNNING —
#   Recording.qml's rule and its reason: a pill that is up whenever the feature
#   is enabled is furniture, and furniture stops being read.
#   ⛔ AND THE COUNT IS GATED ON THE SERVER ACTUALLY RUNNING, in syn-remote
#   itself. It comes from a state file, and a state file outliving what wrote
#   it would leave this claiming a viewer over a server that is not there —
#   worse than no indicator, and the hazard Recording.qml avoids by reading
#   /proc instead.
#
#   ⚠ trn(), not tr(), for the count: two is dual in Arabic and 2-4 is its own
#   form in Russian and Polish. And both glyphs (fa-eye / fa-eye_slash) were
#   checked against SymbolsNerdFontMono with fontTools before being used, which
#   is the rule Icons.qml states twice — a codepoint the font lacks draws as a
#   box that no test notices.
# 597: A FIFTH ANALOG CLOCK FACE — a pink animal holding the dial.
#   velle asked whether the Vista/7 sidebar gadget could be recreated; the code
#   is gone and was behind a site that no longer exists.
#
#   ⛔ SO IT IS DRAWN HERE, NOT COPIED. The gadget's artwork is somebody's
#   drawing and it is theirs — the same rule syn-arcade's icon README states at
#   length about application logos, for the same reason: art somebody else owns
#   has no business inside a GPL package. This is an original animal in the same
#   spirit, and every curve of it is arithmetic in AnalogClock.qml.
#
#   ⚠ IT IS PINK ON EVERY THEME, deliberately. The other four faces are the
#   theme's ink because they are chrome; this one is a CHARACTER, and a
#   character whose colour follows the accent is a different character on every
#   desktop — the call this project already makes for Tux. ⚠ Which is why the
#   dial brings its own white face: `ink` is light on a dark theme, and light
#   ink on the white belly of a pink animal is a clock nobody can read.
#
#   ⛔ FIVE LISTS NAME THE FACES — the enum, the panel's option names (which ARE
#   the config values, folded to lower case by ctl_format), config.c's parser
#   and BOTH QML validators. A face missing from one of the five is silently
#   unreachable: the panel offers it, the file records it, and the widget falls
#   back to `minimal` with nothing said. tests/clock_faces.sh is that check, and
#   it also asserts the option names are NOT translated — a translated one
#   writes a word config.c cannot read back.
# 600: THE DIM INK WAS FIXED ONCE AND KEPT SHIPPING. 492 replaced
#   pick("#3a4a52", "#6b7280") — "a grey from memory" — with a blend measured
#   against the surface, and did it for `Theme.fgDim` ALONE. Both STRIP
#   palettes went on returning the identical pair, and the strip is the one
#   surface a theme can know nothing about: a clear bar is drawn on the
#   WALLPAPER.
#
#   Measured off a screenshot rather than argued. On a portrait bar over a
#   bright sky the empty workspace digits came out #6b7280 on cream at
#   2.84:1; on the dark green at that same bar's left the other half of the
#   pair, #3a4a52, lands at 1.03:1. The ink beside them is 9.9:1 and 9.5:1.
#   `pal.dim` is the empty desktop digit, the muted volume, the stale weather,
#   the disabled network and the paused media — text that is there and cannot
#   be read.
#
#   ⚠ IT LOOKED LIKE A PORTRAIT BUG AND WAS NEVER ONE. The portrait panel is
#   only where a light-measuring span and eight empty desktops coincide; the
#   dark-strip half of the literal is the worse of the two.
#
#   ⛔ THE FIX IS ALPHA, NOT AN OPAQUE MIX. fgDim can blend toward popupBg
#   because it KNOWS popupBg; this palette carries `inkOnDark` and no colour at
#   all, so the only way to sit a fixed fraction from the real background to the
#   ink is to let the compositor blend it — which then tracks every wallpaper,
#   per span, for nothing. ⚠ The objection alpha carries for fgDim does not
#   reach here: that one also fills mixer sliders, the knob and the grip's dots,
#   where a groove would show through, and on the strip `dim` is only ever a
#   glyph. hoverBg and activeBg beside it are already alpha.
#
#   0.75 is measured, not taste: 5.4:1 on that cream and 6.1:1 on that dark
#   green, still visibly quieter than the ink. An opaque strip takes root.fgDim,
#   the same `clear ? ink : theme` shape every other line in barPalette uses.
#
#   tests/bar_dim_ink.sh is the guard, and it checks BOTH literals — the same
#   reason bar_palette_keys.sh does. No pixel test can reach this: it needs a
#   wallpaper whose measured ink flips, an empty desktop, and the monitor it
#   lands on.
# 601: GAME MODE RELEASES THE AI MODEL AGAIN — 599 stopped doing that and it
#   should not have. It replaced the daemon stop with SYN_MSG_DEMAND "high",
#   which only raises the VRAM floor the offload policy defends: synapd sheds
#   GPU layers and keeps the model.
#   ⛔ A SHED LAYER IS NOT A FREED LAYER. llama.cpp has no live migration, so a
#     re-fit reloads at a lower n_gpu_layers and the weights that came off the
#     card are resident in SYSTEM RAM and computed on the CPU. A game competes
#     for VRAM, RAM and CPU; 599 measured the first (7300 MiB with synapd up
#     against 2655 without) and moved the cost onto the other two. Nobody is
#     querying the assistant during a game.
#   ⛔ SYN_MSG_SLEEP on the way in, SYN_MSG_WAKE on the way out — the suspend
#     hook's path, which since synapd 51 leaves the retrieval embedder alone.
#     That was the real defect in the `systemctl stop synapd.socket
#     synapd.service` both of these replaced: it took a 274 MB embedder down
#     with the chat model and killed the socket, so chibi's memory went dark for
#     the length of every game. Releasing keeps all of that and gives the game
#     everything it actually competes for.
#   ⚠ WAKE IS NOT OPTIONAL. Nothing reloads a sleeping model on its own — a
#     query in that window is refused, not queued behind a load — so game_leave
#     AND game_finish send it, the latter for a compositor dying mid-game.
#   ⚠ 1.5 s socket timeout, stated rather than discovered: SLEEP answers only
#     once the VRAM is really gone (that is what lets the suspend hook wait for
#     it), and this runs on the compositor thread. A shorter timeout would not
#     make the release faster, only make us report that it did not happen.
#   The indicator reads "released" now; a state file from 599/600 still says
#   "yielded" and is rendered as what it actually meant — VRAM only, model still
#   in RAM. tests/game_actions.sh pins the policy in four checks, RED-verified
#   against 600's game.c.
# 602: A BAR TOOLTIP OPENED CLIPPED OFF THE RIGHT EDGE OF THE SCREEN, and came
#   up correctly on the second hover. That second half is the whole diagnosis:
#   two independent defects met, and either one alone produces it.
#   ⛔ BarModule.qml centred the popup on `tip.width`. That is the WINDOW's
#     CONFIGURED width, which always lags its content — unset before the
#     compositor's first configure and stale after it — so the first show was
#     placed by a width the popup no longer had. Measured on the rightmost
#     module: 134px of a 211px tooltip on screen, 77px off the side. rect.y
#     beside it had always asked implicitHeight, and both bar menus already
#     asked implicitWidth; this one line was the odd one out.
#   ⛔ layer.c unconstrained a popup on its INITIAL COMMIT ONLY. When the true
#     width landed, the binding re-ran and the popup REPOSITIONED —
#     xdg_popup.reposition installs a fresh positioner and recomputes the
#     geometry from its rules, discarding what the map-time unconstrain decided.
#     wlroots fires wlr_xdg_popup.events.reposition for exactly this and synui
#     listened to neither popup path's copy of it, so every repositioned popup
#     went precisely where the client asked. The second hover already knew its
#     width, never repositioned, and so never hit the hole.
#   Both are fixed. The QML one removes the pointless round-trip (and the jump
#   that goes with it); the compositor one is the wider fix — it was never about
#   the bar, and any client that moves a live popup was landing unconstrained,
#   synui_main.c's toplevel-rooted path included.
#   ⚠ NOT clamped in QML. The compositor owns unconstraining; a clamp beside it
#     would be a second owner of one rule, disagreeing at every bar shape.
#   tests/bar_tooltip_edge.sh is the guard and it hovers TWICE, because a
#   screenshot cannot tell a clipped tooltip from a correctly slid one — both
#   end at the last column. The second hover is the known-good placement and the
#   first must match it. RED-verified against 601: first 1146..1279, second
#   1069..1279. An earlier draft asserted the right edge instead and passed on
#   the bug; the note in that file says so.
pkgrel=602
pkgdesc="SynapseOS Wayland Compositor"
arch=('x86_64')
# GPL-2.0-or-later is synui's own code. MIT covers quickshell-antiquity/, a port
# of diinki's linux-antiquity whose LICENSE.antiquity ships inside that tree; the
# fonts it carries are Apache-2.0 (Material Symbols) and ITF-credit-clause
# (Boska/Recia/Quilon), both documented in quickshell-antiquity/FONTS.md.
# CC-BY-SA-4.0 is ONE wallpaper — Daniel Schwen's Gateway Arch photograph in
# data/wallpapers/, shipped verbatim. Mere aggregation: it sits beside the code
# in the package, is not linked into it and is not a derivative of it, so it
# raises no GPL/CC compatibility question. Attribution ships to
# /usr/share/licenses/synui/WALLPAPERS.md; the reasoning is in
# data/wallpapers/WALLPAPERS.md.
#
# ⚠ AND OMARCHY (MIT, © David Heinemeier Hansson) IS A THIRD RELATIONSHIP, and
# the distinction is worth stating because all three words apply somewhere:
#
#   * quickshell/Ui and quickshell/Commons are OURS. They carry Omarchy's NAMES
#     and their documented contract — which is not copyrightable — over synui's
#     own theme, so a widget written for their bar runs on this one and looks
#     like it belongs here. Nothing is copied; GPL, like the rest of synui.
#   * tests/plugin_load.sh vendors ONE file of theirs verbatim as a fixture,
#     with their copyright and permission notice inline beside it, because a
#     compatibility claim proved with a widget we wrote ourselves proves nothing.
#   * Anything a user installs with `synui-plugins add` is THEIRS and is fetched
#     at run time — never in this package. The installer copies the repository's
#     LICENSE in with the code, because MIT wants the notice in "all copies" and
#     a file put on somebody's disk is one however it got there.
license=('GPL-2.0-or-later' 'MIT' 'Apache-2.0' 'CC-BY-SA-4.0')
# xdg-desktop-portal{,-wlr,-gtk}: screen sharing. Without the wlr backend, portal
#   falls back to GTK, which cannot do ScreenCast on wlroots — so Firefox/OBS/
#   Discord screen capture silently fails. slurp is xdpw's output chooser.
# rtkit: pipewire/wireplumber cannot get realtime scheduling without it and the
#   audio stack xruns under load (chibi's TTS, and anything playing during a game).
# grim/slurp/wl-clipboard: the Print-key screenshot tool (synui-screenshot).
#   slurp is also xdpw's output chooser.
# udisks2/zenity: synui-iso-mount, Dolphin's right-click Mount Image. udisks2
#   does the privileged loop-setup+mount (polkit allow_active, so no root);
#   zenity is how the service menu reports a failure — a menu item that fails
#   silently looks like a broken menu item.
# quickshell/wtype: the bar (quickshell/ + synui-bar), and the virtual keyboard
#   the bar's SYNAPSE button uses to ask synui for the start menu. synui ships
#   the bar's whole QML tree to /usr/share/synui/quickshell, so depending on the
#   runtime keeps the config and the thing that reads it together.
#   A SECOND shell ships alongside it: /usr/share/synui/quickshell-antiquity, a
#   port of diinki's linux-antiquity. `bar_shell = antiquity` in synuirc (or
#   Control panel ▸ Desktop ▸ Bar shell) picks it; synui-bar resolves the key.
#   Both trees are complete and independent — there is no shared QML — because
#   the two disagree about nearly every visual decision, and a common base would
#   be a third thing to keep working.
#   waybar was the bar until 2026-07-24 and is deliberately NOT a dependency any
#   more — see systemd/synui-bar.sh for what moved and why. The waybar config is
#   still packaged (config/waybar/, and synui-apply-theme still generates its
#   stylesheet), so a user who installs waybar themselves keeps a working bar;
#   nothing in SynapseOS starts it.
# libpulse: synui-record asks pactl which sink/source to record — wf-recorder's
#   bare -a is the MICROPHONE (ffmpeg's pulse default source), so the desktop
#   audio device has to be resolved and named explicitly. Always present via
#   pipewire-pulse in practice; named here because a binary is exec'd.
# wf-recorder: Super+Shift+R (synui-record). Recording means encoding, and
#   ffmpeg is not something the compositor should grow — wf-recorder captures
#   through wlr-screencopy, which synui already exports for grim.
# libnotify: synui *serves* org.freedesktop.Notifications (src/notif.c), so it
#   needs no client library for that — but notify-send is what synui-screenshot
#   uses to say "saved", and it lives here. Nothing else on the ISO pulled it in,
#   so that toast had no chance of appearing even once a daemon existed.
# bluez: bt.c is a native BlueZ client (Super+B) — it talks org.bluez over sd-bus
#   rather than driving bluetoothctl, but the daemon still has to be there, and
#   bluez enables no unit of its own: without `systemctl enable bluetooth` the
#   radio stays down and the panel correctly reports no adapter.
# ttf-nerd-fonts-symbols-mono: the bar renders its modules as glyphs (bluetooth,
#   wifi, volume, cpu…) — quickshell/Icons.qml names the codepoints. DejaVu Sans
#   Mono has none of those Private-Use codepoints, so without this font every
#   icon is a tofu box, and the icon-only modules (bluetooth) collapse to
#   zero width and vanish rather than showing anything at all.
# noto-fonts-emoji / noto-fonts-cjk: what src/text.c falls back TO. The
#   compositor draws every panel through cairo's toy font API, which resolves to
#   ONE face and has no per-glyph fallback, so a character that face lacks used
#   to draw as '?'. text.c now asks fontconfig for a font that can draw it —
#   but fontconfig can only answer with a font that is installed, so without
#   these two the fallback is a no-op for exactly the cases it exists for.
#   Measured on the dev box before they were added: 932 of 1824 emoji
#   codepoints drawable and NONE in colour, and no CJK coverage at all.
#   noto-fonts-emoji is also what makes the emoji picker anything other than a
#   grid of tofu, so it is a depend rather than an optdepend.
# curl: news.c fetches the RSS/Atom feeds with libcurl (on its own thread).
# xdg-utils: the news panel opens a story with xdg-open — no shell in between,
#   since those URLs come off the network. Without it, Enter does nothing.
# rofi: the Super+= application launcher. A hard depend for the same reason
#   syntty is one — it backs a DEFAULT bind, so without it one of the most-pressed
#   keys on the desktop silently does nothing. rofi 2.0.0 merged lbonn's Wayland
#   port into mainline (it Provides/Replaces rofi-wayland), so this is
#   layer-shell, not XWayland. The AI command bar has Super+Space.
         # syntty is the default terminal (0.1.0-359) and the one every fresh
         # synuirc names, so it is the terminal this depends on. kitty was the
         # default between 215 and 358 and is an optdepend now: it stayed a
         # hard dependency for four months after it stopped being the default,
         # which made 65 MiB un-removable — `synpkg remove kitty` was refused
         # with "synui: requires kitty" for a terminal nothing opens by default.
# ⛔ THE ICON THEMES ARE A DEPENDENCY, NOT A NICETY, and until now they were
# neither — not here and not in the ISO's package list. Every synui-*.desktop
# this package ships names a STANDARD freedesktop icon (office-calendar,
# preferences-system-bluetooth, utilities-terminal …), and a standard name is
# only a name: something has to provide the file. On the machine SynapseOS is
# built on, KDE and GTK applications had dragged both themes in years ago, so
# the menu looked right and nothing said it was borrowing. On a clean install —
# velle's ThinkPad, 2026-08-28 — the apps menu is missing calendar, bluetooth
# and the rest, with no error anywhere.
#
# Counted rather than guessed: of the icon names these entries use, 22 come from
# breeze-icons and 15 from adwaita-icon-theme-legacy. Neither covers both sets,
# so both are named. adwaita-icon-theme-legacy also pulls hicolor-icon-theme,
# which is where this package installs its OWN icons.
#
# ⚠ THE `legacy` ONE, NOT adwaita-icon-theme ALONE. Adwaita dropped the full-
# colour legacy names in its move to symbolic-only; utilities-terminal and
# media-eject resolve out of AdwaitaLegacy on this machine and nowhere else.
#
# Same reasoning as synfiles' shared-mime-info: it is a base package on any
# desktop, depending on it costs nothing, and it removes a whole class of "why
# is half the menu blank".
depends=('breeze-icons' 'adwaita-icon-theme-legacy'
         'wlroots0.20' 'scenefx0.5' 'wayland' 'libxkbcommon' 'pixman' 'seatd' 'syntty' 'rofi' 'cairo'
         # dbus: synui_run() execs dbus-update-activation-environment before the
         # autostarts, without which D-Bus-activated GUI services get no display.
         # Always present transitively, named here because we exec its binary.
         'dbus' 'librsvg' 'fontconfig'
         # text.c shapes Arabic and the Indic scripts through HarfBuzz and
         # resolves their reading order through FriBidi. Both are already here
         # transitively — harfbuzz under librsvg one line up, fribidi under
         # pango and gtk — so neither adds a package to the ISO. Named because
         # synui links them, not because they were missing.
         'harfbuzz' 'fribidi' 'ttf-dejavu' 'ttf-nerd-fonts-symbols-mono'
         'noto-fonts-emoji' 'noto-fonts-cjk' 'libinput' 'libglvnd' 'swaylock' 'libjpeg-turbo' 'giflib' 'curl' 'libdisplay-info' 'xdg-utils' 'pipewire' 'rtkit' 'python' 'xdg-desktop-portal' 'xdg-desktop-portal-wlr' 'xdg-desktop-portal-gtk' 'slurp' 'grim' 'wl-clipboard' 'udisks2' 'zenity' 'bluez' 'libnotify' 'quickshell' 'wtype' 'wf-recorder' 'libpulse' 'unzip'
         # Archive extraction for the Dolphin service menu. 7zip reads zip and 7z
         # (and rar), unrar is kept because 7-Zip's rar support does not cover
         # every variant; the menu offers rar, so a fallback is not enough.
         '7zip' 'unrar'
         # ⛔ jq is not for synui's own code — this tree deliberately has none
         # (omarchy-weather-location.sh rewrote upstream's jq calls as a printf
         # and a sed rather than take the dependency). It is here because synui
         # HOSTS Omarchy bar plugins, and Omarchy's base ships jq, so plugin
         # authors shell out to it freely. deep-thoughts prints "jq: command not
         # found" where the thought belongs; quattro-gp's self-registration is
         # jq-guarded inside an execDetached, so without jq its keybinding just
         # never registers and NOTHING is logged. The plugin ABI is not only the
         # qs.Ui types and the omarchy-* commands — it is also the binaries a
         # plugin may assume, and jq is one of them. Rewriting each plugin is
         # not the fix: they update from upstream. 1 MiB.
         'jq'
         # ⚠ cava was an optdepend and the desktop's audio visualiser widget is
         # DARK without it — widgets.c says "cava is not installed, so it will
         # stay dark" and render.c draws "needs cava" where the bars belong. A
         # widget that answers its own switch with an apology on a stock
         # install is not a feature being offered, it is one being listed.
         # There is no alternative to depend on instead: an FFT in QML over raw
         # audio is not something this compositor should grow, which is why the
         # widget shells out in the first place. 200 KiB.
         'cava')
makedepends=('meson' 'ninja' 'wayland-protocols' 'pkg-config' 'cairo' 'librsvg' 'libjpeg-turbo' 'giflib' 'curl' 'libdisplay-info' 'gettext')
optdepends=(# Network printers: cups does the discovery (its own dnssd backend) and
            # owns the queues; avahi is what makes the announcements visible at
            # all. Neither is a hard dependency — a desktop with no printer
            # should not pull in a print system — and synui-printers says which
            # one is missing rather than reporting an empty network.
            'cups: find and set up network printers (synui-printers)'
            'avahi: the mDNS announcements network printers and shares make'
            # ⚠ NEITHER OF THESE IS INSTALLED WITH SYNAPSEOS ANY MORE — kitty
            # came off the base set and the ISO in syn-install 100, because a
            # fresh box was carrying THREE terminals and opening one of them.
            # syntty is the default and a hard `depends`; these two are what
            # somebody adds on purpose, and what an older install already has.
            'kitty: the terminal SynapseOS defaulted to before syntty — add it if you want it; every synuirc written between synui 215 and 358 still names it, and anim.c draws it on glass'
            'foot: a second terminal from a different codebase, so a bug that stops syntty opening still leaves a way to get a prompt — the `term` keybind falls back to it'
            # The lights follow the wallpaper's accent when this is here and
            # `syn-rgb on` has been said. Not a dependency: a desktop with
            # nothing in it that glows should not pull in a lighting daemon,
            # and syn-rgb says which one is missing rather than doing nothing.
            'openrgb: put the desktop accent on RGB hardware (syn-rgb)'
            # ⚠ THE SECOND VOICE, NOT THE FIRST. syn-speak speaks through
            # `vibe voice say` where vibe is installed — chibi's piper — so the
            # screen reader and the assistant sound like the same desktop and
            # one place works out which engine this box has. espeak-ng is what
            # is left when vibe is absent, which on an install that chose no AI
            # stack is the only thing left; without either, the switch turns on
            # and the machine stays silent. Not a hard depends: a desktop that
            # never speaks should not carry a synthesiser.
            'espeak-ng: the voice the screen reader falls back to where vibe is not installed (syn-speak)'
            'xorg-xcursorgen: build cursor themes from source (synui-cursor build)'
            # The music widget's LIBRARY half. Without it the widget is the
            # transport it shipped as — MPRIS, three buttons, whatever is
            # already making noise — which works on every install and can only
            # act on music something else started. syn-arcade brings `big
            # music`: the source picker, the Plex albums and the YouTube
            # stations, and the widget grows a chip that unfolds into them.
            # An optdepend rather than a depend because the shell must not
            # require the television's package to draw a card, and
            # MusicLibrary.qml probes for the command rather than assuming it.
            'syn-arcade: pick a source and play from a library in the music widget'
            'imagemagick: same — most cursor makefiles render their PNGs with it'
            # Event sounds (Super+S) ship OFF, so neither of these is needed for
            # a working desktop — but with sounds turned on and no theme
            # installed, every event resolves to a sample that is not there and
            # the panel's toggles look broken. sound-theme-freedesktop is the
            # one every other desktop uses; libcanberra does the XDG lookup
            # (theme inheritance included) that synui-sound only approximates
            # with a directory search when it is absent.
            'sound-theme-freedesktop: samples for the Super+S event sounds'
            'libcanberra: XDG sound-theme lookup for the event sounds'
            # Extra sound themes. Each adds one entry to the Super+S theme row
            # and to `synui-sound themes`; listed here because "how do I get
            # different sounds?" has no answer the desktop can show you
            # otherwise. Anything not packaged goes in with `synui-sound install`.
            'ocean-sound-theme: extra sound theme for the Super+S picker (Plasma)'
            'sound-theme-elementary: extra sound theme for the Super+S picker'
            'pop-sound-theme: extra sound theme for the Super+S picker'
            'deepin-sound-theme: extra sound theme for the Super+S picker'
            # synui-open-folder takes any of these, so this is a genuine choice
            # rather than a hint. synfiles is first because it is SynapseOS's
            # own and, since 2026-08-10, the distribution default for
            # inode/directory — a complete install already has it, and this
            # entry is for a synui installed on its own. The KIO service menus
            # (Mount Image, Run with Wine, Extract, Crop, Set as Wallpaper) are
            # no longer a reason to prefer Dolphin: synfiles reads
            # $XDG_DATA_DIRS/kio/servicemenus itself, so one helper written
            # once still shows up in both.
            'synfiles: SynapseOS file manager — the default for the Files button and folder handling'
            'dolphin: file manager, the KDE one; no longer the default'
            'nautilus: file manager, alternative to synfiles'
            'thunar: file manager, alternative to synfiles'
            'wine: run Windows .exe/.msi from the file manager (Run with Wine service menu)'
            'mangohud: in-game FPS/temp overlay, any launcher (Shift_R+F12)'
            'lib32-mangohud: same overlay for 32-bit Vulkan titles'
            'gamemode: CPU/GPU governor for synui-game-run (gamemoderun)'
            'gamescope: nesting micro-compositor for synui-game-run --gamescope'
            # An optdepend and not a depend on purpose: this ships on desktops
            # far more often than on laptops with a reader, and pulling in
            # fprintd would start a D-Bus service on every one of them for a
            # device that does not exist. Without it, pam_fprintd.so is missing,
            # synui-lock-fprint reports "unavailable" and the lock screen looks
            # exactly as it always has. Install it (and `fprintd-enroll`) to get
            # the reader on the lock screen.
            'fprintd: unlock the lock screen AND log in with a fingerprint,\n            and enrol one in syn-settings ▸ Fingerprint')
# /etc/MangoHud.conf is meant to be tuned in place — keep a user's edits across
# upgrades instead of overwriting them.
backup=('etc/MangoHud.conf' 'etc/synapseos/mangohud.conf')

# Puts pam_fprintd into greetd's auth stack, so a fingerprint works at the
# LOGIN screen and not only at the lock. See synui.install for why that has to
# be a scriptlet: /etc/pam.d/greetd is greetd's file, and shipping our own copy
# would be a file conflict.
install=synui.install
# ── Where the source comes from, here and everywhere else ──────────────────
#
# ⛔ ONE source LINE SERVES BOTH, AND THAT IS DELIBERATE. build-all.sh runs
# tools/collect-source.sh, which drops $pkgname-$pkgver.tar.gz beside this file;
# makepkg finds it (`-> Found ...`) and never touches the URL. Anybody WITHOUT
# this checkout has no such file, so makepkg fetches the identical tarball from
# the release that carries this exact pkgver-pkgrel. A second PKGBUILD for
# outside use would be a second set of depends and install rules, free to drift
# from this one — and the person it broke for could not see this file at all.
#
# ⚠ ITS OWN REPOSITORY, NOT THIS ONE. The source release lives at
# github.com/velle999/$pkgname — which is also where the PKGBUILD is published
# as a clonable package repo — because putting them on SYNAPSE's releases page
# buried the ISO downloads under a component tarball per bump, and made the
# newest of those GitHub's "Latest release" for the whole project.
#
# ⚠ THE TAG CARRIES THE pkgrel, so the URL cannot point at the wrong source.
# preflight.sh already refuses a source edit that does not bump pkgrel, which
# means every change to what gets built moves this URL with it.
#
# ⛔ AND sha256sums STAYS 'SKIP'. A real checksum would break every LOCAL build
# the moment somebody edited a source file, because the tarball beside this file
# is regenerated from the working tree and would no longer match. The published
# asset is reproducible instead — collect-source.sh sorts and zeroes the
# timestamps, so `tools/collect-source.sh <name>` at the tagged commit
# re-derives it byte for byte. packaging/README.md has the whole of it.
source=("$pkgname-$pkgver.tar.gz::https://github.com/velle999/$pkgname/releases/download/$pkgver-$pkgrel/$pkgname-$pkgver.tar.gz")
sha256sums=('SKIP')

# Refuse to build a tarball that is older than the working tree.
#
# The tarball, not src/, is what gets compiled — mktarball.sh exists precisely
# because of that, but nothing forced anyone to run it. Editing a source file
# and running makepkg produces a package that builds cleanly, installs cleanly,
# and contains the PREVIOUS binary. Nothing in the output says so, which is why
# the check belongs here rather than in a comment.
#
# The extraction dir ($pkgname-$pkgver, under src/ alongside the real sources)
# is always newer than the tarball because makepkg just created it; excluded by
# path, or this would fire on every build.
prepare() {
    local tarball="$startdir/$pkgname-$pkgver.tar.gz"
    [ -f "$tarball" ] || return 0

    # Only hand find paths that exist. It exits non-zero for a missing one, and
    # under makepkg's errexit that aborts prepare() with no message at all —
    # which is what `include/` (dropped from the tree long ago, still named here)
    # did: this guard could never run, so bare makepkg refused to build synui
    # while looking like the stale-tarball check firing. `include` stays listed
    # so the check covers it again if it ever comes back.
    local -a watched=()
    local p
    for p in src include meson.build config data hooks protocols quickshell systemd tests; do
        [ -e "$startdir/$p" ] && watched+=("$startdir/$p")
    done

    local stale
    stale=$(find "${watched[@]}" \
                 -type f -newer "$tarball" \
                 -not -path "*/$pkgname-$pkgver/*" \
                 -print -quit 2>/dev/null)

    if [ -n "$stale" ]; then
        error "$pkgname-$pkgver.tar.gz is older than ${stale#"$startdir"/}"
        error "makepkg would silently package the PREVIOUS build. Run:"
        error "    ./mktarball.sh"
        return 1
    fi
}

build() {
    cd "$srcdir/synui-0.1.0"
    meson setup build --prefix=/usr --buildtype=release
    ninja -C build
}

package() {
    cd "$srcdir/synui-0.1.0"
    DESTDIR="$pkgdir" ninja -C build install

    install -Dm644 systemd/synui.service \
        "$pkgdir/usr/lib/systemd/system/synui.service"
    install -Dm644 systemd/synui-foot.service \
        "$pkgdir/usr/lib/systemd/system/synui-foot.service"
    # Where a session publishes what its LOCK screen shows, so the LOGIN screen
    # can show the same one. tmpfiles rather than a packaged directory: the
    # sticky bit and root ownership have to be right at every boot, including
    # after somebody has been in there with rm, and a directory shipped in the
    # package is one pacman will happily leave with whatever mode it finds.
    install -Dm644 systemd/synui-greeter-bg.conf \
        "$pkgdir/usr/lib/tmpfiles.d/synui-greeter-bg.conf"

    # The login-screen fingerprint switch. ⛔ THREE FILES, AND THE DROP-IN IS
    # THE ONE THAT MAKES IT A SETTING: it pulls the sync in whenever greetd
    # starts, so /etc/synui/login-fingerprint.enable takes effect at the next
    # boot instead of the next time pacman happens to reinstall synui. The
    # scriptlet runs the same script, so there is one implementation of which
    # line is ours and when it comes out.
    install -Dm755 systemd/synui-login-fprint.sh \
        "$pkgdir/usr/lib/synui/synui-login-fprint"
    install -Dm644 systemd/synui-login-fprint.service \
        "$pkgdir/usr/lib/systemd/system/synui-login-fprint.service"
    install -Dm644 systemd/greetd.service.d/synui-login-fprint.conf \
        "$pkgdir/usr/lib/systemd/system/greetd.service.d/synui-login-fprint.conf"
    install -Dm644 config/synui.desktop \
        "$pkgdir/usr/share/wayland-sessions/synui.desktop"

    # Without this, kbuildsycoca6 indexes no applications and every KDE app
    # falls back to the "Open With" chooser. See config/applications.menu.
    install -Dm644 config/applications.menu \
        "$pkgdir/etc/xdg/menus/applications.menu"

    # Point KDE's terminal actions (Dolphin's "Open Terminal" / F4) at kitty;
    # KIO's fallback is a hard-coded konsole, which we don't ship. See
    # config/kdeglobals.
    install -Dm644 config/kdeglobals \
        "$pkgdir/etc/xdg/kdeglobals"

    # ...and the scheme that kdeglobals' ColorScheme= names. synui-apply-theme
    # writes a per-user copy of this on every theme switch, which shadows it —
    # this is the one that exists on a fresh install, before any synui session
    # has run the helper. Without it the name is dangling and the theme has to
    # be picked by hand in System Settings. See config/SynapseTheme.colors.
    install -Dm644 config/SynapseTheme.colors \
        "$pkgdir/usr/share/color-schemes/SynapseTheme.colors"

    # Same problem one layer down: GLib launches every Terminal=true .desktop
    # entry, and its built-in terminal list has neither kitty nor foot, so
    # without this ALL of them fail with "Unable to find terminal required for
    # application" -- silently, since the launcher is usually a browser. GLib
    # looks for this name in PATH before falling back to that list. See
    # config/xdg-terminal-exec.
    install -Dm755 config/xdg-terminal-exec \
        "$pkgdir/usr/bin/xdg-terminal-exec"

    # PAM service for the native lock's auth helper (synui-lock-auth). Without
    # it pam_start("synui-lock") fails and the screen can never be unlocked.
    install -Dm644 config/pam/synui-lock \
        "$pkgdir/etc/pam.d/synui-lock"

    # And the fingerprint service (synui-lock-fprint), which is pam_fprintd
    # alone. Installed even when fprintd is not: pam_start would otherwise fail
    # for a missing FILE rather than a missing module, which reports the same
    # "unavailable" but hides a genuine misinstall behind it.
    install -Dm644 config/pam/synui-lock-fprint \
        "$pkgdir/etc/pam.d/synui-lock-fprint"

    # Laptop lid policy: close = suspend (synui locks first via its before-sleep
    # inhibitor), ignored when docked to an external display. logind's own
    # defaults, made explicit as SynapseOS policy. A drop-in, so it layers over
    # the stock logind.conf without replacing a file pacman would then own.
    install -Dm644 config/logind.conf.d/synapse-lid.conf \
        "$pkgdir/etc/systemd/logind.conf.d/synapse-lid.conf"

    # DO NOT mask nvidia-{suspend,resume,hibernate} here. pkgrel 205 shipped
    # those three as symlinks to /dev/null and it was wrong.
    #
    # This hardware sets NVreg_PreserveVideoMemoryAllocations=1, which only
    # tells the driver to dump VRAM to TemporaryFilePath on suspend. The dump
    # and the reload are performed by nvidia-sleep.sh, which runs ONLY from
    # those three units. Param on + units off is the combination that wakes the
    # GPU with lost framebuffers, i.e. the unrecoverable black screen of
    # 2026-07-17; enabling them was that day's fix.
    #
    # 205's rationale was that they had been "found enabled again" after an
    # nvidia upgrade, read as a driver package undoing a deliberate disable.
    # Enabled was the intended state. The journal shows the units running on 17
    # consecutive suspend/resume cycles from Jul 17 19:41 to Jul 28 09:50,
    # including four clean resumes AFTER the Jul 26 22:02 driver upgrade. The
    # one bad resume in that run, Jul 28 09:50, was this compositor deadlocking
    # on primary_lock — same blank screen, unrelated cause. Fixing that is what
    # the rest of 205 does.
    #
    # If suspend ever regresses again, get a backtrace before blaming these
    # units: synui alive with `synctl` hanging is a main-thread block, not a
    # GPU fault.

    # ── Bluetooth: headsets that connect at 100% ─────────────────────────────
    #
    # Some headsets announce max AVRCP absolute volume the moment the A2DP
    # transport comes up, overwriting the volume WirePlumber restored — full
    # blast into your ears on every connect. PipeWire's fix is a per-device
    # `no-features = [ hw-volume ]` quirk, and it ships a list of the devices it
    # knows about; bluez5-quirks.conf holds the ones it does not.
    #
    # The three parts have to ship together. The quirk list is not read from
    # here: that database has no drop-in and no /etc path, so SPA_DATA_DIR is
    # the only supported redirect and it redirects the whole tree. The hook
    # regenerates our copy of the tree from PipeWire's on every upgrade — which
    # is the only reason shadowing it is safe — and the drop-in arms the
    # redirect. See synui-bluez-quirks for the full reasoning, including the two
    # wireplumber settings that look like they replace all of this and do not.
    install -Dm644 config/bluez5/synapse-quirks.conf \
        "$pkgdir/usr/share/synui/bluez5-quirks.conf"
    install -Dm755 systemd/synui-bluez-quirks.sh \
        "$pkgdir/usr/bin/synui-bluez-quirks"
    # The accent, on the hardware that has lights in it. ⛔ The units are
    # SHIPPED but not enabled: syn-rgb writes to hardware, and hardware doing
    # what its owner asked is not a thing to take over on an update. `syn-rgb
    # on` is the whole opt-in.
    install -Dm755 tools/syn-rgb "$pkgdir/usr/bin/syn-rgb"
    # The desktop, out loud. ⚠ Speaks through `vibe voice say` where vibe is
    # installed, so the screen reader and the assistant are one voice; espeak-ng
    # is the fallback, not a second engine.
    install -Dm755 tools/syn-speak "$pkgdir/usr/bin/syn-speak"
    # ⛔ THE ANNOUNCER IS THE UNIT. `syn-speak on` enables it; shipping the
    # script without it is a switch with nothing behind it — see 564.
    install -Dm644 systemd/syn-speak.service \
        "$pkgdir/usr/lib/systemd/user/syn-speak.service"
    install -Dm644 systemd/syn-rgb.service \
        "$pkgdir/usr/lib/systemd/user/syn-rgb.service"
    install -Dm644 systemd/syn-rgb.path \
        "$pkgdir/usr/lib/systemd/user/syn-rgb.path"
    # The SDK server. Shipped and not enabled, like the two above it — and it
    # is an OPTIMISATION, not a dependency: openrgb probes the bus itself when
    # nothing is serving, so syn-rgb is correct without it and merely slow.
    install -Dm644 systemd/syn-rgb-server.service \
        "$pkgdir/usr/lib/systemd/user/syn-rgb-server.service"
    install -Dm644 hooks/72-synui-bluez-quirks.hook \
        "$pkgdir/usr/share/libalpm/hooks/72-synui-bluez-quirks.hook"
    install -Dm644 config/wireplumber.service.d/10-synapse-spa-data-dir.conf \
        "$pkgdir/usr/lib/systemd/user/wireplumber.service.d/10-synapse-spa-data-dir.conf"

    # The start menu's curated not-an-application list. Data rather than a table
    # in StartMenu.qml because it is a judgement about other people's packages,
    # and a judgement has to be overridable without editing QML — MenuState
    # reads this first and ~/.config/synui/menu-hidden.conf second, so a `!id`
    # line at home puts any of these back. Not in backup=: this file is ours to
    # revise on upgrade, and the user's edits belong in their own copy.
    install -Dm644 data/menu-hidden.conf \
        "$pkgdir/usr/share/synui/menu-hidden.conf"

    # System-wide MangoHud defaults. The session sets MANGOHUD=1, so the Vulkan
    # layer loads in every game regardless of launcher; this file is what keeps
    # the overlay hidden until Shift_R+F12 rather than up from launch. Without
    # it a fresh install gets MangoHud's own defaults, which draw immediately.
    # Unowned by any package (mangohud ships no /etc config), and in backup= so
    # pacman preserves a user's edits.
    install -Dm644 config/MangoHud.conf \
        "$pkgdir/etc/MangoHud.conf"

    # ⚠ AND THE SWITCH THAT DECIDES WHETHER THE LAYER LOADS AT ALL, which is a
    # different question from the file above — that one is only the overlay's
    # look. The session reads this at login; it ships saying 0. In backup=
    # beside MangoHud.conf so an edit survives an upgrade.
    install -Dm644 config/mangohud-session.conf \
        "$pkgdir/etc/synapseos/mangohud.conf"

    # ── The bar: quickshell ──────────────────────────────────────────────────
    #
    # The whole QML tree, preserving its directory shape. qmldir MUST ship with
    # it: it declares Theme and Icons as singletons, which turns off implicit
    # sibling resolution, so without that one file every type in the tree fails
    # to resolve and the bar does not start at all.
    #
    # NOT ~/.config/quickshell: a packaged default belongs to the package, and
    # quickshell's `-c <name>` only searches the user's config dir — hence
    # synui-bar pointing at this path with -p, and handing over to
    # ~/.config/quickshell/synapse by name when the user has their own.
    # Globbed, not listed: a named list silently drops a new top-level type
    # (Osd.qml, OsdState.qml) and the bar then fails to load with an error
    # naming a file the user cannot see. qmldir is named separately because it
    # is the one non-.qml file here and the most load-bearing.
    for _qml in quickshell/*.qml; do
        install -Dm644 "$_qml" \
            "$pkgdir/usr/share/synui/quickshell/${_qml##*/}"
    done
    install -Dm644 quickshell/qmldir \
        "$pkgdir/usr/share/synui/quickshell/qmldir"
    # Globbed for the same reason the top-level types are: naming one file here
    # silently dropped BarMenu.qml, and the bar then failed to start outright
    # ("BarMenu is not a type") — from a package that built and installed
    # cleanly. Caught only by running the INSTALLED tree.
    for _cmp in quickshell/components/*.qml; do
        install -Dm644 "$_cmp" \
            "$pkgdir/usr/share/synui/quickshell/components/${_cmp##*/}"
    done
    for _mod in quickshell/modules/*.qml; do
        install -Dm644 "$_mod" \
            "$pkgdir/usr/share/synui/quickshell/modules/${_mod##*/}"
    done
    # ⚠ Ui/ IS AN IMPORT TARGET, NOT A FOLDER OF COMPONENTS, and its qmldir is
    # what makes it one. quickshell resolves `import qs.Ui` to <shell root>/Ui,
    # which is how a bar plugin reaches BarWidget — drop the qmldir and every
    # plugin fails with "BarWidget is not a type" from a package that installed
    # cleanly. Same trap the top-level qmldir's note above describes, one
    # directory down.
    for _ui in quickshell/Ui/*.qml; do
        install -Dm644 "$_ui" \
            "$pkgdir/usr/share/synui/quickshell/Ui/${_ui##*/}"
    done
    install -Dm644 quickshell/Ui/qmldir \
        "$pkgdir/usr/share/synui/quickshell/Ui/qmldir"
    # …and the other half a bar plugin imports. Same qmldir trap: `import
    # qs.Commons` resolves to <shell root>/Commons, and without the file the
    # module is not a module.
    for _cm in quickshell/Commons/*.qml; do
        install -Dm644 "$_cm" \
            "$pkgdir/usr/share/synui/quickshell/Commons/${_cm##*/}"
    done
    install -Dm644 quickshell/Commons/qmldir \
        "$pkgdir/usr/share/synui/quickshell/Commons/qmldir"
    for _wid in quickshell/widgets/*.qml; do
        install -Dm644 "$_wid" \
            "$pkgdir/usr/share/synui/quickshell/widgets/${_wid##*/}"
    done
    # Artwork a widget draws, kept BESIDE the .qml that draws it rather than in
    # /usr/share/synui with the compositor's own assets. That is what lets the
    # QML say `source: "pizza.png"` and resolve it identically from the packaged
    # tree and from a source tree run with `quickshell -p` — an absolute
    # /usr/share path (as Launcher.qml still uses for the logo) is a file that
    # silently does not exist in every rig.
    for _art in quickshell/widgets/*.png; do
        install -Dm644 "$_art" \
            "$pkgdir/usr/share/synui/quickshell/widgets/${_art##*/}"
    done
    # Sprite tables. tuxart.js is a `.pragma library` rather than a QML
    # singleton, and that is load-bearing: importing quickshell/'s module
    # instantiates EVERY singleton declared in its qmldir, so art registered
    # there would drag Theme, WidgetState and the rest of the bar in behind it —
    # which is what stopped tests/tux_screen.qml drawing a penguin without a
    # compositor. A .js needs its own glob here; without it the widget installs
    # and every sprite is `undefined`, which draws NOTHING and says nothing.
    for _lib in quickshell/widgets/*.js; do
        install -Dm644 "$_lib" \
            "$pkgdir/usr/share/synui/quickshell/widgets/${_lib##*/}"
    done
    # ── The welcome guide ────────────────────────────────────────────────────
    #
    # A SECOND ENTRY POINT into this same tree, not a second tree:
    # `synui-welcome` runs `quickshell -p .../quickshell/welcome.qml`, which
    # makes this directory the shell root, so welcome/ reaches Theme.qml and the
    # picked font with `import ".."` and copies nothing. welcome.qml itself is
    # installed by the `quickshell/*.qml` glob above; this is the module beside
    # it.
    #
    # ⚠ THE .js NEEDS ITS OWN GLOB, exactly as tuxart.js does one directory
    # over: pages.js is the guide's entire content — every page, row, blurb and
    # fallback chord — and without this loop the package installs, the guide
    # starts, and `Pages.pages` is `undefined`, which draws an empty card and
    # says nothing.
    #
    # ⚠ AND THE qmldir, for the third time on this page: welcome/ declares a
    # singleton, which turns OFF implicit sibling resolution, so a tree missing
    # the file fails with "Guide is not a type" from a package that installed
    # cleanly.
    for _wel in quickshell/welcome/*.qml; do
        install -Dm644 "$_wel" \
            "$pkgdir/usr/share/synui/quickshell/welcome/${_wel##*/}"
    done
    for _wjs in quickshell/welcome/*.js; do
        install -Dm644 "$_wjs" \
            "$pkgdir/usr/share/synui/quickshell/welcome/${_wjs##*/}"
    done
    install -Dm644 quickshell/welcome/qmldir \
        "$pkgdir/usr/share/synui/quickshell/welcome/qmldir"

    # Tuxagotchi's voice: eleven square-wave chirps, generated by
    # tools/mkbeeps.py and committed because a package must not need python to
    # make a noise. Beside the QML that plays them for the same reason the pizza
    # artwork is beside Pizza.qml — one relative path that resolves identically
    # from the packaged tree and from `quickshell -p` on a source one.
    for _wav in quickshell/widgets/sounds/*.wav; do
        install -Dm644 "$_wav" \
            "$pkgdir/usr/share/synui/quickshell/widgets/sounds/${_wav##*/}"
    done

    # ── The Antiquity shell ──────────────────────────────────────────────────
    #
    # A port of diinki's linux-antiquity (MIT), selectable with `bar_shell =
    # antiquity` in synuirc or Control panel ▸ Desktop ▸ Bar shell. synui-bar
    # resolves the key and starts this tree instead of quickshell/ above.
    #
    # Copied with `find`, NOT with the per-directory globs the SYNAPSE bar
    # uses. That style is written out above because a hand-listed file silently
    # disappears; the globs have the same failure one level up — this tree is
    # five directories deep (smallicons/weatherWidget/), and mixes .qml, .js,
    # qmldir, .svg, .png, .ttf and .md. Six globs per directory is six chances
    # to forget one, and the symptom is the same as it has been twice before:
    # a package that builds and installs cleanly and a shell that will not
    # start, naming a file the user cannot see.
    #
    # -type f only: there is nothing else in the tree, and it keeps a stray
    # symlink from being followed out of the source dir.
    find quickshell-antiquity -type f | while read -r _f; do
        install -Dm644 "$_f" "$pkgdir/usr/share/synui/${_f}"
    done

    # The single writer of widgets.state. synui's Super+Shift+A bind and the
    # control panel's Desktop widgets row both run this, so there is one
    # implementation of the toggle however it is reached.
    install -Dm755 systemd/synui-widgets.sh \
        "$pkgdir/usr/bin/synui-widgets"

    # Bar plugins, in Omarchy's shell-plugin format. The single writer of
    # plugins.state and the only thing that parses a manifest — the bar reads
    # its TSV, so the command line and the bar cannot come to different
    # conclusions about what is installed, which for a plugin system is the
    # difference between "it is off" and "it was refused".
    install -Dm755 systemd/synui-plugins.sh \
        "$pkgdir/usr/bin/synui-plugins"

    # ⚠ THE LAUNCHER OMARCHY'S PLUGINS CALL BY NAME. Not a shim for `omarchy`
    # the command — this is the one thing a plugin hardcodes into its own QML
    # (`bar.run("omarchy-launch-or-focus-tui --app-id=… " + path)`), so on a
    # desktop without it every terminal game in their catalogue has a Play
    # button that does nothing at all. Installed under THEIR name because the
    # call site is not configurable.
    install -Dm755 systemd/omarchy-launch-or-focus-tui.sh \
        "$pkgdir/usr/bin/omarchy-launch-or-focus-tui"

    # ⚠ AND THE TWO THE WEATHER WIDGETS NAME. Every weather widget in their
    # catalogue keeps its location in ONE file, and omarchy-weather-location is
    # the only thing that writes it: without the command the city picker takes
    # a click, spawns nothing, and the widget goes on showing no location —
    # Quickshell reports the missing binary to the shell log, never to the
    # plugin that asked. omarchy-notification-send is the same story one step
    # quieter: it carries the storm alerts, so its absence is a setting that is
    # on and a warning that never arrives.
    install -Dm755 systemd/omarchy-weather-location.sh \
        "$pkgdir/usr/bin/omarchy-weather-location"
    install -Dm755 systemd/omarchy-notification-send.sh \
        "$pkgdir/usr/bin/omarchy-notification-send"

    # ⛔ AND THEIR SHELL'S OWN CLI, WHICH IS THE QUIETEST OF THE LOT. The three
    # above are commands a plugin spawns, so Quickshell logs a missing one to
    # the bar log. This one is reached through `bar.run`, which hands the string
    # to `sh -c` — a missing binary is exit 127 inside a detached shell and
    # NOTHING is written anywhere. YT Mini's whole bar button is
    # `omarchy-shell shell toggle <id> '{"clipboard":true}'`, and without the
    # command it was a button that could not be told from a working one.
    install -Dm755 systemd/omarchy-shell.sh \
        "$pkgdir/usr/bin/omarchy-shell"

    # The worked example, shipped OFF like every plugin. It is the third search
    # directory, so `synui-plugins list` has something in it on a machine that
    # has never installed one — a plugin system whose list is empty until you
    # have already written a plugin teaches nobody the format.
    #
    # ⚠ `*/` AND NOT `*`: the trailing slash matches DIRECTORIES only, which is
    # what keeps README.md below out of the loop. Without it the README would be
    # installed as a plugin directory of its own and `synui-plugins list` would
    # show a row for it — a manifest-less one, so the answer would be silence
    # rather than an error.
    for _pl in data/plugins/*/; do
        _plid=${_pl%/}; _plid=${_plid##*/}
        for _plf in "$_pl"*; do
            install -Dm644 "$_plf" \
                "$pkgdir/usr/share/synui/plugins/$_plid/${_plf##*/}"
        done
    done
    # The format, the contract and what cannot be hosted, beside the example
    # that demonstrates all three.
    install -Dm644 data/plugins/README.md \
        "$pkgdir/usr/share/synui/plugins/README.md"
    # What `synui-plugins browse` offers. Not a plugin directory — the loop
    # above matches `*/` and skips it, same as the README.
    install -Dm644 data/plugins/catalogue.tsv \
        "$pkgdir/usr/share/synui/plugins/catalogue.tsv"
    # ⚠ EXECUTABLE, AND synui-plugins EXECS IT BY PATH. It turns
    # omarchyplugins.com's catalog.json into catalogue.tsv's twelve columns —
    # the only thing in the plugin system that speaks JSON. Without it
    # `synui-plugins refresh` reports it is missing and `browse` falls back to
    # the five shipped rows, which is a browser showing five widgets.
    install -Dm755 data/plugins/registry.py \
        "$pkgdir/usr/share/synui/plugins/registry.py"
    # The window. Beside the tree it manages rather than inside it: this is a
    # standalone quickshell app like synpkg's, not part of the bar's own QML —
    # `synui-plugins gui` execs quickshell against it.
    install -Dm644 data/plugins-gui.qml \
        "$pkgdir/usr/share/synui/plugins-gui.qml"
    install -Dm644 data/synui-plugins.desktop \
        "$pkgdir/usr/share/applications/synui-plugins.desktop"

    # The single writer of sounds.state AND the thing that plays a sample —
    # both jobs in one script so "is this event enabled?" has one answer. The
    # compositor caches the same file to skip a fork when everything is off,
    # which is the shipped default: nothing here makes a sound until asked.
    install -Dm755 systemd/synui-sound.sh \
        "$pkgdir/usr/bin/synui-sound"
    install -Dm755 systemd/synui-bar.sh \
        "$pkgdir/usr/bin/synui-bar"
    # The welcome guide's launcher. Not part of synui-bar: two bars ship and the
    # guide belongs to neither — see the header of systemd/synui-welcome.sh.
    install -Dm755 systemd/synui-welcome.sh \
        "$pkgdir/usr/bin/synui-welcome"
    # Where more wallpapers come from. The launcher, the CLI and the network
    # switch are one script; wallhaven.qml rides in on the quickshell/*.qml
    # glob above, like welcome.qml.
    #
    # ⛔ OFF BY DEFAULT and there is nothing to enable here — the switch is a
    # state file the user writes with `synui-wallhaven on`. A package that
    # turned on a third-party network call at install time would be the exact
    # surprise the weather switch exists to prevent.
    install -Dm755 systemd/synui-wallhaven.sh \
        "$pkgdir/usr/bin/synui-wallhaven"

    # NOT /usr/bin: it takes root to do anything, it is named by exactly one
    # sudoers rule, and it is not a command to find by tab-completing. Same
    # placement reasoning as syn's /usr/lib/syn/syn-resolve.
    install -Dm755 systemd/synui-kmod-events.sh \
        "$pkgdir/usr/lib/synui/synui-kmod-events"

    # The system equalizer. Single writer of eq.state AND the thing that runs
    # the PipeWire filter chain, for the reason synui-sound is shaped the same
    # way: "what is the equalizer set to" needs exactly one answer. src/eq.c is
    # only the panel and shells out to this, so without it every row in that
    # panel is a no-op.
    install -Dm755 systemd/synui-eq.sh \
        "$pkgdir/usr/bin/synui-eq"
    # The volume keys go through this, not wpctl — it finds the device behind
    # the equalizer. input.c spawns it by name, so it has to be on PATH.
    install -Dm755 systemd/synui-volume.sh \
        "$pkgdir/usr/bin/synui-volume"

    # The Wallpaper Engine control script. It used to ship inside
    # linux-wallpaperengine, which syn-update can never rebuild on an installed
    # system (a ~1.3 GB CEF blob and ~6 GB of scratch), so every fix to a plain
    # shell script had to be copied into place by hand. It is a synui control
    # script by name and by job, and synui IS updatable.
    install -Dm755 systemd/synui-wpengine.sh \
        "$pkgdir/usr/bin/synui-wpengine"

    # synuirc files already in the wild — including the one syn-install wrote for
    # every existing install — say `autostart = synui-waybar`. Without this the
    # bar simply would not come up after an upgrade, with nothing on screen to
    # say why. The symlink is the compatibility shim, not a second bar.
    ln -s synui-bar "$pkgdir/usr/bin/synui-waybar"

    # ── The old bar's config, kept but not started ───────────────────────────
    #
    # waybar is no longer a dependency and nothing in SynapseOS launches it, but
    # its config still ships: synui-apply-theme still generates the themed
    # stylesheet, so a user who installs waybar by hand keeps a working, themed
    # bar. Deleting the config would take that away for no gain.
    #
    # NOT /etc/xdg/waybar: those paths are owned by the *waybar package*, and
    # shipping ours there is a hard file conflict that fails the whole pacstrap
    # transaction — which is how it broke the ISO build. It only ever "worked"
    # on a dev box because build-all.sh installs with --overwrite '*', silently
    # taking ownership of waybar's own files.
    #
    # This is still the ONLY copy of the config: syn-install used to write its
    # own from heredocs, which drifted from the repo's and once shipped a menu
    # whose items launched the wrong app. A packaged default cannot drift from
    # itself. It is also where quickshell's Icons.qml took its glyph codepoints
    # from, so the two bars are glyph-for-glyph identical.
    install -Dm644 config/waybar/config.jsonc \
        "$pkgdir/usr/share/synui/waybar/config.jsonc"
    install -Dm644 config/waybar/style.css \
        "$pkgdir/usr/share/synui/waybar/style.css"
    install -Dm644 config/waybar/network-menu.xml \
        "$pkgdir/usr/share/synui/waybar/network-menu.xml"
    install -Dm644 config/waybar/bluetooth-menu.xml \
        "$pkgdir/usr/share/synui/waybar/bluetooth-menu.xml"

    # Media idle-inhibit (synui-idle-inhibit binary is installed by meson).
    install -Dm755 systemd/synui-media-inhibit.sh \
        "$pkgdir/usr/lib/synui/synui-media-inhibit"
    install -Dm644 systemd/synui-media-inhibit.service \
        "$pkgdir/usr/lib/systemd/user/synui-media-inhibit.service"

    # Screen sharing. The portals file is keyed on XDG_CURRENT_DESKTOP=synui,
    # which the session sets; both halves are needed or portal falls back to
    # the GTK backend, which cannot ScreenCast on wlroots.
    install -Dm644 config/synui-portals.conf \
        "$pkgdir/usr/share/xdg-desktop-portal/synui-portals.conf"
    install -Dm644 config/xdg-desktop-portal-wlr.conf \
        "$pkgdir/etc/xdg/xdg-desktop-portal-wlr/config"

    # AI backend GPU/CPU toggle (the welcome guide's "AI Backend" row runs it).
    install -Dm755 systemd/synui-ai-backend.sh \
        "$pkgdir/usr/bin/synui-ai-backend"

    # Game mode indicator for waybar (custom/gamemode polls this).
    install -Dm755 systemd/synui-game-status.sh \
        "$pkgdir/usr/bin/synui-game-status"

    # Bar clock for waybar (custom/clock polls this). Reads the format the
    # Date & Time settings panel writes to ~/.config/synui/clock.state, so the
    # panel's 12/24-hour and seconds toggles actually reach the bar.
    install -Dm755 systemd/synui-clock.sh \
        "$pkgdir/usr/bin/synui-clock"

    # Screenshots: Print / Shift+Print / Ctrl+Print (see seed_default_binds).
    install -Dm755 systemd/synui-screenshot.sh \
        "$pkgdir/usr/bin/synui-screenshot"

    # Screen recording: Super+Shift+R starts, and pressing it again stops.
    install -Dm755 systemd/synui-record.sh \
        "$pkgdir/usr/bin/synui-record"

    # …and the bar's recording pill polls this to know a take is running. The
    # pill is the only on-screen sign that Super+Shift+R is still recording, so
    # without this installed the indicator is permanently invisible.
    install -Dm755 systemd/synui-record-status.sh \
        "$pkgdir/usr/bin/synui-record-status"

    # synui-cursor: install/switch X cursor themes. The Super+Shift+P picker
    # shells out to this on Enter, so it is not optional — without it the panel
    # changes synui's own pointer and nothing else on the desktop follows.
    install -Dm755 systemd/synui-cursor.sh \
        "$pkgdir/usr/bin/synui-cursor"

    # The one place that knows how to open a folder. dolphin is an optdepend,
    # so the bar's Files button, the desktop menu and the ISO mounter all go
    # through this instead of naming it — see the script for the search order.
    install -Dm755 systemd/synui-open-folder.sh \
        "$pkgdir/usr/bin/synui-open-folder"

    # The image viewer's .desktop Exec. Not the way in from inside synui (that
    # is `synctl dispatch view`, super+shift+i, or the Image Viewer entry's own
    # dispatch) — this is the wrapper that keeps the DISTRIBUTION DEFAULT for
    # image/png and image/jpeg honest in a session where synui is not the
    # compositor. See the header of systemd/synui-view-image.sh.
    install -Dm755 systemd/synui-view-image.sh \
        "$pkgdir/usr/bin/synui-view-image"

    # Find network printers and set them up driverless. The control panel's
    # Printers row opens CUPS's web UI, which is a complete admin interface and
    # the wrong first answer to "there is a printer on my network" — it asks for
    # a discovery protocol and a driver, and on anything sold this decade
    # neither question has an interesting answer. See the script's header for
    # why it is lpinfo and not avahi, and why driverless only.
    install -Dm755 systemd/synui-printers.sh \
        "$pkgdir/usr/bin/synui-printers"

    # Right-click an .iso in Dolphin -> Mount/Unmount Image, Write to USB Stick.
    # The service menu must be executable or KIO 6 skips it. See
    # config/synui-iso-mount.desktop.
    install -Dm755 systemd/synui-iso-mount.sh \
        "$pkgdir/usr/bin/synui-iso-mount"
    install -Dm755 systemd/synui-iso-write.sh \
        "$pkgdir/usr/bin/synui-iso-write"
    install -Dm755 config/synui-iso-mount.desktop \
        "$pkgdir/usr/share/kio/servicemenus/synui-iso-mount.desktop"

    # Right-click a .rar/.7z/.zip in Dolphin -> Extract Here / to Subfolder /
    # To…. Executable or KIO 6 skips it. See config/synui-extract.desktop.
    install -Dm755 systemd/synui-extract.sh \
        "$pkgdir/usr/bin/synui-extract"
    install -Dm755 config/synui-extract.desktop \
        "$pkgdir/usr/share/kio/servicemenus/synui-extract.desktop"

    # Right-click a .png/.jpg in Dolphin -> Crop Image. Executable or KIO 6
    # skips it. No helper script: the action is `synctl dispatch crop %f`, and
    # the cropper is a compositor panel (src/crop.c) because the region has to
    # be selected over a displayed image and nothing else here displays one.
    install -Dm755 config/synui-crop-servicemenu.desktop \
        "$pkgdir/usr/share/kio/servicemenus/synui-crop.desktop"

    # Right-click a .png/.jpg in Dolphin -> Set as Wallpaper. Executable or
    # KIO 6 skips it. Replaces plasma-workspace's plugin of the same name, which
    # calls plasmashell and cannot work here; kservicemenurc below switches that
    # one off. See config/synui-set-wallpaper.desktop.
    install -Dm755 config/synui-set-wallpaper.desktop \
        "$pkgdir/usr/share/kio/servicemenus/synui-set-wallpaper.desktop"

    # ...and the [Show] defaults that suppress it. A KFileItemAction PLUGIN has
    # no desktop file to override — switching it off in kservicemenurc is the
    # only lever. Merged under the user's own copy, so Dolphin ▸ Settings ▸
    # Context Menu still wins.
    install -Dm644 config/kservicemenurc \
        "$pkgdir/etc/xdg/kservicemenurc"

    # Right-click an .exe/.msi in Dolphin -> Run with Wine. Executable or KIO 6
    # skips it. wine itself is an optdepend (SynapseOS installs it via
    # syn-install); the menu is harmless if wine is absent.
    install -Dm755 systemd/synui-wine-run.sh \
        "$pkgdir/usr/bin/synui-wine-run"
    install -Dm755 config/synui-wine-run.desktop \
        "$pkgdir/usr/share/kio/servicemenus/synui-wine-run.desktop"

    # Theme manager (Super+T) fires this to push its light/dark scheme out
    # to GTK / Dolphin (kdeglobals) / Firefox — best-effort, a no-op where the
    # toolkits aren't installed. See src/theme.c.
    install -Dm755 systemd/synui-apply-theme.sh \
        "$pkgdir/usr/bin/synui-apply-theme"

    # The font picker (Super panel ▸ Appearance ▸ UI font) fires this to push
    # the chosen family out to GTK 2/3/4, Qt/KDE, the bar, rofi and the
    # terminal. Without it the picker sets the font synui draws its OWN panels
    # in and nothing else. synui-apply-theme calls it last on every theme
    # switch, which is what keeps a theme from undoing the choice. See
    # systemd/synui-apply-font.sh.
    install -Dm755 systemd/synui-apply-font.sh \
        "$pkgdir/usr/bin/synui-apply-font"

    # Moves the default audio sink to a screen when one is plugged in, and back
    # when it goes. synui fires it from the output hotplug paths when
    # `hdmi_audio` resolves to on (auto = laptops). It owns every part of the
    # decision that needs the PipeWire graph — in particular the ELD check that
    # tells a connector with a display on it from the half-dozen dormant HDMI
    # pins a GPU advertises permanently. See systemd/synui-hdmi-audio.sh.
    install -Dm755 systemd/synui-hdmi-audio.sh \
        "$pkgdir/usr/bin/synui-hdmi-audio"

    # The transparency slider fires this to set foot's own background alpha, so a
    # translucent terminal keeps opaque text ("text on glass"). No-op without foot.
    install -Dm755 systemd/synui-glass.sh \
        "$pkgdir/usr/bin/synui-glass"

    # Firefox glass. Unlike every other window, Firefox cannot be made
    # translucent from the compositor side — it declares an opaque region and
    # fills its own surface, so it takes three prefs plus a userChrome.css
    # INSIDE the profile. synui-apply-theme delegates the whole business here.
    # On by default; `synui-firefox-glass off` reverts the prefs and removes the
    # files (deleting them alone would not: a written user_pref persists in
    # prefs.js). The stylesheets are templates, not config — the installer only
    # overwrites a copy still carrying its SYNUI-MANAGED marker.
    install -Dm755 systemd/synui-firefox-glass.sh \
        "$pkgdir/usr/bin/synui-firefox-glass"
    install -Dm644 data/firefox/userChrome.css \
        "$pkgdir/usr/share/synui/firefox/userChrome.css"
    install -Dm644 data/firefox/userContent.css \
        "$pkgdir/usr/share/synui/firefox/userContent.css"

    # SteamOS-style game launcher: gamemoderun + MangoHud overlay + opt-in
    # gamescope. mangohud/gamemode are optdepends — the wrapper drops whichever
    # is absent, so the game still launches. Point a .desktop/Steam launch
    # option at it: `synui-game-run -- %command%`.
    install -Dm755 systemd/synui-game-run.sh \
        "$pkgdir/usr/bin/synui-game-run"

    # ── The third-party wallpapers ───────────────────────────────────────────
    #
    # data/wallpapers/ is the THIRD-PARTY directory — SynapseOS's own artwork is
    # data/synapse-*.png and goes through install_data() in meson.build. Nothing
    # lands here without a grant recorded in data/wallpapers/WALLPAPERS.md; read
    # that before adding a fifth.
    #
    # Three antiquity-* from diinki's linux-antiquity, under the same MIT grant
    # as the QML (the full write-up stays in quickshell-antiquity/WALLPAPERS.md),
    # and two commons-st-louis-night*, from Daniel Schwen's Gateway Arch
    # photograph on Wikimedia Commons under CC BY-SA 4.0.
    #
    # ⚠ THE TWO ARE NOT UNDER THE SAME OBLIGATION, which is the whole reason
    # they are documented separately. The colour one is byte-for-byte
    # upstream's, on purpose — a verbatim copy owes attribution and nothing
    # more. The *-noir one is OUR greyscale grade of it, which makes it an
    # ADAPTED work: CC BY-SA 4.0 §3(b) forces the adaptation to CC BY-SA 4.0
    # too, and §3(a) still wants Schwen credited and the change INDICATED.
    # Neither is a decision we get to make. Re-encoding the noir one was
    # therefore free, and re-encoding the colour one would cost the exemption.
    #
    # /usr/share/backgrounds FLAT, not a subdirectory of it: wppick_scan_dir()
    # in src/wppick.c reads one directory and does not recurse, so a tidy
    # backgrounds/antiquity/ would hide them all from the Super+W picker, which
    # is the only reason to ship them. Hence the source prefix on the filenames
    # — that list is flat and shared with SynapseOS's own artwork, so the name
    # has to say where it came from.
    #
    # NOT under /usr/share/synui/quickshell-antiquity with the rest of the
    # tree, even though the `find` above would have picked them up for free: a
    # wallpaper is not a property of a bar, and both shells and the
    # compositor's own picker should see them.
    #
    # ⚠ Images ONLY — WALLPAPERS.md lives in that directory too and a bare
    # `data/wallpapers/*` would drop it into the picker's directory. wppick
    # filters on extension so it would not have drawn a row for it, which is
    # exactly why this would have gone unnoticed.
    #
    # A `case` rather than an extglob `*.@(png|jpg)`: extglob is not reliably on
    # inside a PKGBUILD, and rather than a `*.png *.jpg *.jpeg` glob list, which
    # passes the unmatched pattern through as a literal filename the moment one
    # of the three extensions has no files and dies in install(). The extension
    # list matches wp_is_image() in src/wppick.c — keep them in step.
    for _wp in data/wallpapers/*; do
        case "$_wp" in
            *.png|*.jpg|*.jpeg) ;;
            *) continue ;;
        esac
        install -Dm644 "$_wp" "$pkgdir/usr/share/backgrounds/${_wp##*/}"
    done

    # CC BY-SA 4.0 §3(a) wants the creator, the licence notice, the warranty
    # disclaimer and a link to the material to travel WITH the copy — a line in
    # a repo file the user never receives does not satisfy it. /usr/share/
    # licenses/$pkgname is where a non-common licence goes on Arch; the picker
    # directory is a list, not a document store.
    install -Dm644 data/wallpapers/WALLPAPERS.md \
        "$pkgdir/usr/share/licenses/$pkgname/WALLPAPERS.md"
}
