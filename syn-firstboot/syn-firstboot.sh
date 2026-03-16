#!/usr/bin/env bash
# syn-firstboot — SynapseOS first boot setup wizard
# Note: intentionally no set -euo pipefail — failures must be handled gracefully

MODEL_DIR="/var/lib/synapd/models"
MODEL_PATH="$MODEL_DIR/synapse.gguf"
DONE_FLAG="/var/lib/synapseos/firstboot.done"
DE_CONF="/etc/synapseos/desktop.conf"
COLS=$(tput cols 2>/dev/null || echo 80)

# ── Background setup (runs in parallel with wizard display) ───
# Builds synapse_kmod via DKMS and ensures synapd is running.
KMOD_LOG="/var/log/synapse_kmod-dkms.log"
_bg_setup() {
    local kver; kver="$(uname -r)"
    local src="/usr/src/synapse_kmod-0.1.0"
    {
        echo "=== synapse_kmod DKMS build: $(date) ==="
        echo "kernel: $kver"
        if [[ -d "$src" ]] && command -v dkms &>/dev/null; then
            dkms add synapse_kmod/0.1.0 2>&1 || true
            dkms build synapse_kmod/0.1.0 -k "$kver" 2>&1 || true
            dkms install synapse_kmod/0.1.0 -k "$kver" 2>&1 || true
            depmod -a "$kver" 2>&1 || true
            modprobe synapse_kmod 2>&1 || true
            echo "dkms status: $(dkms status synapse_kmod 2>&1)"
        else
            echo "SKIP: src=$src exists=$([ -d "$src" ] && echo y || echo n) dkms=$(command -v dkms || echo missing)"
        fi
    } >> "$KMOD_LOG" 2>&1
    systemctl start synapd 2>/dev/null || true
}
_bg_setup &
BG_PID=$!

# ── UI helpers ────────────────────────────────────────────
cyan()  { printf '\033[1;36m%s\033[0m' "$*"; }
green() { printf '\033[1;32m%s\033[0m' "$*"; }
red()   { printf '\033[1;31m%s\033[0m' "$*"; }
bold()  { printf '\033[1m%s\033[0m' "$*"; }
line()  { printf '%*s\n' "$COLS" '' | tr ' ' '-'; }

header() {
    clear
    cyan "
  ███████╗██╗   ██╗███╗   ██╗
  ██╔════╝╚██╗ ██╔╝████╗  ██║
  ███████╗ ╚████╔╝ ██╔██╗ ██║
  ╚════██║  ╚██╔╝  ██║╚██╗██║
  ███████║   ██║   ██║ ╚████║
  ╚══════╝   ╚═╝   ╚═╝  ╚═══╝
"
    echo "  $(bold 'SynapseOS 0.1.0') — Where the kernel thinks."
    line
    echo ""
}

step()    { echo ""; cyan "  ▶ $*"; echo ""; }
success() { echo ""; green "  ✓ $*"; echo ""; }
fail()    { echo ""; red "  ✗ $*"; echo ""; }
prompt()  { printf "  $(bold "$1") "; }

# ── Safety net — always land in a shell on error ──────────
trap 'echo ""; red "  firstboot error — dropping to synsh"; echo ""; exec /usr/bin/synsh' ERR

# ── Check if already done ─────────────────────────────────
LIVE_ISO=0
mountpoint -q /run/archiso/airootfs 2>/dev/null && LIVE_ISO=1

if [ "$LIVE_ISO" = "0" ] && [ -f "$DONE_FLAG" ]; then
    # Already configured — start the chosen DE directly
    if [ -f "$DE_CONF" ]; then
        source "$DE_CONF" 2>/dev/null || true
        case "${DE:-synui}" in
            kde)   exec systemctl start sddm.service ;;
            gnome) exec systemctl start gdm.service ;;
            tty)   exec /usr/bin/synsh ;;
            *)     exec systemctl start synui.service ;;
        esac
    fi
    exec systemctl start synui.service
