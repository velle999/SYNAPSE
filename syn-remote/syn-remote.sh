#!/usr/bin/env bash
# syn-remote — the desktop, from somewhere else.
#
# A thin wrapper over wayvnc, which is the wlroots-native VNC server: it
# captures through zwlr_screencopy_manager_v1 and drives the seat through
# zwp_virtual_pointer_manager_v1 and zwp_virtual_keyboard_manager_v1. synui
# implements all three and hands them to any native client, so nothing here
# goes through a portal and nothing prompts.
#
# ⛔ WHICH IS WHY THE USUAL "WAYLAND CANNOT DO REMOTE DESKTOP" DOES NOT APPLY,
# and it is worth writing down because it is the first thing anybody says. The
# three real reasons that is said elsewhere are all about other stacks:
#
#   - GNOME and KDE gate capture behind a portal that asks a human, per
#     session. That is a policy, not a protocol, and it makes unattended access
#     impossible by design.
#   - xdg-desktop-portal-wlr implements ScreenCast but NOT RemoteDesktop, so
#     portal-based tools (RustDesk and friends) can watch a wlroots desktop and
#     cannot touch it. Going native sidesteps the portal entirely.
#   - Nothing exists to connect to before somebody logs in. That one is true
#     here too — see `syn-remote status`, which says so rather than pretending.
#
# ── WHAT THIS ADDS THAT wayvnc ALONE DOES NOT ────────────────────────────────
#
# ⛔ A BLANKED OUTPUT CANNOT BE CAPTURED. Measured, not assumed: with synui's
# idle blank stage fired, `grim` returns "failed to copy output" and a viewer
# sees nothing at all. power_blank_timeout defaults to 600, so an unattended
# machine goes dark to a viewer ten minutes after the last keypress and stays
# dark — the single thing that makes unattended VNC on this desktop look
# broken. So on connect this turns every output back on (wlopm) and holds a
# real idle inhibitor (synui-idle-inhibit) for as long as somebody is
# connected, releasing it when the last one leaves.
#
# ⛔ AND IT BINDS TO LOOPBACK. synnet's base firewall accepts everything from
# 10/8, 172.16/12 and 192.168/16 — read monitor.c — so "default-drop input" does
# NOT mean a port bound to 0.0.0.0 is private. It means the whole LAN can reach
# it. Loopback plus an SSH tunnel is the default; `syn-remote listen lan` is the
# deliberate way to change that, and it says what it is doing.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

PROG=syn-remote
CONF_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/syn-remote"
WAYVNC_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/wayvnc"
WAYVNC_CONF="$WAYVNC_DIR/config"
CERT="$CONF_DIR/cert.pem"
KEY="$CONF_DIR/key.pem"
SETTINGS="$CONF_DIR/syn-remote.conf"
UNIT=syn-remote.service
IDLE_INHIBIT=/usr/lib/synui/synui-idle-inhibit
# The one file anything else reads to know somebody is watching. In the runtime
# dir because it describes THIS session and must not outlive it.
STATE="${XDG_RUNTIME_DIR:-/tmp}/syn-remote.state"

DEFAULT_PORT=5900

err()  { printf '%s: %s\n' "$PROG" "$*" >&2; }
die()  { err "$*"; exit 1; }
# ⚠ TO STDERR. `PW=$(syn-remote password)` has to get the password and nothing
# else — the first run of that also makes the TLS certificate, and its progress
# line on stdout would end up inside the password.
note() { printf '  %s\n' "$*" >&2; }

have() { command -v "$1" >/dev/null 2>&1; }

# ── Settings ──────────────────────────────────────────────
#
# Ours, not wayvnc's: which address to bind and which port. wayvnc's own config
# is GENERATED from these plus the credentials, and is never hand-edited —
# saying so here rather than leaving somebody to discover that their edit was
# overwritten at the next start.
setting() {   # setting <key> <default>
    local v
    v=$(sed -n "s/^$1=//p" "$SETTINGS" 2>/dev/null | tail -1)
    printf '%s' "${v:-$2}"
}

