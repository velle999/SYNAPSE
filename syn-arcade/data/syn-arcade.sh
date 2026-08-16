# syn-arcade — session setup for the game overlay and controller mappings.
#
# Installed to /etc/profile.d. Sourced by every login shell, which is what the
# graphical session inherits from.
#
# SynapseOS Project — GPL-2.0-or-later

# ── The overlay config ──────────────────────────────────────────────────────
#
# MangoHud reads exactly ONE config file, chosen from five candidates walked in
# reverse — and /etc/MangoHud.conf, which this desktop ships, OUTRANKS
# ~/.config/MangoHud/MangoHud.conf. There is no merging. So on a stock install
# the user's own overlay config is never read at all, and neither a per-user
# setting nor a keybind running as the user can change anything.
#
# MANGOHUD_CONFIGFILE collapses that whole list to one writable path, which is
# the only way round it.
#
# ⚠ Set only if the user has not set it themselves. Somebody who has pointed
# MangoHud at their own file has made a decision, and a login script that
# quietly overrides it every session is a bug that is very hard to see.
if [ -z "${MANGOHUD_CONFIGFILE:-}" ]; then
    MANGOHUD_CONFIGFILE="${XDG_CONFIG_HOME:-$HOME/.config}/MangoHud/MangoHud.conf"
    export MANGOHUD_CONFIGFILE
fi

# ── SDL controller mappings ─────────────────────────────────────────────────
#
# Without this variable no game reads any mapping `syn-arcade map add` writes,
# and nothing anywhere reports that. The name survived the SDL2 → SDL3 rename;
# both libraries still look for it.
if [ -z "${SDL_GAMECONTROLLERCONFIG_FILE:-}" ]; then
    _syn_arcade_db="${XDG_CONFIG_HOME:-$HOME/.config}/syn-arcade/gamecontrollerdb.txt"
    # Only if it exists. Pointing SDL at a missing file is harmless in every
    # SDL version tested, but an unset variable is easier to diagnose than one
    # naming a file that is not there — `syn-arcade map path` can then say
    # "unset" rather than the user wondering why a listed path is ignored.
    [ -f "$_syn_arcade_db" ] && export SDL_GAMECONTROLLERCONFIG_FILE="$_syn_arcade_db"
    unset _syn_arcade_db
fi

# ── The three things that must happen before the session is usable ──────────
#
# 1. The overlay config file must EXIST. MangoHud adds its inotify watch once,
#    at layer init; if the path is missing at that moment the watch fails and
#    that process never sees a config change for the rest of its life — every
#    overlay keybind silently dead in that game.
#
# 2. Saved controller deadzones must be re-applied. They live in the kernel's
#    copy of the device and are destroyed when it is unplugged, so a login is
#    exactly when they need putting back.
#
# 3. The keybind block in the user's synuirc must EXIST, and must gain any key
#    a NEWER syn-arcade defines. Nothing in a package upgrade can reach a
#    user's home, so a version that adds a shortcut adds it to the defaults and
#    to blocks installed from then on, and every machine that ran `binds
#    install` under the older version keeps the keys it was born with — the
#    feature ships, the docs name the key, `binds show` prints the key, and the
#    key is not in the file. That is exactly how big screen mode shipped in
#    0.1.0-2 with no super+F10 on any machine that already had the block.
#
#    ⚠ And on a machine where nobody ever ran `binds install`, there was no
#    block to refresh and none of the three keys existed AT ALL — super+F10
#    included, which is the only key that opens big screen mode. The package
#    shipped a feature reachable only by somebody who had read the README.
#
#    `binds ensure` refreshes a block that exists and writes one where there is
#    none. It keeps every combo the user chose, refuses rather than writing a
#    key that clashes with one already in the file, writes nothing when the
#    result is byte-identical — the case at almost every login — and leaves a
#    deliberate `binds remove` alone, which it can tell apart because remove
#    leaves a marker line behind.
#
# ⚠ Backgrounded, redirected, and failure-tolerant. This is /etc/profile.d: it
# runs for every login shell including non-interactive ones, and anything that
# blocks, prints, or returns non-zero here breaks logins for everybody. `pads
# apply` in particular opens device nodes, which can stall on a wedged USB
# device.
if command -v syn-arcade >/dev/null 2>&1; then
    ( syn-arcade hud ensure          >/dev/null 2>&1
      syn-arcade pads apply          >/dev/null 2>&1
      syn-arcade binds ensure --quiet  >/dev/null 2>&1 ) &
fi
