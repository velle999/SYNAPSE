#!/bin/sh
# speak_announcer.sh — the screen reader's switch has an announcer behind it
#
# Three surfaces offer "screen reader": Super+Shift+U, synui's control panel
# (Sound ▸ Speech) and syn-settings. All three run `syn-speak on`, which writes
# on=yes, says "Speech is on", and enables syn-speak.service.
#
# ⛔ THE UNIT WAS NOT IN THE PACKAGE for four pkgrels. `unit_do` ends in
# `|| true` — deliberately, so a box without systemd is not a broken install —
# so the failed enable was swallowed, every surface reported On, and no window
# was ever announced. The state file is not the announcer. Nothing in the tree
# could notice: the script was right, the panel was right, the keybind was
# right, and the one file that ties them together did not exist.
#
# So this asserts both halves, separately:
#
#   1. the unit the switch enables is a unit this package SHIPS, it runs
#      `syn-speak watch`, and the PKGBUILD installs it — read out of the
#      sources, so it fails in the tree rather than on somebody's machine;
#   2. `syn-speak watch` actually SPEAKS the focused window when it changes,
#      says it once rather than on every poll, and stops when switched off;
#   3. ...including with NO WAYLAND_DISPLAY, which is the environment a user
#      unit really starts in — nothing here runs `systemctl --user
#      import-environment`, so the announcer has to resolve the display from
#      the name synui publishes or it polls for ever in silence.
#
# ⚠ NO REAL VOICE IS REACHABLE. syn-speak speaks through `vibe voice say` where
# vibe exists and espeak-ng otherwise, and BOTH are stubbed — the positive case
# has to stub every candidate or a real one is launched, out loud, on the
# developer's session. synctl is stubbed the same way, through the env hook the
# script already has.
#
# Usage: speak_announcer.sh /path/to/syn-speak /path/to/systemd-dir /path/to/PKGBUILD

set -u

SPEAK=${1:?usage: speak_announcer.sh <syn-speak> <systemd-dir> <PKGBUILD>}
UNITDIR=${2:?usage: speak_announcer.sh <syn-speak> <systemd-dir> <PKGBUILD>}
PKGBUILD=${3:?usage: speak_announcer.sh <syn-speak> <systemd-dir> <PKGBUILD>}

fails=0
ok()   { printf '  ok    %s\n' "$*"; }
bad()  { printf '  FAIL  %s\n' "$*"; fails=$((fails + 1)); }

cleanup() { [ -n "${TMP:-}" ] && rm -rf "$TMP"; }
trap cleanup INT TERM EXIT

TMP=$(mktemp -d /tmp/synui-speak.XXXXXX) || exit 1
chmod 700 "$TMP"

# ── 1. the switch and the thing it switches ─────────────────────────────
# The unit name comes out of the script rather than being typed here: the point
# is that what `syn-speak on` enables and what the package installs are the same
# string, and a test that hard-codes it cannot tell.
UNIT=$(sed -n 's/.*systemctl --user "\$1" --now \([A-Za-z0-9._-]*\).*/\1/p' "$SPEAK" | head -1)
if [ -n "$UNIT" ]; then
    ok "the switch enables a named unit ($UNIT)"
else
    bad "could not find the unit name in unit_do — has the switch stopped
        enabling anything at all?"
    UNIT=syn-speak.service
fi

if [ -f "$UNITDIR/$UNIT" ]; then
    ok "$UNIT exists in systemd/"
else
    bad "$UNIT is what \`syn-speak on\` enables and systemd/ does not contain
        it. That is the whole bug this file exists for: the switch reports On
        from three surfaces and nothing announces anything."
fi

if grep -q 'ExecStart=.*syn-speak watch' "$UNITDIR/$UNIT" 2>/dev/null; then
    ok "$UNIT runs \`syn-speak watch\`"
else
    bad "$UNIT does not run \`syn-speak watch\` — the announcer is that loop,
        and a unit that starts anything else is a unit that starts nothing."
fi

if grep -q "systemd/$UNIT" "$PKGBUILD" && \
   grep -q "usr/lib/systemd/user/$UNIT" "$PKGBUILD"; then
    ok "the PKGBUILD installs $UNIT into usr/lib/systemd/user/"