set_setting() {   # set_setting <key> <value>
    mkdir -p "$CONF_DIR" || return 1
    # ⛔ ONE LINE PER SETTING, so a value carrying a newline would silently
    # become a second setting — and the value that can carry one is the
    # password, which somebody may paste.
    case "$2" in *$'\n'*) err "a value cannot contain a newline"; return 1 ;; esac
    local tmp="$SETTINGS.new"
    { grep -v "^$1=" "$SETTINGS" 2>/dev/null || true; printf '%s=%s\n' "$1" "$2"; } > "$tmp" || return 1
    # ⛔ BEFORE THE RENAME, AND ON EVERY WRITE. This file holds the password;
    # chmod'ing it once at creation is undone by the next write through a new
    # temporary, which is how it ended up world-readable after `listen lan`.
    chmod 600 "$tmp" 2>/dev/null
    mv -f "$tmp" "$SETTINGS"
}

bind_address() { setting address 127.0.0.1; }
bind_port()    { setting port "$DEFAULT_PORT"; }

# ── Credentials ───────────────────────────────────────────
#
# ⚠ enable_auth REQUIRES ALL THREE — certificate, private key and password
# (wayvnc(1)). There is no "password without TLS" and that is the right way
# round: VNC's own authentication is DES with an eight-character key, and a
# remote desktop is exactly the traffic nobody should be able to read.
ensure_credentials() {
    mkdir -p "$CONF_DIR" || die "cannot create $CONF_DIR"
    chmod 700 "$CONF_DIR" 2>/dev/null

    if [ ! -s "$CERT" ] || [ ! -s "$KEY" ]; then
        have openssl || die "openssl is needed to make the TLS certificate"
        note "Making a TLS certificate for this machine..."
        # Self-signed and long-lived: there is no authority to ask, the client
        # pins it on first connection, and a certificate that expires in a year
        # is a remote desktop that stops working while nobody is at the machine.
        openssl req -x509 -newkey rsa:2048 -nodes -days 3650 \
            -subj "/CN=$(hostname 2>/dev/null || echo synapseos)" \
            -keyout "$KEY" -out "$CERT" >/dev/null 2>&1 ||
            die "could not create the certificate"
        chmod 600 "$KEY" "$CERT"
    fi

    if [ -z "$(setting password "")" ]; then
        local pw
        # ⚠ From the kernel, not from $RANDOM, which is a 15-bit LCG seeded
        # from the pid and the clock — guessable by anybody who knows roughly
        # when the machine was set up.
        pw=$(tr -dc 'A-Za-z0-9' < /dev/urandom 2>/dev/null | head -c 20)
        [ ${#pw} -eq 20 ] || die "could not generate a password"
        set_setting password "$pw" || die "could not save the password"
    fi
    chmod 600 "$SETTINGS" 2>/dev/null
}

# wayvnc's config is generated, every time, from ours.
write_wayvnc_config() {
    mkdir -p "$WAYVNC_DIR" || die "cannot create $WAYVNC_DIR"
    local tmp="$WAYVNC_CONF.new"
    {
        echo "# Generated by syn-remote — edits here are overwritten."
        echo "# Change these with \`syn-remote listen\` and \`syn-remote password\`."
        printf 'address=%s\n' "$(bind_address)"
        printf 'port=%s\n'    "$(bind_port)"
        echo   'enable_auth=true'
        printf 'certificate_file=%s\n' "$CERT"
        printf 'private_key_file=%s\n' "$KEY"
        if [ "$(setting pam off)" = on ]; then
            # PAM overrides username/password and authenticates against this
            # machine's own accounts — one fewer secret to keep, and it is
            # subject to the same lockout as every other login (deny=3 in
            # /etc/pam.d/wayvnc).
            echo 'enable_pam=true'
        else
            printf 'username=%s\n' "$(setting username "${USER:-$(id -un)}")"
            printf 'password=%s\n' "$(setting password "")"
        fi
    } > "$tmp" && chmod 600 "$tmp" && mv -f "$tmp" "$WAYVNC_CONF"
}

# ── The session this needs to exist ───────────────────────
#
# There is no compositor before somebody logs in, on any Wayland desktop, so
# there is nothing for wayvnc to capture. Said plainly rather than failing with
# "failed to connect to Wayland display", which reads as a bug.
wayland_socket() {
    local f="${XDG_RUNTIME_DIR:-}/synui-display"
    [ -n "${XDG_RUNTIME_DIR:-}" ] || f=/tmp/synui-display
    if [ -r "$f" ]; then
        tr -d '[:space:]' < "$f"
    else
        printf '%s' "${WAYLAND_DISPLAY:-}"
    fi
}

# ── Running ───────────────────────────────────────────────

# The watcher. Everything this wrapper exists for happens here.
#
# ⚠ --reconnect, SO IT SURVIVES wayvnc RESTARTING under it. Without it a single
# wayvnc crash leaves the session with a running unit and no wake-on-connect
# for the rest of the login — the exact shape of failure that is invisible
# until somebody is locked out of a machine they are not standing next to.
watch_clients() {
    local count=0
    # Written before the first event, so a reader never has to tell "nobody is
    # connected" apart from "the file is not there yet".
    printf 'connections=0\n' > "$STATE" 2>/dev/null

    # The inhibitor is a coprocess fed '1'/'0' bytes — synui's own helper,
    # rather than a second implementation of an idle inhibitor here.
    local inhibit_fd=
    if [ -x "$IDLE_INHIBIT" ]; then
        exec {inhibit_fd}> >("$IDLE_INHIBIT" 2>/dev/null)
    fi

    wayvncctl --json event-receive --wait --reconnect 2>/dev/null |
    while IFS= read -r line; do
        case "$line" in
            *'"client-connected"'*|*'"client-disconnected"'*) ;;
            *) continue ;;
        esac
        # ⚠ connection_count, NOT a tally kept here. wayvnc reports the number
        # it actually has, so a missed event or a reconnect cannot leave this
        # holding an inhibitor for a client that left — which would keep the
        # screen awake until the next logout.
        local n
        n=$(printf '%s' "$line" | sed -n 's/.*"connection_count":\([0-9]*\).*/\1/p')
        [ -n "$n" ] || continue
        printf 'connections=%s\n' "$n" > "$STATE" 2>/dev/null

        if [ "$n" -gt 0 ] && [ "$count" -eq 0 ]; then
            # ⛔ THE OUTPUT FIRST. A blanked output cannot be captured at all,
            # so without this the first thing a person sees after connecting to
            # an idle machine is nothing — and there is no way to click their
            # way out of it, because there is no frame to click on.
            have wlopm && wlopm --on '*' >/dev/null 2>&1
            [ -n "$inhibit_fd" ] && printf '1' >&"$inhibit_fd" 2>/dev/null
        elif [ "$n" -eq 0 ] && [ "$count" -gt 0 ]; then
            # Released, so the machine goes back to sleeping normally. A remote
            # desktop that leaves the screen on for ever after one connection
            # is a power setting nobody agreed to.
            [ -n "$inhibit_fd" ] && printf '0' >&"$inhibit_fd" 2>/dev/null
        fi
        count=$n
    done
}

