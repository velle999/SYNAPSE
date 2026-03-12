#!/usr/bin/env bash
# syn-firstboot — SynapseOS first boot setup wizard
set -euo pipefail

MODEL_DIR="/var/lib/synapd/models"
MODEL_PATH="$MODEL_DIR/synapse.gguf"
DONE_FLAG="/var/lib/synguard/.firstboot_done"
COLS=$(tput cols 2>/dev/null || echo 80)

# -- UI helpers --------------------------------------------
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

step() {
    echo ""
    cyan "  ▶ $*"
    echo ""
}

success() {
    echo ""
    green "  ✓ $*"
    echo ""
}

fail() {
    echo ""
    red "  ✗ $*"
    echo ""
}

prompt() {
    printf "  $(bold "$1") "
}

# -- Check if already done ---------------------------------
# On live ISO always run firstboot
if mountpoint -q /run/archiso/airootfs 2>/dev/null; then
    LIVE_ISO=1
else
    LIVE_ISO=0
fi

if [ "$LIVE_ISO" = "0" ] && [ -f "$DONE_FLAG" ] && [ -f "$MODEL_PATH" ]; then
    exit 0
fi


# -- Detect if running from live ISO ----------------------
# already set above

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
    read -r install_choice

    if [ "${install_choice:-2}" = "1" ]; then
        exec /usr/bin/syn-install
    fi
fi

# -- Welcome -----------------------------------------------
header
echo "  Welcome to SynapseOS."
echo ""
echo "  This wizard will set up your system in a few steps:"
echo "    1. Network check"
echo "    2. AI model download"
echo "    3. System verification"
echo ""
prompt "Press ENTER to begin, or Ctrl+C to skip..."
read -r

# -- Step 1: Network ---------------------------------------
header
step "Step 1/3 — Network"

echo "  Checking network connectivity..."
if ping -c 1 -W 3 8.8.8.8 &>/dev/null; then
    success "Network is up"
    HAVE_NET=1
else
    echo "  No network detected. Attempting to connect..."
    systemctl start NetworkManager 2>/dev/null || true
    sleep 3
    nmcli device connect "$(nmcli -t -f DEVICE d | head -1)" 2>/dev/null || true
    sleep 5
    if ping -c 1 -W 3 8.8.8.8 &>/dev/null; then
        success "Network connected"
        HAVE_NET=1
    else
        fail "No network available"
        HAVE_NET=0
        echo "  You can download a model later with: $(cyan 'syn model download')"
    fi
fi

# -- Step 2: Model -----------------------------------------
header
step "Step 2/3 — AI Model"

if [ -f "$MODEL_PATH" ]; then
    success "Model already installed ($(du -sh "$MODEL_PATH" | cut -f1))"
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
    read -r choice

    case "$choice" in
        1) MODEL="tiny" ;;
        2) MODEL="phi3" ;;
        3) MODEL="mistral-7b" ;;
        4) MODEL="" ;;
        *) MODEL="tiny" ;;
    esac

    if [ -n "$MODEL" ]; then
        echo ""
        echo "  Downloading $(bold "$MODEL")..."
        echo ""
        mkdir -p "$MODEL_DIR"
        if syn-model download "$MODEL"; then
            success "Model downloaded"
        else
            fail "Download failed — run: syn model download $MODEL"
        fi
    else
        echo "  Skipped. Run $(cyan 'syn model download') later."
    fi
fi

# -- Step 3: Verify ----------------------------------------
header
step "Step 3/3 — System Check"

check() {
    local name="$1" cmd="$2"
    printf "  %-20s" "$name"
    if eval "$cmd" &>/dev/null; then
        green "✓"
    else
        red "✗"
    fi
    echo ""
}

check "synapd"        "systemctl is-active synapd"
check "synnet"        "systemctl is-active synnet"
check "synguard"      "systemctl is-active synguard"
check "synapse_kmod"  "lsmod | grep -q synapse_kmod"
check "AI model"      "test -f $MODEL_PATH"
check "network"       "ping -c1 -W2 8.8.8.8"

# -- Done --------------------------------------------------
echo ""
line
echo ""
green "  SynapseOS is ready."
echo ""
echo "  $(bold 'synsh')  — AI shell (type naturally or use commands)"
echo "  $(bold 'syn status')  — system dashboard"
echo "  $(bold 'syn info')    — system info"
echo "  $(bold 'syn model')   — manage AI models"
echo ""
line
echo ""

# Mark done
mkdir -p "$(dirname "$DONE_FLAG")"
touch "$DONE_FLAG"

prompt "Press ENTER to launch synsh..."
read -r
exec /usr/bin/synsh
