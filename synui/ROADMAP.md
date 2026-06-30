# synui Roadmap — toward a full AI-aware Wayland compositor

`synui` is SynapseOS's wlroots-based Wayland compositor. This document tracks
the gap between its current state and the roadmap goal, *"full Wayland
compositor with AI-aware window management."*

## Current state (real `src/`, ~2,400 LOC)

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
- **Security borders**: `view_set_security()` exists but no synguard verdict
  feed drives it.
- **Multi-output**: layout/UI always use `output[0]`; AI prompt hardcodes
  `1920x1080`.
- Interactive move/resize: `SYNUI_CURSOR_MOVE/RESIZE` enum unused.
- Documented binds Super+H/L (master factor) and Super+Shift+J/K (move in
  stack) are not implemented.

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
- [ ] **A2 (next):** feed synguard verdicts into `view_set_security()` —
      requires a synguard alert consumer + matching surface client pid
      (`wl_client_get_credentials`) to synguard events.

### Phase B — Interactive & complete window management
Cursor move/resize for floating windows (the unused enum); finish the
documented binds (master factor, move-in-stack); sane floating placement and
min/max-size handling.

### Phase C — Multi-output correctness
Per-output geometry (stop hardcoding `output[0]`), output-aware workspaces/UI,
real output size in the AI prompt, hotplug re-layout. Verifiable headless with
`WLR_BACKENDS=headless` and two outputs.

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
