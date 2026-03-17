#!/usr/bin/env bash
# customize_airootfs.sh
#
# Executed by mkarchiso inside a chroot of the new root filesystem.
# This is where we do final system configuration that can't be done
# by dropping files into airootfs/ alone.
#
# SynapseOS Project — GPLv2

set -euo pipefail

# ── System identity ───────────────────────────────────────────
echo "SynapseOS" > /etc/hostname
echo "synapseos" > /etc/machine-id

cat > /etc/os-release << 'EOF'
NAME="SynapseOS"
PRETTY_NAME="SynapseOS 0.1.0"
ID=synapseos
ID_LIKE=arch
VERSION="0.1.0"
VERSION_ID="0.1.0"
BUILD_ID=rolling
ANSI_COLOR="38;5;51"
HOME_URL="https://synapseos.dev"
DOCUMENTATION_URL="https://synapseos.dev/docs"
SUPPORT_URL="https://synapseos.dev/support"
BUG_REPORT_URL="https://github.com/synapseos/synapseos/issues"
LOGO="synapseos"
EOF

# ── Users ─────────────────────────────────────────────────────
# Create system groups
groupadd -r synapse   2>/dev/null || true   # synapd group
groupadd -r synguard  2>/dev/null || true   # synguard group

# Live user: syn (passwordless sudo for demo)
useradd -m -G wheel,audio,video,input,synapse -s /bin/bash syn 2>/dev/null || true
echo "syn:synapse" | chpasswd
echo "%wheel ALL=(ALL:ALL) NOPASSWD: ALL" >> /etc/sudoers.d/wheel

# Root password disabled on live ISO
passwd -d root

# ── Shell defaults ────────────────────────────────────────────
# Make synsh the default for the syn user
echo "/usr/bin/synsh" >> /etc/shells
chsh -s /usr/bin/synsh syn

# System-wide shell aliases
cat > /etc/profile.d/synapseos.sh << 'EOF'
# SynapseOS environment
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=SynapseOS
# WAYLAND_DISPLAY is set by synui at runtime — do not hard-code it here

# Compositor hint
export SYNUI_RUNNING=0  # will be 1 when synui is active

# synsh as default
[ -f /usr/bin/synsh ] && export SHELL=/usr/bin/synsh

# Pretty prompt for bash (before synsh loads)
PS1='\[\033[38;5;51m\]⚡\[\033[0m\] \[\033[1m\]\u@synapseos\[\033[0m\]:\[\033[38;5;214m\]\w\[\033[0m\]\$ '

# syn CLI shortcut
alias s='syn'
alias sa='syn ask'
alias sd='syn do'
EOF

# ── systemd configuration ─────────────────────────────────────
# Enable services
systemctl enable synapd.service
systemctl enable synguard.service
systemctl enable NetworkManager.service
systemctl enable sshd.service
systemctl enable bluetooth.service
systemctl enable pipewire.service
systemctl enable pipewire-pulse.service
# synui.service is enabled via multi-user.target.wants/ symlink in airootfs
# seatd provides seat/DRM access for synui (Wayland compositor)
systemctl enable seatd.service
usermod -aG seat syn

# Display manager — firstboot wizard on tty1 handles DE selection.
# synui starts via synui.service (multi-user.target) after firstboot completes.
# Autologin as root so firstboot service can run interactively on tty1.
mkdir -p /etc/systemd/system/getty@tty1.service.d
cat > /etc/systemd/system/getty@tty1.service.d/autologin.conf << 'EOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin root --noclear %I $TERM
EOF

# SDDM base config (activated by firstboot wizard when KDE is selected)
if command -v sddm &>/dev/null; then
    mkdir -p /etc/sddm.conf.d
    cat > /etc/sddm.conf.d/synapseos-base.conf << 'SDDM'
[Theme]
Current=breeze

[General]
DisplayServer=wayland
GreeterEnvironment=QT_WAYLAND_SHELL_INTEGRATION=layer-shell
SDDM
fi

