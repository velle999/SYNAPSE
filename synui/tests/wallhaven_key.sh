#!/bin/sh
# wallhaven_key.sh — the keys that open the wallhaven browser, pressed
#
# ⛔ THIS RIG EXISTS BECAUSE THE KEY DID NOTHING. Super+Ctrl+W was bound,
# dispatched, and spawned `synui-wallhaven toggle` correctly — and the launcher
# refused, because the network switch is off by default, onto the stderr of a
# process nobody was reading. Every layer reported success and the desktop did
# not move. velle reported it as the keybind not responding.
#
# So what is checked here is the whole path, from a real keypress to the
# launcher actually being RUN — not that a dispatch was accepted:
#
#   1. Super+Ctrl+W with nothing open runs the launcher exactly once
#   2. `w` inside the Super+W picker runs the same launcher, once
#   3. …and the picker is DOWN afterwards, which is proved by a SECOND `w`
#      reaching nothing — two full-screen surfaces both holding the keyboard is
#      the failure this closes
#
# The ledger is a fake `synui-wallhaven` on PATH, the same shape
# deskmenu_widgets.sh uses: synui spawns the launcher through /bin/sh, so the
# shim is what "the key reached it" means. The launcher's own refusal is not
# this rig's business — it is the script's, and the window asks now.
#
# ⚠ ONE wtype PER SEQUENCE. On a headless seat wtype's virtual keyboard is the
# only keyboard the seat ever has, so each invocation adds and removes the
# seat's keyboard capability; a sequence split across calls is a sequence with a
# teardown in the middle of it.
#
# Usage: wallhaven_key.sh /path/to/synui /path/to/synctl
# Skips (77) without a DRM render node or without wtype.
set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: wallhaven_key.sh /path/to/synui /path/to/synctl}
SYNCTL=${2:?usage: wallhaven_key.sh /path/to/synui /path/to/synctl}

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node — synui's fx_renderer is GLES2/DMA-BUF only."
    exit 77
fi
if ! command -v wtype >/dev/null; then
    echo "SKIP: wtype is not installed (this rig is nothing without it)."
    exit 77
fi

# ⚠ Before anything else. A rig that synthesises input with a WAYLAND_DISPLAY
# still set drives the developer's REAL desktop.
unset SYNUI_SOCKET WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY

TMP=$(mktemp -d /tmp/synui-wallhaven.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
CALLS="$TMP/calls"
: > "$CALLS"

fail() { echo "FAIL: $*" >&2; [ -f "$LOG" ] && tail -20 "$LOG" >&2; cleanup; exit 1; }
cleanup() {
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    [ -n "${TMP:-}" ] && rm -rf "$TMP"
}
trap cleanup INT TERM

# ⚠ welcome_at_startup/start_overlay OFF. A scratch HOME is a FIRST RUN, and the
# welcome guide is a full-screen quickshell surface that would be over the
# picker. The power stages are pushed out because an empty rc suspends the whole
# machine in 30 seconds.
cat > "$TMP/synuirc" <<'RC'
welcome_at_startup = off
start_overlay = off
desktop_icons = 0
power_enabled = 0
power_dim_timeout = 86400
power_blank_timeout = 86400
power_lock_timeout = 86400
power_suspend_timeout = 86400
RC

# The ledger, and the seatbelt: without the shim a nested synui runs the REAL
# launcher, which would put a browser on the developer's screen.
mkdir -p "$TMP/bin"
cat > "$TMP/bin/synui-wallhaven" <<RECORD
#!/bin/sh
echo "wallhaven \$*" >> "$CALLS"
exit 0
RECORD
chmod +x "$TMP/bin/synui-wallhaven"
for h in synui-apply-theme synui-glass synui-apply-font synui-wpengine; do
    cat > "$TMP/bin/$h" <<RECORD
#!/bin/sh
echo "$h \$*" >> "$CALLS"
exit 0
RECORD
    chmod +x "$TMP/bin/$h"
done

export PATH="$TMP/bin:$PATH"
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc" SYNUI_WINDOWS="$TMP/windows.conf"
export GSETTINGS_BACKEND=memory
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export WLR_HEADLESS_OUTPUTS=1

"$SYNUI" -d >"$LOG" 2>&1 &
SYNUI_PID=$!

SOCK=
i=0
while [ $i -lt 100 ]; do
    for c in "$TMP"/wayland-*; do
        case "$c" in *.lock) continue;; esac
        [ -S "$c" ] && SOCK=$(basename "$c") && break
    done
    [ -n "$SOCK" ] && break
    kill -0 "$SYNUI_PID" 2>/dev/null || fail "synui exited during startup:
       $(tail -5 "$LOG")"
    i=$((i + 1)); sleep 0.1
done
[ -n "$SOCK" ] || fail "no wayland socket after 10s"
export WAYLAND_DISPLAY="$SOCK"
CTLSOCK="$TMP/synui-$SOCK.sock"

synctl() { SYNUI_SOCKET="$CTLSOCK" "$SYNCTL" "$@" 2>/dev/null; }
calls()  { grep -c '^wallhaven ' "$CALLS" 2>/dev/null || echo 0; }
settle() { sleep 0.6; }

pass=0
ok() { echo "  ok    $1"; pass=$((pass + 1)); }

# ── 1. Super+Ctrl+W ─────────────────────────────────────────────────────
wtype -s 200 -M logo -M ctrl -k w -m ctrl -m logo || fail "wtype could not send Super+Ctrl+W"
settle
[ "$(calls)" = 1 ] || fail "Super+Ctrl+W ran the launcher $(calls) times, not 1.
       This is the reported bug's own shape: the bind exists, the dispatch is
       accepted, and nothing runs.
       Ledger: $(tr '\n' '|' < "$CALLS")"
grep -q '^wallhaven toggle$' "$CALLS" ||
    fail "the launcher was run, but not as 'toggle': $(tr '\n' '|' < "$CALLS")"
ok "Super+Ctrl+W runs 'synui-wallhaven toggle', once"

# ── 2. `w` in the picker, and 3. the picker is down afterwards ──────────
synctl dispatch wallpaper >/dev/null
settle
# ⛔ BOTH PRESSES IN ONE wtype. The second is the assertion: if `w` left the
# picker up, it would open the browser a second time (and the toggle across a
# process boundary would close what the first one opened).
wtype -s 250 -k w -k w || fail "wtype could not send w"
settle
[ "$(calls)" = 2 ] || fail "two presses of w with the picker open ran the
       launcher $(( $(calls) - 1 )) times, not exactly 1.
       Either w is not wired into wppick_key() at all (0), or it does not close
       the picker and the second press opened the browser again (2).
       Ledger: $(tr '\n' '|' < "$CALLS")"
ok "w in the picker runs the launcher once"
ok "…and the picker is down afterwards, so the second w reaches nothing"

echo "PASS ($pass checks)"
cleanup
exit 0