fi

# ── Live ISO — offer install or live session ──────────────
if [ "$LIVE_ISO" = "1" ]; then
    header
    echo "  SynapseOS is running from a live ISO."
    echo ""
    echo "  Would you like to install SynapseOS to a disk?"
    echo ""
    echo "    $(bold '1)') Install to disk   — permanent installation"
    echo "    $(bold '2)') Try live session  — continue without installing"
    echo ""
    prompt "Choice [1-2]:"
    read -r install_choice || true

    if [ "${install_choice:-2}" = "1" ]; then
        exec /usr/bin/syn-install
    fi
    # Live session — skip firstboot wizard, go straight to synsh
    exec /usr/bin/synsh
fi

# ── Welcome ───────────────────────────────────────────────
header
echo "  Welcome to SynapseOS."
echo ""
echo "  This wizard will set up your system in a few steps:"
echo "    1. Set root password"
echo "    2. Network check"
echo "    3. AI model download"
echo "    4. System verification"
echo "    5. Desktop environment"
echo ""
prompt "Press ENTER to begin, or Ctrl+C to skip to shell..."
read -r || true

# ── Step 1: Root password ─────────────────────────────────
header
step "Step 1/5 — Set Root Password"

echo "  Set a password for the root account."
echo "  (Leave blank to keep no password)"
echo ""
passwd root || true

# ── Step 2: Network ───────────────────────────────────────
header
step "Step 2/5 — Network"

echo "  Checking network connectivity..."
HAVE_NET=0
if ping -c 1 -W 3 8.8.8.8 &>/dev/null; then
    success "Network is up"
    HAVE_NET=1
else
    echo "  No network detected. Starting NetworkManager..."
    systemctl start NetworkManager 2>/dev/null || true
    sleep 3
    if ping -c 1 -W 3 8.8.8.8 &>/dev/null; then
        success "Network connected"
        HAVE_NET=1
    else
        fail "No network available"
        echo "  You can download a model later with: $(cyan 'syn model download')"
    fi
fi

# ── Step 3: Model ─────────────────────────────────────────
header
step "Step 3/5 — AI Model"

if [ -f "$MODEL_PATH" ]; then
    success "Model already installed ($(du -sh "$MODEL_PATH" 2>/dev/null | cut -f1))"
elif [ "$HAVE_NET" = "0" ]; then
    echo "  Skipping model download (no network)"
    echo "  Run $(cyan 'syn model download') when connected"
else
    echo "  SynapseOS needs a local AI model to enable full features."
    echo "  Choose a model to download:"
    echo ""
    echo "    $(bold '1)') tiny      Qwen2 0.5B    ~400MB   Fast, lower quality"
    echo "    $(bold '2)') phi3      Phi-3 Mini    ~2.2GB   Good balance"
    echo "    $(bold '3)') mistral   Mistral 7B    ~4.1GB   Best quality (recommended)"
    echo "    $(bold '4)') skip      Skip for now"
    echo ""
    prompt "Choice [1-4]:"
    read -r choice || true

    case "${choice:-4}" in
        1) MODEL="tiny" ;;
        2) MODEL="phi3" ;;
        3) MODEL="mistral-7b" ;;
        *) MODEL="" ;;
    esac

    if [ -n "$MODEL" ]; then
        echo ""
        echo "  Downloading $(bold "$MODEL")..."
        mkdir -p "$MODEL_DIR"
        if syn-model download "$MODEL" 2>/dev/null; then
            success "Model downloaded"
        else
            fail "Download failed — run: syn model download $MODEL"
        fi
    else
        echo "  Skipped. Run $(cyan 'syn model download') later."
    fi
fi

# ── Step 4: System check ──────────────────────────────────
header
step "Step 4/5 — System Check"

# Wait for background DKMS build to finish before checking kmod
wait "$BG_PID" 2>/dev/null || true

