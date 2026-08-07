#!/usr/bin/env bash
# syn — SynapseOS unified CLI
VERSION="0.1.0-synapse"

usage() {
    cat << HELP
syn $VERSION — SynapseOS CLI

Usage:
  syn status              Overall system status
  syn info                System info (fastfetch, with built-in fallback)
  syn model <cmd>         Model manager (download/list/status/remove)
  syn net <cmd>           Network policy (allow/block/status)
  syn guard <cmd>         Security monitor (status/mode/alerts)
  syn nix <cmd>           Declarative user environment via Nix + Home Manager
                          (status/apply/build/update/facts/edit/rollback/init)
  syn arsenal             Browse/install BlackArch security tooling
  syn shell               Launch synsh
  syn ui                  Launch synui Wayland compositor
  syn install             Install SynapseOS to disk
  syn update [check|apply]  Update SynapseOS itself from git
  syn help                This help

HELP
}

cmd_status() {
    echo ""
    echo "  +- SynapseOS $VERSION ------------------------------+"
    echo ""
    local synapd_status=$(systemctl is-active synapd 2>/dev/null)
    local model_status=$(journalctl -t synapd -n 3 --no-pager 2>/dev/null | grep -o "model=[^ ]*" | grep -v "unloaded" | tail -1)
    printf "  %-12s %s  %s\n" "synapd" "$([ "$synapd_status" = "active" ] && echo "✓ running" || echo "✗ stopped")" "${model_status:-}"
    local synnet_status=$(systemctl is-active synnet 2>/dev/null)
    printf "  %-12s %s\n" "synnet" "$([ "$synnet_status" = "active" ] && echo "✓ running" || echo "✗ stopped")"
    local synguard_status=$(systemctl is-active synguard 2>/dev/null)
    printf "  %-12s %s\n" "synguard" "$([ "$synguard_status" = "active" ] && echo "✓ running" || echo "✗ stopped")"
    if lsmod | grep -q synapse_kmod; then
        printf "  %-12s %s\n" "kmod" "✓ loaded"
    else
        printf "  %-12s %s\n" "kmod" "✗ not loaded"
    fi
    local model_path="/var/lib/synapd/models/synapse.gguf"
    if [ -f "$model_path" ]; then
        local size=$(du -sh "$model_path" 2>/dev/null | cut -f1)
        printf "  %-12s %s  (%s)\n" "model" "✓ installed" "$size"
    else
        printf "  %-12s %s\n" "model" "✗ not installed — run: syn model download"
    fi
    echo ""
    echo "  +----------------------------------------------------+"
    echo ""
}

cmd_info() {
    # Prefer fastfetch (ships with a branded SynapseOS logo config); fall back
    # to the built-in ASCII fetch when fastfetch isn't installed.
    if command -v fastfetch &>/dev/null; then
        exec fastfetch "$@"
    fi

    local kernel=$(uname -r)
    local uptime=$(uptime -p 2>/dev/null | sed 's/up //')
    local mem=$(free -m | awk '/Mem:/ {printf "%dMB / %dMB", $3, $2}')
    local cpu=$(grep "model name" /proc/cpuinfo 2>/dev/null | head -1 | cut -d: -f2 | xargs)
    local model_path="/var/lib/synapd/models/synapse.gguf"
    local ai_status=$([ -f "$model_path" ] && echo "loaded" || echo "no model")
    # The dendrite mark, laid out the way fastfetch lays out the full-size one
    # in /usr/share/synapseos/logo.txt. Regenerate with
    # `python3 archiso/mkasciilogo.py --compact --plain`.
    #
    # printf with a SINGLE-quoted format, not a heredoc: the art is built from
    # backticks, and an unquoted heredoc runs those as commands. The values go
    # through %s instead, in the order they appear.
    printf '
             oo
            `oo`
           `:oo:`
          `:+oo+:`           OS:      SynapseOS %s
         .:++oo++:.          Kernel:  %s
        .:+++oo+++:.         Uptime:  %s
       .++ssooooss++.        Memory:  %s
     `.+soooossoooos+.`      CPU:     %s
    `ssooooos++soooooss`     AI:      %s
   `oooooos++++++soooooo`    Shell:   synsh %s
  `:ssssos++++++++sossss:`

  Where the kernel thinks.

' "$VERSION" "$kernel" "$uptime" "$mem" "$cpu" "$ai_status" "$VERSION"
}

cmd_model() {
    command -v syn-model &>/dev/null && syn-model "$@" || echo "syn-model not found"
}

cmd_net() {
    case "${1:-status}" in
        status) systemctl status synnet --no-pager -l | tail -10 ;;
        allow) [ -z "${2:-}" ] && { echo "Usage: syn net allow <ip>"; exit 1; }; synnet --allow "$2" ;;
        block) [ -z "${2:-}" ] && { echo "Usage: syn net block <ip>"; exit 1; }; synnet --block "$2" ;;
        *) echo "Usage: syn net [status|allow <ip>|block <ip>]" ;;
    esac
}

