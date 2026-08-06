# synui Roadmap — toward a full AI-aware Wayland compositor

`synui` is SynapseOS's wlroots-based Wayland compositor. This document tracks
the gap between its current state and the roadmap goal, *"full Wayland
compositor with AI-aware window management."*

## Current state (real `src/`, ~5,640 LOC)

Working:
- wlroots backend/scene init, VM detection → pixman fallback
- xdg-shell toplevels (map/unmap/destroy/commit), keyboard focus
- Tiling (master-stack), monocle and niri (scrollable-tiling) layouts; 9 workspaces
- Keyboard bindings; basic pointer (focus / click / axis); clipboard selection
- Cairo-rendered UI: welcome screen, AI command bar, neural overlay
- Every compositor-drawn panel takes the pointer as well as the keyboard: hover
  selects, a left click does the row's primary key, a click off the panel closes
  it, and the wheel scrolls. One contract, documented at the top of `synui.h`;
  one row-geometry struct (`syn_hit_t`, `hit.c`) written by `render.c` and read
  by the panels; one list of panels in `input.c` (`SYN_PANEL_LIST`) walked by all
  four pointer paths
- AI thread IPC to synapd (hardened: framed `write_all` + reassembling poll)
- Security-border colour states (rendering only)
- Interactive move/resize; per-workspace master factor; multi-output aware
- layer-shell (panels/bars/wallpaper/launchers) + xdg-output
- XWayland (X11 apps, managed + override-redirect) + xdg-decoration (SSD)
- output-management + DPMS, fractional-scale, idle-notify/inhibit, session-lock
- pointer-constraints + relative-pointer, touch, tablet (pointer emulation),
  touchpad gestures, libinput config, configurable keymap + keybindings
- foreign-toplevel (zwlr + ext), screencopy + export-dmabuf, data-control
  (zwlr + ext) + primary selection, gamma-control, drag-and-drop

Partial / broken:
- **AI layout**: request path exists but `layout_apply_ai_response()` was never
  called — responses carried no type discriminator, so the frame loop could
  only route command-bar replies. (Fixed in Phase A.)
- **Per-window AI context**: `ai_ctx.has_ctx` drives the AI border colour but
  nothing set it. (Addressed in Phase A.)
- **Security borders**: driven by the synguard verdict feed (Phase A2).
- **Surface lifecycle**: subscribed to `xdg_shell.new_surface` and asserted the
  role, which under wlroots 0.19 fires before the role is set — the compositor
  aborted on the first client window. Now subscribes to `new_toplevel` /
  `new_popup`. (Fixed in Phase B.)
- **Window-close lifecycle**: the per-view `destroy` listener hung off the
  *surface*, but wlroots 0.19 tears the toplevel down first and asserts its
  signal lists are empty — closing any window aborted. Now listens on
  `toplevel->events.destroy`. (Fixed in Phase C.)
- **Teardown**: the compositor's singleton cursor/seat/backend listeners were
  never detached, so a clean exit tripped `wlr_cursor_destroy`'s assert; there
  was also no SIGINT/SIGTERM handler, so it could only be hard-killed. Added a
  signalfd-based terminate handler and listener cleanup in `synui_destroy`.
  (Fixed in Phase C.)
- Interactive move/resize (`SYNUI_CURSOR_MOVE/RESIZE`) and the documented
  Super+H/L (master factor) / Super+Shift+J/K (move in stack) binds. (Done in
  Phase B.)
- **Initial configure**: the initial xdg commit was never answered, so plain
  xdg clients (anything not using xdg-decoration) hung unmapped forever; the
  seat also advertised no pointer capability until a device appeared, killing
  early `get_pointer` clients. (Both fixed in Phase G.)

Missing: full tablet-v2.

## Phases (ordered by value ÷ effort)

### Phase A — Make AI window management actually work  *(done)*
The distinguishing SynapseOS feature.
- [x] Add a `type` discriminator to `syn_ai_response_t`; AI thread preserves
      the request type so replies can be routed.
- [x] Route AI-layout responses to `layout_apply_ai_response()` (resurrect the
      dead path); route STATUS_UPDATE replies to the neural overlay.
- [x] Mark AI-placed windows as AI-managed (`ai_ctx`) so they get the AI
      border; clear it under non-AI layouts.
- [x] **A2:** feed synguard verdicts into `view_set_security()`. synguard now
      broadcasts verdict records over `/run/synguard.sock` (the long-stubbed
      "syn guard watch" feed, world-connectable so the unprivileged compositor
      can read it); synui subscribes on a thread, hands records to the frame
      loop, and matches each verdict's pid to a window via
      `wl_client_get_credentials` → DENY/QUARANTINE = red "denied", ALERT =
      "alert" border.

### Phase B — Interactive & complete window management  *(done)*
- [x] Cursor move/resize for floating windows (resurrect `SYNUI_CURSOR_MOVE/
      RESIZE`): Super+Left-drag moves, Super+Right-drag resizes from the nearest
      corner; a tiled window is auto-floated and the workspace reflows.
- [x] Documented binds: Super+H/L adjust the per-workspace master factor
      (0.10–0.90); Super+Shift+J/K move the focused window down/up the stack.
      Layout-cycle moved from Super+L to Super+Tab to free H/L.
- [x] Sane floating placement (`layout_float_place`): prefer the client's own
      geometry, clamp to the output, centre it.
- [x] min/max-size handling during interactive resize (honours the toplevel's
      client-set constraints with a hard floor; anchors the opposite edge).
