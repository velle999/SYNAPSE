#!/bin/sh
# smoke.sh — headless smoke test for synui
#
# Boots the compositor on the wlroots headless backend with the pixman
# renderer (no GPU, no seat, no privileges needed), then asserts:
#
#   1. it comes up and creates a Wayland socket;
#   2. every protocol global synui is supposed to provide is advertised
#      (wayland-info);
#   3. a real client can map and unmap a toplevel (foot, if installed);
#   4. screencopy produces a real image (grim, if installed);
#   5. SIGHUP reloads the config live (new gap/bind applied, still serving);
#   6. SIGTERM tears it down cleanly — exit 0, within a deadline (a hang
#      here is exactly the listener/thread-teardown class of bug);
#   7. an ASan build reports no errors or leaks (when built with
#      -Db_sanitize=address; harmless otherwise);
#   8. a second instance with WLR_HEADLESS_OUTPUTS=2 gives each output its
#      shows the *same* workspace (virtual desktops span every monitor) and
#      also shuts down cleanly;
#   9. spawned children inherit neither synui's blocked signal mask nor its
#      SIG_IGN dispositions — both survive exec, and leaking them makes every
#      app synui launches immune to SIGTERM;
#  10. the control panel opens over virtual-keyboard-v1 (wtype), which is the
#      path waybar's start menu uses to reach the compositor.
#
# Usage: smoke.sh /path/to/synui
# Run by `meson test -C <builddir>`; needs wayland-info (wayland-utils).
# foot and grim tests are skipped when the tools aren't installed.

set -u

SYNUI=${1:?usage: smoke.sh /path/to/synui [/path/to/x11_remap_test]}
X11_REMAP=${2:-}
TESTDIR=$(dirname "$0")

fail() { echo "FAIL: $*" >&2; [ -n "${LOG:-}" ] && tail -40 "$LOG" >&2; cleanup; exit 1; }

cleanup() {
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    [ -n "${FOOT_PID:-}" ] && kill -9 "$FOOT_PID" 2>/dev/null
    [ -n "${TMP:-}" ] && rm -rf "$TMP"
}
trap cleanup INT TERM

command -v wayland-info >/dev/null || fail "wayland-info not installed (wayland-utils)"

