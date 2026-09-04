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
# How long `wake` and `connect` wait for a machine to start answering. A desktop
# with a spinning disk and a NVIDIA card takes its time coming out of S3, and a
# wait that gives up first turns a wake that WORKED into an error message.
WAKE_WAIT=${SYN_REMOTE_WAKE_WAIT:-60}

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

# ── The names the certificate vouches for ─────────────────
#
# ⛔ A CERTIFICATE IS CHECKED AGAINST THE ADDRESS THAT WAS DIALLED, so the only
# addresses worth putting in it are the ones somebody will type. The generated
# set is every address this machine HOLDS, which covers the LAN and covers
# nothing else — see the block in cert_sans. These are the extras, kept as one
# comma-separated setting.

# ⛔ STRICT, BECAUSE THIS VALUE REACHES openssl AS PART OF -addext. A comma
# would forge a second SAN, and anything outside this set has no business in a
# hostname or an address anyway. A leading dash would be read as an option by
# whatever the value is eventually passed to — the same rule valid_name follows.
valid_san() {
    case "${1:-}" in
        ""|-*)                     return 1 ;;
        *[!A-Za-z0-9.:_-]*)        return 1 ;;
    esac
    return 0
}

# IP or DNS, as openssl's -addext spells it.
san_kind() {
    case "$1" in
        *:*)                          printf 'IP'  ;;   # IPv6
        *[!0-9.]*)                    printf 'DNS' ;;
        [0-9]*.[0-9]*.[0-9]*.[0-9]*)  printf 'IP'  ;;
        *)                            printf 'DNS' ;;
    esac
}

# ⚠ THE SAME ANSWER, SPELLED THE OTHER WAY. openssl WRITES `IP:1.2.3.4` and
# READS IT BACK as `IP Address:1.2.3.4`; using one spelling for both is how a
# name that is present reads as missing and re-issues the certificate on every
# single run.
san_read_label() {
    [ "$(san_kind "$1")" = IP ] && printf 'IP Address' || printf 'DNS'
}

# ⛔ ANCHORED, WHOLE-FIELD. openssl prints one line — "DNS:synapse,
# DNS:synapse.local, IP Address:127.0.0.1" — so a plain grep for "DNS:synapse"
# is also satisfied by "DNS:synapse.local", and a name that is genuinely absent
# reads as present. Split on the comma and match the whole field instead.
cert_has_san() {   # cert_has_san <label> <value>
    openssl x509 -in "$CERT" -noout -ext subjectAltName 2>/dev/null |
        tr ',' '\n' | sed 's/^[[:space:]]*//; s/[[:space:]]*$//' |
        grep -qxF "$1:$2"
}

# The configured extras, one per line.
extra_sans() {
    setting names "" | tr ',' '\n' | sed '/^[[:space:]]*$/d'
}