cmd_guard() {
    case "${1:-status}" in
        status) systemctl status synguard --no-pager -l | tail -10 ;;
        alerts) journalctl -t synguard --no-pager 2>/dev/null | grep -i "alert\|deny\|block" | tail -20 ;;
        *) echo "Usage: syn guard [status|alerts|mode <mode>]" ;;
    esac
}

# ── Nix ───────────────────────────────────────────────────
#
# SynapseOS is Arch: pacman owns the system, and Nix is the optional second
# layer for a declarative USER environment (Home Manager). The configurator
# lives at /etc/synapseos/nix — flake.nix + home.nix + a generated facts.nix
# describing this machine, so the expressions branch on the install instead of
# being hand-edited per box.
#
# `init` is deliberately the ONLY implementation of "set this up", and
# syn-install reaches it through `arch-chroot /mnt syn nix init` rather than
# repeating any of it. Every other way of doing this ends as two copies of the
# same eight steps, agreeing until one of them is changed.
NIXDIR="/etc/synapseos/nix"
NIXTPL="/usr/share/syn/nix"
NIXATTR="homeConfigurations.synapse.activationPackage"

# nix-command and flakes are written into /etc/nix/nix.conf by `init`, but
# pass them on every call anyway: a box where nix was installed by hand has
# neither, and the failure is a usage message about an experimental feature
# rather than anything naming SynapseOS.
NIXFEAT=(--extra-experimental-features "nix-command flakes")

