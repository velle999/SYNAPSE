#!/usr/bin/env bash
# syn — SynapseOS unified CLI
VERSION="0.1.0-synapse"

usage() {
    cat << HELP
syn $VERSION — SynapseOS CLI

Usage:
  syn status              Overall system status
  syn info                System info (neofetch-style)
  syn model <cmd>         Model manager (download/list/status/remove)
  syn net <cmd>           Network policy (allow/block/status)
  syn guard <cmd>         Security monitor (status/mode/alerts)
  syn shell               Launch synsh
  syn ui                  Launch synui Wayland compositor
  syn install             Install SynapseOS to disk
  syn help                This help

HELP
}

cmd_status() {
    echo ""
    echo "  ┌─ SynapseOS $VERSION ──────────────────────────────┐"
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
    echo "  └────────────────────────────────────────────────────┘"
    echo ""
}

cmd_info() {
    local kernel=$(uname -r)
    local uptime=$(uptime -p 2>/dev/null | sed 's/up //')
    local mem=$(free -m | awk '/Mem:/ {printf "%dMB / %dMB", $3, $2}')
    local cpu=$(grep "model name" /proc/cpuinfo 2>/dev/null | head -1 | cut -d: -f2 | xargs)
    local model_path="/var/lib/synapd/models/synapse.gguf"
    local ai_status=$([ -f "$model_path" ] && echo "loaded" || echo "no model")
    cat << INFO

  ███████╗██╗   ██╗███╗   ██╗
  ██╔════╝╚██╗ ██╔╝████╗  ██║     OS:      SynapseOS $VERSION
  ███████╗ ╚████╔╝ ██╔██╗ ██║     Kernel:  $kernel
  ╚════██║  ╚██╔╝  ██║╚██╗██║     Uptime:  $uptime
  ███████║   ██║   ██║ ╚████║     Memory:  $mem
  ╚══════╝   ╚═╝   ╚═╝  ╚═══╝     CPU:     $cpu
                                   AI:      $ai_status
  Where the kernel thinks.         Shell:   synsh $VERSION

INFO
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

case "${1:-help}" in
    status)         cmd_status ;;
    info)           cmd_info ;;
    model)          shift; cmd_model "$@" ;;
    net)            shift; cmd_net "$@" ;;
    guard)          shift; cmd_guard "$@" ;;
    install)        exec syn-install ;;
    shell)          exec synsh ;;
    ui)             exec synui ;;
    help|-h|--help) usage ;;
    *)              echo "Unknown command: $1"; usage; exit 1 ;;
esac
