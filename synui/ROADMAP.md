# synui Roadmap — toward a full AI-aware Wayland compositor

`synui` is SynapseOS's wlroots-based Wayland compositor. This document tracks
the gap between its current state and the roadmap goal, *"full Wayland
compositor with AI-aware window management."*

## Current state (real `src/`, ~5,640 LOC)

Working:
- wlroots backend/scene init, VM detection → pixman fallback
- xdg-shell toplevels (map/unmap/destroy/commit), keyboard focus
- Tiling (master-stack), spiral (fibonacci), monocle, floating (inset grid) and
  niri (scrollable-tiling, animated) layouts; 9 workspaces
- Keyboard bindings; basic pointer (focus / click / axis); clipboard selection
- Cairo-rendered UI: AI command bar, neural overlay, and ~30 panels
  (the welcome screen was one of them until 497; it is quickshell now)
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
      `accel_speed`, `accel_profile` — applied per device where supported;
      unset keys leave device defaults. `accel_profile` is checked against the
      device's own supported set, since a device can offer a speed and still
      support only one curve.
- [x] `pointer_smoothing` (0-10): synui's own low-pass filter over the cursor
      path, for a low-DPI or unsteady pointer — libinput has no such option.
      A leaky bucket rather than an average, so no travel is lost, with a
      settle timer that applies the remainder when the pointer stops. Derived
      from elapsed time, so one setting means the same on a 125 Hz mouse and a
      1000 Hz one. Cursor only: relative-pointer clients still get raw motion.
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
- [x] **drop onto the desktop** (deskdrop.c): a drag let go over the wallpaper
      copies its files into `~/Desktop` and pins each icon on the cell it was
      dropped on. The desktop is not a wl_surface, so there is nothing for
      wlroots to deliver a drop to — the compositor talks to the drag's
      `wlr_data_source` directly (accept + dnd_action on hover; dnd_drop, send
      down a pipe read from the event loop, dnd_finish on the drop). The
      release is claimed in `pointer_button()` *before* wlroots sees it,
      because the drag grab reads a release with no client focus as a failed
      drop and destroys the source mid-transfer; the drag is then ended with
      `wlr_seat_pointer_end_grab()`, which leaves the source alone.
      Requires `desktop_icons`; always a COPY (in this protocol the *source*
      deletes the original on MOVE, and there is no exit status to promise it
      on); sources that never called `set_actions` are refused, since wlroots'
      dnd_drop/dnd_finish assert on pre-v3 `wl_data_source`. `text/uri-list`
      only, local `file://` URIs only — a link drag from a browser is declined
      rather than half-handled. Collisions become `name (copy).ext`; nothing
      is ever overwritten. Rigs: `deskdrop` (uri-list parsing, collision
      renaming) and `deskicon_drag` 22–25 (where a drop lands, and that it
      persists).

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
      `dock_tick`/`cat_tick`. A duration of 0 disables it; every animation then
      jumps straight to its end state so nothing else has to care.
      **Both events are now configurable separately** — `anim_window`
      (off/fade/rise) on `anim_window_ms`, `anim_workspace` (off/fade/slide) on
      `anim_workspace_ms`, sharing one `anim_curve`. The styles that MOVE
      something ride `view->anim_dx/dy`, a draw-time offset on top of the
      window's logical geometry, so a slide costs no client round trips and
      nothing else on the system sees the window move. `animation_ms` is still
      accepted and sets both lengths.
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

> **Superseded in 0.1.0-425, settled in 0.1.0-426.** rofi is still the launcher
> and still a `spawn_toggle`; `Super`+`Space` is the command bar again — this is
> SynapseOS and the command bar is what it is for — and the launcher has
> `Super`+`=`, next to `Super`+`Backspace` (ai_ask), which is where the command
> bar used to sit. 425 put the launcher on `Super`+`,` instead and pushed the
> niri column moves off niri's own keys onto `Super`+`[`/`]` to make room; 426
> put the column moves back on `Super`+`,`/`.` and left the launcher one key,
> not two. Only the chords moved; everything else in this phase stands.

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
- [x] **The swap was a setting, and that was the mistake — REMOVED (2026-08-12).**
      Control panel ▸ Desktop ▸ "Super+Space opens" flipped the pair live and
      `super_space = launcher|cmdbar` in synuirc was the same switch, one setting
      rather than two `bind =` lines a user has to keep consistent.
      `synui_config_apply_launcher_binds()` ran **dead last** in
      `synui_config_load()` — after the seed table, after synuirc, after
      settings.state — because all three write those combos and the setting had
      to have the last word.
      Then Phase S's rebind helper (F2) landed and `binds.state` became a fourth
      writer of the same two combos, loaded just *before* that call: a chord
      moved in the palette was silently put back at the next config load, and
      velle hit exactly that. The guard did not help — it refused only when a key
      held something OUTSIDE the shipped pair, and swapping the pair by hand is
      precisely the case it read as "still ours, and the wrong way round".
      It was the same bug `ctlpanel_shortcuts()` exists to prevent, in the other
      direction: a second place a keybinding is declared. **The palette owns both
      chords now.** `super_space` is parsed only to log that it is obsolete;
      `settings.c` keeps a `g_obsolete[]` list so an upgraded settings.state
      drops the dead key instead of rewriting it forever.
      `SYN_BIND_LAUNCHER` / `SYN_BIND_CMDBAR` stay — the seed table and the tests
      still want the launcher command spelled once.
      `tests/settings_test.c` pins both halves: the key moves nothing, and a swap
      written as two `bind =` lines STAYS swapped.
- [x] **rofi is themed by `synui-apply-theme.sh`**, so it tracks SYNAPSE/Dark/
      XP/95 like everything else. It is the one themed surface that needs **no
      reload signal** — no SIGUSR2 like waybar, no D-Bus nudge like Dolphin, no
      "next restart" caveat like Firefox — because rofi is spawned fresh on every
      keypress and re-reads the file each time it opens.
      Same file split as kitty, for the same reason: the palette is generated
      into `~/.config/rofi/synui.rasi` and the user-owned `config.rasi` only
      gains an `@import` if it lacks one (appended, since rofi takes the LAST
      definition of a property). The import is written bare rather than with
      `$HOME` — rofi resolves it against its own config dir, so the file survives
      being copied to another machine or user.
      Colours are `#rrggbb`, not rasi `rgba()`: the popup alpha belongs to the
      *window*, and putting it on the shared `*` block makes the text translucent
      too, which reads as a rendering bug rather than as glass. The selected row
      is accent-background plus `ink_for()`'s contrast pick — the only pairing
      that holds across a neon magenta, XP's beige and 95's silver.

### Phase S — The shortcut palette (`Super+/`)  *(done)*

- [x] **A searchable list of every keybinding, one key from anywhere.**
      `src/keys.c` + `synui_render_keys()`. Super+/ opens it, you type, it
      narrows, Enter runs the shortcut you land on.
      The list already existed — Control panel ▸ Shortcuts — but as a page you
      have to *be in the control panel* to reach, that does not filter and
      cannot run anything. Fine as documentation, no use as the thing you reach
      for when you know synui does something and cannot remember which key. That
      column stays exactly as it was; this is the same data as a palette.
- [x] **It is not a second list.** Both come out of `ctlpanel_shortcuts()`,
      which walks the live bind table — the reason that function exists at all
      is that a hand-maintained shortcut list drifts, and this project has
      shipped that bug once already (the waybar start menu's stale entry list
      mapped menu items to the wrong commands). A `bind =` line in synuirc shows
      up in the palette with no second edit.
      `syn_ctl_shortcut_t` grew `action`/`arg` to carry the bind itself, so the
      palette can press Enter on a row. The control panel's column ignores both
      — it is read-only — but generating the two from one function is the point.
- [x] **Enter goes through `synui_binding_execute()`**, the same path a keypress
      takes, so a row cannot do something subtly different from the key it
      describes. The panel hides **first**: half these actions open a modal
      panel, and running one with the palette still up leaves two stacked with
      the wrong one swallowing the keys. Pinned in the test, from inside the
      dispatch stub, because it is an *ordering* bug and asserting afterwards
      would not see it.
- [x] **Bound twice, on purpose.** `super+slash` and `super+shift+question`. On
      a US layout Super+? *is* Super+Shift+/, and xkb hands the compositor the
      shifted keysym (`question`, not `slash`) — so a single `super+slash` bind
      is a key that mysteriously stops working the moment you hold Shift, which
      is exactly what a hand reaching for "?" does.