cmd_nix() {
    local sub="${1:-status}"; shift 2>/dev/null || true

    if ! command -v nix >/dev/null 2>&1; then
        echo "  Nix is not installed."
        echo "    sudo pacman -S nix && sudo syn nix init"
        return 1
    fi

    case "$sub" in
        init)
            [ "$(id -u)" = 0 ] || { echo "  syn nix init must run as root."; return 1; }

            mkdir -p "$NIXDIR" /nix/store /nix/var/nix

            # Templates are copied, never overwritten: home.nix is the user's
            # file the moment they edit it, and an upgrade that reset it would
            # throw away the whole reason this directory exists. facts.nix is
            # the opposite — generated, always replaced.
            local f
            for f in flake.nix home.nix; do
                if [ -f "$NIXDIR/$f" ]; then
                    echo "  $f: kept (already present)"
                elif [ -f "$NIXTPL/$f" ]; then
                    cp "$NIXTPL/$f" "$NIXDIR/$f"
                    echo "  $f: installed"
                else
                    echo "  $f: MISSING from $NIXTPL — syn is installed incompletely"
                    return 1
                fi
            done

            /usr/lib/syn/syn-nix-facts / > "$NIXDIR/facts.nix" \
                && echo "  facts.nix: generated"

            # /etc/nix/nix.conf is a pacman BACKUP file shipping
            # `build-users-group = nixbld`. Appending keeps that and keeps the
            # user's own edits; overwriting it would break every build with an
            # error about build users that names nothing we did.
            if ! grep -q '^experimental-features' /etc/nix/nix.conf 2>/dev/null; then
                printf '\n# SynapseOS: the flake in %s needs both.\nexperimental-features = nix-command flakes\n' \
                    "$NIXDIR" >> /etc/nix/nix.conf
                echo "  nix.conf: enabled nix-command + flakes"
            fi

            # No group to join. Arch's nix package leaves
            # /nix/var/nix/daemon-socket at 0755 root root and the socket
            # itself world-writable, so any user can reach the daemon — the
            # `nix-users` group the older guides talk about is not a thing
            # this package creates.
            systemctl enable nix-daemon.socket 2>/dev/null \
                && echo "  nix-daemon.socket: enabled" \
                || echo "  nix-daemon.socket: enable failed (chroot?) — check after boot"

            # The configurator is the user's to edit, so it is theirs to own.
            # Unusual for /etc, and the alternative is worse: a config file you
            # need sudo to change is one people edit as root and then wonder
            # why the build cannot write flake.lock beside it.
            local owner
            owner=$(awk -F: '$3 >= 1000 && $3 < 65534 { print $1; exit }' /etc/passwd)
            [ -n "$owner" ] && chown -R "$owner:$owner" "$NIXDIR" \
                && echo "  $NIXDIR: owned by $owner"

            echo ""
            echo "  Ready. As $owner, after a reboot:  syn nix apply"
            ;;

        apply)
            [ "$(id -u)" = 0 ] && { echo "  Run syn nix apply as your user, not root —
  it builds a HOME environment, and root's is not the one you log in to."; return 1; }
            [ -f "$NIXDIR/flake.nix" ] || { echo "  Not set up. Run: sudo syn nix init"; return 1; }

            # Build the activation package straight out of the flake and run
            # it, rather than `nix run home-manager -- switch`. That form
            # resolves home-manager through the flake registry, which fetches a
            # SECOND nixpkgs unrelated to the one in flake.lock — a gigabyte to
            # do what the lock already pinned, and the two can disagree.
            local out
            out=$(nix "${NIXFEAT[@]}" build --no-link --print-out-paths \
                      "$NIXDIR#$NIXATTR" "$@") || return 1
            "$out/activate"
            ;;

        build)
            # Same build, no activation — the dry run before a switch.
            nix "${NIXFEAT[@]}" build --no-link --print-out-paths \
                "$NIXDIR#$NIXATTR" "$@"
            ;;

        update)
            # Bumps flake.lock. Nothing takes effect until `syn nix apply`.
            nix "${NIXFEAT[@]}" flake update --flake "$NIXDIR" "$@"
            ;;

        facts)
            /usr/lib/syn/syn-nix-facts / > "$NIXDIR/facts.nix" \
                && echo "  facts.nix regenerated from this machine:" \
                && sed -n '/^{/,$p' "$NIXDIR/facts.nix"
            ;;

        edit)   "${EDITOR:-nano}" "$NIXDIR/home.nix" ;;

        profile)
            # Renders an INSTALL profile, which has nothing to do with
            # $NIXDIR — it is for feeding syn-install on some other machine.
            # Here because this is where a nix evaluator is known to exist:
            # the live ISO may have none, which is exactly why pre-rendering
            # to key=value is worth having.
            local f="${1:-}"
            [ -n "$f" ] || { echo "Usage: syn nix profile <profile.nix>"; return 1; }
            [ -f "$f" ] || { echo "  not found: $f"; return 1; }
            [ -f "$NIXTPL/render.nix" ] || { echo "  $NIXTPL/render.nix is missing"; return 1; }
            nix-instantiate --eval --strict --raw \
                --argstr profile "$(readlink -f "$f")" "$NIXTPL/render.nix"
            ;;

        rollback)
            # home-manager's own generation list is the source of truth, and
            # programs.home-manager.enable in the shipped home.nix puts the
            # command in the profile. Before the first apply it is absent.
            if command -v home-manager >/dev/null 2>&1; then
                home-manager generations | head -10
                echo ""
                echo "  Activate one with:  /nix/store/<hash>-home-manager-generation/activate"
            else
                echo "  home-manager is not in your profile yet — run 'syn nix apply' first."
            fi
            ;;

        status)
            echo ""
            echo "  +- Nix ---------------------------------------------+"
            echo ""
            printf "  %-12s %s\n" "nix" "$(nix --version 2>/dev/null || echo 'not installed')"
            printf "  %-12s %s\n" "daemon" "$(systemctl is-active nix-daemon.socket 2>/dev/null)"
            printf "  %-12s %s\n" "config" \
                "$([ -f "$NIXDIR/flake.nix" ] && echo "$NIXDIR" || echo 'not set up — sudo syn nix init')"
            printf "  %-12s %s\n" "locked" \
                "$([ -f "$NIXDIR/flake.lock" ] && echo yes || echo 'no — first apply writes it')"
            if [ -d /nix/store ]; then
                printf "  %-12s %s\n" "store" "$(du -sh /nix/store 2>/dev/null | cut -f1)"
            fi
            if command -v home-manager >/dev/null 2>&1; then
                printf "  %-12s %s\n" "generation" \
                    "$(home-manager generations 2>/dev/null | head -1)"
            fi
            echo ""
            echo "  +----------------------------------------------------+"
            echo ""
            ;;

        *)
            echo "Usage: syn nix [status|apply|build|update|facts|edit|rollback|profile|init]"
            echo ""
            echo "  status    daemon, store size, whether the flake is set up (default)"
            echo "  apply     build $NIXDIR and activate it"
            echo "  build     build it without activating"
            echo "  update    bump flake.lock to current nixpkgs/home-manager"
            echo "  facts     re-derive facts.nix from this machine"
            echo "  edit      open home.nix in \$EDITOR"
            echo "  rollback  list home-manager generations"
            echo "  profile   render an install profile to key=value for"
            echo "            'syn-install --config' (unrelated to the above)"
            echo "  init      first-time setup (root; syn-install runs this for you)"
            ;;
    esac
}

case "${1:-help}" in
    status)         cmd_status ;;
    info)           cmd_info ;;
    model)          shift; cmd_model "$@" ;;
    net)            shift; cmd_net "$@" ;;
    guard)          shift; cmd_guard "$@" ;;
    nix)            shift; cmd_nix "$@" ;;
    # --tui rather than bare syn-arsenal: `syn` is the terminal entry point, so
    # a subcommand typed in a shell must not fork a GUI window at the user.
    arsenal)        shift; exec syn-arsenal --tui "$@" ;;
    install)        exec syn-install ;;
    update)         shift; exec syn-update "$@" ;;
    shell)          exec synsh ;;
    ui)             exec synui ;;
    help|-h|--help) usage ;;
    *)              echo "Unknown command: $1"; usage; exit 1 ;;
esac
