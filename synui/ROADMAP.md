# synui Roadmap — toward a full AI-aware Wayland compositor

`synui` is SynapseOS's wlroots-based Wayland compositor. This document tracks
the gap between its current state and the roadmap goal, *"full Wayland
compositor with AI-aware window management."*

## Current state (real `src/`, ~5,640 LOC)

Working:
- wlroots backend/scene init, VM detection → pixman fallback
- xdg-shell toplevels (map/unmap/destroy/commit), keyboard focus
- Tiling (master-stack) and monocle layouts; 9 workspaces
- Keyboard bindings; basic pointer (focus / click / axis); clipboard selection
- Cairo-rendered UI: welcome screen, AI command bar, neural overlay
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

Missing: full tablet-v2, per-output workspaces, config reload (SIGHUP).

## Phases (ordered by value ÷ effort)

### Phase A — Make AI window management actually work  *(in progress)*
The distinguishing SynapseOS feature, currently non-functional.
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
per-output workspaces land.

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