else
    bad "the PKGBUILD does not install $UNIT. A unit in the tree that is not in
        the package is exactly as absent as one that was never written —
        \`systemctl --user enable\` on the target has nothing to find."
fi

# ── the stubs ───────────────────────────────────────────────────────────
BIN="$TMP/bin"
mkdir -p "$BIN"

# ⚠ EVERY voice candidate, not just the expected one. `have vibe` picks the
# first vibe on PATH, and the real one is installed on a developer box: an
# unstubbed run says the window title out loud on the live desktop.
cat > "$BIN/vibe" <<'EOF'
#!/bin/sh
# vibe voice say TEXT
[ "${1:-}" = voice ] && [ "${2:-}" = say ] && printf '%s\n' "$3" >> "$SPEAK_SAID"
exit 0
EOF
cat > "$BIN/espeak-ng" <<'EOF'
#!/bin/sh
# Whatever flags it is given, the last argument is the line.
for a in "$@"; do last=$a; done
printf '%s\n' "$last" >> "$SPEAK_SAID"
exit 0
EOF
cat > "$BIN/synctl" <<'EOF'
#!/bin/sh
# ⚠ REFUSES WITHOUT A DISPLAY, because the real one effectively does: the
# control socket's path is keyed on WAYLAND_DISPLAY, so a synctl with none
# cannot find the compositor. A stub that answered anyway would make phase 4
# pass with the fallback deleted — checked, and it did.
[ -n "${WAYLAND_DISPLAY:-}" ] || exit 1
[ "${1:-}" = activewindow ] || exit 1
cat "$SPEAK_WINDOW"
EOF
chmod +x "$BIN/vibe" "$BIN/espeak-ng" "$BIN/synctl"

export SPEAK_SAID="$TMP/said"
export SPEAK_WINDOW="$TMP/window.json"
export SYN_SPEAK_BIN="$BIN/espeak-ng"
export SYN_SPEAK_SYNCTL="$BIN/synctl"
export PATH="$BIN:$PATH"
export XDG_CONFIG_HOME="$TMP/config" XDG_RUNTIME_DIR="$TMP/run" HOME="$TMP"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

: > "$SPEAK_SAID"
printf '{"title":"Inbox","app_id":"thunderbird"}\n' > "$SPEAK_WINDOW"

# on=yes written directly: `syn-speak on` would also try to reach systemd, and
# what is under test here is the loop, not the enable.
mkdir -p "$XDG_CONFIG_HOME/synui"
printf 'on=yes\ninterval=0.2\n' > "$XDG_CONFIG_HOME/synui/speak.state"

# ── 2. the loop announces, once per change ──────────────────────────────
export WAYLAND_DISPLAY=wayland-test
sh "$SPEAK" watch >/dev/null 2>&1 &
WATCH=$!
sleep 1.2
printf '{"title":"Terminal","app_id":"syntty"}\n' > "$SPEAK_WINDOW"
sleep 1.2
printf 'on=no\ninterval=0.2\n' > "$XDG_CONFIG_HOME/synui/speak.state"

# ── 3. and it stops on its own ──────────────────────────────────────────
# The loop returns 0 the moment it reads on=no. A reader that keeps talking
# after being switched off is the failure the toggle exists to prevent.
i=0
while kill -0 "$WATCH" 2>/dev/null; do
    i=$((i + 1))
    if [ $i -gt 40 ]; then
        kill -9 "$WATCH" 2>/dev/null
        bad "\`syn-speak watch\` did not exit within 4s of on=no. The loop tests
        the state file every pass for exactly this reason — a screen reader has
        to stop when it is switched off, not at the next login."
        break
    fi
    sleep 0.1
done
[ $i -le 40 ] && ok "the loop exits when the switch goes off"
wait "$WATCH" 2>/dev/null

SAID=$(cat "$SPEAK_SAID")
case "$SAID" in
    *"Inbox, thunderbird"*) ok "it announced the focused window" ;;
    *) bad "the focused window was never announced. \`syn-speak watch\` polls
        \`synctl activewindow\` and speaks the line when it changes; nothing
        reached the voice. Said: [$SAID]" ;;
esac
case "$SAID" in
    *"Terminal, syntty"*) ok "...and announced the next one when focus moved" ;;
    *) bad "focus moved to a second window and it was not announced — the loop
        speaks on CHANGE, so a first line with no second means it stopped
        looking. Said: [$SAID]" ;;
esac
N=$(grep -c "Inbox, thunderbird" "$SPEAK_SAID")
if [ "$N" = 1 ]; then
    ok "each window is announced once, not once per poll"
else
    bad "the first window was announced $N times. The loop keeps the last line
        and speaks only when it differs; repeating it every 0.2s is a desktop
        that talks over itself."
fi

# ── 4. ...with no WAYLAND_DISPLAY, which is how the unit really starts ──
# Nothing runs `systemctl --user import-environment`, so the announcer's
# environment has no display in it and synctl cannot find the compositor. It
# resolves the name synui publishes instead — and the failure without that is
# the quietest one there is: the loop runs, polls, and says nothing for ever.
: > "$SPEAK_SAID"
printf '{"title":"Files","app_id":"synfiles"}\n' > "$SPEAK_WINDOW"
printf 'on=yes\ninterval=0.2\n' > "$XDG_CONFIG_HOME/synui/speak.state"
printf 'wayland-published\n' > "$XDG_RUNTIME_DIR/synui-display"

env -u WAYLAND_DISPLAY sh "$SPEAK" watch >/dev/null 2>&1 &
WATCH=$!
sleep 1.2
printf 'on=no\n' > "$XDG_CONFIG_HOME/synui/speak.state"
sleep 0.6
kill -9 "$WATCH" 2>/dev/null
wait "$WATCH" 2>/dev/null

case "$(cat "$SPEAK_SAID")" in
    *"Files, synfiles"*) ok "it finds the display synui published when it has none" ;;
    *) bad "with no WAYLAND_DISPLAY nothing was announced. That is the
        environment syn-speak.service actually starts in; display_name() must
        fall back to \$XDG_RUNTIME_DIR/synui-display, the same file
        synui-foot.service and synui-media-inhibit read." ;;
esac

# ── 5. and an explicit display is never overridden ──────────────────────
# ⛔ The published file names the LIVE desktop. A nested or headless synui sets
# its own WAYLAND_DISPLAY, and preferring the file there would point the
# announcer — and anything else that copies this — at the session somebody is
# using. Same trap as SYNUI_SOCKET.
: > "$SPEAK_SAID"
cat > "$BIN/synctl" <<'EOF'
#!/bin/sh
[ "${1:-}" = activewindow ] || exit 1
printf '{"title":"%s","app_id":"probe"}\n' "$WAYLAND_DISPLAY"
EOF
chmod +x "$BIN/synctl"
printf 'on=yes\ninterval=0.2\n' > "$XDG_CONFIG_HOME/synui/speak.state"
WAYLAND_DISPLAY=wayland-explicit sh "$SPEAK" watch >/dev/null 2>&1 &
WATCH=$!
sleep 0.9
printf 'on=no\n' > "$XDG_CONFIG_HOME/synui/speak.state"
sleep 0.5
kill -9 "$WATCH" 2>/dev/null
wait "$WATCH" 2>/dev/null

case "$(cat "$SPEAK_SAID")" in
    *wayland-explicit*) ok "an explicit WAYLAND_DISPLAY wins over the published one" ;;
    *) bad "the published display beat an explicit WAYLAND_DISPLAY. On a
        developer box that file names the LIVE session: a nested synui's
        announcer would read the real desktop's windows out loud. Said:
        [$(cat "$SPEAK_SAID")]" ;;
esac

# ── 6. `status` names the engine that would actually speak ─────────────
# It used to answer yes/no on espeak-ng alone, so a box speaking perfectly well
# through vibe's piper reported `engine no` — a working setup described as a
# broken one, on the first line anybody reads when the reader is silent.
#
# ⚠ THE ABSENT CASES NEED A PATH WITHOUT /usr/bin. Prepending a stub directory
# proves "installed" and never "not installed": vibe is on a developer box, and
# `command -v vibe` would find the real one straight through the stub dir. Every
# tool the script reaches for is resolved into $ONLY first, because bash, sed
# and the rest vanish with /usr/bin too and the failure then reads as 127 from
# the test rather than an answer from the program.
ONLY="$TMP/only"
mkdir -p "$ONLY"
SH_BIN=$(command -v sh)
for t in sed tr cat mkdir; do ln -sf "$(command -v "$t")" "$ONLY/$t"; done

engine_of() {   # engine_of "vibe espeak-ng" -> the reported engine
    rm -f "$ONLY/vibe" "$ONLY/espeak-ng"
    for w in $1; do printf '#!/bin/sh\nexit 0\n' > "$ONLY/$w"; chmod +x "$ONLY/$w"; done
    env -i PATH="$ONLY" HOME="$TMP" XDG_CONFIG_HOME="$TMP/config" \
        XDG_RUNTIME_DIR="$TMP/run" SYN_SPEAK_SYNCTL=/nonexistent \
        "$SH_BIN" "$SPEAK" status 2>/dev/null \
        | sed -n 's/^engine[[:space:]]*//p'
}

E_BOTH=$(engine_of "vibe espeak-ng")
E_TTS=$(engine_of "espeak-ng")
E_NONE=$(engine_of "")
printf '  ..    engine reported: both=[%s] espeak-only=[%s] neither=[%s]\n' \
    "$E_BOTH" "$E_TTS" "$E_NONE"
[ "$E_BOTH" = vibe ] \
    && ok "with vibe present, status names vibe — the engine speak() picks" \
    || bad "with vibe present status said [$E_BOTH]. speak() prefers
        \`vibe voice say\`, so anything else here describes a machine that is
        not this one."
[ "$E_TTS" = espeak-ng ] \
    && ok "without vibe it names the fallback synthesiser" \
    || bad "with only espeak-ng, status said [$E_TTS]."
[ "$E_NONE" = none ] \
    && ok "with neither it says so plainly" \
    || bad "with no voice at all status said [$E_NONE] — the one case where
        this line has to be unambiguous, because the switch will turn on and
        the machine will stay silent."

echo
if [ "$fails" -eq 0 ]; then
    echo "PASS"
    exit 0
fi
echo "speak_announcer: $fails check(s) failed"
exit 1
