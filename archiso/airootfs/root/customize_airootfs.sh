#!/usr/bin/env bash
# customize_airootfs.sh — runs inside the airootfs chroot at ISO build time.
#
# Only things that can't be plain files in airootfs/ belong here: users,
# groups, passwords, /etc/shells registration. Configs, units, and MOTD
# are declarative files under archiso/airootfs/.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

# ── Passwords (live media only — changeable in firstboot) ─────
echo 'root:synapse' | chpasswd

# ── Groups ────────────────────────────────────────────────────
groupadd -r synapse  2>/dev/null || true
groupadd -r synguard 2>/dev/null || true

# ── Generate the locale /etc/locale.conf actually names ───────
#
# airootfs ships /etc/locale.conf (LANG=en_US.UTF-8) and /etc/locale.gen with
# en_US.UTF-8 uncommented, and nothing ever ran locale-gen — so the ISO booted
# pointing LANG at a locale that did not exist. Only C.utf8 was in
# /usr/lib/locale, which is why foot greets every live session with
# "invalid locale, falling back to 'C.UTF-8'" and why anything that sorts,
# formats a date, or measures a string got the C locale's answer instead of the
# configured one.
#
# This is the chroot, so it is the one place that can generate it: locale-gen
# writes /usr/lib/locale/locale-archive into the image at build time. A declar-
# ative file cannot do it — which is exactly why it belongs here per this
# script's own rule about what can't be a plain file.
locale-gen

# ── synsh is a valid login shell ──────────────────────────────
grep -qxF '/usr/bin/synsh' /etc/shells || echo '/usr/bin/synsh' >> /etc/shells

# ── Live user: syn (passwordless sudo for demo) ───────────────
useradd -m -G wheel,audio,video,input,synapse,seat -s /usr/bin/synsh syn \
    2>/dev/null || true
echo 'syn:synapse' | chpasswd
install -Dm440 /dev/null /etc/sudoers.d/wheel
echo '%wheel ALL=(ALL:ALL) NOPASSWD: ALL' > /etc/sudoers.d/wheel

# ── syn user configs ──────────────────────────────────────────
# kitty terminal — Synapse brand colors.
#
# dynamic_background_opacity ships ON deliberately. kitty cannot enable that
# option via a config reload (its own docs say so), and a background_opacity
# change only lands on reload if dynamic opacity was already on when kitty
# started — so a live session whose kitty.conf lacked it would have synui's
# transparency slider do nothing at all, unfixably, for the life of that kitty.
install -dm755 /home/syn/.config/kitty
cat > /home/syn/.config/kitty/kitty.conf << 'EOF'
font_family              monospace
font_size                11

background_opacity         0.92
dynamic_background_opacity yes

background               #0d0f14
foreground               #cdd6f4
cursor                   #12d9f5
cursor_text_color        #0d0f14

color0                   #45475a
color1                   #f38ba8
color2                   #a6e3a1
color3                   #f9e2af
color4                   #89b4fa
color5                   #f5c2e7
color6                   #94e2d5
color7                   #bac2de
color8                   #585b70
color9                   #f38ba8
color10                  #a6e3a1
color11                  #f9e2af
color12                  #89b4fa
color13                  #f5c2e7
color14                  #94e2d5
color15                  #a6ef87
EOF

# foot, same palette — the rescue terminal. kitty needs working OpenGL; foot
# renders on the CPU, which is what makes it the useful fallback in a VM on
# llvmpipe. It is 793 KiB, so carrying it is what makes synui's
# `kitty || foot || alacritty || xterm` chain real rather than decorative.
install -dm755 /home/syn/.config/foot
cat > /home/syn/.config/foot/foot.ini << 'EOF'
[main]
font=monospace:size=11

# [colors] is deprecated in foot >= 1.17 and [cursor].color was removed
# outright — the palette and the cursor colour both live in [colors-dark].
[colors-dark]
background=0d0f14
foreground=cdd6f4
cursor=0d0f14 12d9f5
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
EOF

# fastfetch — branded logo config. fastfetch isn't on the ISO (`syn info`
# falls back), but syn-install copies this to installed systems.
# areofyl/fetch reads its logo only from ~/.config/fetch/logo.txt -- it cannot
# be pointed at a system path -- so the shared mark is COPIED rather than
# referenced. Same file fastfetch renders below: one asset, two consumers, so a
# change to the mark cannot land in one and miss the other.
#
# Without this file fetch falls back through ID_LIKE to fastfetch's ARCH logo,
# which is a perfectly good logo and the wrong distro's.
if [ -r /usr/share/synapseos/logo.txt ]; then
    install -dm755 /home/syn/.config/fetch
    install -m644 /usr/share/synapseos/logo.txt /home/syn/.config/fetch/logo.txt
fi

install -dm755 /home/syn/.config/fastfetch
cat > /home/syn/.config/fastfetch/config.jsonc << 'EOF'
{
  "$schema": "https://github.com/fastfetch-cli/fastfetch/raw/dev/doc/json_schema.json",
  "display": {
    "separator": "  ",
    "color": { "keys": "38;5;51", "title": "38;5;51" }
  },
  "logo": {
    "type": "file-raw",
    "source": "/usr/share/synapseos/logo.txt",
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
    { "type": "wm",     "key": "WM" },
    { "type": "cpu",    "key": "CPU" },
    { "type": "gpu",    "key": "GPU" },
    { "type": "memory", "key": "Memory" },
    { "type": "disk",   "key": "Disk" },
    "break",
    { "type": "command", "key": "  ⚡ synapd",   "text": "systemctl is-active synapd 2>/dev/null || echo offline" },
    { "type": "command", "key": "  🛡 synguard", "text": "systemctl is-active synguard 2>/dev/null || echo offline" }
  ]
}
EOF

chown -R syn:syn /home/syn

# ── The wallpaper's accent, on the hardware that has lights in it ────────────
#
# The live image autologins ROOT and the desktop runs as root, so the state and
# the user unit go in /root — /home/syn above is the template syn-install copies
# from, not the session. syn-install writes the same pair into the new user's
# home, and this is here so the live desktop's Appearance ▸ RGB lights row reads
# the same On it will read after installing.
#
# ⚠ syn-rgb ITSELF STILL SHIPS OFF. Its rule is about not taking over hardware
# on an UPDATE; a live session has nothing to preserve. `syn-rgb off` turns it
# back off, and nothing here survives a reboot anyway.
#
# openrgb is NOT on the ISO, and that is fine on purpose: the row is drawn
# whether openrgb is installed or not (a toggle that vanishes with its optdepend
# is a feature nobody finds out about), syn-rgb says which package is missing
# and exits, and the unit treats that as its ordinary answer rather than a
# failure. So this costs the live session one file and one symlink.
#
# The symlink is what `systemctl --user enable` would have written for
# WantedBy=default.target. It cannot be run here — there is no user bus in the
# build chroot — and systemd reads the file it would have made just the same.
install -dm755 /root/.config/synui
cat > /root/.config/synui/rgb.state << 'EOF'
# syn-rgb — the desktop accent on the lights.
on=yes
EOF
if [ -f /usr/lib/systemd/user/syn-rgb.path ]; then
    install -dm755 /root/.config/systemd/user/default.target.wants
    ln -sf /usr/lib/systemd/user/syn-rgb.path \
        /root/.config/systemd/user/default.target.wants/syn-rgb.path
else
    echo "customize_airootfs.sh: syn-rgb.path missing; RGB lights stay off"
fi

echo "customize_airootfs.sh complete"