check() {
    local name="$1" cmd="$2"
    printf "  %-20s" "$name"
    if eval "$cmd" &>/dev/null 2>&1; then
        green "✓"
    else
        red "✗"
    fi
    echo ""
}

check "synapd"        "systemctl is-active synapd"
check "synnet"        "systemctl is-active synnet"
check "synguard"      "systemctl is-active synguard"
printf "  %-20s" "synapse_kmod"
if lsmod | grep -q synapse_kmod 2>/dev/null; then
    green "✓"; echo ""
else
    red "✗"; echo "  (see $KMOD_LOG)"
fi
check "AI model"      "test -f $MODEL_PATH"
check "network"       "ping -c1 -W2 8.8.8.8"

# ── Step 5: Desktop Environment ───────────────────────────
header
step "Step 5/5 — Desktop Environment"

echo "  Choose your desktop environment:"
echo "  (SynapseUI is the native AI-first compositor)"
echo ""
echo "    $(bold '1)') SynapseUI  — AI-native Wayland compositor  (default)"
echo "    $(bold '2)') KDE Plasma — Full-featured Wayland desktop"
echo "    $(bold '3)') GNOME      — Clean, modern Wayland desktop"
echo "    $(bold '4)') TTY only   — No GUI (headless/server)"
echo ""
prompt "Choice [1-4, default=1]:"
read -r de_choice || true

mkdir -p /etc/synapseos

case "${de_choice:-1}" in
    2)
        echo "  Enabling KDE Plasma with SDDM..."
        echo "DE=kde" > "$DE_CONF"
        mkdir -p /etc/sddm.conf.d
        cat > /etc/sddm.conf.d/synapseos.conf << 'SDDM'
[Autologin]
Session=plasmawayland
User=syn
SDDM
        systemctl disable synui.service 2>/dev/null || true
        systemctl enable sddm.service 2>/dev/null || true
        success "KDE Plasma selected — SDDM enabled"
        ;;
    3)
        echo "  Enabling GNOME with GDM..."
        echo "DE=gnome" > "$DE_CONF"
        systemctl disable synui.service 2>/dev/null || true
        systemctl enable gdm.service 2>/dev/null || true
        success "GNOME selected — GDM enabled"
        ;;
    4)
        echo "  No GUI selected."
        echo "DE=tty" > "$DE_CONF"
        systemctl disable synui.service 2>/dev/null || true
        ;;
    *)
        echo "  Keeping SynapseUI (default)."
        echo "DE=synui" > "$DE_CONF"
        systemctl enable synui.service 2>/dev/null || true
        success "SynapseUI selected"
        ;;
esac

# ── Mark firstboot complete ────────────────────────────────
mkdir -p "$(dirname "$DONE_FLAG")"
echo "firstboot completed: $(date)" > "$DONE_FLAG"

# ── Summary ───────────────────────────────────────────────
echo ""
line
echo ""
green "  SynapseOS is ready."
echo ""
echo "  $(bold 'synsh')          — AI shell"
echo "  $(bold 'syn status')     — system dashboard"
echo "  $(bold 'syn model')      — manage AI models"
echo ""
line
echo ""

# ── Launch chosen desktop ─────────────────────────────────
# NOTE: Do NOT use "exec systemctl start" here — this script runs inside
# synapseos-firstboot.service which has Before=synui.service.  Calling
# systemctl start synui would deadlock (synui waits for firstboot to
# finish, but firstboot is blocked waiting for synui).  Instead, just
# exit and let systemd's ordering start the chosen DE automatically.
case "${de_choice:-1}" in
    2)
        prompt "Press ENTER to start KDE Plasma..."
        read -r || true
        ;;
    3)
        prompt "Press ENTER to start GNOME..."
        read -r || true
        ;;
    4)
        prompt "Press ENTER to continue to shell..."
        read -r || true
        ;;
    *)
        prompt "Press ENTER to start SynapseUI..."
        read -r || true
        ;;
esac