- [x] **Typing is the whole interaction**, which is why the navigation keys are
      Up/Down and Ctrl+N/P rather than the j/k every other panel uses: j and k
      have to type a j and a k. It claims bare Shift and every printable key
      (`q` types a q rather than closing) but Super and Alt always fall
      through — Super+/ is how you close it, and a search box that swallowed
      Super+C would be a box you are stuck in.
      Search is over **both** columns and words are ANDed order-independently,
      so `super+w`, `wallpaper` and `float super` all find what you meant.
      Backspace on an empty query deliberately does *not* close the panel, unlike
      the control panel's search box: there the box is a mode over a list that is
      still underneath, here the box *is* the panel.
- [x] **The rows that cannot run are greyed, not hidden.** "Super+1–9" is nine
      binds collapsed into one line and names no single one of them, so Enter
      refuses rather than guessing a workspace. Super-tap is the opposite case —
      not a bind at all, but it *does* have an action (`start_menu`), so it runs.
- [x] **The tap can be moved as well** — `tap_key = super|ctrl|alt|shift|none`,
      and F2 on the "Start menu" row captures it. It was the one row that refused
      a rebind, on the grounds that it is not a bind; but "not a bind" describes
      what it IS, not a reason it cannot be changed, and a palette that lists a
      shortcut and then declines to move it is the control panel's old problem in
      miniature. Its capture is the mirror image of every other one: the modifier
      press that a chord capture must throw away (or every rebind comes out as
      "Super") is the only key this one accepts, so `syn_rebind_capture_ignores()`
      takes the ROW and not just the keysym, and both panels ask it rather than
      each keeping a line about modifiers.
      Delete captures "none", because off has to be sayable — otherwise the only
      way to stop a stray Super opening the menu is to hand-edit synuirc. The
      menu itself is unaffected: Super+Escape's welcome page, the bar's start
      button and `synctl dispatch start_menu` all still open it.
      It rides in `binds.state` with the rebinds, as a diff against the loaded
      config, so Ctrl+Shift+R puts it back with everything else. In
      settings.state it would have been the one shortcut "reset every shortcut"
      did not reset. Arming a capture also disarms the tap in `input.c`, or
      capturing Super would open the start menu on the release of the very key
      just captured.
- [x] **Discoverable without already knowing the key**: it is the second entry
      of the welcome menu (Super+Escape), it lists *itself*, and `--help` and the
      shipped `synuirc` both document it.
- [x] **`tests/keys_test.c`** — 51 checks, driven by keysym the way
      `ctlpanel_choice_test` is, for the reason `panel_pointer_test` gives at
      length. `config.c` is deliberately **not** linked: the bind table is seeded
      by hand, so "the palette lists whatever is in the table" is a real
      assertion rather than a restatement of the defaults.

### Phase T — The Antiquity shell  *(done)*

- [x] **A second bar ships**: `quickshell-antiquity/`, a port of
      [linux-antiquity](https://github.com/diinki/linux-antiquity) (MIT, ©
      2026 diinki) — radial taskbar, sidebar, tarot-card power menu. Selected
      with `bar_shell = antiquity` in synuirc or Control panel ▸ Desktop ▸ Bar
      shell. The two trees are complete and independent: they disagree about
      nearly every visual decision, and a shared base would be a third thing to
      keep working.
- [x] **`SynWorkspaces.qml` is the port's one real rewrite.** Upstream binds
      `Quickshell.Hyprland`, which is a live model with change signals. synui has
      no such thing — `synctl` is request/response with no event stream — so this
      polls `synctl workspaces` on one 400ms timer and shares it. ONE poller, not
      the three upstream could afford for free.
      The semantic difference is why it is not a rename: Hyprland scopes a
      workspace to a monitor and each bar filtered on `w.monitor.name`; synui's
      desktops span every monitor at once, so the filter is **gone** rather than
      reimplemented. Reintroducing it would mean inventing a partition the
      compositor does not have. A click dispatches the same `ws` action the
      Super+N bind runs, so click and keypress cannot drift.
- [x] **The licence audit is in `quickshell-antiquity/FONTS.md`**, read out of
      the fonts' own OpenType `name` tables rather than inferred from filenames.
      Three of upstream's nine fonts could not ship — Monaco and Charcoal (Apple
      / The Font Bureau, proprietary) and DOMINICA (Harold Lohner, donationware).
      Charcoal is loaded upstream and never referenced, so dropping it changes no
      pixels; the other two were rehomed onto `Config.fontMono` (DejaVu Sans
      Mono, already a hard depend) and Recia. Boska/Recia/Quilon carry ITF's
      credit clause, which FONTS.md discharges by name; `LICENSE.antiquity`
      vendors upstream's MIT text, and `license=()` gained MIT and Apache-2.0.
- [x] **The icon theme is deliberately NOT pinned.** Upstream hard-pins
      `buuf-nestort` with a static `//@ pragma IconTheme`; SYNAPSE does not
      redistribute it (7,552 loose files, no licence of any kind in upstream's
      tree, and Buuf is Paul Davey's non-commercial artwork). A pragma would also
      have overridden whatever `synui-apply-theme` just wrote for GTK/Qt and split
      the desktop in two. `bar_icon_theme` in synuirc is the dynamic equivalent —
      synui-bar exports it as `QS_ICON_THEME` — and empty, the default, means
      "follow the system theme" so a theme switch carries the bar along.
- [x] **The key is parsed by a compositor that never acts on it.** synui does not
      start the bar; the session does, and `synui-bar` reads `bar_shell` itself
      out of settings.state then synuirc, in synuirc's own `key = value` language.
      It is declared in `config.c` anyway so the key has ONE spelling and the
      control panel can persist it through settings.state like every other
      setting — a row writing a private file would be the eighth per-subject
      state file `settings.c` exists to stop. The row's help says it takes effect
      at the next login, because `CTL_APPLY_NONE` is literally correct here and a
      row that appears to do nothing reads as broken.
- [x] **`mktarball.sh` is why this could not have shipped by accident.** Its
      `contents=()` is the tarball's whole world, and a top-level directory
      missing from it silently does not exist in the package — the tree sat in
      the working copy for three days in exactly that state. Now listed.
      The PKGBUILD copies it with `find`, not the per-directory globs the SYNAPSE
      bar uses: this tree is five deep and mixes seven file types, so six globs
      per directory is six chances to drop one, with the same symptom the globs
      were written for (a package that installs cleanly and a shell that will not
      start). Verified by diffing the source file list against the built package
      — 98 files in, 98 files out.
- [x] **Known gap: no `IpcHandler`.** Upstream never needed one, because Hyprland
      could be told things directly. So the Super tap does not open a start menu
      on this shell; `synui-bar ipc` against it fails rather than doing nothing
      quietly. Documented in `synui-bar.sh` and in the shipped `synuirc`.

### `corner_radius` reaches the desktop's own furniture  *(done)*

Since the scenefx migration `corner_radius` rounded every *window* on the
desktop — and nothing synui or the bar drew themselves. Turning corners on
rounded all thirty applications and left the control panel, the task manager,
the pickers, the desktop and dock menus, the start menu and the bar's popups
square: the setting appeared to work, and the parts of the desktop that are
actually ours were the exception.

- [x] **The compositor's panels: `panel_chrome_sync()` (`render.c`), one table,
      called from `output_frame`.** Not a radius call in each of the twenty-nine
      renderers, because the panels' background rects are created lazily on first
      render and resized on every one after — a radius set at creation time would
      need repeating anyway. Cheap per frame: 29 pointer tests, and
      `wlr_scene_rect_set_corner_radii()` returns without damaging anything when
      the radii already match. Running it per frame is also what lands a radius
      change on panels that are already open.
      The accent rule gets the radius on its TOP corners only — not cosmetic
      symmetry: a full-width 2px strip over a rounded background pokes out as two
      tabs where the curve has taken the background away. scenefx's corner shader
      is an SDF with no clamp to the rect's own height, so a radius taller than
      the strip eats its ends and the rule fades out exactly where the curve
      begins.
      **Not the overview.** Mission control's "panel" is a full-screen dim the
      size of the output; rounding it cuts a transparent notch out of each corner
      of the SCREEN. A background that covers everything has no corners to round.
- [x] **The bar's panels follow the same setting** — the right-click menu, the
      start menu, the mixer, the module tooltips and the OSD, all of which now
      take `Theme.panelRadius`. The controls INSIDE them keep `Theme.radius` (4):
      they are 18–22px tall, and `corner_radius = 14` on a menu row is a capsule,
      which is not what anybody asking for rounded panels asked for.
- [x] **The retro rule travels as a DERIVED fact, not as an enum.**
      `chrome_corner_radius()` forces 0 for LUNA and BEVEL — a Win95 desktop with
      a 14px-rounded start menu is neither one thing nor the other — and the bar
      cannot work that out without a copy of the chrome table in QML, which would
      be wrong the first time a preset changed. So `theme.state` carries
      `square_chrome = on|off`, written by `theme.c`, and the bar's whole share of
      the rule is one ternary.
      It is in theme.state and not theme.json because theme.json is written by
      `synui-apply-theme`, which is handed a palette and never learns which
      chrome drew it (the Antiquity bar calls that helper directly, with nothing
      but colours). The radius itself has the other lifetime — it changes at any
      moment — so `BarConfig` reads it from the three files synui reads it from,
      **in synui's order**: `uifx.state` (the corner slider's own file) over
      `settings.state` over `synuirc`. Read in any other order the bar disagrees
      with the compositor drawing two inches below it, which is the whole class
      of bug this closes.