- [x] Fixed a wlroots-0.19 surface-lifecycle crash (`new_surface` → role assert)
      by migrating to `new_toplevel` / `new_popup` — the compositor could not
      map any client window before this.

### Phase C — Multi-output correctness  *(done)*
Geometry-correct + active-output model (kept the single-active-workspace design;
independent per-output workspace sets remain a possible follow-up).
- [x] `server_focused_output()` / `server_output_box()`: the output the user is
      working on = under the cursor, else holding the focused window, else the
      first output. All geometry keys off it instead of `output[0]`.
- [x] `layout.c` tiles the active workspace onto the focused output's box; the
      AI layout prompt now reports that output's real `WxH` (was `1920x1080`).
- [x] `render.c` positions welcome / cmdbar / overlay within the focused
      output's layout box (offset by its `x,y`), so panels land on the active
      monitor rather than always output[0].
- [x] Hotplug: `new_output` re-layouts and re-homes all visible UI;
      `output_destroy` re-flows onto a surviving output (guarded by
      `shutting_down` so it's a no-op once the scene graph is gone).
- [x] Verified headless with `WLR_HEADLESS_OUTPUTS=2`: both outputs come up,
      no crash, and SIGTERM tears down cleanly.

Follow-up (deferred): re-layout is recomputed on the next layout-triggering
action, so windows don't auto-migrate the instant the cursor crosses monitors;
and monitor 2 mirrors the active workspace rather than showing its own until
per-output workspaces land. *(Per-output workspaces landed in Phase K.)*

### Phase D — Desktop shell surfaces: layer-shell  *(done)*
- [x] Vendored `protocols/wlr-layer-shell-unstable-v1.xml` (wlroots doesn't
      export the generated code) and wired wayland-scanner in `meson.build`.
- [x] `layer.c`: create `wlr_layer_shell_v1`; per-surface map/unmap/commit/
      destroy; assign an output when the client leaves it NULL; set_layer
      reparents between layer trees.
- [x] Scene z-order reworked into ordered trees: bg_rect < layer[BACKGROUND] <
      layer[BOTTOM] < window_tree < layer[TOP] < layer[OVERLAY] < compositor UI.
      Toplevels now parent to `window_tree`.
- [x] `layer_arrange_output()`: two-pass (exclusive then non-exclusive, top
      layers first) using `wlr_scene_layer_surface_v1_configure`; the leftover
      usable area feeds the tiling layout (`server_usable_box`).
- [x] Pointer reaches layer surfaces (`surface_at` refactor); keyboard focus for
      interactive surfaces (launchers), restored to a toplevel on unmap.
- [x] Layer-surface xdg popups routed through the shared popup handler; output
      destroy closes its layer surfaces.
- [x] Added `xdg-output-manager` (waybar and other bars require it).
- [x] Verified against real clients headless: **waybar** reserves a 30px top
      exclusive zone (usable area 1280x720 → 1280x690+0+30, tiling follows) and
      **wofi** maps as a keyboard-interactive launcher; both without crashing.

### Phase E — App compatibility: XWayland + decorations  *(done)*
- [x] View abstraction: `syn_view_t` now wraps either an xdg toplevel or an
      `wlr_xwayland_surface`, behind accessors (`view_surface/app_id/title/pid/
      close/set_activated/set_maximized/set_fullscreen`). Layout, focus, the AI
      prompt and the security feed are surface-type agnostic.
- [x] `xwayland.c`: run a (lazy) Xwayland server; publish `DISPLAY`; set the X
      cursor; associate/dissociate/map/unmap/destroy; request_configure /
      _maximize / _fullscreen / _activate. Managed windows tile/float with
      borders + focus; override-redirect windows (menus/tooltips) go to the
      overlay layer at absolute coords and self-focus if they ask.
- [x] `focus_view` now toggles activation (X11 needs it); hardened focus/grab
      pointers to clear on unmap/destroy (fixes a latent xdg use-after-free).
- [x] `view_pid()` uses the real X11 pid (`xsurface->pid`) so synguard verdicts
      match individual X apps, not the shared Xwayland client.
- [x] xdg-decoration: force SERVER_SIDE mode (we draw borders), deferred to the
      surface's first initialized commit to avoid a `schedule_configure` assert.
- [x] Vendored no new protocol (wlroots ships XWayland); added xcb/xcb-icccm/
      xcb-ewmh build deps.

Verified: builds clean; the Xwayland server is created, `DISPLAY` published, and
lazy-start triggers on the first X client; xdg clients (foot) map with SSD; two
real crashes were found and fixed along the way (decoration configure-before-
init, and border rects with not-yet-sized geometry). Full X11 *window* mapping
could not be exercised in this sandbox because Xwayland's own XKB keymap
compilation fails here (host xkeyboard-config / nested-Xwayland limitation,
external to synui); synui degrades gracefully when Xwayland can't start.

### Phase F — Output & session management  *(done)*
- [x] fractional-scale (`wp_fractional_scale_v1`) — HiDPI sub-integer scaling.
- [x] idle-notify (`ext_idle_notifier_v1`) fed from every input event, plus
      idle-inhibit (`zwp_idle_inhibit`): each inhibitor suppresses idle-notify.
- [x] output-management (`output_mgmt.c`): `wlr_output_manager_v1` with all-or-
      nothing apply/test, position via the output layout, and `set_configuration`
      broadcast on hotplug/apply (wlr-randr / kanshi / wdisplays).
- [x] DPMS (`wlr_output_power_manager_v1`): set_mode toggles output enable.
      Vendored `protocols/wlr-output-power-management-unstable-v1.xml` (its
      wlroots header needs the generated enum).
- [x] session-lock (`session.c`, `ext-session-lock`): black backstop above all
      layers, per-output lock surfaces sized to their output, keyboard routed to
      the lock surface, all compositor bindings disabled while locked, and the
      session stays blanked if the lock client dies without unlocking.

Verified headless: all six globals advertised; a purpose-built lock client
drove new_lock → per-output lock surfaces → `locked` → unlock/destroy across two
outputs, and a SIGKILL'd lock client left the session locked (secure) without
crashing. output-management/DPMS apply couldn't be exercised (wlr-randr not
installed) but the config-broadcast path is hit by any manager client.

