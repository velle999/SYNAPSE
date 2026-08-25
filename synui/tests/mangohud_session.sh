#!/usr/bin/env bash
#
# mangohud_session.sh — whether MangoHud's layer loads in EVERYTHING.
#
# ⛔ THE BUG THIS ENDS. MangoHud's Vulkan manifest declares
#
#     "enable_environment":  { "MANGOHUD": "1" }
#     "disable_environment": { "DISABLE_MANGOHUD": "1" }
#
# so exporting MANGOHUD=1 for the session — which SynapseOS did — is one
# variable that loads VK_LAYER_MANGOHUD_overlay into every Vulkan client in it,
# game or not. On AMD (Renoir) that layer segfaults the client inside its own
# vkCreateDevice hook and on NVIDIA it never does, so it took the live
# wallpaper (synui 409), synstudio (0.1.0-16) and synstudio's own test suite
# down on the ThinkPad while every run on the dev desktop passed. Each fix was
# another DISABLE_MANGOHUD=1 in another launcher; the next Vulkan client to
# arrive would have been the fourth.
#
# So the export is gone and the hud comes from the launcher. What has to hold:
#
#   * a session with no configuration exports nothing
#   * MANGOHUD_EVERYWHERE=1 puts it back, because that is the promise made to
#     anyone who liked the old behaviour
#   * a user's answer beats the machine's, in both directions
#   * and ⚠ THE THREE COPIES OF THIS LOGIC AGREE. The live ISO has one
#     (profile.d) and the installer writes two more into an installed system;
#     they are three separate texts of the same rule, and the installer's own
#     comment already says "this path must stay in step with it". Nothing but
#     a test makes that true.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
PROFILE=${1:-$ROOT/archiso/airootfs/etc/profile.d/synapseos.sh}
INSTALLER=${2:-$ROOT/syn-install/syn-install.sh}
GAMERUN=${3:-$ROOT/synui/systemd/synui-game-run.sh}

for f in "$PROFILE" "$INSTALLER" "$GAMERUN"; do
    [ -r "$f" ] || { echo "not readable: $f" >&2; exit 1; }
done

TMP=$(mktemp -d) || exit 1
trap 'rm -rf "$TMP"' EXIT

pass=0 fail=0
ok()   { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad()  { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail + 1)); }
check(){ if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 (want '$2', got '$3')"; fi; }

echo "mangohud session default — $PROFILE"

# ── The rule itself, as the session runs it ─────────────────────────────────
#
# Sourced in a subshell with a scratch HOME, and asked one question: what is
# MANGOHUD afterwards. `unset`, not empty — the layer's enable_environment
# matches on the value, and an exported MANGOHUD= would be a different bug.
ask() {  # ask <user-conf-value|-> <system-conf-value|->
    (
        export HOME="$TMP/home" XDG_CONFIG_HOME="$TMP/home/.config"
        mkdir -p "$XDG_CONFIG_HOME/synapseos" "$TMP/etc/synapseos"
        rm -f "$XDG_CONFIG_HOME/synapseos/mangohud.conf" "$TMP/etc/synapseos/mangohud.conf"
        [ "$1" != - ] && printf 'MANGOHUD_EVERYWHERE=%s\n' "$1" \
                         > "$XDG_CONFIG_HOME/synapseos/mangohud.conf"
        [ "$2" != - ] && printf 'MANGOHUD_EVERYWHERE=%s\n' "$2" \
                         > "$TMP/etc/synapseos/mangohud.conf"
        # The system path is absolute in the real file; point it at the fixture
        # for the duration of the read.
        sed "s#/etc/synapseos/mangohud.conf#$TMP/etc/synapseos/mangohud.conf#g" \
            "$PROFILE" > "$TMP/profile.sh"
        unset MANGOHUD
        . "$TMP/profile.sh" >/dev/null 2>&1
        printf '%s' "${MANGOHUD-unset}"
    )
}

check "no configuration at all: nothing is exported"      "unset" "$(ask - -)"
check "the machine says 1: exported"                      "1"     "$(ask - 1)"
check "the machine says 0: still nothing"                 "unset" "$(ask - 0)"
check "a user opting in beats a machine that has not"     "1"     "$(ask 1 -)"
check "…and a user opting OUT beats a machine that did"   "unset" "$(ask 0 1)"
check "a user opting in beats a machine that said 0"      "1"     "$(ask 1 0)"

# ⛔ The literal export must be gone, not merely guarded somewhere else in the
# file: a second unconditional one further down would pass every check above.
if grep -qE '^[[:space:]]*export MANGOHUD=1' "$PROFILE"; then
    bad "profile.d still exports MANGOHUD=1 unconditionally"
else
    ok "no unconditional export left in profile.d"
fi

# ── The installer's two copies say the same thing ───────────────────────────
n=$(grep -c 'MANGOHUD_EVERYWHERE' "$INSTALLER")
check "the installer writes the switch into both session paths it owns" 2 "$n"
if grep -qE '^[[:space:]]*export MANGOHUD=1' "$INSTALLER"; then
    bad "the installer still writes an unconditional export"
else
    ok "…and neither of them is an unconditional export"
fi

# ── The switch, from the front door ─────────────────────────────────────────
export HOME="$TMP/home2" XDG_CONFIG_HOME="$TMP/home2/.config"
mkdir -p "$XDG_CONFIG_HOME"
CONF="$XDG_CONFIG_HOME/synapseos/mangohud.conf"

bash "$GAMERUN" --hud-everywhere status >"$TMP/out" 2>&1
check "status with nothing set exits 0" 0 "$?"
grep -q "only in what" "$TMP/out" \
    && ok "…and says the layer is not everywhere" \
    || bad "status did not describe the default: $(cat "$TMP/out")"

bash "$GAMERUN" --hud-everywhere on >/dev/null 2>&1
check "hud on writes the user's file" "MANGOHUD_EVERYWHERE=1" \
      "$(grep '^MANGOHUD_EVERYWHERE' "$CONF" 2>/dev/null)"
check "…and the session then exports it" "1" "$(ask 1 -)"

bash "$GAMERUN" --hud-everywhere off >/dev/null 2>&1
check "hud off writes it back" "MANGOHUD_EVERYWHERE=0" \
      "$(grep '^MANGOHUD_EVERYWHERE' "$CONF" 2>/dev/null)"

# ⚠ ONE LINE, ALWAYS. The file is rewritten rather than appended to: two
# MANGOHUD_EVERYWHERE lines would make the answer depend on which one the
# reader stops at, and the session stops at the last.
bash "$GAMERUN" --hud-everywhere on  >/dev/null 2>&1
bash "$GAMERUN" --hud-everywhere off >/dev/null 2>&1
bash "$GAMERUN" --hud-everywhere on  >/dev/null 2>&1
check "…and repeated flips leave exactly one setting line" 1 \
      "$(grep -c '^MANGOHUD_EVERYWHERE' "$CONF")"

bash "$GAMERUN" --hud-everywhere nonsense >/dev/null 2>&1
check "a value that is not on/off/status is refused" 2 "$?"

printf '\n  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" = 0 ] || exit 1
echo "mangohud_session: PASS"