# ── Credentials ───────────────────────────────────────────
#
# ⚠ enable_auth REQUIRES ALL THREE — certificate, private key and password
# (wayvnc(1)). There is no "password without TLS" and that is the right way
# round: VNC's own authentication is DES with an eight-character key, and a
# remote desktop is exactly the traffic nobody should be able to read.
ensure_credentials() {
    mkdir -p "$CONF_DIR" || die "cannot create $CONF_DIR"
    chmod 700 "$CONF_DIR" 2>/dev/null

    # ⛔ EVERY NAME AND ADDRESS A CLIENT MIGHT DIAL, AS A subjectAltName.
    #
    # A TLS client checks the certificate against the address it CONNECTED to
    # — gtk-vnc calls gnutls_x509_crt_check_hostname — so a certificate that
    # does not carry that address cannot be validated no matter how correct
    # everything else is. The first version of this had no SAN at all and a CN
    # of `hostname`, which is worth two separate warnings:
    #
    #   ⛔ `hostname` IS NOT INSTALLED ON SynapseOS. The fallback in
    #     `$(hostname || echo synapseos)` therefore always fired, so every
    #     certificate this ever produced was issued to the literal name
    #     "synapseos" — a name nothing on any network resolves to. `uname -n`
    #     is coreutils and always answers.
    #
    #   ⛔ AND NOBODY DIALS A HOSTNAME ANYWAY. `syn-remote address` tells
    #     people to point a viewer at an IP, so the IP is the name that has to
    #     be in the certificate. A CN alone would not have been enough even
    #     with the right hostname in it.
    #
    # Measured: with the old certificate, validating as 127.0.0.1 or as
    # 192.168.1.50 both failed with "IP address mismatch"; only the literal
    # "synapseos" passed. The server was fine, the firewall was fine, and every
    # correct client hung at the handshake.
    cert_sans() {
        local host sans
        host=$(uname -n 2>/dev/null || echo synapse)
        sans="DNS:$host,DNS:$host.local,DNS:localhost,IP:127.0.0.1"
        local ip
        for ip in $(ip -4 -o addr show scope global 2>/dev/null |
                    awk '{print $4}' | cut -d/ -f1); do
            sans="$sans,IP:$ip"
        done
        # ⛔ AND THE NAMES THIS MACHINE CANNOT WORK OUT FOR ITSELF. Everything
        # above is an address the box actually holds, which is exactly the set
        # that is WRONG the moment it is reached through anything else: a port
        # forward presents a public IP the box has never seen, and a dynamic-DNS
        # name resolves to one. A viewer validates against the address it
        # DIALLED, so neither can ever match a certificate built only from local
        # addresses — measured: dialling 203.0.113.7 or a DDNS name against this
        # server fails "IP address mismatch" / "Hostname mismatch" while the LAN
        # address passes. `syn-remote names add` is how that gap is closed.
        local extra
        for extra in $(extra_sans); do
            sans="$sans,$(san_kind "$extra"):$extra"
        done
        printf '%s' "$sans"
    }

    # ⚠ AND IT HAS TO BE RE-ISSUED WHEN THE ADDRESS MOVES. A DHCP lease change
    # silently invalidates a certificate that was correct when it was made, and
    # the symptom is identical to the bug above: every client hangs. Cheap to
    # check, and openssl is already a dependency.
    local want need_new=0
    [ -s "$CERT" ] && [ -s "$KEY" ] || need_new=1
    if [ "$need_new" -eq 0 ]; then
        for want in $(ip -4 -o addr show scope global 2>/dev/null |
                      awk '{print $4}' | cut -d/ -f1); do
            cert_has_san "IP Address" "$want" || need_new=1
        done
        # ⚠ THE CONFIGURED NAMES ARE PART OF THE SAME QUESTION. Adding one has
        # to re-issue, or the setting would be saved, reported, and carried by
        # no certificate — the shape of failure this package already has a
        # paragraph about.
        for want in $(extra_sans); do
            cert_has_san "$(san_read_label "$want")" "$want" || need_new=1
        done
    fi

    if [ "$need_new" -eq 1 ]; then
        have openssl || die "openssl is needed to make the TLS certificate"
        [ -s "$CERT" ] && note "This machine's address changed — re-issuing the TLS certificate."
        note "Making a TLS certificate for this machine..."
        # Self-signed and long-lived: there is no authority to ask, the client
        # pins it on first connection, and a certificate that expires in a year
        # is a remote desktop that stops working while nobody is at the machine.
        openssl req -x509 -newkey rsa:2048 -nodes -days 3650 \
            -subj "/CN=$(uname -n 2>/dev/null || echo synapse)" \
            -addext "subjectAltName=$(cert_sans)" \
            -keyout "$KEY" -out "$CERT" >/dev/null 2>&1 ||
            die "could not create the certificate"
        chmod 600 "$KEY" "$CERT"
        # ⚠ A RE-ISSUED CERTIFICATE IS A NEW ONE, and every client that pinned
        # the old one will refuse it — correctly, since that is what pinning is
        # for. Said here so the reason is in the journal when it happens.
        note "Clients that already trust this machine must accept the new certificate:"
        note "  syn-remote trust <name> --renew"
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

# ⛔ WAIT FOR THE COMPOSITOR, DO NOT DIE ON IT. The user manager reaches
# default.target BEFORE synui exists. Measured on an installed system: velle's
# `systemd --user` reached Main User Target at 19:40:50 and had not yet been
# handed a session, so $XDG_RUNTIME_DIR/synui-display was not there to read.
# `run` used to die at that instant, and with Restart=on-failure/RestartSec=3
# against systemd's DEFAULT start limit — five starts in ten seconds — the unit
# burned every retry inside the race and then gave up PERMANENTLY. Enabled, and
# dead for the rest of the login.
#
# That is the whole of "it says it is on but it is not running until I switch
# it off and on again": `syn-remote on` does `enable --now`, which starts the
# unit at a moment when the compositor is already up, so the toggle appears to
# be the fix when it is only better timing.
#
# ⚠ AND THE USER MANAGER OUTLIVES THE SESSION. It is still the same process
# across a logout and a fresh login, so default.target is never reached a
# second time and a later login cannot re-trigger the unit either. Waiting here
# is what makes the unit FOLLOW the session instead of racing it once: when a
# session ends wayvnc exits, systemd restarts the unit, and this loop parks
# until somebody logs in again.
#
# ⚠ BOUNDED, NOT INFINITE. A unit sitting `active` for ever with no server
# behind it would make `syn-remote status` — and the bar, which reads it —
# report a running remote desktop that is not there, which is the one lie this
# package has a paragraph about not telling. On a timeout it exits instead, and
# systemd restarts it, so the waiting is visible in the journal rather than
# hidden inside one long-lived process.
SESSION_WAIT=${SYN_REMOTE_SESSION_WAIT:-90}

# ⚠ THE SOCKET ON STDOUT, EVERYTHING ELSE ON STDERR — `sock=$(wait_for_session)`
# has to capture the socket and nothing else, the same rule `note` follows.
wait_for_session() {
    local sock waited=0
    sock=$(wayland_socket)
    [ -n "$sock" ] && { printf '%s' "$sock"; return 0; }

    err "no desktop session yet — waiting up to ${SESSION_WAIT}s for one"
    while [ "$waited" -lt "$SESSION_WAIT" ]; do
        sleep 1
        waited=$((waited + 1))
        sock=$(wayland_socket)
        [ -n "$sock" ] && {
            err "session appeared after ${waited}s"
            printf '%s' "$sock"
            return 0
        }
    done
    return 1
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
    sock=$(wait_for_session) ||
        die "no Wayland session after ${SESSION_WAIT}s — nothing to share yet"
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
    note "Waking it     $PROG wakeable"
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

# ── The names the certificate answers to ──────────────────

# Re-issue if anything above changed, put it in wayvnc's config, and restart a
# running server so it is serving the certificate that was just made rather
# than the one it started with.
# ⚠ SAYS NOTHING ITSELF. ensure_credentials already explains a re-issue, and
# telling somebody twice that their clients must re-trust reads as two events.
reissue() {
    ensure_credentials
    write_wayvnc_config
    if systemctl --user is-active "$UNIT" >/dev/null 2>&1; then
        systemctl --user restart "$UNIT" >/dev/null 2>&1
    fi
    return 0
}

cmd_names() {
    local action=${1:-} name=${2:-}
    case "$action" in
        ""|list)
            local have
            have=$(extra_sans)
            if [ -n "$have" ]; then
                printf 'The certificate also vouches for:\n\n'
                printf '%s\n' "$have" | sed 's/^/  /'
                printf '\n'
            else
                printf 'The certificate vouches for this machine'"'"'s own addresses only.\n\n'
            fi
            note "Always included: $(uname -n 2>/dev/null || echo synapse), localhost,"
            note "and every address this machine holds."
            # ⛔ SAID EVERY TIME, because the failure it prevents is silent and
            # looks like a firewall. A viewer validates against what it DIALLED,
            # so an address this machine cannot see — a public IP in front of a
            # port forward, a dynamic-DNS name — is not in there and cannot be.
            note ""
            note "An address reached through a port forward, or a dynamic-DNS name,"
            note "is NOT one of those and has to be named here:"
            note "  $PROG names add myhouse.duckdns.org"
            ;;
        add)
            valid_san "$name" ||
                die "a name is letters, digits, dot, colon, dash or underscore"
            if extra_sans | grep -qxF "$name"; then
                note "$name is already in the list."
            else
                local cur
                cur=$(setting names "")
                set_setting names "${cur:+$cur,}$name" ||
                    die "could not save the name"
            fi
            # ⚠ UNCONDITIONALLY, even when the name was already saved. A setting
            # that is stored and a certificate that carries it are two facts, and
            # repairing the second here is what stops `names` reporting a name
            # that nothing vouches for.
            reissue
            printf 'The certificate now vouches for %s.\n' "$name"
            ;;
        remove|rm)
            valid_san "$name" || die "no such name"
            extra_sans | grep -qxF "$name" ||
                die "'$name' is not one of the added names"
            local kept
            kept=$(extra_sans | grep -vxF "$name" | paste -sd, -)
            set_setting names "$kept" || die "could not save the change"
            # ⛔ FORCED. Removing a name means the certificate must stop vouching
            # for it, and nothing else here would notice: the re-issue check asks
            # whether every WANTED name is present, and a name that is no longer
            # wanted is still present quite happily.
            rm -f "$CERT" "$KEY"
            reissue
            printf 'The certificate no longer vouches for %s.\n' "$name"
            ;;
        *)  die "names takes nothing, 'add <name>' or 'remove <name>'" ;;
    esac
}