### Phase G — Input completeness  *(done)*
- [x] relative-pointer (`zwp_relative_pointer_v1`): every motion (relative or
      absolute-converted) is broadcast with raw + unaccelerated deltas.
- [x] pointer-constraints (`zwp_pointer_constraints_v1`, `constraints.c`): the
      constraint owned by the pointer-focused surface is activated; LOCKED
      absorbs cursor motion (deltas still flow to relative-pointer — FPS look
      input), CONFINED clamps the delta into the constraint region
      (`wlr_region_confine`); cursor-hint warp on destroy of the active
      constraint. Vendored nothing (protocol XML comes from wayland-protocols;
      only the wlroots-required generated header is built).
- [x] Touch: devices attach to `wlr_cursor`, seat advertises TOUCH; down maps
      through the scene to the surface (and focuses its view), per-point focus
      keeps motion on the surface the finger started on; up/cancel/frame.
- [x] Tablet (pointer emulation): pen motion/proximity drives the cursor
      (axis-partial updates honoured via NAN), tip = left button, stylus
      buttons = right/middle, all through the shared button path so
      Super+drag works with a pen. Full tablet-v2 is a follow-up.
- [x] Touchpad gestures: `zwp_pointer_gestures_v1` relay (swipe/pinch/hold).
- [x] libinput config from synuirc: `tap`, `natural_scroll`, `left_handed`,
      `accel_speed` — applied per device where supported; unset keys leave
      device defaults.
- [x] Configurable keymap: `xkb_rules/model/layout/variant/options`,
      `repeat_rate`, `repeat_delay` (empty = XKB_DEFAULT_* env / system).
- [x] Configurable keybindings: the hardcoded bind table became data —
      config.c seeds the old defaults, `bind = <mod>+<key> <action> [arg]`
      lines add or replace (same-combo overrides). Actions cover all previous
      behavior plus `spawn`; shifted-number normalization preserved.
- [x] Fixed: seat advertises the pointer capability from startup (a client
      calling `get_pointer` before any input device appeared was killed with
      a protocol error — always the case on headless).
- [x] Fixed: the initial xdg commit is now answered with a configure
      (`set_size(0,0)`). Plain xdg clients (no xdg-decoration) previously
      never got one and hung unmapped forever; foot only worked because our
      decoration set_mode scheduled a configure as a side effect.

Verified headless: `zwp_pointer_constraints_v1`, `zwp_relative_pointer_manager_v1`
and `zwp_pointer_gestures_v1` advertised; a purpose-built client mapped a plain
xdg toplevel (initial-commit fix) and created/destroyed a locked constraint
with cursor hint, compositor surviving with a clean SIGTERM exit; bind parsing
verified by table dump (33 defaults, user override of super+return, added
combo, bad key/action rejected with log); foot regression-checked. Constraint
*activation* needs real pointer focus, which headless input can't produce —
verify on hardware with a game or `wlroots`' pointer-constraints example.

### Phase H — Ecosystem protocols  *(done)*
- [x] foreign-toplevel (`foreign_toplevel.c`): every mapped managed window is
      published to taskbars via **both** zwlr-foreign-toplevel-management
      (state + requests: activate focuses and switches workspace, close/
      fullscreen/maximize reuse the client-request paths; minimize ignored)
      and the newer list-only **ext-foreign-toplevel-list**. Title/app-id
      changes stream live (xdg set_title/set_app_id, X11 set_title/set_class);
      activated/maximized/fullscreen state synced from focus_view and the
      view_set_* accessors.
- [x] screencopy (`zwlr_screencopy_manager_v1`) + export-dmabuf — grim,
      slurp-based tools, wf-recorder.
- [x] data-control (zwlr v2 **and** ext) + primary-selection
      (`zwp_primary_selection_device_manager_v1` + seat handler) — clipboard
      managers work without focus; middle-click paste.
- [x] gamma-control (`zwlr_gamma_control_manager_v1`, in output_mgmt.c):
      set_gamma commits the ramp to the output; NULL control resets; a failed
      commit (backend without gamma, e.g. pixman) sends failed_and_destroy so
      wlsunset/gammastep aren't left hanging.
- [x] drag-and-drop: seat request_start_drag validates the pointer/touch grab
      serial and starts the wlroots drag; the drag icon lives in a topmost
      scene tree that rides the cursor (mouse and tablet paths); when the
      drag ends, pointer focus is re-derived from the surface under the
      cursor.

Verified headless: all 8 new globals advertised; **grim** captured a real
1280x720 PNG (screencopy); **wl-copy/wl-paste** round-tripped the clipboard
(zwlr data-control) and `-p` the primary selection; a purpose-built
ext-foreign-toplevel-list client saw foot appear with correct title/app_id/
identifier and an empty list after close; clean SIGTERM teardown. Not
drivable headless: zwlr taskbar *requests* (same code paths as the client
requests), gamma commits (pixman has no ramps — failure path exercised by
design), and DnD (needs two real surfaces + a grab); verify on hardware with
waybar's taskbar module, wlsunset, and any file manager drag.