- [x] **The export is written on the first login after an upgrade**, not only on
      the next theme pick — `theme_apply_from_config()` re-saves a theme.state
      that is already there. A box that picked Win95 under an older synui would
      otherwise round its bar menus over a square desktop until someone happened
      to visit the theme manager. It does NOT create the file: no theme.state
      means a desktop on the FLAT chrome, which is what the bar assumes when the
      key is missing, and creating it would hand it precedence over
      `settings.state`'s opacity keys on a desktop that never asked.
- [x] **`tests/bar_radius.sh`** — headless nested synui + the real bar, three
      captures of the same open start menu: `corner_radius=0` square,
      `=14` rounded, `=14` with `square_chrome=on` square again. The 14 is written
      while everything is running, so it also proves the FileView watch. Asserted
      on rendered pixels: the diff between the first two is 300+ pixels in four
      14px corner clusters and nothing else, and the first and third are
      corner-identical.

### A windowed panel gets the wheel back  *(done)*

The control panel, the calculator and the task manager did not scroll AT ALL in
window mode. The wheel went straight past them to whatever client was under the
pointer, which usually scrolled instead — so the panel looked frozen and the
window behind it moved.

- [x] **The cause was one word of the modality filter.** `panel_mem_is_modal()`
      excludes the three panels that have a window mode from
      `panel_pointer_active()`, which is right — a windowed panel must not
      swallow clicks and the stray release for the whole desktop, and that is
      most of what "forces focus" felt like. But `panel_pointer_active()` is also
      what gates the WHEEL in `server_cursor_axis()`, so excluding them took the
      wheel away with everything else. The wheel is now offered to the panels
      when nothing modal is open, and forwarded to the client only if no panel
      takes it.
- [x] **The rule that keeps that honest lives in the panels**, because a modal
      panel deliberately answers the wheel from ANYWHERE on the desktop —
      `ctlpanel_scroll()`'s last `else` moves the focused column for exactly that
      case. A windowed panel doing the same would eat every client's scroll for
      as long as it sat open in a corner. So all three `_scroll()`s now open with
      the guard their own `_motion()` already had: windowed, and off the panel,
      takes nothing.
      That also fixes an ordering bug nobody had hit yet — with a windowed
      calculator open, `calc_scroll()` answered 1 from anywhere and it comes
      SIXTH in `SYN_PANEL_LIST`, so it would have swallowed a modal picker's
      wheel before the picker was ever asked.
- [x] **`tests/panel_wheel_test.c`** — modal takes the wheel from off the panel,
      windowed takes it only over the panel, both answer 0 when shut, and the
      box test is checked on the exclusive edge. It pins the panels' half only:
      `panel_pointer_active()`/`panel_pointer_scroll()` are static in `input.c`
      and need a seat, a cursor and a real axis event, so the half that offers
      the wheel is verified by reading. Nothing can synthesise a pointer into a
      headless synui — `panel_pointer_test.c` gives that argument at length.

### The bar's own shape, and the borders the radius did not reach  *(done)*

The corner radius reaching "the desktop's own furniture" above stopped one layer
short in two places, both found the same way — by turning the corners on and
looking.

- [x] **The two right-click menus kept a square outline.** `panel_chrome_sync()`
      rounds a panel's *background rect*, and a panel is two layers: that rect,
      and a cairo buffer of text and lines drawn over it. The desktop and dock
      menus are the only panels that stroke their own 1px frame into that
      buffer — they are the two `PANEL_BG` entries, with no accent rule of their
      own — so the background curved underneath and a 90° border stayed painted
      on top, at any radius. Both now pass `chrome_corner_radius()` into the same
      path. The row highlight had the same fault in its other form: a full-width
      fill on the first and last rows, 3px in from a side the curve has taken
      away, grows a square nib past the corner. The *content* is clipped to the
      rounded shape, which catches the separators too; the border is deliberately
      left outside that clip, because a stroke straddles its path and clipping it
      to the same path discards the outer half and thins the frame along the
      curve only.
- [x] **`cairo_rounded_rect()` is one path, in `src/cairo_shapes.c`.** It was a
      static in `dock.c`. Its own file rather than a corner of `render.c` so the
      arithmetic links without a compositor behind it — the clamp is the part
      worth pinning, and `tests/rounded_rect_test.c` rasterises and counts:
      corners cut by r²(4 - π), `r <= 0` **and NaN** pixel-identical to
      `cairo_rectangle()`, and a radius past half the shorter side clamped to the
      capsule rather than crossing the arc centres over into a bow-tie. NaN is
      the one that would not fail loudly — every comparison against it is false,
      so an `r < 0` guard passes it to `cairo_arc()`, which drops the path and
      draws nothing.
- [x] **`bar_shape = full-width | rounded-ends | floating-pill`.** The bar's
      share of the radius, as a Desktop row. `rounded-ends` curves the two
      corners facing the desktop and leaves the pair at the screen edge square —
      rounding those cuts two notches out of the corners of the *screen*, which
      is the same reason `panel_chrome_sync()` skips the overview's full-screen
      dim. `floating-pill` lifts the strip off that edge and in from both sides
      and closes it into a capsule: half the bar's height, because that is what
      makes the ends semicircular, and NOT the user's radius, which is about
      corners rather than about being round. The gap is a flat 6 for the same
      reason — tied to the radius, a desktop at 2 gets a pill that looks like a
      rendering fault and one at 48 gets a bar floating mid-screen.
- [x] **All of it gated on the corners being on**, which is the whole contract:
      at `corner_radius = 0`, and on every retro chrome, all three values draw
      the identical square strip. One rule rather than a shape row that has to
      know what LUNA and BEVEL are — `squareChrome` already folded that in for
      the popups. So Windows 95 squares the bar for free.
- [x] **The exclusive zone is `Theme.barSpan`, and that forced two files to
      block.** A pill reserves the gap it floats by or maximized windows slide
      under it. The zone is the one value that cannot arrive late — quickshell
      sends it once per surface, which is why `BarConfig` already read `bar.json`
      synchronously — and it now depends on the radius and the chrome. So
      `uifxFile` and `Theme`'s `chromeFile` are `blockLoading` too. Both had a
      sound comment saying they need not be, and both comments' *premise* was
      that nothing on screen at startup was drawn with those values. `bar_shape`
      ended that. Read async, a desktop with the corners off reserved the pill's
      taller zone for the whole session.
- [x] **The accent rule is inset to the curve**, and the first and last modules
      with it. The rule runs along the edge whose corners curve, so at full width
      its ends stick out past the background as two tabs — the same thing
      `render.c` hit on the compositor's panels and solved there with top-only
      radii. A capsule takes a full half-height out of each end, and the start
      button sat in it: on a shape with no clip, "clipped" means drawn *over* the
      corner, hanging off the pill.