# ── Being woken ───────────────────────────────────────────
#
# The last gap in "reach this desktop from somewhere else": a machine that is
# asleep answers nothing at all. Wake-on-LAN closes it — the network card stays
# powered and watches for one magic packet — but it is off by default on most
# cards and there is no obvious place to switch it on.
#
# ⚠ TWO MECHANISMS, TWO JOBS, AND NOT A CHOICE BETWEEN THEM.
#   NetworkManager REMEMBERS: `802-3-ethernet.wake-on-lan` is re-applied every
#   time the connection is activated, which is what survives a reboot, a cable
#   replug and a driver that resets the flag on link-down.
#   The helper APPLIES: it sets the flag on the live interface now, without
#   activating anything — because activating the connection you are reaching
#   this machine over drops the link underneath you.
# Setting only the first leaves the machine unwakeable until its next reboot;
# setting only the second loses it AT that reboot.
WOL_HELPER="${SYN_REMOTE_WOL_HELPER:-/usr/lib/syn-remote/syn-remote-wol}"
# ⚠ A TEST SEAM, and it redirects a WRITE — so it is named after the tool it
# stands in for and is never used on a path that escalates. Neither the helper
# nor nmcli is run through pkexec by the suite: the suite runs the wrapper's
# own logic against stand-ins and asserts the live connection is untouched.
NMCLI="${SYN_REMOTE_NMCLI:-nmcli}"
# ⚠ A TEST SEAM FOR A READ ONLY, and the same trick synui's lid test uses for
# /sys/class/power_supply: which interface a magic packet would arrive on is a
# decision made by walking sysfs, and a decision that picks `docker0` is a
# switch that arms the wrong card and reports success. It is reachable in a
# fixture; the shipped default is the real one.
NET_DIR="${SYN_REMOTE_NET_DIR:-/sys/class/net}"

# The interface a magic packet would arrive on: wired, and carrying a link.
#
# ⛔ WIRED ONLY, said out loud rather than quietly skipped. Wi-Fi wake
# (WoWLAN) needs the card to stay associated through suspend, and on the cards
# in these machines it does not — offering the switch for a wlan device would
# be offering something that cannot work.
wired_iface() {
    local d
    for d in "$NET_DIR"/*; do
        [ -e "$d/device" ] || continue          # skip lo, bridges, veth, docker0
        [ -e "$d/wireless" ] && continue
        [ -e "$d/phy80211" ] && continue
        # DEVTYPE is set for bridges, vlans and bonds, which have a `device`
        # link of their own on some kernels; a plain NIC has none.
        grep -q '^DEVTYPE=' "$d/uevent" 2>/dev/null && continue
        [ "$(cat "$d/carrier" 2>/dev/null)" = 1 ] || continue
        printf '%s' "${d##*/}"
        return 0
    done
    return 1
}

# `field<TAB>value` from the helper, escalating ONCE if the kernel says the
# read needs privilege — which it does for everybody but root, because the same
# ioctl can return a SecureOn password.
#
# ⚠ ON THE EXIT CODE, NOT ON `id -u`. Asking "am I root" is asking the wrong
# question: the answer that matters is whether the ioctl was refused, and a
# test rig, a container or a machine with the capability granted gets through
# without a polkit round trip it does not need.
wol_query() {   # wol_query <iface>
    local out rc
    out=$("$WOL_HELPER" get "$1" 2>/dev/null); rc=$?
    if [ "$rc" -eq 77 ] && have pkexec; then
        out=$(pkexec "$WOL_HELPER" get "$1" 2>/dev/null); rc=$?
    fi
    [ "$rc" -eq 0 ] || return "$rc"
    printf '%s\n' "$out"
}

wol_apply() {   # wol_apply <iface> on|off
    local rc
    "$WOL_HELPER" set "$1" "$2" >/dev/null 2>&1; rc=$?
    if [ "$rc" -eq 77 ] && have pkexec; then
        pkexec "$WOL_HELPER" set "$1" "$2" >/dev/null 2>&1; rc=$?
    fi
    return "$rc"
}

# The NetworkManager connection carrying an interface, if NM is what manages it.
nm_connection() {   # nm_connection <iface>
    have "$NMCLI" || return 1
    "$NMCLI" -t -f NAME,DEVICE connection show --active 2>/dev/null |
        awk -F: -v d="$1" '$2==d {print $1; found=1; exit} END {exit !found}'
}

# What NM will re-apply at the next activation: magic, none, or default
# (= "leave whatever the driver came up with", which is a coin toss).
nm_wol_setting() {   # nm_wol_setting <connection>
    have "$NMCLI" || return 1
    "$NMCLI" -t -f 802-3-ethernet.wake-on-lan connection show "$1" 2>/dev/null |
        sed 's/^[^:]*://'
}

cmd_wakeable() {
    local iface con want=${1:-status}
    case "$want" in on|off|status|--rec|"") ;; *) die "wakeable takes 'on', 'off' or nothing" ;; esac
    [ -n "$want" ] || want=status
    [ "$want" = --rec ] && want=status

    iface=$(wired_iface) || die "no wired network interface with a link — a magic packet has nowhere to arrive"
    con=$(nm_connection "$iface" || true)

    if [ "$want" != status ]; then
        local on=off; [ "$want" = on ] && on=on
        # ⛔ THE LIVE FLAG FIRST. If the card cannot do magic packets at all,
        # the helper says so and exits 2, and nothing has been written to a
        # NetworkManager profile that would claim otherwise for ever.
        local rc; wol_apply "$iface" "$on"; rc=$?
        case "$rc" in
            0)  ;;
            2)   die "$iface cannot be woken by a magic packet — the card does not support it" ;;
            77)  die "not allowed to change it from here — this needs a session at the machine itself" ;;
            # ⛔ NAMED, because this is what a half-installed package looks like
            # from here and "could not change the flag" would send somebody to
            # look at their network card.
            127) die "the helper is missing: $WOL_HELPER — reinstall syn-remote" ;;
            *)   die "could not change the wake flag on $iface" ;;
        esac
        if [ -n "$con" ]; then
            "$NMCLI" connection modify "$con" \
                  802-3-ethernet.wake-on-lan "$([ "$on" = on ] && echo magic || echo none)" \
                  >/dev/null 2>&1 ||
                note "⚠ set on $iface now, but NetworkManager would not remember it — it will be lost at the next reboot."
        else
            note "⚠ $iface is not managed by NetworkManager, so this is set until the next reboot only."
        fi
    fi

    # ── What it actually is now, asked rather than assumed ──
    local q supported magic
    q=$(wol_query "$iface" || true)
    supported=$(printf '%s\n' "$q" | awk -F'\t' '$1=="supported"{print $2}')
    magic=$(printf '%s\n' "$q" | awk -F'\t' '$1=="magic"{print $2}')

    if [ "${1:-}" = "--rec" ] || [ "${SYN_REMOTE_REC:-}" = 1 ]; then
        printf 'field\tvalue\n'
        printf 'interface\t%s\n' "$iface"
        printf 'supported\t%s\n' "${supported:-unknown}"
        printf 'armed\t%s\n'     "${magic:-unknown}"
        printf 'remembered\t%s\n' "$(if [ -n "$con" ]; then nm_wol_setting "$con"; else echo none; fi)"
        printf 'mac\t%s\n'       "$(cat "$NET_DIR/$iface/address" 2>/dev/null)"
        return 0
    fi

    case "$magic" in
        yes) printf 'This machine can be woken over the network.\n\n' ;;
        no)  printf 'This machine cannot be woken over the network.\n\n' ;;
        *)   printf 'Cannot tell whether this machine can be woken.\n\n' ;;
    esac
    note "Interface     $iface  ($(cat "$NET_DIR/$iface/address" 2>/dev/null))"
    # ⛔ THREE ANSWERS, NOT TWO. "could not ask" is not "no": a helper that is
    # missing or refused would otherwise be reported as a card that cannot be
    # woken, which is a hardware verdict this has no business reaching.
    note "Card can      $(case "$supported" in
            yes) echo 'wake on a magic packet' ;;
            no)  echo 'not wake on a magic packet' ;;
            *)   echo 'not known — the card could not be asked' ;;
        esac)"
    note "Right now     $(case "$magic" in
            yes) echo 'armed' ;;
            no)  echo 'not armed' ;;
            *)   echo 'not known' ;;
        esac)"
    [ -n "$magic" ] || note "⚠ $WOL_HELPER could not be run, so this is what is remembered, not what is set." 
    # ⚠ REPORTED SEPARATELY FROM "armed", because they disagree in the case
    # that matters: armed now, forgotten at the next boot, which reads as
    # working right up until the reboot nobody connects to it after.
    if [ -n "$con" ]; then
        local remembered; remembered=$(nm_wol_setting "$con")
        note "Next boot     $(case "$remembered" in
                magic) echo 'still armed' ;;
                none)  echo 'not armed' ;;
                *)     echo "whatever the driver decides (NetworkManager: ${remembered:-default})" ;;
            esac)"
    fi
    [ "$magic" = yes ] || { note ""; note "Switch it on:  $PROG wakeable on"; return 0; }

    note ""
    note "From another machine on this network:"
    note "  syn-remote wake <name>          (a saved connection)"
    note "Anything that sends a magic packet to $(cat "$NET_DIR/$iface/address" 2>/dev/null) will do."
    note ""
    # ⛔ SAID EVERY TIME IT IS ON, because these are the two reasons it is
    # armed and still does not work, and neither of them is visible from here.
    note "A magic packet is a broadcast: it does not cross a router, so the"
    note "machine sending it has to be on this network. And a machine that was"
    note "shut down rather than suspended also needs Wake-on-LAN enabled in its"
    note "own firmware setup."
    return 0
}

