#!/bin/sh
# deskmenu_widgets.sh — the desktop right-click menu offers the widget manager.
#
# WHAT THIS EXISTS FOR (velle, 2026-08-23): the seven desktop widgets could only
# be reached from Super+Shift+A or the control panel. Right-clicking the very
# desktop they sit on offered a wallpaper picker, an appearance panel and a
# display panel, and nothing at all about the things drawn on top of it.
#
# ⚠ A BUILD CANNOT SEE THIS ROW GO MISSING. The switch statements in deskmenu.c
# are covered by -Wswitch, so a dropped `case` is a compile error — but the row
# is put on screen by an n++ walk in deskmenu_open(), and deleting that one line
# builds clean, runs clean, and simply draws a menu with nothing in it about
# widgets. The only witness is a pointer landing where the row is supposed to be.
#
# So the coordinates here MIRROR deskmenu.c's geometry (4px of padding, 30px
# rows, 9px separators) and are not read back from the compositor. That is
# deliberate: if the row moves, this fails, and a row somewhere else is a row
# the hand that learned this menu misses. It is also what makes the failure
# discriminating — the row below "Desktop Widgets…" is "Show Desktop Icons", so
# a menu built without this entry does not do nothing when clicked at 208px
# down, it turns the desktop icons on, and no widget is ever asked for.
#
# The assertion is on what the compositor DID, not on what it drew: the panel is
# opened with the mouse, then flipped with the keyboard (Space is the group
# toggle from any row), and the ledger is a fake `synui-widgets` on PATH —
# widgets.c has never written widgets.state itself, it spawns that helper, so
# the helper being called is the whole of "the manager is up and live".
#
# Usage: deskmenu_widgets.sh /path/to/synui /path/to/synctl /path/to/vpointer_click
# Skips (77) without a DRM render node or without wtype.
set -u

TESTDIR=$(dirname "$0")
export LSAN_OPTIONS="suppressions=$TESTDIR/lsan.supp:print_suppressions=0"
export ASAN_OPTIONS="protect_shadow_gap=0:fast_unwind_on_malloc=0:halt_on_error=1:abort_on_error=1:print_summary=1"

SYNUI=${1:?usage: deskmenu_widgets.sh synui synctl vpointer_click}
SYNCTL=${2:?usage: deskmenu_widgets.sh synui synctl vpointer_click}
VPTR=${3:?usage: deskmenu_widgets.sh synui synctl vpointer_click}

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node — synui's fx_renderer is GLES2/DMA-BUF only."
    exit 77
fi
if ! command -v wtype >/dev/null; then
    echo "SKIP: wtype is not installed (the panel is flipped from the keyboard)."
    exit 77
fi

# ⚠ Before anything else. A rig that synthesises input with a WAYLAND_DISPLAY
# still set drives the developer's REAL desktop, and this one right-clicks.
unset SYNUI_SOCKET WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY

TMP=$(mktemp -d /tmp/synui-deskmenu.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG="$TMP/synui.log"
CALLS="$TMP/calls"
: > "$CALLS"

fail() { echo "FAIL: $*" >&2; cleanup; exit 1; }
cleanup() {
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    [ -n "${TMP:-}" ] && rm -rf "$TMP"
}
trap cleanup INT TERM

# desktop_icons pinned OFF — it is the default, but it is also what decides the
# menu's row list (the arrange rows only exist while the icons do), so the
# coordinates below are only right with it stated. The power stages are pushed
# out for the reason theme_seat_guard.sh documents: suspend is logind's, not
# this compositor's, and an empty rc suspends the whole machine in 30 seconds.
# ⚠ welcome_at_startup/start_overlay OFF. A scratch HOME is a FIRST RUN, and
# the welcome panel is on SYN_PANEL_LIST — it comes up over the wallpaper and
# swallows every click, so the right-click never reaches the desktop at all and
# this rig fails with the menu perfectly intact.
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

# The ledger, and the seatbelt: without it a nested synui runs the REAL
# synui-widgets against $HOME, and $HOME here is a scratch dir the bar is not
# watching — but synui-apply-theme is not so harmless, so it is stubbed too.
mkdir -p "$TMP/bin"
for h in synui-widgets synui-apply-theme synui-glass synui-apply-font; do
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
vptr()   { WAYLAND_DISPLAY="$SOCK" "$VPTR" "$@" >/dev/null 2>&1; }
send()   { wtype "$@" || fail "wtype could not send: $*"; }
settle() { sleep 0.4; }

# The headless output, read rather than assumed — a default mode change here
# would otherwise move every coordinate below without saying so.
OUTBOX=$(synctl outputs | sed -n 's/.*"size":\[\([0-9]*\),\([0-9]*\)\].*/\1 \2/p' | head -1)
OW=${OUTBOX%% *}; OH=${OUTBOX##* }
[ -n "$OW" ] && [ -n "$OH" ] || fail "could not read the output size from synctl outputs"
echo "output: ${OW}x${OH}"

# deskmenu.c: DESKMENU_W 210, DESKMENU_ITEM_H 30, DESKMENU_SEP_H 9, 4px of pad
# at the top and 8 of padding in the height. With the icons off the rows are
#   Terminal, Files, Applications…, ──, Wallpaper…, Appearance…, Display…,
#   Desktop Widgets…, Show Desktop Icons, ──, Task Manager
MENU_W=210
MENU_H=$(( 8 + 9 * 30 + 2 * 9 ))
# Top of the widgets row: three items, a separator, then three more items.
ROW_TOP=$(( 4 + 3 * 30 + 9 + 3 * 30 ))

# Where to right-click. Far enough from the far edges that the menu is not
# clamped, because a clamped menu is not at the cursor and every offset below
# would be measured from the wrong corner.
CX=60; CY=60
[ $(( CX + MENU_W )) -lt "$OW" ] && [ $(( CY + MENU_H )) -lt "$OH" ] || \
    fail "the headless output is ${OW}x${OH} — too small for the menu to open
       at $CX,$CY unclamped, so these coordinates cannot be trusted"

ROW_X=$(( CX + MENU_W / 2 ))
ROW_Y=$(( CY + ROW_TOP + 15 ))

pass=0
ok() { echo "  ok    $1"; pass=$((pass + 1)); }

# ── the menu opens on the wallpaper ─────────────────────────────────────
vptr "$CX" "$CY" right; settle
ok "right-clicking the wallpaper is accepted"

# ── the widgets row opens the manager ───────────────────────────────────
vptr "$ROW_X" "$ROW_Y" 1; settle
send " "; settle

if ! grep -q '^synui-widgets ' "$CALLS"; then
    fail "clicking $ROW_X,$ROW_Y — 'Desktop Widgets…', ${ROW_TOP}px down the
       menu — and pressing Space asked synui-widgets for nothing.
       Calls recorded: $(cat "$CALLS" | tr '\n' '|')
       Either deskmenu_open() no longer puts the row there (the row below it is
       'Show Desktop Icons', which would have swallowed the click), or the row
       is drawn and does not reach widgets_toggle()."
fi
ok "'Desktop Widgets…' opens the widget manager"

# ── and the manager it opened is the live one ───────────────────────────
# Space is the group flip, and with a scratch HOME nothing is on, so the panel
# must have asked for everything ON. A stale or half-built panel would still
# have answered the key — this is what says it read the state first.
grep -q '^synui-widgets all on$' "$CALLS" || \
    fail "the panel opened but Space did not turn every widget on:
       $(cat "$CALLS" | tr '\n' '|')"
ok "…and it is live: Space flipped the whole group on"

cleanup
echo
echo "$pass checks passed"