cmd_run() {
    have wayvnc || die "wayvnc is not installed"
    local sock
    sock=$(wayland_socket)
    [ -n "$sock" ] || die "no Wayland session — nothing to share yet"
    export WAYLAND_DISPLAY="$sock"

    ensure_credentials
    write_wayvnc_config

    # ⚠ BACKGROUNDED, THEN exec. The watcher has to outlive this shell and die
    # with the unit; systemd's cgroup takes care of the second, and exec'ing
    # wayvnc as the main process takes care of the first — a wrapper that
    # stayed in the middle would make `systemctl --user status` report on a
    # shell rather than on the server.
    watch_clients &
    exec wayvnc --config="$WAYVNC_CONF"
}

# ── Status ────────────────────────────────────────────────

# ⛔ ZERO UNLESS THE SERVER IS ACTUALLY RUNNING, whatever the file says. This
# is read by the bar, and a state file outliving the thing that wrote it would
# leave an indicator claiming somebody is watching this screen over a server
# that is not there — which is worse than no indicator at all, and is the
# failure synui's Recording.qml has a paragraph about avoiding.
#
# ⚠ It narrows the window rather than closing it: a watcher that died under a
# live wayvnc could still over-report until the unit is restarted. The count is
# wayvnc's own (connection_count), so that needs the event stream to break
# while the server survives it.
connections() {
    systemctl --user is-active "$UNIT" >/dev/null 2>&1 || { printf '0'; return; }
    sed -n 's/^connections=//p' "$STATE" 2>/dev/null | tail -1
}

