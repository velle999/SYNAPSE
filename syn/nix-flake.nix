# SynapseOS — declarative user environment, bridged to the running system.
#
# Installed to /etc/synapseos/nix by syn-install, driven by `syn nix`.
# Three files, and only one of them is yours:
#
#   facts.nix   GENERATED. What this machine actually is — GPU, desktop, which
#               optional groups the install ended up with. Do not edit it; edit
#               the machine and re-run `syn nix facts`.
#   home.nix    YOURS. Packages and dotfiles, free to branch on facts.
#   flake.nix   this file: the inputs, and the wiring between those two.
#
# DELIBERATELY NOT A GIT REPOSITORY. Flakes copy their directory into the
# store, and inside a git repo they copy only what git TRACKS — so a freshly
# generated facts.nix would be invisible and the build would either evaluate
# last week's facts or fail with "path does not exist", neither of which points
# anywhere near git. If you ever want this under version control, keep the repo
# somewhere else and symlink, or `git add` facts.nix on every regeneration.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
{
  description = "SynapseOS user environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    home-manager = {
      url = "github:nix-community/home-manager";
      # home-manager's default branch tracks nixpkgs-unstable, so the pair
      # above is matched. If you pin nixpkgs to a RELEASE channel
      # (nixos-25.05, say) you must ALSO move home-manager to its matching
      # release-25.05 branch — mismatching them is the usual way this stops
      # evaluating, and the error names some module option rather than the
      # version skew that actually caused it.
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { nixpkgs, home-manager, ... }:
    let
      facts = import ./facts.nix;

      pkgs = import nixpkgs {
        inherit (facts) system;
        # nvidia drivers and Steam are unfree. Rather than make you discover
        # that through an evaluation error, the fact generator decides it from
        # what is actually installed — see `syn nix facts`.
        config.allowUnfree = facts.allowUnfree;
      };
    in
    {
      # Named `synapse`, not after the account: `syn nix` then has one stable
      # attribute to build, and renaming the user does not break the command.
      # The real username reaches home.nix through facts.
      homeConfigurations.synapse = home-manager.lib.homeManagerConfiguration {
        inherit pkgs;
        modules = [ ./home.nix ];
        extraSpecialArgs = { inherit facts; };
      };
    };
}