# ── Pacman configuration ──────────────────────────────────────
# Set up mirrors and enable multilib
cat >> /etc/pacman.conf << 'EOF'

[multilib]
Include = /etc/pacman.d/mirrorlist
EOF

# ── Default config files ──────────────────────────────────────
# synuirc for the syn user
mkdir -p /home/syn/.config/synui
cp /etc/synui/synuirc /home/syn/.config/synui/synuirc 2>/dev/null || \
cat > /home/syn/.config/synui/synuirc << 'EOF'
terminal = foot
ai_layout = on
ai_ctx_decor = on
start_overlay = on
workspace_3_intent = writing code and running tests
EOF

# foot terminal config with Synapse brand
mkdir -p /home/syn/.config/foot
cat > /home/syn/.config/foot/foot.ini << 'EOF'
[main]
font=JetBrainsMono Nerd Font:size=11

[colors]
background=0d0f14
foreground=cdd6f4
regular0=45475a
regular1=f38ba8
regular2=a6e3a1
regular3=f9e2af
regular4=89b4fa
regular5=f5c2e7
regular6=94e2d5
regular7=bac2de
bright0=585b70
bright1=f38ba8
bright2=a6e3a1
bright3=f9e2af
bright4=89b4fa
bright5=f5c2e7
bright6=94e2d5
bright7=a6ef87

[cursor]
color=0d0f14 12d9f5
EOF

# neovim config
mkdir -p /home/syn/.config/nvim
cat > /home/syn/.config/nvim/init.lua << 'EOF'
-- SynapseOS neovim defaults
vim.opt.number = true
vim.opt.termguicolors = true
vim.opt.expandtab = true
vim.opt.shiftwidth = 4
vim.opt.tabstop = 4
vim.opt.signcolumn = "yes"
vim.g.mapleader = " "
-- syn ask integration: <leader>a asks AI about selected text
vim.keymap.set("n", "<leader>a", function()
  local word = vim.fn.expand("<cword>")
  vim.cmd("terminal syn ask 'explain " .. word .. "'")
end)
EOF

# ── fastfetch config ──────────────────────────────────────────
mkdir -p /home/syn/.config/fastfetch
cat > /home/syn/.config/fastfetch/config.jsonc << 'FETCH'
{
  "$schema": "https://github.com/fastfetch-cli/fastfetch/raw/dev/doc/json_schema.json",
  "display": {
    "separator": "  ",
    "color": { "keys": "38;5;51", "title": "38;5;51" }
  },
  "logo": {
    "source": "synapseos",
    "padding": { "left": 2, "top": 1 }
  },
  "modules": [
    { "type": "title",  "format": "{user-name}@{host-name}" },
    "separator",
    { "type": "os",     "key": "OS" },
    { "type": "kernel", "key": "Kernel" },
    { "type": "packages","key": "Packages" },
    { "type": "shell",  "key": "Shell" },
    { "type": "display","key": "Resolution" },
    { "type": "de",     "key": "WM" },
    { "type": "cpu",    "key": "CPU" },
    { "type": "gpu",    "key": "GPU" },
    { "type": "memory", "key": "Memory" },
    { "type": "disk",   "key": "Disk" },
    "break",
    { "type": "custom", "format": "  \u26a1 synapd:  $(systemctl is-active synapd 2>/dev/null || echo offline)" },
    { "type": "custom", "format": "  \ud83d\udee1 synguard: $(systemctl is-active synguard 2>/dev/null || echo offline)" }
  ]
}
FETCH

# ── Fix permissions ───────────────────────────────────────────
chown -R syn:syn /home/syn

# ── MOTD ──────────────────────────────────────────────────────
cat > /etc/motd << 'MOTD'

  ⚡ SynapseOS — Where the kernel thinks.

  Getting started:
    syn ask "what should I know about this system?"
    syn do  "open a terminal and show system info"
    Super+Space  — AI command bar
    Super+A      — neural overlay

  Install:
    sudo syninstall

MOTD

echo ""
echo "customize_airootfs.sh complete"