# ── The other end: connecting OUT ─────────────────────────
#
# Everything above serves THIS machine. Everything below reaches somebody
# else's, because a remote desktop that only answers the door is half a tool —
# and the half that is missing is the one a person uses from the couch.
#
# ⛔ THE VIEWER IS OURS, AND THAT IS NOT NOT-INVENTED-HERE. It is the only way
# a saved password can ever be used. wayvnc authenticates over VeNCrypt — a
# username and a password carried INSIDE the TLS session — and no packaged
# client can be handed either one without a human typing it:
#
#   - TigerVNC's `vncviewer -passwd FILE` is the obfuscated file used by
#     classic VncAuth (RFB security type 2). It is not consulted for VeNCrypt
#     Plain, and vncviewer(1) documents no way to supply a username at all.
#   - gtk-vnc's own gvncviewer builds a dialog in its credential callback.
#
# So a connection manager wrapping either of them would remember a password it
# could never use, and prompt anyway — a padlock drawn on a door that does not
# lock. syn-remote-view is a GtkVncDisplay and a credential callback that
# answers from this file instead of from a dialog, which is about two hundred
# lines and is the entire reason the feature is real.

HOSTS="$CONF_DIR/hosts"
# ⛔ TAB IS AN IFS *WHITESPACE* CHARACTER, so `IFS=$'\t' read` treats a RUN of
# tabs as one separator and an EMPTY FIELD SIMPLY DISAPPEARS — every field
# after it shifts one column left, silently. A connection saved with no user
# name is exactly that record, and it read back with the NEXT column's value in
# the user's place: the TUI showed the wrong password state for such a host
# long before there was a mac column for it to eat.
#
# ⚠ So every record is re-separated onto a NON-whitespace character before it
# is read. US (0x1f) is the ASCII unit separator, it is what the character is
# for, and `add` refuses a value containing a control character — so it cannot
# appear inside a field and be mistaken for the boundary between two.
SEP=$'\x1f'
SECRETS="$CONF_DIR/secrets"
VIEWER=/usr/lib/syn-remote/syn-remote-view
GETCERT=/usr/lib/syn-remote/syn-remote-getcert
# ⛔ ONE PINNED CERTIFICATE PER SAVED CONNECTION, not one trust store. wayvnc's
# certificate is self-signed, so there is no authority to check it against and
# the only meaningful question is "is this the same machine I trusted before".
# A shared store would answer that for the wrong host the moment two machines
# were saved.
PINS="$CONF_DIR/certs"

# ⛔ A NAME IS A KEY IN THREE PLACES — this file, the keyring attribute, and the
# window title — so it is restricted rather than escaped. A tab would split a
# record, a newline would forge one, and a leading dash would be read as an
# option by whatever the name is eventually passed to.
valid_name() {
    case "${1:-}" in
        ""|-*)          return 1 ;;
        *[!A-Za-z0-9._-]*) return 1 ;;
    esac
    return 0
}

hosts_init() {
    mkdir -p "$CONF_DIR" || die "cannot create $CONF_DIR"
    chmod 700 "$CONF_DIR" 2>/dev/null
    [ -e "$HOSTS" ] || { : > "$HOSTS"; }
    chmod 600 "$HOSTS" 2>/dev/null
}

# One record, by name. Prints `host<TAB>port<TAB>user<TAB>mac` or nothing.
#
# ⚠ FOUR FIELDS EVEN FOR A RECORD SAVED WITH THREE. awk prints an absent field
# as empty, so a hosts file written before there was a MAC column reads back
# with an empty one rather than short — and every caller can split into four
# variables without asking how old the file is.
host_record() {
    [ -r "$HOSTS" ] || return 1
    awk -F'\t' -v n="$1" '$1==n {print $2 "\t" $3 "\t" $4 "\t" $5; found=1; exit}
                          END {exit !found}' "$HOSTS"
}

# ── The magic packet ──────────────────────────────────────

# ⛔ SIX PAIRS OF HEX, and nothing else, because this string is expanded into
# the packet a machine is woken with and is read back out of a file that a
# person edits. Both separators are accepted because both are what gets pasted:
# `ip link` prints colons, a router's DHCP table often prints dashes.
valid_mac() {
    case "${1:-}" in
        [0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]) return 0 ;;
        [0-9A-Fa-f][0-9A-Fa-f]-[0-9A-Fa-f][0-9A-Fa-f]-[0-9A-Fa-f][0-9A-Fa-f]-[0-9A-Fa-f][0-9A-Fa-f]-[0-9A-Fa-f][0-9A-Fa-f]-[0-9A-Fa-f][0-9A-Fa-f]) return 0 ;;
    esac
    return 1
}

# The hardware address a name or an address currently answers from, out of the
# neighbour table — so `add` can fill the column in while the machine is still
# awake, which is the only moment it can be learned at all.
#
# ⚠ A PING FIRST. The neighbour table only holds what this machine has recently
# talked to, so a host that has just been named to us is usually not in it; one
# packet puts it there. A failure is not an error — a machine that is already
# asleep, or behind a router, simply has no entry to learn, and `add` says so
# rather than refusing to save the connection.
mac_of_host() {   # mac_of_host <host>
    have ip || return 1
    local ip4
    ip4=$(getent ahostsv4 "$1" 2>/dev/null | awk 'NR==1{print $1}')
    [ -n "$ip4" ] || return 1
    have ping && ping -c1 -W1 -n "$ip4" >/dev/null 2>&1
    ip neigh show "$ip4" 2>/dev/null |
        awk '{for (i=1;i<NF;i++) if ($i=="lladdr") {print $(i+1); found=1; exit}}
             END {exit !found}'
}