- [x] **`tests/bar_shape.sh`** — four captures on one compositor and one bar.
      `rounded-ends` carves the bottom corners (58 and 60px) and moves the top
      row by nothing; `floating-pill` vacates all 1280 columns of the top row and
      the left edge; and the pill at radius 0 comes back pixel-identical to
      full-width on every probe, which is the gating contract. No probe knows what
      colour a bar is — each compares one capture to another at the same pixels —
      and none goes near the middle, where the clock ticks between captures.
- [x] **`bar_radius.sh` was passing by luck.** It locates the panel as the only
      saturated colour on screen, and two things on that rig are saturated and
      bigger: the bundled wallpaper (a bright emblem, so the "panel" came back as
      its bounding box and three corners reported a failure the bar never had)
      and `autostart`, which `config.c` defaults to `kitty`. The second was a
      RACE — the same code passed or failed on whether kitty won the four seconds
      before the first capture. Both rigs now write a synuirc, which resets the
      autostart list as a side effect of existing, and point it at a flat grey
      wallpaper: unsaturated for this test's locator, bright for the other's
      bar-or-desktop probes.

### The screensaver, and the keys its own footer promised  *(done)*

A screensaver (`saver.c`), a lock screen that follows the theme, and `Super`+`Z`
to configure both. The saver is a **fifth idle stage** in `power.c`, armed and
disarmed beside dim/blank/lock/suspend, and **off by default** — `saver_timeout`
0, which is also what a config predating the feature parses to, so no existing
install changed behaviour until it was asked to.

- [x] **Five modes** — `blank`, `clock`, `starfield`, `slideshow`, `matrix` —
      with the vocabulary in `saver_state.c` rather than in the panel, because
      the parser, the panel and `saver.state` all resolve names through it and a
      second copy of either table is a second thing to keep in step. Modes are
      saved as NAMES: the enum will grow, and a saved `3` must not quietly become
      a different mode after it does.
- [x] **The lock screen was ~20 hardcoded cairo literals**, all SYNAPSE cyan, so
      a theme switch reached every panel in the desktop *except the two screens
      people look at longest*. It runs off `panel_accent` and an ink ladder now,
      and the greeter inherits it for free — the greeter IS the lock. The ladder
      is picked from the **measured** luminance of the built background under the
      panel, not from `lock_bg_dim`: the first version estimated it and guessed
      low on the cream engravings in `data/wallpapers`.
- [x] **Input dismissal is swallowed.** `saver_ate_event()` runs in `input.c`
      BEFORE `notify_activity()` on both the key and the button path. Otherwise
      the keystroke that wakes the screen also types into whatever had focus,
      which is the classic screensaver bug.
- [x] **`screensaver.c` is not this.** It owns the `org.freedesktop.ScreenSaver`
      D-Bus name so Firefox and mpv can inhibit idle, it has existed for ages,
      and it draws nothing. The two meet only at `idle_inhibited()`. Reaching for
      it because of the name costs a cycle every time.

**The panel drew a footer listing five keys and answered none of them.**
`saver_key()` was there in `saver.c` — Up/Down, Left/Right, `p` preview, `s`
save, `Esc` close, all complete and all correct — and `input.c`'s key chain
never called it. The mouse worked, because `saver` HAD been added to
`SYN_PANEL_LIST`, the one roster the pointer walks. The keyboard path is a
*fifth* hand-kept walker of that same list, and the comment above
`SYN_PANEL_LIST` warns in as many words that hand-kept copies are how a panel
gets added to some of them and not the rest. This is that bug, with the panel
advertising the keys on screen the whole time.

- [x] **Wired, in the pointer roster's order** so the two lists read the same
      way down the file. With `saver` added, the only names in the key chain that
      are not in `SYN_PANEL_LIST` are `bt` and `welcome_menu`, both of which
      predate the contract and are documented exceptions.
- [x] **No build could have caught it.** An uncalled non-static function with a
      prototype and a definition is not a warning. Only pressing the keys sees
      it — so `tests/saver_keys.sh` presses them, over `virtual-keyboard-v1`
      into a headless instance, and judges by the two things a dead key path
      cannot fake: `p` makes `saver_show()` log, and Down-then-Right-then-`s`
      must write a `saver.state` with the **timeout** moved off 0 and the mode
      still `clock`. Those two halves are what separate navigation from
      adjustment — had `Down` been ignored, `Right` would have stepped the MODE
      instead, and the file says so.
- [x] **Every check was verified to fail without the fix**, and the `Esc` one
      separately against a deliberately-dead `Escape`, because it was the one
      that could pass on a stale log line. It counts occurrences rather than
      grepping for presence for exactly that reason.

### Three Macs — macOS 26, Aqua and Platinum  *(done)*

velle asked for three Mac themes from three screenshots, "one with rounded".
They are `macos26`, `aqua` and `platinum` in the Super+T picker, and they follow
the winxp/win95 precedent exactly: **a theme is a STYLE, not a palette**. Colour
alone could no more make a Mac than it could make a Luna — the give-away is that
the window controls are on the **left** and the caption is **centred**, and after
that it is per-era: Tahoe's radius, Aqua's pinstripes and glossy traffic lights,
Platinum's racing stripes and square close box.

- [x] **Three new `syn_chrome_t` styles** — `LIQUID`, `AQUA`, `PLATINUM` — each
      a caption painter in `deco.c` on the same cairo titlebar surface the retro
      ones use. Aqua and Platinum draw their own top corners and stripes;
      LIQUID draws none, because `chrome_corner_radius()` is non-zero for it and
      **scenefx already rounds that buffer's top corners** (`corner_radii_top`
      in anim.c). Rounding it a second time in cairo leaves a dark fringe.
- [x] **`chrome_square()` is now the single answer** to "does this desktop round
      its corners". Three things read it — the radius override, `theme.state`'s
      `square_chrome` export for the bar, and the GTK rule pushed at clients that
      decorate themselves — and they were three copies of `chrome != FLAT`. That
      spelling was correct for exactly as long as every non-FLAT style was square,
      which ended with macOS 26: it is not FLAT and it is the **roundest** thing
      that ships. One function, three callers, no drift.
- [x] **The radius is a FLOOR, not a value.** `CHROME_LIQUID_RADIUS_MIN` is 16
      and a larger `corner_radius` still wins. Tahoe's corner is the theme rather
      than a taste — 0px reads as a different OS — but the user's setting is only
      overridden in the direction the theme is arguing for. The uifx panel says
      so on the row rather than leaving it as a mystery.
- [x] **Button placement moved into `synui.h`** (`chrome_btn_x`,
      `chrome_btn_region`, `chrome_btn_at`) because **two** pieces of code have
      to agree about it: the painter in `deco.c` and the hit test 800 lines
      below it, which had its own copy of the arithmetic. With every style
      right-aligned the copies could not disagree; with the Mac styles they can,
      and the failure is a **click that closes a window somebody aimed to
      minimize** — silent, and invisible in a screenshot.
- [x] **`tests/chrome_layout_test.c`** pins that: the three layouts as absolute
      pixels, and the round trip at *every* x of a 600px bar — no slot may claim
      a pixel the hit test calls bare, and no named button may be one nothing
      drew. Plus the two degenerate directions of a too-narrow window (the
      right-aligned slots overlap and the LAST painted must win; the Mac slots
      fall off the right and close must survive at x 0).
- [x] **Platinum's widgets are drawn, not glyphed.** Mac OS 8 identified them by
      POSITION — an empty close box, a line for collapse, an inner square for
      zoom — so `draw_glyph` is suppressed and the boxes draw their own marks.
      Inactive windows keep flat, unbevelled boxes: authentic Platinum dropped
      them entirely, and an invisible-but-clickable widget is a trap.
- [x] **Traffic lights grey out when the window is not focused**, which is both
      the real behaviour and the honest one, and their × − + appear on hover
      only. `platinum` and `aqua` say the same thing with their stripes: the
      focused window is the striped one. That is how these desktops signalled
      focus before anything glowed.
- [x] **Provenance is in the preset comments.** Platinum's `#9B9CCE` desktop,
      its `#DEDEDE` face and the white/grey title stripes, and Aqua's `#356CBC`
      menu highlight and `#345CA5` desktop, were **sampled off velle's
      screenshots**; macOS 26's are Apple's published system colours. Anything
      else was tuned by eye and says so — these are not registry values the way
      Luna's are, and claiming otherwise would be the kind of comment that
      survives its own truth.
