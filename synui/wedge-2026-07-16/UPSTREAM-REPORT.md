# xwayland: a surface that re-associates while still buffered never maps again

**Where to file:** https://gitlab.freedesktop.org/wlroots/wlroots/-/issues
**Verified against:** master `d64acff` (also present in 0.19.3)

## Summary

When an X11 client unmaps and re-maps a window, Xwayland may reuse the same
`wl_surface`. The re-association then never produces a `map`, because the only
code that maps an Xwayland surface is the commit handler — and no commit is
coming, since nothing about the surface's content changed.

The window is left `IsViewable` in X, associated, holding a buffer, and
permanently invisible. `_NET_CLIENT_LIST` stays empty for it. Nothing the
compositor does recovers it; only destroying and rebuilding the surface does.

## Mechanism

Three facts in current master combine:

1. **The commit handler is the only thing that ever maps an Xwayland surface**
   (`xwayland/xwm.c:1128`):

   ```c
   static void xwayland_surface_handle_commit(struct wl_listener *listener, void *data) {
       struct wlr_xwayland_surface *xsurface = wl_container_of(listener, xsurface, surface_commit);
       if (wlr_surface_has_buffer(xsurface->surface)) {
           wlr_surface_map(xsurface->surface);
       }
   }
   ```

   `wlr_surface_map` is called from exactly two places in `xwm.c` — this one, and
   nowhere else on the association path.

2. **`xwayland_surface_associate` (`xwayland/xwm.c:1186`) maps nothing.** It
   clears `surface_id`, installs the commit/map/unmap listeners, and reads
   properties. It never inspects `wlr_surface_has_buffer`, however buffered the
   surface it was just handed already is.

3. **`xwayland_surface_dissociate` (`xwayland/xwm.c:551`) unmaps but leaves the
   surface "buffered".** It calls `wlr_surface_unmap()`, and
   `wlr_surface_unmap` (`types/wlr_compositor.c:854`) sets `mapped = false` and
   emits `events.unmap` — but never clears `current.buffer_width/height`. Since

   ```c
   bool wlr_surface_state_has_buffer(const struct wlr_surface_state *state) {
       return state->buffer_width > 0 && state->buffer_height > 0;
   }
   ```

   (`types/wlr_compositor.c:827`) is *stale dimensions* rather than a live buffer
   pointer, `wlr_surface_has_buffer()` keeps returning true on a dissociated
   surface indefinitely.

So the sequence is:

```
X unmap  → dissociate → wlr_surface_unmap()  (mapped = false, has_buffer STILL true)
                        Xwayland keeps the wl_surface alive
X map    → new WL_SURFACE_SERIAL for that SAME surface → associate
         → no content changed ⇒ no commit ⇒ nothing calls wlr_surface_map()
         ⇒ wedged: IsViewable in X, associated, buffered, mapped == 0
```

## Evidence

Observed with Steam (Chromium/CEF) on a wlroots-based compositor. Captured at the
instant of the wedge:

```
WEDGE-STATE Steam (0x1e00046): ASSOCIATED
            (surface 0x55dcf25c9d80, mapped 0, has_buffer 1, serial 5)
```

Both halves of the usual assumption are false: the surface is **not** unassociated,
and a buffer **is** present. It simply never maps.

Corroborating observations:

- `xwininfo` reports the window `IsViewable`, `WM_STATE` Normal, full size.
- `xprop -root _NET_CLIENT_LIST` is empty — wlroots' own bookkeeping agrees no
  xsurface ever mapped (`xwayland_surface_handle_map` is what sets that list).
- Forcing a `ConfigureNotify` (resize ±1) does **not** recover it.
- Forcing a full `Expose` (`XClearArea exposures=True`) does **not** recover it.
- `XUnmapWindow` + `XMapWindow` **does** recover it, in ~1s — because that makes
  Xwayland build a *fresh* surface, which must commit before it can be shown.

That A (re-layout) and B (repaint) both fail while C (teardown) succeeds is the
tell: the client's renderer is fine, and the stale surface simply cannot be
mapped by any amount of poking.

The trigger here is Steam's ordinary startup handoff (login window → main
window), not any unusual close/restore cycling: one wedge formed ~12s into a
fresh session with nothing cycled.

**Ruled out:** this is *not* the `serial != 0` guard in
`xwm_handle_surface_serial_message` dropping a message. I patched that guard to
log at `WLR_ERROR` and caught a natural wedge with **zero** hits across formation,
the wedged period, and recovery. The pairing is not lost there.

**Not reproducible headlessly.** A nested-compositor rig doing tight X unmap/map
with no round trip between the requests produced 0 wedges in 150 cycles. Real GPU
session + Steam's ARGB Chromium window is what shows it; a non-reproducing rig
proves nothing either way.

## Suggested fix

Have `associate` do what the commit that never arrives would have done — map a
surface that is already buffered at association time:

```c
 static void xwayland_surface_associate(struct wlr_xwm *xwm,
 		struct wlr_xwayland_surface *xsurface, struct wlr_surface *surface) {
 	...
 	xsurface->surface_unmap.notify = xwayland_surface_handle_unmap;
 	wl_signal_add(&surface->events.unmap, &xsurface->surface_unmap);
+
+	/* The surface may already carry a buffer — dissociate unmaps but does not
+	 * clear buffer_width/height, so a reused surface associates already
+	 * "buffered". No commit will follow, so map it here or it never maps. */
+	if (!surface->mapped && wlr_surface_has_buffer(surface)) {
+		wlr_surface_map(surface);
+	}
```

An alternative would be for `wlr_surface_unmap` (or `dissociate`) to clear the
cached buffer dimensions, so `has_buffer` reflects reality and the next real
commit maps normally. That is the more principled fix but a wider behavioural
change, since `wlr_surface_has_buffer` is public API and other callers may lean
on the current semantics.

We ship the `associate` variant downstream (synui, SynapseOS). It resolves the
wedge completely: across a session it intercepted the condition 5 times, mapping
in the same millisecond each time, with no wedge ever forming.

## Downstream reference

- Fix: https://github.com/velle999/SYNAPSE — `synui/src/xwayland.c`, commit `3aba848`
- Diagnostic patch used to rule out the serial-race theory:
  `synui/wedge-2026-07-16/wlroots-wedge-diag.patch`
