# synui Roadmap — toward a full AI-aware Wayland compositor

`synui` is SynapseOS's wlroots-based Wayland compositor. This document tracks
the gap between its current state and the roadmap goal, *"full Wayland
compositor with AI-aware window management."*

## Current state (real `src/`, ~3,300 LOC)

Working:
- wlroots backend/scene init, VM detection → pixman fallback
- xdg-shell toplevels (map/unmap/destroy/commit), keyboard focus
- Tiling (master-stack) and monocle layouts; 9 workspaces
- Keyboard bindings; basic pointer (focus / click / axis); clipboard selection
- Cairo-rendered UI: welcome screen, AI command bar, neural overlay
- AI thread IPC to synapd (hardened: framed `write_all` + reassembling poll)
- Security-border colour states (rendering only)

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

Missing: layer-shell (panels/bars/wallpaper/lock), XWayland, xdg-decoration,
output-management, fractional-scale, session-lock, idle, foreign-toplevel,
screencopy, pointer-constraints, touch/tablet, libinput config.

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

### Phase D — Desktop shell surfaces: layer-shell
Create `wlr_layer_shell_v1`; handle the four layers + exclusive zones. Unlocks
waybar / swaybg / mako / wofi.

### Phase E — App compatibility: XWayland + decorations
X11 app support (separate surface type, override-redirect, focus) and
`xdg-decoration` negotiation. The largest single chunk.

### Phase F — Output & session management
`wlr-output-management` (scale/mode/rotation), fractional-scale, DPMS,
`ext-session-lock`, idle-notify / idle-inhibit.

### Phase G — Input completeness
Pointer constraints + relative pointer (games), touch/tablet/gestures,
libinput config, configurable keymap and keybindings.

### Phase H — Ecosystem protocols
foreign-toplevel (taskbars), screencopy (screenshots), data-control / primary
selection, gamma-control (night light), drag-and-drop.

### Phase I — Robustness & CI
Listener-leak / memory audit, headless smoke test in CI, a small test harness.