- [x] **All three are PALE**, which triples the population of the branch that
      has actually shipped bugs, so all three are in `panel_contrast_test.c`'s
      table. `macos26`'s `#F5F5F7` is the palest surface any preset ships; every
      status colour in render.c is corrected against it and clears 4.5:1.
- [x] **Verified by capture, and by an A/B.** The headless rig from the XP/95
      work (SYNUI_CONFIG + hermetic HOME, `WLR_BACKENDS=headless`, two foot
      clients, `synctl dispatch theme`, `grim`) shot all three plus the four
      themes that already existed — and `synapse`, `dark`, `winxp` and `win95`
      came back **pixel-identical to the same shot from HEAD's binary**. The
      button-layout refactor touches every style, so "the new ones look right"
      was never the whole question.

### The macOS 26 bar has no background  *(done)*

Reported against the three Mac themes: the macOS 26 bar is not transparent, and
Tahoe's is. Correct, and it was not close — the bar's alpha had never been the
theme's to decide. `synui-apply-theme` picked it from the SCHEME
(0.85 dark, 0.95 light) and macOS 26 is a light scheme, so Tahoe shipped with
window chrome from 2025 and a bar at **0.95, near-solid**.

The fix is one number. Everything below is the consequence of that number, and
the consequence is the whole change: **a clear bar is drawn on the wallpaper, and
the wallpaper is not something a theme can know.** Measured on velle's own
desktop before writing any of it — the top strip of the wallpaper in use reads
0.001 relative luminance, where the theme's `#1D1D1F` ink is **1.2:1** and its
`#0056D6` glyphs are **3.2:1**. Shipping the alpha alone would have deleted the
clock and called it Tahoe.

- [x] **`theme_bar_alpha()`** beside `chrome_square()` — negative for "no
      opinion", which is twelve of the thirteen presets. Not a field in the
      preset table: a zero-initialised float cannot tell "unset" from "clear",
      and clear is now a value a theme can mean. It rides to the helper as a
      15th positional argument with `-` for unset, the same out-of-band spelling
      `square_chrome`'s omission already uses.
- [x] **It reaches theme.json and deliberately NOT the waybar CSS.** Both bars
      read a palette from that helper; only the quickshell one can pick an ink
      that survives having no background. Handing waybar 0.00 would leave the
      Antiquity bar's near-black text on whatever the desktop happens to show.
- [x] **`backdrop.state`, written by wallpaper.c** — which ink survives the
      strip the bar covers, as `dark`, `light` or `none`. Measured off the
      **painted buffer**, not the source image: `fill` crops, `fit` letterboxes,
      `center` on a small image is mostly not the image, and `tile` repeats it.
      Sampling the buffer means the scaling question is answered by the code that
      answers it for the screen, and it runs exactly where a repaint does.
- [x] **Its own file, not theme.state.** That file holds facts that change
      exactly when the theme does; this one changes when the WALLPAPER does.
      Filing it under the theme means either rewriting theme.state from the
      picker or leaving the bar on a stale answer — and the second looks like it
      works.
- [x] **`none` is a real answer and the important one.** Between roughly 0.183
      and 0.230 relative luminance neither black nor white clears AA, and an
      evenly-lit photograph lands there. A clear bar cannot tint its way out — it
      has no background to tint — so the bar KEEPS its background. Two monitors
      that need different answers fold to `none` for the same reason: the bar's
      palette is a QML singleton, so picking a side means the other screen's
      clock is the one that vanishes.
- [x] **The strip's ink is now a SEPARATE set of names from the palette's.**
      `Theme.barFg`/`barGlyph`/`barAccent`/`barClock` and the washes, used by
      `modules/*` and `BarModule`'s pill. Not the existing names redirected: the
      right-click menu, the start menu, the mixer and the widgets all draw on
      `popupBg`, which is solid and pale on a light theme, and flipping their ink
      to white to suit the wallpaper behind the strip would black-on-black every
      surface this change never touches. When the strip is not clear each bar\*
      colour **is** its counterpart, which is what makes this inert for the
      twelve themes that do not ask.
- [x] **Monochrome, like Tahoe's.** Over an unknown backdrop the palette's blues
      and yellows are a contrast nothing can vouch for. The status colours keep
      their meaning (a red battery warning turned white is a warning nobody
      reads) and only swap which half of their pair suits the backdrop.
- [x] **The accent rule is gone on a clear bar.** A 2px line across the screen
      with nothing above it is not a bar, it is a line. One boolean binding —
      never a second ternary on that item, see pkgrel 295.
- [x] **`bar_edge` stopped being `CTL_APPLY_NONE`.** The bar still moves itself;
      what the compositor has to do now is repaint, because moving the bar moves
      which strip it is drawn on. Reading the top while sitting at the bottom is
      a wrong answer visible on one theme and in no screenshot.

Three tests, and the split between them is the point. `bar_ink_test.c` links
`contrast.c` alone and sweeps 1001 luminances asserting the property rather than
the boundary numbers: an ink is returned only where it clears target, `none`
only where neither does, and the band is **not empty** (47 of 1001) — if that
ever reads zero the "keep your background" path has become dead code while the
bar still has it. `apply_theme_bar_alpha.sh` pins the file contract including
that a malformed value is refused rather than pasted into JSON, where it takes
the bar's *whole* palette down rather than one colour. `bar_backdrop.sh` runs a
real headless synui and asserts the number handed to the arithmetic is the right
one — a pale band across one eighth of the wallpaper averages dark as a picture,
and both band cases would answer `light` if the whole wallpaper were being
measured, which is the obvious implementation.

Verified on the rig with velle's actual wallpaper: `bar_ink=light`.

### …and neither does anyone who asks for one  *(done)*

Two things, and the second is only worth having because of the first.

**`bar_ink=none` was answering for backdrops that were never unmeasurable.**
`wallpaper.c` measures the cairo surface it paints, so the two picker rows that
paint no picture — **None** (the solid `bg_rect`) and **Matrix** (its own GPU
buffer) — both reported -1, and the bar reads that as "no ink is safe here" and
puts its whole opaque background back. So the glass bar was glass over every
photograph and a solid strip over those two, flipping as the *wallpaper* changed
and the theme did not. Measured off the screenshots that reported it:
`rgb(234,234,237)` opaque under both, and tracking the wallpaper gradient pixel
for pixel under both image wallpapers.

- [x] **No image to sample is not nothing on screen.** The solid background is a
      colour synui itself chooses (`syn_bg_color`, shared out of `synui_main.c`),
      and it measures. What is left in -1 is what was always meant by it: an
      external client painting over us, which is wallpaper-engine and nothing
      else.
- [x] **The rain measures its own frame.** `matrix.c` reads the strip under the
      bar back off the buffer it just rendered, while the FBO is still bound,
      throttled to one read every two seconds and never while it is the
      screensaver. Measured, not declared dark: what the mean comes to depends on
      how much of the strip the glyphs cover, and a shader is the kind of file
      somebody tunes. It reads **0.0009** on the rig, an order of magnitude under
      the solid colour it seeds from.
- [x] **`glReadPixels`' y is the same GL window coordinate the vertex shader
      writes to**, so reading from 0 reads the rows a top bar covers whatever the
      buffer's memory layout turns out to be — and `bar_edge = bottom` reads the
      far end, exactly as in `wallpaper.c`.

**`bar_opacity = auto | 0.00-1.00`.** Which makes the clear bar something any
theme can have, instead of a property of one preset.

- [x] **Control panel ▸ Desktop ▸ Bar opacity**, beside Bar edge and Bar shape,
      in the same order the dock's three rows use: what kind of surface, then how
      much wallpaper it lets through, then its shape.
- [x] **It starts on an `auto` rung BELOW the range, not at a number.** The theme
      already has an opinion (`theme_bar_alpha()`), so a numeric default would
      silently overrule the one preset that has one. A new `.vauto` field on the
      row carries the label; the file gets the word `auto`, and the rung is
      `CTL_AUTO` (-1) for the same reason `CTL_VAL_TRI`'s "device default" is.
- [x] **0.00 is a real position and the point of the row** — no background at
      all, ink off the wallpaper. Which is exactly the path the fix above
      repaired, and is why these ship together: a row that produced an opaque bar
      on two of the picker's own wallpapers would have been a row that does not
      work.