# ⚠ python3, WHICH IS ALREADY A DEPENDENCY (it is the certificate fetcher), and
# not a new one: bash cannot send this. A magic packet goes to the BROADCAST
# address, and a socket has to have SO_BROADCAST set before the kernel will let
# it — which `/dev/udp` gives no way to do. There is no wakeonlan package in the
# dependency list for the same reason there is no ethtool in it.
#
# ⛔ SENT TWICE, to the broadcast address AND to the host's own address. The
# first is what reaches a sleeping machine; the second is what reaches one whose
# address the switch still remembers, on a network where broadcast is filtered.
# Neither is reliable alone and both are one UDP datagram.
magic_packet() {   # magic_packet <mac> [host]
    have python3 || { err "python3 is missing — nothing here can send a magic packet"; return 1; }
    python3 - "$1" "${2:-}" <<'PY'
import socket, sys

mac = sys.argv[1].replace(":", "").replace("-", "")
host = sys.argv[2] if len(sys.argv) > 2 else ""
packet = b"\xff" * 6 + bytes.fromhex(mac) * 16

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
sent = 0
# Port 9 (discard) and port 7 (echo) are both conventional for this; a card
# watching for a magic packet does not care which, and some home routers
# forward only one of them.
for dest in filter(None, ["255.255.255.255", host]):
    for port in (9, 7):
        try:
            s.sendto(packet, (dest, port))
            sent += 1
        except OSError:
            pass
sys.exit(0 if sent else 1)
PY
}

# ── Where a password lives ────────────────────────────────
#
# ⛔ NEVER BRANCH ON secret-tool's EXIT STATUS. With no keyring daemon running
# it prints "The name is not activatable" to stderr and EXITS 0 — measured on
# this desktop, which has secret-tool installed and no daemon started. For a
# credential store that is the worst failure available: telling somebody their
# password is saved when there is no password anywhere. So every write is read
# back and compared byte for byte, and the answer to "where is it" is whichever
# store actually produced the bytes.
#
# ⚠ AND THE FALLBACK IS A FILE, DELIBERATELY. A keyring-only store would make
# this feature do nothing at all on a machine with no keyring daemon — which is
# the default SynapseOS install. The file is 0600 inside a 0700 directory,
# exactly where this package already keeps the SERVER's password, and it is
# base64 so a password containing a tab or a newline survives the round trip.
# ⛔ base64 IS NOT ENCRYPTION and nothing here pretends otherwise: `hosts` names
# the store for every entry, so a person can see which of their passwords is in
# a keyring and which is in a file on disk.
secret_keyring_get() {
    have secret-tool || return 1
    secret-tool lookup service syn-remote host "$1" 2>/dev/null
}

secret_file_get() {
    [ -r "$SECRETS" ] || return 1
    local b64
    b64=$(awk -F'\t' -v n="$1" '$1==n {print $2; exit}' "$SECRETS")
    [ -n "$b64" ] || return 1
    printf '%s' "$b64" | base64 -d 2>/dev/null
}

# keyring | file | none
secret_where() {
    [ -n "$(secret_keyring_get "$1")" ] && { printf 'keyring'; return 0; }
    [ -n "$(secret_file_get    "$1")" ] && { printf 'file';    return 0; }
    printf 'none'
}

secret_get() {
    local v
    v=$(secret_keyring_get "$1"); [ -n "$v" ] && { printf '%s' "$v"; return 0; }
    v=$(secret_file_get    "$1"); [ -n "$v" ] && { printf '%s' "$v"; return 0; }
    return 1
}

secret_file_put() {   # secret_file_put <name> <password>
    hosts_init
    local tmp="$SECRETS.new"
    { grep -v "^$1	" "$SECRETS" 2>/dev/null || true
      printf '%s\t%s\n' "$1" "$(printf '%s' "$2" | base64 -w0)"
    } > "$tmp" || return 1
    # ⛔ BEFORE THE RENAME, AND ON EVERY WRITE — the same rule set_setting
    # learned the hard way. chmod'ing once at creation is undone by the next
    # write through a fresh temporary.
    chmod 600 "$tmp" 2>/dev/null
    mv -f "$tmp" "$SECRETS"
}

secret_put() {   # secret_put <name> <password>; prints where it landed
    if have secret-tool; then
        printf '%s' "$2" | secret-tool store --label="syn-remote: $1" \
            service syn-remote host "$1" >/dev/null 2>&1
        # THE READ-BACK. Not the exit status — see the block comment above.
        if [ "$(secret_keyring_get "$1")" = "$2" ]; then
            # Any older copy on disk is now a stale second answer to the same
            # question, and the one that would be found first if the keyring
            # were ever locked. Removed rather than left to disagree.
            secret_file_clear "$1"
            printf 'keyring'; return 0
        fi
    fi
    secret_file_put "$1" "$2" || return 1
    printf 'file'
}

secret_file_clear() {
    [ -r "$SECRETS" ] || return 0
    local tmp="$SECRETS.new"
    grep -v "^$1	" "$SECRETS" 2>/dev/null > "$tmp"
    chmod 600 "$tmp" 2>/dev/null
    mv -f "$tmp" "$SECRETS"
}

secret_clear() {
    have secret-tool && secret-tool clear service syn-remote host "$1" >/dev/null 2>&1
    secret_file_clear "$1"
}

# ── The saved connections ─────────────────────────────────

cmd_hosts() {
    hosts_init
    if [ "${1:-}" = "--tsv" ]; then
        # ⚠ MACHINE-READABLE AND NEVER TRANSLATED — the window and the TUI both
        # match on these values. First row names the columns, as everywhere
        # else here.
        # ⚠ mac is APPENDED, not inserted. The window and the TUI both index
        # these columns by position, and a column added in the middle would
        # silently shift every one after it — a saved password reading as a
        # port number, in a build where nothing failed.
        printf 'name\thost\tport\tuser\tsecret\tpinned\tmac\n'
        while IFS="$SEP" read -r n h p u m; do
            [ -n "$n" ] || continue
            printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$n" "$h" "$p" "$u" \
                   "$(secret_where "$n")" \
                   "$([ -s "$(pin_path "$n")" ] && echo yes || echo no)" \
                   "$m"
        done < <(tr '\t' "$SEP" < "$HOSTS")
        return 0
    fi

    if [ ! -s "$HOSTS" ]; then
        printf 'No saved connections.\n\n'
        note "Add one:  $PROG add <name> <host>[:port] [user]"
        return 0
    fi
    printf 'Saved connections\n'
    while IFS="$SEP" read -r n h p u m; do
        [ -n "$n" ] || continue
        local where; where=$(secret_where "$n")
        note "$(printf '%-16s %s:%s%s  [%s%s]' "$n" "$h" "$p" \
                 "$([ -n "$u" ] && printf ' as %s' "$u")" \
                 "$(case $where in
                        keyring) echo 'password in the keyring' ;;
                        file)    echo 'password in a file on disk' ;;
                        *)       echo 'no password saved' ;;
                    esac)" \
                 "$([ -n "$m" ] && printf ', can be woken')")"
    done < <(tr '\t' "$SEP" < "$HOSTS")
}