### Phase I — Robustness & CI  *(done)*
- [x] Listener audit: every `wl_signal_add` paired against its `wl_list_remove`
      across all sources. Per-object listeners (views, outputs, keyboards,
      layer/lock surfaces, decorations, inhibitors, constraints) all detach in
      their destroy handlers; the drag listener re-arms safely (`wl_list_init`).
- [x] Thread-lifecycle fixes found by the audit: the AI thread and the synguard
      secfeed thread were never stopped or joined and their pipes leaked.
      Added `ai_thread_stop()` (close the request pipe's write end → the
      thread's blocking read returns 0 → join) and `secfeed_stop()` (atomic
      stop flag + `shutdown()` on the feed socket + sliced retry sleep → join);
      both called first thing in `synui_destroy`.
- [x] The pipes are now `O_CLOEXEC` (`pipe2`): forked autostart / AI "CMD:"
      children used to inherit the write ends across exec, so closing ours
      never EOF'd the reader — shutdown would hang in `pthread_join` whenever
      an autostart client was alive.
- [x] `ai_thread_start()` failure paths harden: fds are closed and marked -1
      (previously a half-built pipe could block the event loop on send, and
      zeroed fds aliased stdin); `synui_init` now checks the return and falls
      back to a plain compositor.
- [x] Memory audit under ASan/LSan: `synui_destroy` now destroys the
      renderer + allocator and removes the SIGINT/SIGTERM event sources
      (the event loop doesn't free sources it still holds). Result: zero
      leaks on a full run (AI + secfeed threads, autostarted client, foot
      map/unmap, wayland-info, grim) — only fontconfig/cairo static caches
      remain, suppressed via `tests/lsan.supp`.
- [x] Test harness: `tests/smoke.sh`, wired as `meson test` ("smoke").
      Boots headless/pixman in a private short-path XDG_RUNTIME_DIR (socket
      paths cap at 108 bytes) with a hermetic empty config via the new
      `$SYNUI_CONFIG` override (without it the machine's /etc/synui/synuirc
      autostart leaked into the test), then asserts: socket up, all 29
      required protocol globals advertised, a client maps/unmaps (foot,
      skipped if absent), screencopy captures a frame (grim, skipped if
      absent), SIGTERM exits 0 within a deadline (catches teardown hangs),
      and no sanitizer errors in the log.
- [x] CI: the build job now runs the smoke test on the release build **and**
      on a fresh `-Db_sanitize=address` build, so the leak-free teardown is
      enforced on every push.

Verified: `meson test` green on both the release and ASan build dirs; the
ASan run reports zero leaks with only the two library-static suppressions.

### Phase J — Runtime config reload (SIGHUP)  *(done)*
- [x] `SIGHUP` → `synui_config_reload()`: reparse synuirc and reapply at
      runtime — keybindings (the bind table is read per keypress), xkb keymap
      + repeat (helper extracted from `server_new_keyboard`, applied to every
      keyboard in `s->keyboards`), libinput options (non-keyboard devices now
      tracked in `s->input_devs` with their own destroy listeners),
      gap/border (visible workspace re-tiled, every mapped view's borders
      refreshed). Autostart entries are start-only; per-workspace master
      factors keep interactively adjusted values.
- [x] Made the `gap` and `border_width` config keys real: they were parsed
      but never applied — layout and borders used the hardcoded `GAP` /
      `BORDER_WIDTH` macros. All geometry now reads `s->config`; the macros
      survive only as `*_DEFAULT` seeds. Values clamped (0–128 / 0–32) so a
      bad config can't feed negative sizes into scene rects.
- [x] Fixed a latent border bug the reload exposed: `view_update_borders()`
      only positioned existing border rects, never resized them — a retiled
      window kept its original border lengths. Now sets size too.
- [x] Smoke test grew a reload check: rewrite the hermetic config (gap 20,
      border 4, extra bind), SIGHUP, assert the "config reloaded" log line
      reports the new values and the compositor still answers wayland-info;
      runs green on release and ASan builds.

Not exercisable headless: keymap/libinput reapplication needs real input
devices — verify on hardware by switching `xkb_layout` and sending SIGHUP.

### Phase K — Per-output workspaces  *(done — superseded by Phase M)*
Each monitor now shows its own workspace instead of mirroring the active one.
**Superseded:** this model made a workspace a per-monitor slot, which pinned
workspaces 1..N to the N connected monitors and left `Super+1..N` unable to
switch anything. Phase M replaces it with desk-spanning virtual desktops.
- [x] Model: `syn_output_t.active_workspace` (what this output shows) +
      `syn_workspace_t.output` (where this workspace lives; NULL =
      unassigned/orphaned). The global `s->active_workspace` field is gone —
      "the" active workspace (keybinds, new windows, overlay, AI prompts) is
      `server_active_workspace()`: the workspace on the focused output, which
      follows the cursor. `workspace_visible()` = shown on its output now.
- [x] Layout targets the workspace's *own* output: `get_output_geom()` takes
      the workspace and resolves `ws->output`'s usable box (new
      `output_box_of`/`output_usable_box_of` helpers; the focused-output
      wrappers remain for the compositor UI). `layout_apply()` early-outs on
      hidden workspaces so laying out an invisible one can't re-enable its
      scene nodes over the visible one.