`ctlpanel_table_test.c` gained the rung (it draws as words, Left off the bottom
lands on it rather than the minimum, and getting back there CLEARS the key rather
than storing "no opinion" as an opinion). `bar_opacity.sh` proves it arrives:
three captures over a magenta desktop, asserting 0.00 leaves **85%** of the strip
showing the wallpaper, 1.00 leaves none, and `auto` returns pixel-identical to
the capture taken before the key existed. `bar_backdrop.sh` covers both
wallpapers that paint no picture — and skips the rain rather than passing it
where the kanji atlas is not in the build's datadir, because the fallback there
answers `light` too and a check that passes either way is not a check.

### Super+Return opened two terminals  *(done)*

`spawn("syntty || kitty || foot || alacritty || xterm")`. The chain is there so a
box whose terminal package failed to install still opens *something* — but `||`
runs the next command when the previous one **exits non-zero**, and a terminal's
exit status is the exit status of the shell inside it. So the chain was asking
"did your shell session succeed?" and answering "no, here is another terminal".

- [x] **Closing the window did it every time.** syntty's teardown closes the pty
      master, which hangs the shell up, and `st_pty_reap` returns
      `128 + SIGHUP` = **129**. Measured on a headless rig, not inferred:
      `syntty -e false` → 1, `syntty -e true` → 0, `syntty -e sh -c 'kill -HUP
      $$'` → 129.
- [x] **So did `exit` after any command that failed**, which carries that
      command's status out of the shell with it.
- [x] **Why it took three years to show.** The head of the chain used to be
      kitty, which answers the compositor's close request by quitting cleanly
      with 0, and the name after it was usually not installed. Putting a terminal
      that reports the close *honestly* at the head turned a latent bug into one
      that fires on every close.
- [x] **`command -v` in a loop, then `exec`** — which is what
      `config/xdg-terminal-exec` has always done, and what its comment already
      claimed the keybind did. exec also means nothing can run after the terminal
      at all. Moved to `synui_terminal_cmd()` in synui.h beside the other policy
      inlines, so it is testable without linking input.c.
- [x] **xdg-terminal-exec's own chain was stale the other way** — no syntty in
      it. Only visible with a synuirc that has no `terminal =` line, where the
      keybind opened syntty and everything GLib launched opened kitty.

`tests/terminal_chain_test.c` runs the string the keybind spawns against stub
terminals on PATH that exit 129, and counts how many ran. Verified by restoring
the old chain: it reports `syntty` then `kitty`, which is the bug as reported.
It also holds the fallback down — with syntty off PATH the next installed name
runs, so the fix cannot be satisfied by a chain of one — and pins that an
explicit `terminal =` arrives unwrapped, since that setting can carry arguments
and a `command -v` loop would look for a program with a space in its name.

### One amount of glass, and the rows allowed to leave it  *(done)*

Three complaints with one shape: the Glass slider was not a master, the two
presets built on a clear bar had stopped drawing one, and the dock and the
widgets could not reach clear at all.

**The bar went solid on macOS 26 and Prism, and one television did it.**
`backdrop_export()` folds every monitor's ink with `syn_ink_combine`, which
vetoes on disagreement — right for *one* surface lying across two screens, and
wrong for the bar, which is a separate layer surface on each output over its own
strip of its own wallpaper.

- [x] **Measured on the three-monitor desk that reported it.** Two 1440p
      desktops read 0.67 under the strip and wanted dark ink; the television
      shows the same wallpaper letterboxed, so its top row of cells is the black
      band, and it wanted light. The fold said `none`, `clearBar` went false, and
      an opaque strip came back on **all three screens**.
- [x] **`bar_ink.<output>` / `bar_ink_best.<output>`** beside the folded pair,
      which stays for a bar older than this. `Theme.barPalette(screen)` resolves
      the ink, the washes and the background for one screen and hands back one
      object — a dozen `…On(name)` properties would be a dozen chances for the
      fg and the hover wash to disagree about whether the bar is clear.
- [x] **Fifteen call sites take it from the window they are in**, through
      `barPaletteOf(QsWindow.window)`: `QsWindow.window` is typed as a bare
      QObject, so dotting `.screen` off it at each site is fifteen qmllint
      warnings for a fact qmllint cannot know.

**`glass_sync`, and pins.** The Glass row already overwrote `active_opacity`,
`inactive_opacity` and `bar_opacity` — unconditionally, with no way to keep a
value set by hand — and never touched `dock_opacity` or `foot_alpha` at all. So
the one control for "how much glass does this desktop have" moved three of five
surfaces and could be overruled on none of them.

- [x] **All five follow it**, and the bar and the dock land on the *same* number:
      they are the same kind of surface, and a desktop whose top strip and bottom
      strip are see-through by different amounts is what one slider is for.
- [x] **Dragging a driven row pins it** — there is no separate pin control,
      because taking hold of a row *is* claiming it. `Sync all glass` back on
      releases every pin at once.
- [x] **A row is pinned exactly when settings.state records it.** Pinning on
      every move and never releasing leaves two states the panel cannot draw
      honestly: a value dragged back to its own default (key dropped, dot gone,
      pin surviving with nothing recording the number it pins), and a row stepped
      back onto its `auto` rung — the row saying it has *no* opinion, pinned
      there, blocking the slider.
- [x] **Auto is no longer a one-way door.** It used to stop writing the five
      alphas and leave the last values it wrote in the config with nothing
      recording them: the screen kept them for the session and the next login,
      rebuilt from settings.state, did not have them. `synui_config_glass_release()`
      hands every unpinned row back to its compiled default.
- [x] **The slider reaches the bar and the widgets**, which it never did: those
      are quickshell's and read `bar_opacity`/`dock_opacity` out of settings.state,
      a file the slider does not write, because a synced value is not a value
      anybody chose. The resolved numbers go to `theme.state` — written **only**
      for rows the sync still owns, so a pinned row is simply absent and
      settings.state shows through, and the shell needs no notion of pinning.
- [x] **…and theme.state stays quiet about a pinned row.** It is read *after*
      settings.state, so `active_opacity` and `foot_alpha` have always beaten the
      control panel's own file — harmless until the sync started resolving them.
      Pin Terminal glass, log out, and the synced number would have overwritten it.

**`glass_legibility`, and the end of the arbitrary floors.** Five separate
numbers guarded `dock_opacity`, none of them agreeing on what they guarded
against: config.c's 0.20, the row's `vmin` 0.20, `dock_paint_body`'s 0.05,
BarConfig's re-clamp to 0.20, and the widgets' `+0.16`. All gone.

- [x] **0.00 is a row of icons on the wallpaper.** The icons are painted over the
      body at full opacity, so the bottom of the range was never "a dock nobody
      can find" — it was the one thing the floors made unreachable.
- [x] **The chrome of a surface cannot be more present than the surface.** The
      dock's outline, rim and specular and the widgets' rim, specular and shadow
      rings carry their own literal alphas, so a body at 0.00 left the shape drawn
      in full — a rectangle of nothing with a bright edge round it. A ramp that is
      1.0 above 0.35, so nothing anyone has configured moves.
- [x] **The widgets are the dock's opacity, verbatim.** The `+0.16` lift was
      written when quickshell's layer surfaces got no backdrop blur; since -379
      the card claims the `synui-glass` namespace and layer.c puts the dock's own
      blur node behind it. The lift outlived its reason and became a constant that
      stopped the widgets ever matching the thing they are defined as matching.
- [x] **The measured floor is a switch, not a law.** `panel_alpha_floor()` and
      `popupAlphaOn()` raise a surface's alpha until its text clears AA against
      the wallpaper under it — measured rather than guessed, and still the
      default. Off draws exactly what was asked, including nothing, and takes the
      window and terminal curves to 0 and the clear bar's "no legible ink, keep
      your background" with it. A guard that is invisible when it fires is one
      that has to be turnable off.

`glass_sync_test.c` drives the resolution against the pin set — all five move,
each curve is monotonic across 0..100, a pin holds through any travel, release
restores only unpinned rows, and every pin round trips through its name (they
*are* the synuirc keys, which is what lets a pin be looked up off the ctl_item
table). `ctlpanel_table_test` drives the real key handler for the pin invariant
including both release paths and a reload from disk. `bar_ink_test.c` gained the
letterboxed-television case as arithmetic; `bar_backdrop.sh` asserts the
per-output keys are in the file and that one screen folds to its own answer.

---

## The dock grows up: size, swell, a way to all your apps, and a clock you can put where you like