cmd_add() {
    # --mac is pulled out first so it can be written anywhere in the line, and
    # the positional arguments keep the shape they have always had.
    local args=() mac=""
    while [ $# -gt 0 ]; do
        case "$1" in
            --mac) mac=${2:-}; shift 2 || true ;;
            *)     args+=("$1"); shift ;;
        esac
    done
    set -- "${args[@]+"${args[@]}"}"

    local name=${1:-} target=${2:-} user=${3:-}
    valid_name "$name" || die "a name is letters, digits, dot, dash or underscore"
    [ -n "$target" ] || die "usage: $PROG add <name> <host>[:port] [user] [--mac <address>]"
    [ -z "$mac" ] || valid_mac "$mac" || die "a hardware address is six pairs of hex, like bc:fc:e7:e8:fd:e3"

    local host port
    case "$target" in
        *:*) host=${target%:*}; port=${target##*:} ;;
        *)   host=$target;      port=$DEFAULT_PORT ;;
    esac
    [ -n "$host" ] || die "no host in '$target'"
    case "$port" in *[!0-9]*|"") die "a port is a number" ;; esac
    [ "$port" -ge 1 ] && [ "$port" -le 65535 ] || die "a port is 1-65535"
    # ⛔ EVERY CONTROL CHARACTER, not just tab and newline. These fields are
    # written into a separated record and read back on a separator of their
    # own, so any one of them could forge a field boundary.
    case "$host$user" in *[[:cntrl:]]*) die "a host or user cannot contain a control character" ;; esac

    # Learned rather than asked for, and only when it was not given: the
    # machine being saved is nearly always awake at the moment somebody saves
    # it, and that is the only moment its hardware address can be read off the
    # network. Asking a person to find it is asking them to give up.
    local learned=no
    if [ -z "$mac" ]; then
        mac=$(mac_of_host "$host" 2>/dev/null || true)
        valid_mac "$mac" || mac=""
        [ -n "$mac" ] && learned=yes
    fi

    hosts_init
    local tmp="$HOSTS.new"
    { grep -v "^$name	" "$HOSTS" 2>/dev/null || true
      printf '%s\t%s\t%s\t%s\t%s\n' "$name" "$host" "$port" "$user" "$mac"
    } > "$tmp" || die "could not write $HOSTS"
    chmod 600 "$tmp" 2>/dev/null
    mv -f "$tmp" "$HOSTS"

    printf 'Saved %s as %s:%s.\n' "$name" "$host" "$port"
    note "Remember its password:  $PROG saved $name set"
    note "Open it:                $PROG connect $name"
    if [ -n "$mac" ]; then
        note "Wake it when asleep:    $PROG wake $name$([ "$learned" = yes ] && printf '   (%s, read off the network)' "$mac")"
    else
        # ⚠ NOT AN ERROR, and not silent either. Everything else about the
        # connection works; the one thing that will not is waking it, and the
        # moment to say so is now rather than at midnight from a hotel.
        note ""
        note "Its hardware address could not be read, so $PROG cannot wake it."
        note "On that machine:  syn-remote wakeable   — then:  $PROG add $name $target --mac <address>"
    fi
}

# Is anything answering on that port yet?
#
# ⚠ bash's own /dev/tcp, so there is no nc/netcat dependency and no difference
# between the three incompatible netcats a distribution might have installed.
port_open() {   # port_open <host> <port>
    timeout 2 bash -c ": >/dev/tcp/$1/$2" 2>/dev/null
}

# ⛔ A WAKE IS NOT A REQUEST THAT ANSWERS. Nothing comes back from a magic
# packet — not an acknowledgement, not an error — so the only way to know it
# worked is to watch the port the machine will start answering on. Without this
# wait, `connect` would fire the packet and hand a sleeping machine to the
# viewer, which would fail with a connection error and no hint that a wake was
# even attempted.
wait_for_port() {   # wait_for_port <host> <port> <seconds>
    local waited=0
    while [ "$waited" -lt "$3" ]; do
        port_open "$1" "$2" && return 0
        sleep 1
        waited=$((waited + 1))
    done
    return 1
}

cmd_wake() {
    local name=${1:-}
    valid_name "$name" || die "usage: $PROG wake <name>"
    local rec; rec=$(host_record "$name") || die "no saved connection called '$name'"
    local host port user mac
    IFS="$SEP" read -r host port user mac <<< "${rec//$'\t'/$SEP}"

    if [ -z "$mac" ]; then
        # Worth one attempt: a machine that is awake can still be learned from,
        # and then the column is filled in for the next time it is not.
        mac=$(mac_of_host "$host" 2>/dev/null || true)
        valid_mac "$mac" || mac=""
        if [ -n "$mac" ]; then
            host_set_mac "$name" "$mac"
            note "Learned its hardware address: $mac"
        else
            err "$name has no hardware address saved, so there is nothing to send a packet to"
            die "add it with:  $PROG add $name $host:$port${user:+ $user} --mac <address>"
        fi
    fi

    if port_open "$host" "$port"; then
        printf '%s is already awake.\n' "$name"
        return 0
    fi

    magic_packet "$mac" "$host" || die "could not send the packet"
    printf 'Sent a magic packet to %s.\n\n' "$mac"
    if wait_for_port "$host" "$port" "$WAKE_WAIT"; then
        printf '%s is awake.\n\n' "$name"
        note "Open it:  $PROG connect $name"
        return 0
    fi
    # ⛔ NOT A FAILURE OF THE PACKET, which cannot fail visibly. Everything
    # this end can do was done; what is left is the list of reasons the other
    # end did not hear it, and they are all somewhere else.
    err "$name did not answer within ${WAKE_WAIT}s"
    note ""
    note "The packet was sent. If it never wakes, the reason is at the other end:"
    note "  - the machine has to be ON THIS NETWORK — a magic packet is a"
    note "    broadcast and does not cross a router;"
    note "  - it has to be armed:  syn-remote wakeable on   (run there);"
    note "  - a machine that was shut down rather than suspended also needs"
    note "    Wake-on-LAN turned on in its firmware setup."
    return 1
}

# Rewrites one record's MAC in place, leaving every other field as it was.
host_set_mac() {   # host_set_mac <name> <mac>
    hosts_init
    local tmp="$HOSTS.new"
    awk -F'\t' -v OFS='\t' -v n="$1" -v m="$2" \
        '$1==n {$5=m} {print $1, $2, $3, $4, $5}' "$HOSTS" > "$tmp" || return 1
    chmod 600 "$tmp" 2>/dev/null
    mv -f "$tmp" "$HOSTS"
}

# What to read out to somebody trusting THIS machine from somewhere else.
cmd_fingerprint() {
    # ⚠ DOES NOT CALL ensure_credentials. Reading out a fingerprint is a
    # question, and a question must not mint a 2048-bit key and a ten-year
    # certificate as a side effect — least of all on a machine that only ever
    # connects OUT and will never serve anything.
    [ -s "$CERT" ] ||
        die "this machine has no certificate yet — it gets one from: $PROG on"
    printf '%s\n' "$(fingerprint "$CERT")"
}

