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
#      -Db_sanitize=address; harmless otherwise).
#
# Usage: smoke.sh /path/to/synui
# Run by `meson test -C <builddir>`; needs wayland-info (wayland-utils).
# foot and grim tests are skipped when the tools aren't installed.

set -u

SYNUI=${1:?usage: smoke.sh /path/to/synui}
TESTDIR=$(dirname "$0")

fail() { echo "FAIL: $*" >&2; [ -n "${LOG:-}" ] && tail -40 "$LOG" >&2; cleanup; exit 1; }

cleanup() {
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
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
export WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
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
GLOBALS=$(wayland-info 2>/dev/null | sed -n "s/.*interface: '\([^']*\)'.*/\1/p") \
    || fail "wayland-info could not connect"
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

SYNUI_PID=
cleanup
echo "PASS"