Four asks in one pass, and three of them turned out to be the same bug wearing
different clothes: a number in the config that the dock only half obeyed.

**`dock_height` resizes the DOCK, not the slab it is drawn on.** The Dock size
row has always been there and has always moved one rectangle: the icons stayed
48px whatever it said. Past about 80px the row therefore read as broken — a
growing wall of glass with the same small pictures adrift in the middle of it,
and at the 200px top of the range a bar with more empty space than dock.

- [x] **The icon is `dock_height − 16`**, which is exactly what 48-in-64 was, and
      the padding a sixth of the icon, which is exactly what 8-at-48 was. A
      desktop that never touches the row is pixel-identical to the one before.
- [x] **Everything that was a literal is a fraction now** — the running dot's
      offset and radius, the cross-axis nudge, the drag's grab offset and its
      clamp to the body. A stray 48 left anywhere shows up as a hit box that
      misses or a dot through the middle of a picture, and nothing warns.
- [x] **The icon cache is keyed on the size**, not only on `icon_generation()`.
      It holds each picture rasterized at the cell size; keeping the 48px surface
      after the dock grew to 184 leaves every icon on the bar upscaled and soft,
      with a layout that is entirely correct.

**`dock_magnify_scale`** — the swell was a `#define` at 1.60 with a `#define` at
32 beside it for the room to do it in, and the second is *derived* from the
first now. It has to be: the body is welded to the screen edge, so a bigger
scale against a fixed headroom is not a smaller effect, it is an icon clipped
off at the far side of the canvas — silently, because nothing in the scene graph
objects to a buffer too small for what was drawn into it. Rounded up to a
multiple of 8 the way the old constant was, so stock still gets exactly 32.

**A show-all-apps button.** A dock of pinned icons has no route to an app that
is not on it; the bar's start menu does, and nothing on the dock pointed at it.