# Private, *short* runtime dir: unix socket paths are capped at 108 bytes,
# so mktemp under /tmp rather than any nested build path. An empty
# SYNUI_CONFIG makes the run hermetic — without it a developer's synuirc or
# the machine's /etc/synui/synuirc leaks in (autostart!). HOME and
# XDG_CONFIG_HOME are redirected too, for anything else that reads them.
TMP=$(mktemp -d /tmp/synui-smoke.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
: > "$TMP/synuirc"

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1

#
# This suite needs a real DRM render node, and SKIPS without one.
#
# Since the scenefx migration synui builds its renderer with
# fx_renderer_create(): GLES2-only, ignores WLR_RENDERER (so the old
# WLR_RENDERER=pixman here had silently stopped meaning anything), and it
# accepts only DMA-BUF buffers. There is no pixman fallback to drop to —
# scenefx's scene commit requires an fx renderer.
#
# That leaves mesa's llvmpipe as the only GPU-less option, and llvmpipe could
# not carry it. On a GPU-less runner it crashed twice, in both release and ASan
# builds, with NO synui frame in either stack: first its rasterizer worker pool
# died at EGL context creation, then — with the pool disabled — JIT-compiled
# code ("<unknown module>" over libgallium) faulted on an ordinary cursor
# texture upload. Neither reproduces on a machine with a real driver. Capping
# the JIT's vector width and disabling the worker pool did not fix it, and at
# that point the suite is exercising mesa's software rasterizer rather than the
# compositor.
#
# So: run the full 13 tests wherever there is a driver (every developer box, and
# any self-hosted runner with a GPU), and skip loudly where there is not. Exit
# 77 is meson's SKIP code, so `meson test` reports a skip rather than a pass —
# CI does not get to look green on the strength of a test that never ran.
#
if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node (/dev/dri/renderD*)."
    echo "      synui renders through scenefx's fx_renderer, which is GLES2 and"
    echo "      DMA-BUF only, with no pixman fallback. The software rasterizer"
    echo "      (llvmpipe) segfaults inside its own JIT on a GPU-less runner, so"
    echo "      this suite runs only where a real driver exists."
    exit 77
fi

export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"

# protect_shadow_gap=0: ASan's guarded shadow gap collides with the executable
# mappings a graphics driver JITs its shader/blit paths into, and the JIT'd code
# then faults on memory ASan is protecting. Kept now that the suite runs against
# real drivers, which JIT too. halt_on_error stays so a genuine ASan report
# still fails the run rather than scrolling past.
export ASAN_OPTIONS="protect_shadow_gap=0:halt_on_error=1:abort_on_error=1:print_summary=1"

unset DISPLAY WAYLAND_DISPLAY

# ── 1. Boot ────────────────────────────────────────────────
"$SYNUI" >"$LOG" 2>&1 &
SYNUI_PID=$!

SOCK=
i=0
while [ $i -lt 100 ]; do
    SOCK=$(sed -n 's/.*running on WAYLAND_DISPLAY=\(wayland-[0-9]*\).*/\1/p' "$LOG" | head -1)
    [ -n "$SOCK" ] && break
    kill -0 "$SYNUI_PID" 2>/dev/null || fail "synui died during startup"
    sleep 0.1
    i=$((i + 1))
done
[ -n "$SOCK" ] && [ -S "$TMP/$SOCK" ] || fail "no Wayland socket within 10s"
echo "ok 1 - compositor up on $SOCK"

export WAYLAND_DISPLAY="$SOCK"

# ── 2. Protocol globals ────────────────────────────────────
# Run wayland-info on its own so its exit status and stderr are the real thing.
# This used to be one pipeline ending in sed, with `|| fail "could not connect"`
# hung off the end — but $(...) reports the status of the LAST command, so sed's
# success hid every wayland-info failure and the run reported all ~29 globals
# "not advertised" instead of "it never connected". Two very different bugs that
# looked identical, and the message pointed at the wrong one.
WI_OUT="$TMP/wayland-info.out"
WI_ERR="$TMP/wayland-info.err"
if ! wayland-info >"$WI_OUT" 2>"$WI_ERR"; then
    echo "FAIL: wayland-info could not talk to the compositor (exit $?)" >&2
    echo "--- wayland-info stderr ---" >&2; cat "$WI_ERR" >&2
    fail "wayland-info failed"
fi
GLOBALS=$(sed -n "s/.*interface: '\([^']*\)'.*/\1/p" "$WI_OUT")
if [ -z "$GLOBALS" ]; then
    echo "--- wayland-info stdout ---" >&2; cat "$WI_OUT" >&2
    echo "--- wayland-info stderr ---" >&2; cat "$WI_ERR" >&2
    fail "wayland-info connected but listed no globals at all"
fi
missing=
for g in \
    wl_compositor wl_subcompositor wl_seat wl_shm wl_output \
    wl_data_device_manager wp_viewporter wp_presentation \
    xdg_wm_base zxdg_output_manager_v1 zxdg_decoration_manager_v1 \
    zwlr_layer_shell_v1 wp_fractional_scale_manager_v1 \
    ext_idle_notifier_v1 zwp_idle_inhibit_manager_v1 \
    zwlr_output_manager_v1 zwlr_output_power_manager_v1 \
    ext_session_lock_manager_v1 \
    zwlr_screencopy_manager_v1 zwlr_export_dmabuf_manager_v1 \
    zwlr_data_control_manager_v1 ext_data_control_manager_v1 \
    zwp_primary_selection_device_manager_v1 \
    zwlr_foreign_toplevel_manager_v1 ext_foreign_toplevel_list_v1 \
    zwlr_gamma_control_manager_v1 \
    zwp_pointer_constraints_v1 zwp_relative_pointer_manager_v1 \
    zwp_pointer_gestures_v1
do
    echo "$GLOBALS" | grep -qx "$g" || missing="$missing $g"
done
[ -z "$missing" ] || fail "globals not advertised:$missing"
echo "ok 2 - all required globals advertised"

# ── 3. Client map/unmap ────────────────────────────────────
if command -v foot >/dev/null; then
    timeout 15 foot -- true >/dev/null 2>&1 \
        || fail "foot could not map a toplevel (exit $?)"
    echo "ok 3 - client mapped and unmapped a toplevel (foot)"
else
    echo "ok 3 # SKIP foot not installed"
fi

# ── 4. Screencopy ──────────────────────────────────────────
if command -v grim >/dev/null; then
    timeout 15 grim "$TMP/shot.png" >/dev/null 2>&1 \
        || fail "grim screencopy failed"
    [ -s "$TMP/shot.png" ] || fail "grim produced an empty file"
    echo "ok 4 - screencopy captured a frame (grim)"
else
    echo "ok 4 # SKIP grim not installed"
fi

# ── 5. Live config reload (SIGHUP) ─────────────────────────
cat > "$TMP/synuirc" <<'EOF'
gap = 20
border_width = 4
bind = super+shift+r spawn true
EOF
kill -HUP "$SYNUI_PID"
i=0
while ! grep -q "config reloaded" "$LOG"; do
    [ $i -ge 50 ] && fail "no 'config reloaded' log within 5s of SIGHUP"
    kill -0 "$SYNUI_PID" 2>/dev/null || fail "synui died on SIGHUP"
    sleep 0.1
    i=$((i + 1))
done
# the reload log line reports the applied values — pin them
grep -q "config reloaded (.*gap 20, border 4)" "$LOG" \
    || fail "reload did not apply gap/border from the new config"
# and the compositor must still be serving clients afterwards
wayland-info >/dev/null 2>&1 || fail "compositor unresponsive after reload"
echo "ok 5 - SIGHUP reloaded the config live"

# ── 6. Clean shutdown ──────────────────────────────────────
kill -TERM "$SYNUI_PID"
i=0
while kill -0 "$SYNUI_PID" 2>/dev/null; do
    [ $i -ge 100 ] && fail "synui did not exit within 10s of SIGTERM"
    sleep 0.1
    i=$((i + 1))
done
wait "$SYNUI_PID"
rc=$?
[ "$rc" -eq 0 ] || fail "synui exited $rc (expected 0)"
echo "ok 6 - clean SIGTERM exit"

# ── 7. Sanitizer verdict (ASan builds only) ────────────────
# Match actual reports, not LSan's benign "ptrace blocked" warning.
if grep -qE "(ERROR|SUMMARY): (Address|Leak)Sanitizer" "$LOG"; then
    fail "sanitizer reported errors"
fi
echo "ok 7 - no sanitizer errors"

# ── 8. Desk-spanning workspaces (dual-head boot) ───────────
# A workspace is a virtual desktop across the whole desk, not a per-monitor
# slot: both outputs must come up showing the same one. The old model gave each
# output its own workspace, which pinned ws1..N to monitors and made Super+1..N
# unable to switch anything on a multi-head desk.
unset WAYLAND_DISPLAY
LOG="$TMP/synui2.log"
WLR_HEADLESS_OUTPUTS=2 "$SYNUI" >"$LOG" 2>&1 &
SYNUI_PID=$!
i=0
while [ "$(grep -c 'new output' "$LOG")" -lt 2 ]; do
    [ $i -ge 100 ] && fail "second instance: 2 outputs not up within 10s"
    kill -0 "$SYNUI_PID" 2>/dev/null || fail "second instance died"
    sleep 0.1
    i=$((i + 1))
done
[ "$(grep -c 'new output .* showing workspace 1' "$LOG")" -eq 2 ] \
    || fail "dual-head: both outputs should come up showing workspace 1"
grep -q "showing workspace 2" "$LOG" \
    && fail "dual-head: an output claimed its own workspace (per-output model)"
kill -TERM "$SYNUI_PID"
i=0
while kill -0 "$SYNUI_PID" 2>/dev/null; do
    [ $i -ge 100 ] && fail "second instance did not exit within 10s"
    sleep 0.1
    i=$((i + 1))
done
wait "$SYNUI_PID"
rc=$?
[ "$rc" -eq 0 ] || fail "second instance exited $rc (expected 0)"
if grep -qE "(ERROR|SUMMARY): (Address|Leak)Sanitizer" "$LOG"; then
    fail "sanitizer reported errors (dual-head instance)"
fi
echo "ok 8 - dual-head boot: one desktop spanning both outputs"

# ── 9. Children get a clean signal slate ───────────────────
# A process's blocked signal mask survives exec, and so do its SIG_IGN
# dispositions. synui has both: wl_event_loop_add_signal() is signalfd-based,
# so it blocks SIGINT/SIGTERM/SIGHUP process-wide, and SIGPIPE is ignored.
# Leak either into a child and every app the compositor launches becomes
# unkillable by SIGTERM (`pkill waybar` succeeds and nothing happens).
#
# This family of bug has hit this project three times now — SIGCHLD into
# Xwayland, systemd's IgnoreSIGPIPE into the idle-inhibit helper, and the
# signalfd mask into every autostart child. Hence a test.
unset WAYLAND_DISPLAY
LOG="$TMP/synui3.log"
cat > "$TMP/synuirc" <<'EOF'
autostart = sleep 120
EOF
"$SYNUI" >"$LOG" 2>&1 &
SYNUI_PID=$!

CHILD=
i=0
while [ $i -lt 100 ]; do
    CHILD=$(pgrep -P "$SYNUI_PID" -x sleep 2>/dev/null | head -1)
    [ -n "$CHILD" ] && break
    kill -0 "$SYNUI_PID" 2>/dev/null || fail "signal test: synui died during startup"
    sleep 0.1
    i=$((i + 1))
done
[ -n "$CHILD" ] || fail "autostart child never appeared"

# SigBlk/SigIgn are hex bitmasks, signal N = bit N-1. Both must be empty in the
# child: it should start life with the default disposition for everything.
sigblk=$(sed -n 's/^SigBlk:\s*//p' "/proc/$CHILD/status")
sigign=$(sed -n 's/^SigIgn:\s*//p' "/proc/$CHILD/status")
[ "$sigblk" = "0000000000000000" ] \
    || fail "autostart child inherited a blocked signal mask (SigBlk=$sigblk) — SIGTERM would not reach it"
[ "$sigign" = "0000000000000000" ] \
    || fail "autostart child inherited ignored signals (SigIgn=$sigign)"

# And prove it: the child must actually die on SIGTERM.
kill -TERM "$CHILD" 2>/dev/null
i=0
while kill -0 "$CHILD" 2>/dev/null; do
    [ $i -ge 30 ] && fail "autostart child survived SIGTERM"
    sleep 0.1
    i=$((i + 1))
done

kill -TERM "$SYNUI_PID"
i=0
while kill -0 "$SYNUI_PID" 2>/dev/null; do
    [ $i -ge 100 ] && fail "signal-test instance did not exit within 10s"
    sleep 0.1
    i=$((i + 1))
done
wait "$SYNUI_PID"
echo "ok 9 - spawned children get a clean signal slate and die on SIGTERM"

# ── 10. Control panel over the virtual-keyboard bridge ─────
# waybar's start menu cannot call synui directly — there is no IPC into the
# compositor. Its "Control Panel" entry runs `wtype -M logo -k c -m logo`, which
# reaches synui through virtual-keyboard-v1 and lands on the ordinary keybind
# path. That makes wtype load-bearing for a menu entry, and a silent failure
# (wtype missing, the bind renamed, the protocol unadvertised) would look like
# a menu item that just does nothing. So drive the real thing.
unset WAYLAND_DISPLAY
LOG="$TMP/synui4.log"
: > "$TMP/synuirc"
"$SYNUI" >"$LOG" 2>&1 &
SYNUI_PID=$!

SOCK=
i=0
while [ $i -lt 100 ]; do
    SOCK=$(sed -n 's/.*running on WAYLAND_DISPLAY=\(wayland-[0-9]*\).*/\1/p' "$LOG" | head -1)
    [ -n "$SOCK" ] && break
    kill -0 "$SYNUI_PID" 2>/dev/null || fail "control-panel test: synui died during startup"
    sleep 0.1
    i=$((i + 1))
done
[ -n "$SOCK" ] || fail "control-panel test: no Wayland socket within 10s"
export WAYLAND_DISPLAY="$SOCK"

if command -v wtype >/dev/null; then
    wtype -M logo -k c -m logo || fail "wtype could not reach the compositor"
    i=0
    while ! grep -q "control panel shown" "$LOG"; do
        [ $i -ge 30 ] && fail "Super+C over virtual-keyboard-v1 did not open the control panel"
        kill -0 "$SYNUI_PID" 2>/dev/null || fail "synui died handling Super+C"
        sleep 0.1
        i=$((i + 1))
    done
    echo "ok 10 - waybar's menu path (wtype Super+C) opens the control panel"
else
    echo "ok 10 # SKIP wtype not installed"
fi

# ── 11. Super+N on the *numpad* switches workspace ─────────
# The keypad never sends a digit keysym: NumLock on gives KP_1..KP_9, and NumLock
# off — or Shift, which inverts NumLock — gives the cursor names (KP_End for 1,
# KP_Down for 2, …). A bind written "super+2" holds XKB_KEY_2, so none of those
# matched and Super+numpad was silently dead while the number row worked.
# Both spellings must land on the same workspace bind.
if command -v wtype >/dev/null; then
    for key_ws in "KP_2:2" "KP_End:1" "KP_Prior:9"; do
        key=${key_ws%:*}
        ws=${key_ws#*:}
        wtype -M logo -k "$key" -m logo \
            || fail "wtype could not send Super+$key"
        i=0
        while ! grep -q "synui: workspace $ws\$" "$LOG"; do
            [ $i -ge 30 ] && fail "Super+$key (numpad) did not switch to workspace $ws"
            kill -0 "$SYNUI_PID" 2>/dev/null || fail "synui died handling Super+$key"
            sleep 0.1
            i=$((i + 1))
        done
    done
    echo "ok 11 - numpad Super+N switches workspace (KP_N and the NumLock-off names)"
else
    echo "ok 11 # SKIP wtype not installed"
fi

kill -TERM "$SYNUI_PID"
i=0
while kill -0 "$SYNUI_PID" 2>/dev/null; do
    [ $i -ge 100 ] && fail "control-panel instance did not exit within 10s"
    sleep 0.1
    i=$((i + 1))
done
wait "$SYNUI_PID"
rc=$?
[ "$rc" -eq 0 ] || fail "control-panel instance exited $rc (expected 0)"

# ── 12. Shutdown with a client STILL CONNECTED ─────────────
# Every other test lets its client exit first, so nothing here ever covered the
# ordinary case: logging out while apps are still open. It hid a real one —
# synui_destroy() freed the IME relay before wl_display_destroy_clients(), and
# each client text-input's destroy listener then dereferenced it
# (text_input_destroy → ime_deactivate → use-after-free). ASan caught it on
# every logout with a window open. So: leave a client running, then SIGTERM.
if command -v foot >/dev/null; then
    unset WAYLAND_DISPLAY
    LOG="$TMP/synui5.log"
    "$SYNUI" >"$LOG" 2>&1 &
    SYNUI_PID=$!

    SOCK=
    i=0
    while [ $i -lt 100 ]; do
        SOCK=$(sed -n 's/.*running on WAYLAND_DISPLAY=\(wayland-[0-9]*\).*/\1/p' "$LOG" | head -1)
        [ -n "$SOCK" ] && break
        kill -0 "$SYNUI_PID" 2>/dev/null || fail "live-client test: synui died during startup"
        sleep 0.1
        i=$((i + 1))
    done
    [ -n "$SOCK" ] || fail "live-client test: no Wayland socket within 10s"

    # Give it time to connect and map; the point is only that it is still there.
    WAYLAND_DISPLAY="$SOCK" foot -- sleep 300 >/dev/null 2>&1 &
    FOOT_PID=$!
    sleep 2

    kill -TERM "$SYNUI_PID"
    i=0
    while kill -0 "$SYNUI_PID" 2>/dev/null; do
        [ $i -ge 100 ] && fail "synui did not exit within 10s of SIGTERM (client still connected)"
        sleep 0.1
        i=$((i + 1))
    done
    wait "$SYNUI_PID"
    rc=$?
    kill -9 "$FOOT_PID" 2>/dev/null
    [ "$rc" -eq 0 ] \
        || fail "synui exited $rc shutting down with a client connected (expected 0)"
    if grep -qE "(ERROR|SUMMARY): (Address|Leak)Sanitizer" "$LOG"; then
        fail "sanitizer error shutting down with a client connected"
    fi
    echo "ok 12 - clean shutdown with a client still connected"
else
    echo "ok 12 # SKIP foot not installed"
fi

# 13. An X11 toplevel that closes to the tray and comes back leaves nothing
# behind. xw_map builds a fresh frame on every map, so an unmap that dropped
# only the client's surface tree orphaned the frame, and the next map
# overwrote view->frame and leaked it for good — one scene tree per
# close/restore cycle, growing for as long as the app is used, with each
# orphan still tagged node.data = a view that free() would later reclaim.
# Steam does this cycle every time it closes to the tray.
#
# The leak is invisible to LSan: the orphans stay reachable from the scene
# graph and are freed when it is torn down at exit. It has to be counted while
# the compositor is up, which is what `synctl scene` is for.
if [ -n "$X11_REMAP" ] && [ -x "$X11_REMAP" ] && command -v Xwayland >/dev/null 2>&1; then
    LOG=$(mktemp)
    "$SYNUI" >"$LOG" 2>&1 &
    SYNUI_PID=$!

    SOCK=
    i=0
    while [ $i -lt 100 ]; do
        SOCK=$(sed -n 's/.*running on WAYLAND_DISPLAY=\(wayland-[0-9]*\).*/\1/p' "$LOG" | head -1)
        [ -n "$SOCK" ] && break
        kill -0 "$SYNUI_PID" 2>/dev/null || fail "remap test: synui died during startup"
        sleep 0.1
        i=$((i + 1))
    done
    [ -n "$SOCK" ] || fail "remap test: no Wayland socket within 10s"

    # Xwayland is started lazily; wait for it to announce its DISPLAY.
    XDISP=
    i=0
    while [ $i -lt 150 ]; do
        # "synui: Xwayland X11 DISPLAY=:1 (lazy start)" — anchor on DISPLAY=,
        # not on the distance from "Xwayland": the X11 in between is digits.
        XDISP=$(sed -n 's/.*Xwayland.*DISPLAY=\(:[0-9]\{1,\}\).*/\1/p' "$LOG" \
                | head -1)
        [ -n "$XDISP" ] && break
        sleep 0.2
        i=$((i + 1))
    done
    if [ -z "$XDISP" ]; then
        kill -TERM "$SYNUI_PID" 2>/dev/null; wait "$SYNUI_PID" 2>/dev/null
        echo "ok 13 # SKIP Xwayland did not start"
    else
        WAYLAND_DISPLAY="$SOCK" DISPLAY="$XDISP" "$X11_REMAP" 6 >/dev/null 2>&1 \
            || fail "remap test: X11 client failed"

        # The cycle ends on an unmap, so a correct compositor has nothing left:
        # frames=0, views=0. Pristine HEAD reads {"frames":6,"views":0} — one
        # orphaned frame per cycle, every one of them tagged with a view that
        # is gone. Assert on `leaked` (frames - views) rather than on frames
        # alone, so the test still means something if the client is left mapped.
        # synctl from the build dir, not $PATH: the installed one is whatever
        # version is on the system and need not know `scene` at all.
        SYNCTL=$(dirname "$SYNUI")/synctl
        [ -x "$SYNCTL" ] || SYNCTL=synctl
        # SYNUI_SOCKET wins over WAYLAND_DISPLAY in synctl, and running this
        # suite from inside a synui session inherits one pointing at the *live*
        # compositor — so setting WAYLAND_DISPLAY alone silently queries the
        # developer's own desktop instead of the instance under test. Pin it.
        SCENE=$(SYNUI_SOCKET="$XDG_RUNTIME_DIR/synui-$SOCK.sock" \
                WAYLAND_DISPLAY="$SOCK" "$SYNCTL" scene 2>/dev/null)
        LEAKED=$(printf '%s' "$SCENE" | sed -n 's/.*"leaked":\([-0-9]*\).*/\1/p')

        kill -TERM "$SYNUI_PID" 2>/dev/null
        wait "$SYNUI_PID" 2>/dev/null

        [ -n "$LEAKED" ] \
            || fail "remap test: synctl scene gave nothing (got: $SCENE)"
        [ "$LEAKED" -eq 0 ] \
            || fail "remap test: $LEAKED frame(s) left behind after 6 map/unmap cycles ($SCENE)"
        echo "ok 13 - an X11 toplevel re-mapped 6 times leaks no frames"
    fi
    rm -f "$LOG"
else
    echo "ok 13 # SKIP x11_remap_test or Xwayland not available"
fi

SYNUI_PID=
cleanup
echo "PASS"
