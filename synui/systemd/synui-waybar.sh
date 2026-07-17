#!/bin/sh
# synui-waybar — start waybar with SynapseOS's bar config.
#
# Why a wrapper instead of just shipping the config where waybar looks:
# /etc/xdg/waybar/{config.jsonc,style.css} are owned by the *waybar package*.
# Putting synui's there is a hard file conflict — pacstrap refuses the whole
# transaction, which is exactly how the ISO build failed. (It "worked" on the
# dev box only because build-all.sh installs with `pacman -U --overwrite '*'`,
# which silently took ownership of waybar's files. A conflict that only appears
# when you build an ISO is worse than one that appears immediately.)
#
# So the config lives in /usr/share/synui/waybar, which synui owns outright, and
# waybar is pointed at it explicitly.
#
# A user config still wins. waybar's own search order does that for free when it
# is given no -c/-s, so if ~/.config/waybar exists we hand over and let waybar
# find it — copying /usr/share/synui/waybar into your home and editing it works
# the way XDG leads you to expect. There is deliberately no merging: waybar does
# not merge either, and a half-ours-half-yours bar would be unexplainable.
#
# SynapseOS Project — GPLv2
# https://github.com/velle999/SYNAPSE
set -u

CONF_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
SYNUI_BAR="/usr/share/synui/waybar"

# waybar accepts config or config.jsonc; check both, or a user who wrote the
# other name gets silently overridden by ours.
for user_cfg in "$CONF_HOME/waybar/config.jsonc" "$CONF_HOME/waybar/config"; do
    if [ -f "$user_cfg" ]; then
        exec waybar "$@"
    fi
done

exec waybar -c "$SYNUI_BAR/config.jsonc" -s "$SYNUI_BAR/style.css" "$@"