- [x] **A 3×3 grid of dots** in a cell at the end of the run, drawn in
      `panel_ink` rather than pulled from the icon theme — there is no `.desktop`
      behind this button, and the ink is by definition the colour that reads on
      this bar (the same rule the running dot follows, and for the same reason: a
      white glyph vanishes on XP's beige).
- [x] **`dock_apps_at()` is asked BEFORE `dock_bar_at()`**, and that ordering is
      the whole of the wiring. The button is drawn *on* the body, so every press
      that lands on it also lands on the bar; asked the other way round it would
      start dragging the dock to another screen edge. Asserted in the test,
      including the fact that the bar answers true for the same point.
- [x] **A switch like the others** — the right-click menu and Control panel ▸
      Desktop, persisted to `dock.state` beside the edge and the pins.

**The clock can be dragged anywhere in the row.** It sat past the last icon
because that is where it was appended, which is a fact about the layout loop
rather than a decision about the clock.

- [x] **`dock_clock_slot` counts icons to its left**, and the layout walks slots
      rather than appending. `-1` means "past the last one" and is a *position*,
      not a fallback: a clock pinned to slot 5 walks back up the row every time
      an app quits, so dropping it at the end stores -1 rather than the `n` that
      looks identical today.
- [x] **The gesture is the third one `dock_drag` carries** (`DOCK_DRAG_CLOCK`),
      beside the bar reposition and the icon rearrange. The cell is not lifted
      under the cursor the way an icon is — an icon needs a picture because the
      gap it came from looks like the gap it is going to, and a second copy of
      the time floating over the bar would be two clocks disagreeing about where
      the clock is.
- [x] **The target slot is counted off the cells as DRAWN**, which is what keeps
      the gesture from oscillating: inserting the clock pushes the icon that just
      decided the answer *away* from the cursor, so the hysteresis is free and in
      the right direction.
- [x] **`dock_slot_at()` subtracts the clock's cell.** Magnification is suppressed
      during an icon drag; the clock is not, so everything past its slot sits one
      cell of a different width further along than icon arithmetic puts it.
      Without this, dragging an icon across a clock parked mid-row drops it a slot
      early — only on the desktops that have moved their clock.
- [x] **A hairline on each side that has a neighbour.** One rule on the left was
      right for the only position the clock used to have.

**…and the rule was too close to the first digit**, which is what started this
half. The cell was a 92px constant and "3:03:11 AM" at 17px is 92px wide, so
seconds plus an am/pm put the separator hard against the time. The cell is
measured from the strings it will actually draw now — 92 is a floor, so a short
"3:03" keeps the roomy cell it always had — off a scratch cairo context, because
the cell's length is geometry and the hit tests need it as much as the renderer
does. The clock's font sizes scale with the slab for the same reason the icons
do.

`dock_options_test` grew to 43 cases: the icons resize with the slab and a click
lands on the new size, the headroom follows the swell, the apps button takes a
cell and is found by the hit test the compositor actually calls, and the clock is
dragged front and back through `dock_clock_drag_begin` → `dock_drag_motion` →
`dock_drag_end` rather than by writing the slot.

---

## The show-all-apps button opens an actual application page

The first version of that dock button asked the bar to toggle its start menu.
That is not what "GNOME-style show all apps" means, and it was wrong in three
separate ways at once — the menu is a categorised LIST rather than a page, it
belongs to one output's popup layer, and on a desktop with `bar_enabled = off`
the button silently did nothing at all. So the page is the compositor's now:
`src/appgrid.c`, full-screen, keyboard-first, the native idiom mission control
already uses.

- [x] **A page, not a menu.** The screen dims behind a scene rect, six columns
      by four rows of large icons with their names under them, page dots at the
      bottom, and a search pill at the top. Type to search, arrows and Page
      Up/Down to move, Enter to launch, Esc to leave — Esc clears a search
      before it closes the page, the rule the emoji picker set.
- [x] **The geometry is derived from the output**, and the cell is CAPPED as
      well as floored: six columns of a 3840px screen give 600px tiles with a
      96px icon marooned in the middle of each, which is the shape "make it
      fill the screen" produces if you only ever test it on one screen.
- [x] **It launches through the desktop's own terminal** for a `Terminal=true`
      entry — synuirc's `terminal`, the same value the file manager and the
      desktop menu use — and it CLOSES before it spawns, because a window that
      maps while a modal panel is up arrives behind it and without focus.

**The list is not the .desktop files**, and getting that wrong is the whole
difference between a launcher and a directory listing.

- [x] **The same `menu-hidden.conf` the start menu reads**, both files, in the
      same order, with the same `!id` un-hide. Shared *data* is the only sharing
      available across a process boundary, so a line added to hide something
      hides it in both launchers.
- [x] **The full freedesktop rules**: `NoDisplay`, `Hidden`, `Type`, `TryExec`,
      and `OnlyShowIn`/`NotShowIn` tested against **both** of this desktop's
      names — `synui` AND `SynapseOS`. Testing one spelling is how an
      application goes missing on a desktop that looks correct everywhere else,
      and the list match is whole-token so `synuix` is not `synui`.
- [x] **The id is the path under `applications/` with `/` folded to `-`**, which
      is what makes a Wine shortcut three directories down come out as
      `wine-Programs-Foo` — the string `menu-hidden.conf` lists and the string
      the Wine rules test. Built any other way, both filters silently stop
      working. The scan is recursive for the same reason: a flat readdir finds
      none of Wine's shortcuts, which on a box with games installed is most of
      what anybody launches.
- [x] **⚠ The Wine noise rules are a SECOND COPY** of `isNoise()` in
      `quickshell/StartMenu.qml`, and there is no way to have one: that is QML
      in another process. They are kept literal and in the same order so a diff
      reads straight across, and both files name the other. The scoping is the
      part that matters — "Help Viewer" outside a prefix is a real application,
      and `.nfo` must not match `nfoview`.

**…and the icon search had to grow up with it.** `find_and_decode_icon()` knew
`hicolor/<size>/apps` and `/usr/share/pixmaps`, which was survivable while the
dock was the only caller — a pinned application almost always ships its own
hicolor icon. A page of ninety entries broke it immediately: a third drew a
letter monogram, because they name a *theme* icon (`accessories-calculator`,
`printer`) that lives in Adwaita under `legacy/` or `devices/`.

- [x] **Every theme, root, size and category subdirectory**, ordered so the first
      hit is the best one — scalable before raster, large before small, the
      configured theme before the fallbacks, `hicolor` last because it is the
      per-application drop rather than a designed set. Only paid on a MISS, only
      once per icon per session.
- [x] **`bar_icon_theme` is pushed into icons.c** (`icon_set_theme`) with the
      accent, so the compositor's pictures and the bar's come out of one theme
      rather than two. Changing it drops the whole decode cache and bumps
      `icon_generation()`, which is what makes the dock throw away its own
      pre-scaled copies.

`appgrid_test` drives the real scan against a mkdtemp sandbox with
`XDG_DATA_HOME` *and* `XDG_DATA_DIRS` pointed at it — unset, the scan falls back
to the tester's own `~/.local/share/applications` and the assertions become
about their desktop. One `.desktop` file per rule, named for the rule; the
two-desktop-names case was confirmed discriminating by removing the `SynapseOS`
half and watching it fail.

---

## The clock that could not be moved, and the button that changed its mind

Two dock regressions from the pass above, and they are opposite failures: one
gesture that was wired correctly and could not run, one button that ran
correctly and should not have been wired at all.

**A clock drag was taking the BAR drag's branch.** `dock_apply_position()` has a
"dragging the bar: float freely under the cursor" case, and it was gated on
`s->dock_drag.icon < 0`. That read as "not an icon" for exactly as long as there
were two gestures. `DOCK_DRAG_CLOCK` is **-2**.

- [x] **The guard names `DOCK_DRAG_BAR`.** The moment a clock drag crossed its
      6px threshold, the whole dock was thrown to `dock_drag.float_x/float_y` —
      coordinates a clock drag never writes, so 0,0 on a fresh session or
      wherever the last edge-drag happened to leave them. Every motion after
      that fed `dock_clock_drag_motion()` a cursor measured against a node
      position with no relation to the bar on screen, so the cell could not be
      aimed anywhere and the release committed nothing.
- [x] **⚠ The model test could not have caught it, and passed the whole time.**
      `tests/dock_clock_drag_test.c` stubbed `create_cairo_buf()` to NULL, and
      `if (!buf) return;` is the third statement of `dock_render_output()` — so
      the test skipped the renderer *including the `dock_apply_position()` call
      at the end of it*, which is the only place the bug lives. Being told "the
      model is sound, so the fault is above dock.c" was the model test reporting
      on the half of the file it ran.
- [x] **The rig renders now.** A real cairo context on a scratch image surface
      (every text, icon and rounded-rect call is still a stub, but the CONTROL
      FLOW is the real one), and the bar's position is read from the code under
      test instead of being assigned by the test — the old `place_tree()` wrote
      `fake_tree.node` by hand, which was self-consistent with every hit test in
      the file and therefore blind to a `dock_apply_position()` that put the bar
      somewhere else entirely. The rig is velle's live dock: 56px, magnify on at
      1.50, sixteen pins, apps button on.
- [x] **Both directions asserted.** The bar does not move for the length of a
      clock drag, and it still *does* float under the cursor for a bar drag —
      excluding the clock by excluding everything would pass every other
      assertion in the file and leave the dock unable to be moved between edges.

**The dock's apps button opens the application overlay. Always.** 442 routed it
through `synui_start_menu_open()` on the reasoning that it and the Super tap are
the same request. They are not, and nobody asked for that: velle's
`start_menu_style` is `rofi`, so pressing a button that draws a 3×3 grid of dots
opened Rofi.

- [x] **`appgrid_toggle()` directly**, as 441 had it. A button is its own label:
      the grid-of-dots means the overlay it is a picture of, the same way the
      bar's start button means the bar's menu and does not change either.
- [x] **Desktop ▸ Start menu governs the START KEY** — the Super tap,
      Super+Escape and the `start_menu` action, which have no picture on them and
      are therefore free to be chosen. Its help line and the apps button's say so
      now; both used to claim the button followed the row.

## The welcome screen is a guide (0.1.0-497)

**Not a great first impression.** The welcome screen was the first thing a new
SynapseOS desktop showed and it was a *menu*: one 513px column, nineteen labels
and their chords, drawn in cairo by the compositor. Nothing on it said what any
row was for, so "Neural Overlay" and "Cat Mode" arrived side by side and the only
way to find out what either did was to press it. Its key column was a hardcoded
`rgba(0.45, 0.45, 0.55)` — a fixed blue-grey no theme could move, under 3:1 on
the panel this desktop actually draws. Present, and unreadable.

- [x] **Six pages, with room to say what things are.** `quickshell/welcome.qml`
      and `quickshell/welcome/`: Welcome, The keys, Make it yours, The AI,
      Everything else, You're set. A rail down the left that doubles as the
      contents page, a description under every row, and prose rows (`kind:
      "note"`) for the things that are facts rather than doors — the workspace
      keys, where the documentation lives.
- [x] **The key chips are `Theme.fgDim`**, the ink 65% of the way from the
      surface to the foreground, recomputed per theme. That is the secondary
      colour the old menu could not move.
- [x] **⚠ IT IS ITS OWN QUICKSHELL, NOT A WINDOW IN THE BAR.** A window in the
      bar would have been less code and it would have been wrong: two bars ship
      (`bar_shell = synapse|antiquity`) and a guide inside the SYNAPSE one simply
      would not exist for anyone running the other — while the panel it replaces
      was drawn by the compositor and every configuration had it. As a second
      ENTRY POINT into the same QML tree it still gets `Theme.qml`, the picked UI
      font and the glass namespace from `import ".."` and copies none of them.
      It also costs nothing when closed (dismissing it quits) and does not
      reappear every time game mode restarts the bar.
- [x] **`synui-welcome(1)` is the launcher and the CLI** — `toggle`, `show`,
      `hide`, `page N`. Toggling across a process boundary works because closing
      QUITS: "closed" and "not running" are the same state, so the script asks a
      running instance first and starts one only when nothing answers. There is
      no pidfile and no third state.
- [x] **`synctl binds` is new**, and it is what stops the guide inheriting the
      old menu's worst habit. That menu carried a hand-typed chord per row and
      said so at length — the command bar has been on Super+Space, on Super+=,
      and back, and each move left the column naming the old one. The chords come
      out of the live bind table now, rendered compositor-side by
      `ctlpanel_combo_str()`, so a rebound key needs no edit anywhere.
- [x] **It closes when a window opens, and NOT BEFORE.** `synui_main.c` used to
      hide the panel on the first map; the guide watches `ToplevelManager`, with
      a 400ms arm in front of it. The arm is not optional and not a style
      choice: `ToplevelManager.toplevels` is EMPTY at `Component.onCompleted`
      and the windows that were already open are inserted one event-loop turn
      later, so an unguarded watch closes the guide instantly on any desktop
      that is not empty. Probed on a headless rig — `completed t=0 count=0`,
      `insert t=1 appId=syntty` — after removing it on a wrong reading and
      watching exactly that happen.
      ⚠ **It is not about autostart.** 497 held it for 1.5s "so the login burst
      passes", on the belief that `autostart` defaults to a terminal. It does
      not: `config.c`'s compiled-in `syntty` is the fallback for finding NO
      config file, and opening any synuirc zeroes the list before parsing. Every
      install ships `/etc/synui/synuirc`, so nothing autostarts unless somebody
      asked for it — the fallback bites only in a hermetic test rig.
- [x] **`welcome.state` did not move.** synui still owns the setting
      (`welcome_at_startup` is a synuirc key with a control-panel row); the guide
      only READS the file and asks for a change with `synctl dispatch
      welcome_startup`. One writer, one spelling of the default.
- [x] **`data/synui-welcome.desktop`** — "Welcome Guide" in the applications
      menu. A panel reachable only by a chord is a panel nobody finds, and
      Super+Escape is not a discoverable key.
- [x] **`tests/welcome_guide.sh`** asks the PIXELS, because every way this can
      fail is silent: a missing `qmldir` line is "Guide is not a type", an
      uninstalled `pages.js` is every page `undefined`, and both come from a
      package that built cleanly. Loads it on a headless synui and asserts a card
      drew, two pages differ, the IPC answers, and a mapped toplevel closes it.
- [x] **~480 lines of C deleted** — `synui_render_welcome()` and its table from
      `render.c`, the arrow/click/wheel handling from `input.c`, `welcome_ui`
      from `syn_server_t`, and the hide-on-map hooks in `synui_main.c`,
      `xwayland.c` and `output_mgmt.c`.