- [x] `workspace_switch()` acts on the focused output; a workspace already
      visible on another output isn't stolen — focus jumps there instead
      (cursor warped to that output's centre, i3/sway semantics). Otherwise
      the target is re-homed to the focused output and shown.
- [x] Hotplug: a new output takes an orphaned workspace that still has
      windows (replugging a monitor brings its windows back), else the
      lowest unassigned one. Output removal orphans its workspaces: windows
      hidden but reachable — switching to the workspace re-homes it.
- [x] `workspace_move_view()` shows/hides the view by the target workspace's
      actual visibility; layer-shell exclusive-zone changes re-tile the
      *owning* output's workspace; SIGHUP reload re-tiles every output.
- [x] Smoke test check 8: a dual-head boot (`WLR_HEADLESS_OUTPUTS=2`) must
      assign workspace 1 and workspace 2 to the two outputs and exit
      cleanly; green on release and ASan builds.

Verified headless (2 outputs): distinct workspace per output; a tiled client
sizes to one output's box (1260x700 on a 1280x720 head — not the 2560-wide
layout); per-output `grim -o` captures both heads; clean SIGTERM. Not
drivable headless: workspace switching / jump-focus need key input, and the
orphan-on-unplug path needs a real disconnect — verify on hardware with two
monitors (Super+N on each head, unplug/replug).

### Phase L — GLES post-process effects ("tier 3" cyberpunk pass)
wlr_scene exposes no shader hooks, so `effects.c` renders the scene into a
private per-output swapchain (`wlr_scene_output_build_state` with a custom
swapchain) and draws that buffer to the real output buffer through a
fullscreen-triangle GLES2 shader (raw EGL/GL against the wlroots renderer
context, `wlr_gles2_renderer_get_buffer_fbo` for the target). Any failure
falls back to the plain scene commit; pixman (VMs) never initializes the
pass, so the ISO look is unchanged.

- [x] L1 — pipeline + CRT pack: barrel curvature (black bezel outside the
      tube), per-row scanlines, chromatic aberration; one shader, 2D and
      external-OES sampler variants. synuirc: `effects = on/off` and
      `effect_scanline/curvature/aberration = 0..1` (0 skips the pass
      entirely), all SIGHUP-reloadable. Verified headless on GLES2: the
      zero-strength pass is pixel-identical to effects-off (no flip, no
      color shift), curvature blacks the corners, scanlines alternate rows
      (1.00/0.74 at full strength), and the full smoke suite passes on both
      pixman (fallback) and gles2 (pass active, dual-head, clean teardown).
      Known limits: non-NORMAL output transforms take the plain path; a
      hardware cursor plane is composited after the pass and stays crisp
      (arguably correct); `glFinish()` before commit is conservative —
      swap for a native-fence sync if it ever shows in frame times.
- [x] L2 (interim) — close glitch: a window unmap (xdg + XWayland paths)
      fires a 200ms decaying screen glitch via `effects_notify_close()`.
      The full snapshot + per-window slice-displacement animation is still
      open (needs buffer capture at unmap and a scene_buffer lifecycle).
- [x] L3 — focus pulse: `focus_view()` calls `effects_notify_focus()` on an
      actual focus change; chromatic aberration ramps +1.5 and decays over
      250ms. Animation clocks live in `struct syn_effects`; while one is
      running the pass adds whole-output damage and schedules the next
      frame, so animations run at output refresh and stop costing anything
      the moment they end.
- [x] L4 — synguard tie-in: while any mapped window holds an ALERT/DENY
      verdict (scanned per effects frame), the screen glitches for as long
      as the verdict stands: 8px horizontal bands displace by a per-band
      hash reseeded ~24x/s (`u_time`/`u_glitch` uniforms). Strength from
      `effect_glitch` (0..1, 0 disables; also gates the close glitch).
      Verified headless on GLES2 via SYNUI_EFFECTS_FORCE_GLITCH=1 (tests
      only): ~25% of bands displaced per frame matching the shader's
      step(0.75) gate, displaced rows are pure horizontal shifts of the
      original, band sets differ between frames 0.7s apart (the animation
      loop renders), clean SIGTERM mid-animation; both smoke suites green.

### Post-K bug sweep  *(done)*
A full review of the compositor sources fixed, in severity order:
- **Crash**: `focus_next()` walked the focused view's workspace list but
  bounds-checked against the cursor output's — when they differed
  (movews to a hidden workspace, or focus on the other monitor),
  `wl_container_of` ran over the wrong sentinel and dereferenced garbage.
- **Lock**: a relock after a crashed lock client leaked the kept black
  backstop; unlocking the new lock left the leaked one blanking the session
  forever. `server_new_session_lock` now destroys a leftover tree.
- **Tiling/focus**: closing an xdg window never re-tiled (XWayland did) and
  left keyboard focus dangling; both unmap paths now reflow and hand focus
  via the new `workspace_focus_first()`, which also fixes switch-to-empty
  and movews leaving focus on an invisible window (`focus_view(NULL)` now
  clears `focused_view` too).
- **Fullscreen/maximize**: request handlers toggled state instead of
  honouring `requested.*`, and xdg fullscreen never resized the window. New
  shared `view_apply_fullscreen()`: full output-box geometry on the
  workspace's own output, raised, borders hidden (view_update_borders
  checks the flag); used by xdg, XWayland and taskbar request paths.
- **Monocle**: keyed visibility off the *global* focused view, blanking the
  whole workspace when focus was on another output or nothing; now shows
  the focused view if it lives here, else the first mapped window.
- **AI IPC**: the synapd reconnect socket had no SO_RCVTIMEO (a wedged
  synapd would hang the thread and, since Phase I joins it, shutdown);
  oversized responses left unread payload in the stream, desyncing every
  later reply (now drained + implausible lengths rejected).