cmd_forget() {
    local name=${1:-}
    valid_name "$name" || die "usage: $PROG forget <name>"
    host_record "$name" >/dev/null || die "no saved connection called '$name'"
    hosts_init
    local tmp="$HOSTS.new"
    grep -v "^$name	" "$HOSTS" 2>/dev/null > "$tmp"
    chmod 600 "$tmp" 2>/dev/null
    mv -f "$tmp" "$HOSTS"
    # ⛔ THE PASSWORD GOES WITH IT. A forgotten connection whose secret stays in
    # the keyring leaves a credential nothing can reach and nothing will ever
    # clean up — and it would silently come back if the same name were added
    # again, attached to a host it was never meant for.
    secret_clear "$name"
    rm -f "$(pin_path "$name")"
    printf 'Forgot %s, its password and its certificate.\n' "$name"
}

cmd_saved() {
    local name=${1:-} action=${2:-}
    valid_name "$name" || die "usage: $PROG saved <name> [set|clear]"
    host_record "$name" >/dev/null || die "no saved connection called '$name'"

    case "$action" in
        set)
            local pw
            # ⛔ READ, NOT AN ARGUMENT. A password on the command line is in
            # `ps` for every user on the machine and in the shell's history
            # afterwards.
            printf 'Password for %s: ' "$name" >&2
            IFS= read -rs pw; printf '\n' >&2
            [ -n "$pw" ] || die "nothing entered — the password is unchanged"
            local where; where=$(secret_put "$name" "$pw") || die "could not save the password"
            case "$where" in
                keyring) printf 'Saved in the keyring.\n' ;;
                file)    printf 'Saved.\n\n'
                         note "⚠ There is no keyring running, so it is in a file:"
                         note "  $SECRETS  (0600, and NOT encrypted)"
                         note "Start a keyring daemon and set it again to move it." ;;
            esac ;;
        clear)
            secret_clear "$name"
            printf 'Forgot the password for %s.\n' "$name" ;;
        ""|status)
            printf '%s\n' "$(secret_where "$name")" ;;
        *)  die "saved takes 'set' or 'clear'" ;;
    esac
}

# ── Trusting a server ─────────────────────────────────────
#
# ⛔ WITHOUT THIS, NOTHING CONNECTS AT ALL — and the way it failed is worth
# writing down, because every layer looked healthy. wayvnc offers exactly one
# security type this viewer can speak (VeNCrypt X509Plain), so the client must
# validate the server's certificate; gtk-vnc asks for a CA to check it against
# and gnutls then matches the ADDRESS DIALLED against the certificate's names.
# With no pinned certificate the TLS session comes up and is then dropped —
# no error on the client, and on the server nothing but "Client handshake
# timed out", which reads like a firewall problem and is not.

pin_path() { printf '%s/%s.pem' "$PINS" "$1"; }

fingerprint() {   # fingerprint <pem-file>
    openssl x509 -in "$1" -noout -fingerprint -sha256 2>/dev/null |
        sed 's/.*=//'
}

cmd_trust() {
    local name=${1:-} renew=${2:-}
    valid_name "$name" || die "usage: $PROG trust <name> [--renew]"
    local rec; rec=$(host_record "$name") || die "no saved connection called '$name'"
    local host port user mac
    IFS="$SEP" read -r host port user mac <<< "${rec//$'\t'/$SEP}"

    hosts_init
    mkdir -p "$PINS" && chmod 700 "$PINS" 2>/dev/null
    local pin; pin=$(pin_path "$name")

    if [ -s "$pin" ] && [ "$renew" != "--renew" ]; then
        printf 'Already trusting %s:\n\n  %s\n\n' "$name" "$(fingerprint "$pin")"
        note "To accept a new certificate:  $PROG trust $name --renew"
        return 0
    fi

    [ -x "$GETCERT" ] || die "the certificate fetcher is missing: $GETCERT"

    local tmp="$pin.new"
    "$GETCERT" "$host" "$port" > "$tmp" 2>"$tmp.err" || {
        err "$(cat "$tmp.err" 2>/dev/null)"
        rm -f "$tmp" "$tmp.err"
        die "could not fetch a certificate from $host:$port"
    }
    rm -f "$tmp.err"
    [ -s "$tmp" ] || { rm -f "$tmp"; die "$host:$port sent no certificate"; }

    printf '%s is offering this certificate:\n\n' "$name"
    printf '  subject     %s\n' "$(openssl x509 -in "$tmp" -noout -subject 2>/dev/null | sed 's/^subject=//')"
    printf '  valid to    %s\n' "$(openssl x509 -in "$tmp" -noout -enddate 2>/dev/null | sed 's/^notAfter=//')"
    printf '  SHA-256     %s\n\n' "$(fingerprint "$tmp")"

    # ⚠ CONFIRMED BY A HUMAN, AGAINST THE FINGERPRINT. This is the one moment
    # the certificate is not yet trusted, so it is the only moment the identity
    # can be checked at all — every later connection just compares against what
    # was accepted here. Auto-accepting would make the pin a record of whoever
    # answered first rather than of the machine somebody meant.
    if [ -t 0 ]; then
        printf 'Compare that with `syn-remote fingerprint` on %s.\n' "$name"
        printf 'Trust it? [y/N] '
        local ans; IFS= read -r ans
        case "$ans" in
            [Yy]*) ;;
            *) rm -f "$tmp"; printf 'Not trusted; nothing was saved.\n'; return 1 ;;
        esac
    else
        rm -f "$tmp"
        err "not a terminal — refusing to accept a certificate nobody has seen"
        die "run:  $PROG trust $name"
    fi

    mv -f "$tmp" "$pin"
    chmod 600 "$pin" 2>/dev/null
    printf 'Trusted. %s will be checked against this certificate from now on.\n' "$name"
}

# ── Opening one ───────────────────────────────────────────

