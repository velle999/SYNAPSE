# Render a SynapseOS install profile to the key=value lines syn-install reads.
#
#   nix-instantiate --eval --strict --raw --argstr profile /abs/path.nix render.nix
#
# Driven by `syn nix profile` and by `syn-install --config foo.nix`. The
# formatting is done HERE, in Nix, rather than by piping JSON through jq: the
# live ISO is not required to carry jq, and `--raw` already emits a string
# exactly as written.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
{ profile }:

let
  p = import (/. + profile);

  # Booleans become yes/no because that is what the installer's prompts take,
  # and what `answer`'s yes=y,no=n maps expect. Writing `true` in the profile
  # and having it arrive as the string "true" would land in the fall-through
  # of a [y/N] case and read as NO — silently, which is the failure this
  # whole file exists to avoid.
  scalar = k: v:
    if builtins.isBool v then (if v then "yes" else "no")
    else if builtins.isInt v then toString v
    else if builtins.isString v then v
    else throw ("install profile key '" + k + "' must be a string, boolean or "
                + "integer, not a " + builtins.typeOf v);

  # One level of nesting is flattened with an underscore, so the natural
  #     want = { steam = true; };
  # produces want_steam=yes — which is the key ask_opt derives from the
  # WANT_STEAM variable it sets. Deeper nesting is refused rather than
  # flattened further; nothing in the installer has a two-level key.
  render = k: v:
    if builtins.isAttrs v
    then builtins.concatStringsSep "\n"
           (map (k2: k + "_" + k2 + "=" + scalar (k + "." + k2) v.${k2})
                (builtins.attrNames v))
    else k + "=" + scalar k v;

in
builtins.concatStringsSep "\n" (map (k: render k p.${k}) (builtins.attrNames p)) + "\n"
