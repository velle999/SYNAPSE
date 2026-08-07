# Your declarative user environment. Edit this file, then `syn nix apply`.
#
# `facts` is the bridge: /etc/synapseos/nix/facts.nix, regenerated from the
# live machine by `syn nix facts`. Branch on it instead of hand-editing this
# file per machine — see the examples at the bottom.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
{ lib, pkgs, facts, ... }:

{
  home.username = facts.username;
  home.homeDirectory = "/home/${facts.username}";

  # SynapseOS is Arch, not NixOS, and this one line is what makes a Nix user
  # environment behave on a foreign distro. It appends the Nix profile to
  # XDG_DATA_DIRS — so .desktop files from packages below appear in synui's
  # launcher and the bar menu instead of being installed but unlaunchable —
  # and it sources the distro's own profile first, so PATH gains Nix rather
  # than being replaced by it.
  targets.genericLinux.enable = true;

  # Puts the `home-manager` command itself in the profile. `syn nix apply`
  # does not need it (it builds the activation package straight out of the
  # flake), but `home-manager generations` and rollback do.
  programs.home-manager.enable = true;

  # NOT "which version am I running". stateVersion is "whose defaults do I
  # want kept", and raising it silently changes defaults underneath a working
  # config. Pinned low on purpose; leave it unless you have read the release
  # notes between here and there.
  home.stateVersion = "24.11";

  # ── Packages ───────────────────────────────────────────────────────────
  #
  # pacman still owns the system — compositor, daemons, drivers, the whole
  # SynapseOS core. This list is for USER tooling.
  #
  # Anything named here that pacman ALSO installs will shadow the pacman copy
  # on PATH, because targets.genericLinux puts the Nix profile first. That is
  # occasionally what you want (a newer version than Arch ships) and usually
  # not (two copies of the same thing, and `pacman -Qo` no longer explains
  # which one you are running). Prefer things pacman does not have.
  home.packages = with pkgs; [
    # ripgrep
    # fd
    # jq
  ]

  # ── Bridged to the install ─────────────────────────────────────────────
  #
  # Everything below reacts to facts.nix rather than to an edit. Reinstall on
  # a different box, run `syn nix facts`, and the same home.nix gives you a
  # different environment.

  # Only on a machine whose install actually took the game stack. On one that
  # declined it these would be several hundred MB of dead weight.
  ++ lib.optionals facts.want.steam [
    # protontricks
    # gamescope   # pacman's came with the Steam group; this would shadow it
  ]

  # BlackArch is a repo, not a package set, so its presence is a decent proxy
  # for "this box is used for security work".
  ++ lib.optionals facts.want.blackarch [
    # nmap
    # sqlmap
  ]

  # A GPU that is not the VM fallback.
  ++ lib.optionals (facts.gpu != "vm" && facts.gpu != "none") [
    # vulkan-tools
  ];

  # ── Do NOT manage synui's config from here ─────────────────────────────
  #
  # It is tempting: home.file."./config/synui/synuirc" and the desktop is
  # declarative too. It does not work, and it fails in a shape that takes a
  # while to recognise.
  #
  # Home Manager materialises the files it manages as READ-ONLY symlinks into
  # /nix/store. But synuirc is written at runtime — by synui itself and by
  # `synctl` — every time a setting changes. Declaring it here does not make
  # this file the winner; it makes every write from the control panel fail,
  # and that surfaces as a panel whose toggles flip back a second later with
  # no error anywhere the user can see.
  #
  # The same applies to anything else written live: ~/.config/fetch/logo.txt,
  # the wallpaper engine's state, kdeglobals while a KDE app is running.
  # Declare the PACKAGES here; leave the live state to the thing that owns it.
}