cmd_connect() {
    local nowake=no
    case "${1:-}" in --no-wake) nowake=yes; shift ;; esac
    case "${2:-}" in --no-wake) nowake=yes; set -- "${1:-}" ;; esac
    local name=${1:-}
    valid_name "$name" || die "usage: $PROG connect <name> [--no-wake]"
    local rec; rec=$(host_record "$name") || die "no saved connection called '$name'"
    local host port user mac
    IFS="$SEP" read -r host port user mac <<< "${rec//$'\t'/$SEP}"

    [ -x "$VIEWER" ] || die "the viewer is missing: $VIEWER"

    # ⛔ ASKED BEFORE THE CERTIFICATE CHECK, because trusting a machine talks to
    # it: a sleeping host would fail the pin check with a connection error, and
    # the person would be told their certificate was the problem.
    #
    # ⚠ ONLY WHEN THE PORT IS SHUT. A magic packet costs nothing, but sending
    # one to a machine that is already answering is a wake for a machine that is
    # awake — and on a shared network it is a packet somebody has to explain.
    if [ -n "$mac" ] && [ "$nowake" != yes ] && ! port_open "$host" "$port"; then
        note "$name is not answering — sending a wake packet."
        if magic_packet "$mac" "$host" && wait_for_port "$host" "$port" "$WAKE_WAIT"; then
            note "$name is awake."
        else
            # Not fatal. The viewer's own error is the better one to end on if
            # the machine was never asleep in the first place, and a person who
            # asked to connect asked to connect.
            note "$name did not answer within ${WAKE_WAIT}s — trying anyway."
        fi
        printf '\n' >&2
    fi

    # ⛔ NO PIN, NO CONNECTION — and it must say so rather than let the viewer
    # hang. Without a certificate to validate against, the TLS session comes up
    # and is then dropped with no error anywhere except "Client handshake timed
    # out" in the SERVER's journal. That is a bug report nobody can act on, so
    # the check happens here where the fix has a name.
    local pin; pin=$(pin_path "$name")
    if [ ! -s "$pin" ]; then
        if [ -t 0 ]; then
            note "First connection to $name — its certificate has to be checked."
            printf '\n'
            cmd_trust "$name" || exit 1
            printf '\n'
        else
            err "$name has no trusted certificate yet"
            die "run:  $PROG trust $name"
        fi
    fi

    local pw; pw=$(secret_get "$name" || true)

    # ⛔ ON STDIN, NOT ON argv AND NOT IN THE ENVIRONMENT. argv is world-visible
    # in `ps`; the environment is readable for the life of the process through
    # /proc/PID/environ. A pipe is read once and is gone.
    #
    # ⚠ AND THE VIEWER STILL ASKS IF THERE IS NOTHING TO SEND. An empty pipe
    # means "no saved password", not "the password is empty" — the viewer draws
    # its own prompt in that case rather than failing to authenticate.
    printf '%s' "$pw" | exec "$VIEWER" \
        --host "$host" --port "$port" --name "$name" \
        --cacert "$pin" \
        ${user:+--user "$user"}
}

# ── The other two faces ───────────────────────────────────

cmd_gui() {
    have syn-remote-gui || die "syn-remote-gui is not installed"
    exec syn-remote-gui "$@"
}

# Arrow keys, a list, and the same three verbs the window has. Reads the same
# --tsv the window does, so the two cannot disagree about what is saved.
cmd_tui() {
    hosts_init
    local names=() rows=() n h p u sec sel=0
    while IFS="$SEP" read -r n h p u sec; do
        [ "$n" = name ] && continue
        [ -n "$n" ] || continue
        names+=("$n")
        rows+=("$(printf '%-16s %s:%s  %s' "$n" "$h" "$p" \
                  "$(case $sec in keyring) echo '[keyring]';; file) echo '[on disk]';; *) echo '[no password]';; esac)")")
    done < <(cmd_hosts --tsv | tr '\t' "$SEP")

    [ ${#names[@]} -gt 0 ] || { printf 'No saved connections. Add one:  %s add <name> <host>\n' "$PROG"; return 0; }

    local key
    while :; do
        printf '\033[H\033[2J'
        printf 'syn-remote — saved connections\n\n'
        local i
        for i in "${!rows[@]}"; do
            if [ "$i" -eq "$sel" ]; then printf '  \033[7m%s\033[0m\n' "${rows[$i]}"
            else                          printf '  %s\n' "${rows[$i]}"; fi
        done
        printf '\n  ↑/↓ choose   Enter connect   w wake   p password   d forget   q quit\n'

        IFS= read -rsn1 key || break
        case "$key" in
            $'\033')
                # An arrow is ESC [ A/B. Read the rest without blocking, so a
                # bare Escape is a quit rather than a hang.
                IFS= read -rsn2 -t 0.1 key || key=""
                case "$key" in
                    '[A') sel=$(( (sel - 1 + ${#names[@]}) % ${#names[@]} )) ;;
                    '[B') sel=$(( (sel + 1) % ${#names[@]} )) ;;
                    "")   return 0 ;;
                esac ;;
            k) sel=$(( (sel - 1 + ${#names[@]}) % ${#names[@]} )) ;;
            j) sel=$(( (sel + 1) % ${#names[@]} )) ;;
            "") printf '\033[H\033[2J'; cmd_connect "${names[$sel]}"; return $? ;;
            w) printf '\n'; cmd_wake "${names[$sel]}"; printf '\n  [any key]'; IFS= read -rsn1 ;;
            p) printf '\n'; cmd_saved "${names[$sel]}" set; printf '\n  [any key]'; IFS= read -rsn1 ;;
            d) printf '\n'; cmd_forget "${names[$sel]}"; return 0 ;;
            q) return 0 ;;
        esac
    done
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
  syn-remote names [add|remove <name>]
                               extra names and addresses the certificate
                               vouches for — needed for a port forward or a
                               dynamic-DNS name, which this machine cannot see
  syn-remote wakeable [on|off] whether a magic packet may wake this machine
                               while it is asleep, and what its address is

Reaching somebody else's desktop:

  syn-remote hosts [--tsv]     the connections you have saved
  syn-remote add <name> <host>[:port] [user] [--mac <address>]
                               its hardware address is read off the network if
                               it is reachable now — it is what `wake` sends to
  syn-remote connect <name>    open it — waking it first if it is asleep
  syn-remote wake <name>       just wake it, without opening anything
  syn-remote saved <name> [set|clear]   the password it is opened with
  syn-remote trust <name> [--renew]    check and pin its certificate
  syn-remote fingerprint       this machine's own certificate, to read out
  syn-remote forget <name>     drop it, its password and its certificate
  syn-remote gui | tui         the same list, in a window or in the terminal

The server is wayvnc; this adds the parts a wrapper has to: it wakes a blanked
screen when somebody connects (a blanked output cannot be captured at all) and
holds the machine awake while they are there.

Three different things are called waking a machine, and only the last needs
switching on. A blanked SCREEN is woken by connecting. A machine held awake
while somebody is watching needs nothing. A machine that is ASLEEP answers
nothing at all, and is woken by a magic packet — `wakeable` on the machine that
sleeps, `wake` on the machine you are sitting at. The two ends are the same
network: a magic packet is a broadcast and does not cross a router.

Nothing exists to share until somebody has logged in — there is no desktop
before a login on any Wayland system.

The viewer is syn-remote's own, because wayvnc authenticates over VeNCrypt and
no packaged VNC client can be handed a username or a password without somebody
typing it — so a saved password could never be used by one.
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
    names)      shift; cmd_names "$@" ;;
    wakeable)   shift; cmd_wakeable "$@" ;;
    hosts)      shift; cmd_hosts "$@" ;;
    add)        shift; cmd_add "$@" ;;
    forget)     shift; cmd_forget "$@" ;;
    saved)      shift; cmd_saved "$@" ;;
    connect|view) shift; cmd_connect "$@" ;;
    wake)       shift; cmd_wake "$@" ;;
    trust)      shift; cmd_trust "$@" ;;
    fingerprint) shift; cmd_fingerprint "$@" ;;
    gui)        shift; cmd_gui "$@" ;;
    tui)        shift; cmd_tui "$@" ;;
    -h|--help|help) usage ;;
    *)          err "unknown command: $1"; usage; exit 1 ;;
esac