cmd_status() {
    local active enabled n sock
    active=$(systemctl --user is-active "$UNIT" 2>/dev/null || true)
    enabled=$(systemctl --user is-enabled "$UNIT" 2>/dev/null || true)
    n=$(connections); n=${n:-0}
    sock=$(wayland_socket)

    if [ "${1:-}" = "--rec" ]; then
        # ⚠ MACHINE-READABLE AND NEVER TRANSLATED — the window matches on these
        # values. First row names the columns, as everywhere else here.
        printf 'field\tvalue\n'
        printf 'running\t%s\n'     "$([ "$active" = active ] && echo yes || echo no)"
        printf 'atlogin\t%s\n'     "$([ "$enabled" = enabled ] && echo yes || echo no)"
        printf 'connections\t%s\n' "$n"
        printf 'address\t%s\n'     "$(bind_address)"
        printf 'port\t%s\n'        "$(bind_port)"
        printf 'scope\t%s\n'       "$([ "$(bind_address)" = 127.0.0.1 ] && echo local || echo lan)"
        printf 'auth\t%s\n'        "$([ "$(setting pam off)" = on ] && echo pam || echo password)"
        printf 'session\t%s\n'     "$([ -n "$sock" ] && echo yes || echo no)"
        printf 'wayvnc\t%s\n'      "$(have wayvnc && echo yes || echo no)"
        return 0
    fi

    printf '%s\n' "SynapseOS remote desktop"
    note "Server        $([ "$active" = active ] && echo 'running' || echo 'stopped')"
    note "At login      $([ "$enabled" = enabled ] && echo 'yes' || echo 'no')"
    note "Listening on  $(bind_address):$(bind_port)$([ "$(bind_address)" = 127.0.0.1 ] && echo '   (this machine only)' || echo '   (reachable from the LAN)')"
    note "Sign in with  $([ "$(setting pam off)" = on ] && echo 'your account password (PAM)' || echo "syn-remote password")"
    [ "$n" -gt 0 ] && note "Connected     $n"
    have wayvnc || note "⚠ wayvnc is not installed — nothing can serve."
    [ -n "$sock" ] || note "⚠ No desktop session yet. Nothing exists to share until somebody logs in."
    return 0
}

cmd_address() {
    local a p
    a=$(bind_address); p=$(bind_port)
    if [ "$a" = 127.0.0.1 ]; then
        printf 'This machine only. From somewhere else, tunnel it:\n\n'
        printf '  ssh -N -L %s:localhost:%s %s@%s\n\n' "$p" "$p" "${USER:-$(id -un)}" "$(hostname 2>/dev/null || echo this-machine)"
        printf 'then point a VNC viewer at localhost:%s.\n' "$p"
        printf '\nTo listen on the network instead:  %s listen lan\n' "$PROG"
    else
        printf 'Point a VNC viewer at:\n\n'
        local ip
        ip=$(ip -4 -o addr show scope global 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | head -1)
        printf '  %s:%s\n' "${ip:-$(hostname 2>/dev/null || echo this-machine)}" "$p"
    fi
}

# ── Switching it on ───────────────────────────────────────

cmd_on() {
    have wayvnc || die "wayvnc is not installed — install it and try again"
    ensure_credentials
    write_wayvnc_config
    systemctl --user enable --now "$UNIT" >/dev/null 2>&1 ||
        die "could not start $UNIT — see: systemctl --user status $UNIT"
    printf 'Remote desktop is on, and will start with every login.\n\n'
    cmd_address
}

cmd_off() {
    systemctl --user disable --now "$UNIT" >/dev/null 2>&1
    rm -f "$STATE"
    printf 'Remote desktop is off.\n'
}

cmd_start() { systemctl --user start "$UNIT" || die "could not start $UNIT"; }
cmd_stop()  { systemctl --user stop  "$UNIT"; rm -f "$STATE"; }

# ── Where it listens ──────────────────────────────────────

cmd_listen() {
    case "${1:-}" in
        local)
            set_setting address 127.0.0.1
            printf 'Listening on this machine only. Reach it over an SSH tunnel:\n\n'
            cmd_address ;;
        lan)
            # ⚠ SAID OUT LOUD, because synnet's default-drop input chain reads
            # like protection and is not, for this: it accepts everything from
            # 10/8, 172.16/12 and 192.168/16. Binding here really does mean
            # every device on the network can reach the port.
            set_setting address 0.0.0.0
            printf 'Listening on the network.\n\n'
            note "Everything on this LAN can now reach port $(bind_port)."
            note "synnet's firewall accepts private-range sources by design, so"
            note "there is no second door to unlock — the TLS certificate and"
            note "the password are what stand between the LAN and this desktop."
            printf '\n'
            cmd_address ;;
        ""|status) printf '%s\n' "$([ "$(bind_address)" = 127.0.0.1 ] && echo local || echo lan)" ;;
        *) die "listen takes 'local' or 'lan'" ;;
    esac
    if systemctl --user is-active "$UNIT" >/dev/null 2>&1; then
        write_wayvnc_config
        systemctl --user restart "$UNIT" >/dev/null 2>&1
    fi
}