- **output-management**: a successful apply now re-arranges layers (usable
  areas + tiling), lock surfaces and the compositor UI instead of leaving
  stale geometry until an unrelated re-layout.
- Minor: tiling clamps against negative sizes (large gap on a tiny output);
  the WINDOW_OPENED advisory query uses a sentinel id instead of the pid
  (which only avoided the workspace-index router because pids are never
  < 9); AI `CMD:` children get `setsid()` like `spawn()`; jump-focus
  re-derives pointer focus right after the cursor warp.

Not drivable headless (needs key/pointer input or real hardware): the
focus_next crash path, monocle multi-head behaviour, fullscreen requests,
and output-management apply — the fixes are compile-verified and the
surrounding paths run leak-free under the ASan smoke test.

### Phase M — Workspaces are virtual desktops  *(done)*
A workspace is a *virtual desktop spanning the whole desk* (KDE/GNOME/Windows
semantics), not a slot owned by one monitor. Phase K's model bound each
workspace to an output, so on a 3-monitor desk workspaces 1–3 were permanently
claimed, one per screen: `Super+1..3` could never bring a desktop to the screen
you were on (it warped the cursor to the monitor that already owned it, or, if
you were already there, did nothing at all), and only `Super+4..9` still
switched. The binds were firing the whole time — the model had nowhere to put
them. Reported as "Super+numbers is unresponsive".
- [x] Model: `syn_server_t.active_workspace` is back — one desktop is shown at
      a time, on **every** output at once. `syn_view_t.output` records which
      monitor a window sits on within its desktop. `syn_workspace_t.output` and
      `syn_output_t.active_workspace` are gone, and with them the whole
      orphan/re-home/steal machinery. `workspace_visible()` is now just
      `ws->visible`.
- [x] `workspace_switch()` switches the entire desk: hide the outgoing
      desktop's windows on all monitors, show the incoming one's, re-tile.
      Always does something, on any number of monitors. No cursor warping.
- [x] Layout is per (workspace, output): `layout_apply()` runs the workspace's
      layout once per output over just the windows that live on that output, so
      each monitor tiles its own share. Monocle is per-output (a 3-monitor
      desktop shows three windows, one per screen), and so is the niri strip —
      each monitor scrolls its own run of columns, which is why the scroll
      offset lives on `syn_output::strip_scroll[]` and not on the workspace.
      `layout` and `master_factor` stay per-desktop — every monitor showing it
      tiles the same way.
      AI layout is focused-output-only (one in-flight request; the others tile),
      with `server::ai_layout_output` routing the async reply to the right box.
- [x] `Super+Shift+1..9` (`workspace_move_view`) sends a window to another
      desktop keeping its monitor, so it's where you left it when you switch.
      `Super+O` (`view_set_output`) moves it between monitors keeping its
      desktop. The two axes are now independent.
- [x] `workspace_focus_first()` prefers a window on the focused output —
      switching desktops must not throw focus onto another screen.
- [x] Hotplug: a new monitor claims no workspace (every desktop already spans
      it); it comes up showing the current desktop's share. Unplugging one
      re-homes its windows — on *every* desktop — onto a surviving monitor, so
      nothing is stranded off-screen.
- [x] Smoke test check 8 inverted: a dual-head boot must show workspace 1 on
      **both** outputs, and no output may claim its own workspace.

### Phase N — Server-side decorations: titlebar, buttons, mouse drag  *(done)*
synui told every client SERVER_SIDE via xdg-decoration but only ever drew a 2px
border, so windows had no titlebar, no buttons, and could only be moved with
Super+drag. They now have all three.
- [x] **Per-view frame tree.** Every managed toplevel gets a `syn_view::frame`
      scene tree; the client's surface tree and all chrome are children of it, so
      one node is enabled/hidden/raised/moved. This fixed two real bugs: the
      borders were siblings in `window_tree`, so (a) a window hidden by a
      workspace switch **left its border rects painted on screen** (verified: an
      empty desktop rendered the same 10304 border pixels as the one with the
      window), and (b) raising a window didn't raise its chrome. `view_node()`
      returns the frame, or the bare surface tree for override-redirect X11.
- [x] **Frame geometry.** `view->x/y/w/h` is now the *outer* frame;
      `view_content_box()` is what the client gets (inset by the border and the
      titlebar). Fullscreen drops both, so frame == content. This also closed a
      pre-existing off-by-a-border gap: the client was positioned at the frame
      origin but sized `w - 2*bw`, leaving a border-wide strip of wallpaper down
      the right and bottom edges.
- [x] **Titlebar** (`deco.c`): one cairo buffer with the title and three
      square, right-aligned buttons — `[ _ ]` minimize, `[ □ ]` maximize,
      `[ × ]` close — repainted only when the size, title, focus or hovered
      button actually changes. Hovering lights the button; close lights alarm
      red. `titlebar_height = 0` in synuirc turns it off (borders and Super+drag
      remain), colors are `titlebar_color[_focus]` / `titlebar_text[_focus]`.
- [x] **Mouse drag.** Press the titlebar to move (auto-floating a tiled window
      as Super+drag already did); press any border to resize from *that* edge,
      with the corners grabbing both axes. Double-click the titlebar to maximize.
      Dragging a maximized window restores it and takes it with the cursor.
- [x] **Maximize was a lie, and now isn't.** `maximize_toggle` set
      `view->maximized` and told the client, but *nothing read the flag* —
      layout.c doesn't know it exists, so the window never changed size.
      `view_apply_maximized()` now leaves the tiling flow, covers the output's
      usable box, and restores the previous geometry (and tiled-ness) on the way
      back. Client-side `set_maximized` requests go through it too.