cmd_port() {
    case "${1:-}" in
        "") bind_port; return 0 ;;
        *[!0-9]*|"") die "a port is a number" ;;
    esac
    [ "$1" -ge 1 ] && [ "$1" -le 65535 ] || die "a port is 1-65535"
    set_setting port "$1"
    if systemctl --user is-active "$UNIT" >/dev/null 2>&1; then
        write_wayvnc_config
        systemctl --user restart "$UNIT" >/dev/null 2>&1
    fi
    printf 'Listening on port %s.\n' "$1"
}

# ── Who may connect ───────────────────────────────────────

cmd_password() {
    case "${1:-}" in
        new)
            set_setting password ""
            ensure_credentials
            write_wayvnc_config
            systemctl --user is-active "$UNIT" >/dev/null 2>&1 &&
                systemctl --user restart "$UNIT" >/dev/null 2>&1
            printf '%s\n' "$(setting password "")" ;;
        "")
            ensure_credentials
            printf '%s\n' "$(setting password "")" ;;
        *)
            set_setting password "$1"
            write_wayvnc_config
            systemctl --user is-active "$UNIT" >/dev/null 2>&1 &&
                systemctl --user restart "$UNIT" >/dev/null 2>&1
            printf 'Password set.\n' ;;
    esac
}

cmd_auth() {
    case "${1:-}" in
        pam)
            # ⚠ /etc/pam.d/wayvnc is pam_unix with deny=3 — the same lockout as
            # any other login on this machine, which is the point of using it.
            [ -r /etc/pam.d/wayvnc ] ||
                die "wayvnc ships no PAM policy on this machine"
            set_setting pam on
            printf 'Sign in with your own account password.\n' ;;
        password)
            set_setting pam off
            ensure_credentials
            printf 'Sign in with the generated password: %s password\n' "$PROG" ;;
        ""|status) [ "$(setting pam off)" = on ] && echo pam || echo password ;;
        *) die "auth takes 'pam' or 'password'" ;;
    esac
    if systemctl --user is-active "$UNIT" >/dev/null 2>&1; then
        write_wayvnc_config
        systemctl --user restart "$UNIT" >/dev/null 2>&1
    fi
}

usage() {
    cat <<'EOF'
syn-remote — the desktop, from somewhere else

  syn-remote on | off          start it now and at every login, or stop
  syn-remote start | stop      just this session
  syn-remote status [--rec]    what it is doing
  syn-remote address           how to connect to it
  syn-remote listen local|lan  this machine only (default), or the network
  syn-remote port [N]          which port (default 5900)
  syn-remote password [new|X]  the password a viewer is asked for
  syn-remote auth pam|password sign in with your account, or that password

The server is wayvnc; this adds the parts a wrapper has to: it wakes a blanked
screen when somebody connects (a blanked output cannot be captured at all) and
holds the machine awake while they are there.

Nothing exists to share until somebody has logged in — there is no desktop
before a login on any Wayland system.
EOF
}

# ⚠ A TEST SEAM, the same one syn-install has. Everything above is a function
# that can be driven directly; sourcing with this set stops here so a suite can
# call them without the script doing anything to this machine.
[ -n "${SYN_REMOTE_SOURCE_ONLY:-}" ] && return 0 2>/dev/null

case "${1:-status}" in
    run)        shift; cmd_run "$@" ;;
    on)         shift; cmd_on "$@" ;;
    off)        shift; cmd_off "$@" ;;
    start)      shift; cmd_start "$@" ;;
    stop)       shift; cmd_stop "$@" ;;
    status)     shift; cmd_status "$@" ;;
    address)    shift; cmd_address "$@" ;;
    listen)     shift; cmd_listen "$@" ;;
    port)       shift; cmd_port "$@" ;;
    password)   shift; cmd_password "$@" ;;
    auth)       shift; cmd_auth "$@" ;;
    -h|--help|help) usage ;;
    *)          err "unknown command: $1"; usage; exit 1 ;;
esac