- [x] Verified headless by driving the real click path (cursor warp +
      `pointer_button`): all five hit regions resolve correctly; maximize fills
      the output and restores to its tiled slot; a border drag resized exactly
      -120px; a titlebar drag moved exactly +100,+80; minimize disabled the node.

### Phase O — Protocol compatibility gaps  *(done)*
Four protocols synui never implemented, all of which clients actively looked for
and fell back from. foot listed every one of them as a warning on startup; it now
starts silent.
- [x] **text-input-v3 + input-method-v2** (`ime.c`) — the big one. Without them
      every toolkit disables its IME ("IME will be disabled" — foot), so **no
      CJK, no compose key, no emoji picker**: nothing that isn't a direct
      keysym→character mapping could be typed at all. `ime.c` is the relay
      between the two protocols, which cannot see each other: it follows keyboard
      focus (activate/deactivate), mirrors the app's surrounding text / content
      type / caret rect to the IME, pushes the IME's preedit + commit strings
      back into the app, routes raw keys to the IME while it holds the keyboard
      grab (so "nihao" reaches fcitx5 to be composed instead of landing in the
      field as five Latin letters), and parks the candidate popup at the caret
      (flipping above it when there's no room below). One IME per seat; a second
      is sent `unavailable` rather than fighting over keystrokes.
- [x] **xdg-activation-v1** — how a running app asks to be raised (clicking a
      link hands the URL to the Firefox you already have open, which then
      requests activation). The request was silently dropped, so the window
      never surfaced. Now honoured fully: un-minimize, switch to its desktop,
      raise, focus.
- [x] **cursor-shape-v1** — clients name a cursor ("text", "grab") instead of
      shipping a pixel buffer. Without it they drew their own, which is why the
      cursor changed theme and size between apps. Ignored while an interactive
      move/resize owns the cursor.
- [x] **xdg-toplevel-icon-v1** — a window naming its own icon is the only icon
      source for an app that ships no .desktop file (Wine, Electron). Fed into
      the icon cache the dock already reads, so it fills exactly the gap where
      the dock used to fall back to a monogram.
- [x] Each new listener is removed at teardown — wlroots asserts empty listener
      lists when it destroys the managers, and xdg-activation aborted the
      compositor on SIGTERM until it was.

### Phase P — Hyprland-style polish: animations + control socket  *(done)*
Cherry-picked from Hyprland rather than adopting it: synui keeps synapd, the
security borders, the dock and game mode, and gains the parts worth having.
- [x] **Animations** (`anim.c`) — windows fade in when they open, and switching
      desktop cross-fades (the outgoing windows fade out and are only disabled
      once actually invisible). Runs off the existing per-output frame tick, like
      `dock_tick`/`cat_tick`. `animation_ms = 0` disables it; every fade then
      jumps straight to its end state so nothing else has to care.
      Fading a window means fading it *whole*: `wlr_scene_node_for_each_buffer`
      covers the client surfaces and the titlebar, while the four border rects
      carry alpha in their colour and are multiplied by `view->alpha` in
      `view_update_decorations`.
      **Not** doing geometry animation: animating a window's size means
      re-configuring the client every frame — a resize storm — which is why
      Hyprland animates a scaled *snapshot* instead. That needs render control
      `wlr_scene` does not expose. Fades are the honest subset.
- [x] **Control socket** (`ipc.c` + `synctl`) — the hyprctl of synui.
      `$XDG_RUNTIME_DIR/synui-$WAYLAND_DISPLAY.sock`, 0600, exported to children
      as `SYNUI_SOCKET`. JSON state (`clients`, `workspaces`, `outputs`,
      `activewindow`, `activeworkspace`) plus `dispatch <action> [arg]`, which
      runs **any keybind action by name** — so anything bindable is scriptable,
      with no second registry to keep in sync (`synctl dispatch ws 3`,
      `synctl dispatch spawn foot`). Listener and clients live on the
      compositor's own `wl_event_loop`, so handlers run on the main thread
      between frames: no locking, no racing the scene graph. Window titles are
      JSON-escaped (arbitrary user data must not be able to forge fields).

### Not done: rounded corners + blur
Both need per-surface render control that `wlr_scene` does not expose — there is
no corner radius, no blur and no per-node shader hook, and the CRT post-process
pass in `effects.c` cannot help: it runs on the *composited* frame, where the
pixels behind a window's corner have already been occluded, so there is nothing
left to round *to*. The real path is **scenefx** (the `wlr_scene` fork SwayFX
uses, which adds corner radius, blur and shadows). It is not packaged here and
would mean migrating every `wlr_scene_*` call and pinning to a wlroots version.
Left as an explicit decision rather than a silent gap.

### Phase Q — niri-style scrollable tiling  *(done)*
A fifth layout, on the Super+Tab cycle after AI and spelled `niri` everywhere
the others are (`layout_label`, `layout_key`/`layouts.state`, `synctl`'s
`layout_name`). What it is, and why it is built the way it is:

- [x] **The strip.** Each (desktop, monitor) holds one endless horizontal strip
      of columns, laid out in `layout_niri()`. A new window opens in a column of
      its own beside the one you are in and the strip gets *longer* — nothing
      already on screen is made narrower. That is the whole difference from
      `layout_tile`, where every extra window costs the stack width, and the
      reason `niri_strip.sh` asserts the columns' width does not change when a
      third window opens.
- [x] **The strip IS the workspace list.** No column tree: the order is
      `ws->windows` filtered to the output, and `syn_view::col_join` says "I
      share the column of the window before me". So `Super+Shift+J/K`
      (`layout_move_in_stack`) moves a window along the strip and in and out of
      columns for free, and every existing path that touches the list — map,
      unmap, `workspace_move_view`, `view_set_output`, `layout_reclaim` — keeps
      working with no second structure to leave holding a freed view.
- [x] **Scroll follows focus.** `syn_output::strip_scroll[WORKSPACE_MAX]` (per
      output: each monitor scrolls its own run of columns), moved the least
      distance that puts the focused column fully on screen and re-clamped
      against the real strip on every reflow, so a stale offset costs one frame
      at most. `focus_view()` reflows a niri desktop for the same reason it
      reflows a monocle one — Alt+Tab, `focus_next`, a dock click and a plain
      click all funnel through it.
- [x] **A partly-visible column is not drawn.** niri proper lets the neighbours
      peek in at the screen edge; synui cannot, because a window is placed by
      moving its scene node in *layout* coordinates and its borders and titlebar
      are separate rects outside `client_tree` — there is nothing that crops a
      frame at the monitor edge, so a half-off column would be painted across
      the monitor next door. Hiding it is the honest version, and the scroll
      rule guarantees the hidden one is never the focused one. A real fix is a
      per-frame clip on the whole frame, which is the same gap that stops
      geometry animation (Phase P).
- [x] **Column width is a fraction of the SLOT**, column plus following gap
      (`niri_col_width`), so 1/n of the screen means n columns fit exactly.
      Taking the fraction of the bare viewport costs a gap per column, which at
      the default 0.5 pushes the second column a few pixels off the edge — and
      the rule above then hides it, leaving one window on a two-thirds-empty
      screen. `layout_tile` subtracts the same gap from its master column.
      `Super+H` / `Super+Shift+L` resize the focused *column* here rather than a
      master slot that does not exist; the fraction is written to every member
      of the column so it survives the leader moving or closing.
- [x] **Columns stack**: `column_consume` (Super+,) pulls the focused window
      into the column on its left, `column_expel` (Super+.) pushes it back out.
      niri's own keys. Both are no-ops on the other four layouts.
- [x] Selecting the layout reclaims the desktop the way tiling and AI do
      (`layout_reclaim`), and `layout_restore_geometry` bypasses `windows.conf`
      on it — the strip owns where a window goes, and a remembered box would put
      windows back outside any column, where nothing can scroll to them.
- [x] `niri_strip.sh` covers the three properties that make it niri and not
      another way of drawing the tiler: columns that do not shrink, a strip that
      scrolls to the focus, and consume/expel.

### Phase R — rofi is the launcher, Super+Space is the launcher key  *(done)*
Super+Space opened the AI command bar, which meant the one key every other
desktop reserves for "start a program" was the one key here that did something
else. The launcher moved onto it and the command bar moved off.

- [x] **`super+space` → `spawn rofi -show drun`.** rofi 2.0.0 merged lbonn's
      Wayland port into mainline, so the plain `rofi` package (it
      Provides/Replaces `rofi-wayland`) talks layer-shell to synui — no
      XWayland, no fork, no AUR. It reads the same `.desktop` roots the bar menu
      already curates, so the applications.menu and Wine-entry work it inherits
      for free.
      A **spawn, not an action**: synui does not own rofi's lifetime. rofi
      single-instances, so a second press while it is up is a no-op rather than
      a second window — this is *not* a toggle, unlike every panel bind, and the
      key will not close it. Escape does. Upstream also documents two
      Wayland-backend limits in the 2.0.0 notes: it cannot see clicks outside
      its own surface (so clicking away does not dismiss it either), and it
      flickers on startup because scale is detected late.
- [x] **The AI command bar moved to `super+equal`**, putting it next to
      `super+backspace` (`ai_ask`) — on a US layout `=` is the key immediately
      left of Backspace, so the two AI popups are physical neighbours. It stays
      a toggle. Nothing else about the cmdbar changed: it still carries the
      synsh intents and the output capture, which is exactly why it was not
      worth replacing with rofi rather than moving.
- [x] **`rofi` is a hard `depends`**, for the same reason `kitty` is one: it
      backs a *default* bind, and an optdepend would mean the most-pressed key
      on the desktop silently does nothing on a fresh install. It is in the
      ISO's `packages.x86_64` too, so the live image ships it.
- [x] **`key_name()` learned `XKB_KEY_equal` → `=`** (`ctlpanel.c`). The
      function exists so the shortcuts column reads like a keycap and not like a
      config file; without the case the panel rendered `Super+equal`. Note the
      asymmetry it is papering over: a bind combo is split on `+`, so `=` *has*
      to be written `equal` in synuirc — `bind = super+= cmdbar` parses as an
      empty key and is dropped with a "bad key" log line. Documented in the
      shipped `synuirc` and in `--help`.
- [x] **Two `--help` drifts fixed while in there.** The list claimed
      `Super+Shift+T` was the calendar (it has been `retile` since 2026-07-31)
      and that `Super+Shift+A` was "intentionally free" (the desktop widgets
      took it). The comment above that list says to keep it in step with
      `seed_default_binds()`; it was not. The control panel's column does not
      have this failure mode — `ctlpanel_shortcuts()` walks the live bind table
      — but its `action_desc()` lookup is a second table that a *new action*
      would still drift from. A `spawn` bind is exempt: it falls through to the
      command string, so the rofi row reads `rofi -show drun`.
