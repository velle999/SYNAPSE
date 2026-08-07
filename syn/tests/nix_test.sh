#!/usr/bin/env bash
# nix_test.sh — the Nix configurator holds together
#
# `syn nix` bridges two languages, and the seam between them is facts.nix: a
# shell script writes it, Nix expressions read it. Nothing checks that seam at
# build time. A field renamed in syn-nix-facts.sh, or a new facts.want.X used
# in home.nix before the generator emits it, produces an evaluation error on a
# user's machine on their first `syn nix apply` — which is the worst possible
# place to find out, because it is also the slowest.
#
# So the first check below is the schema one, and it needs no nix at all: every
# `facts.<path>` the expressions reference must be a key the generator actually
# writes. The rest are guarded — parse and evaluation run only where a nix
# binary exists, and say so plainly where it does not, the same way
# install_test.sh aborts without sshpass rather than passing vacuously.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
syn="$here/.."
flake="$syn/nix-flake.nix"
home="$syn/nix-home.nix"
gen="$syn/syn-nix-facts.sh"

fails=0
check() {  # check <description> <expected> <actual>
    if [ "$2" = "$3" ]; then
        printf '  ok    %s\n' "$1"
    else
        printf '  FAIL  %s — expected %s, got %s\n' "$1" "$2" "$3"
        fails=$((fails + 1))
    fi
}

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "=== files ==="
for f in "$flake" "$home" "$gen"; do
    check "$(basename "$f") exists" yes "$([ -f "$f" ] && echo yes || echo no)"
done

# The PKGBUILD is the only thing that puts these on a disk. Shipped under
# their real names, from the flat nix- prefixed sources makepkg requires.
pkgb="$syn/PKGBUILD"
check "PKGBUILD ships flake.nix" yes \
      "$(grep -q 'nix-flake.nix" "$pkgdir/usr/share/syn/nix/flake.nix"' "$pkgb" && echo yes || echo no)"
check "PKGBUILD ships home.nix" yes \
      "$(grep -q 'nix-home.nix"  "$pkgdir/usr/share/syn/nix/home.nix"' "$pkgb" && echo yes || echo no)"
check "PKGBUILD ships the fact generator" yes \
      "$(grep -q 'usr/lib/syn/syn-nix-facts' "$pkgb" && echo yes || echo no)"

echo ""
echo "=== the generator runs ==="
if ! bash "$gen" / > "$tmp/facts.nix" 2>"$tmp/gen.err"; then
    echo "  ABORT  syn-nix-facts failed:"; sed 's/^/    /' "$tmp/gen.err"; exit 1
fi
check "it wrote something" yes "$([ -s "$tmp/facts.nix" ] && echo yes || echo no)"
check "gpu is one of the five documented values" yes \
      "$(grep -qE 'gpu +=  *"(nvidia|amd|intel|vm|none)";' "$tmp/facts.nix" && echo yes || echo no)"
# The `want = {` header is inside the range and carries an `=` of its own, so
# it has to come out before the rest are held to "= true;" / "= false;".
check "every want.* is a Nix boolean" yes \
      "$(awk '/want = \{/,/\};/' "$tmp/facts.nix" | grep -E '=' | grep -v 'want = {' |
         grep -qvE '= *(true|false);' && echo no || echo yes)"

echo ""
echo "=== schema: what the expressions read is what the generator writes ==="
#
# COMMENTS FIRST. Both files discuss "facts.nix" the FILE in their prose, and
# an extractor that reads those finds a reference to an attribute named `nix`
# that no generator will ever emit — a failure reported against correct code,
# which is worse than no check at all. Neither file has a # inside a string,
# so cutting at the first one is safe here.
code=$(sed 's/#.*//' "$flake" "$home")

# Two reference forms, and the second is easy to forget: flake.nix uses
# `inherit (facts) system;`, which never spells "facts.system" anywhere.
# The lookbehind is not decoration either: `import ./facts.nix` is the flake
# loading the FILE, and without it that reads as an attribute `facts.nix`.
refs=$( { grep -ohP '(?<![./\w])facts\.[a-zA-Z_][a-zA-Z0-9_.]*' <<<"$code" |
            sed 's/^facts\.//'
          grep -ohE 'inherit \(facts\) [a-zA-Z0-9_ ]+;' <<<"$code" |
            sed -e 's/inherit (facts) //' -e 's/;//' | tr ' ' '\n'
        } | grep -v '^$' | sort -u)

# Keys the generator emits: top level at two spaces, want.* at four. Mixed
# case, because allowUnfree is one of them.
emitted=$( { awk -F' *= *' '/^  [a-zA-Z]+ +=/ { gsub(/ /,"",$1); print $1 }' "$tmp/facts.nix"
             awk -F' *= *' '/^    [a-zA-Z]+ +=/ { gsub(/ /,"",$1); print "want." $1 }' "$tmp/facts.nix"
           } | sort -u)

printf '  reads:   %s\n' "$(echo $refs)"
printf '  emits:   %s\n' "$(echo $emitted)"
for r in $refs; do
    # `want` on its own is the attrset holding the rest, not a leaf.
    [ "$r" = want ] && continue
    check "facts.$r is emitted" yes \
          "$(grep -qxF "$r" <<<"$emitted" && echo yes || echo no)"
done

echo ""
echo "=== nix evaluation ==="
#
# NIX may be absent — it is an optdepend of syn, and this box is not required
# to have opted in. Skipped loudly rather than silently.
if ! command -v nix-instantiate >/dev/null 2>&1; then
    echo "  SKIP  nix-instantiate not on PATH — parse and eval checks not run."
    echo "        Install nix to exercise them: sudo pacman -S nix"
else
    d="$tmp/eval"; mkdir -p "$d/fakenixpkgs"
    cp "$flake" "$d/flake.nix"; cp "$home" "$d/home.nix"; cp "$tmp/facts.nix" "$d/"

    for f in flake.nix home.nix facts.nix; do
        check "$f parses" yes \
              "$(nix-instantiate --parse "$d/$f" >/dev/null 2>&1 && echo yes || echo no)"
    done

    # Stub nixpkgs and home-manager so outputs can be applied without fetching
    # a few hundred MB. This does not prove the config BUILDS — only nixpkgs
    # can say that — but it does prove flake.nix, facts.nix and home.nix agree
    # with each other, which is the part that is ours to get wrong.
    cat > "$d/fakenixpkgs/default.nix" <<'STUB'
{ system ? "?", config ? {} }: { inherit system config; }
STUB
    cat > "$d/drive.nix" <<'DRIVE'
let
  flake = import ./flake.nix;
  lib.optionals = c: l: if c then l else [];
  hm.lib.homeManagerConfiguration = { pkgs, modules, extraSpecialArgs }: {
    inherit pkgs;
    module = (import (builtins.head modules)) {
      inherit lib pkgs; inherit (extraSpecialArgs) facts; config = {};
    };
  };
  cfg = (flake.outputs { nixpkgs = ./fakenixpkgs; home-manager = hm; })
        .homeConfigurations.synapse;
in {
  system       = cfg.pkgs.system;
  username     = cfg.module.home.username;
  home         = cfg.module.home.homeDirectory;
  genericLinux = cfg.module.targets.genericLinux.enable;
  packages     = cfg.module.home.packages;
}
DRIVE
    if out=$(nix-instantiate --eval --strict --json -A username "$d/drive.nix" 2>"$tmp/eval.err"); then
        check "the flake evaluates end to end" yes yes
        expect=$(sed -n 's/^  username *= *"\(.*\)";/\1/p' "$tmp/facts.nix")
        check "home.username comes from facts" "\"$expect\"" "$out"
        check "targets.genericLinux is on (this is not NixOS)" true \
              "$(nix-instantiate --eval -A genericLinux "$d/drive.nix" 2>/dev/null)"
    else
        check "the flake evaluates end to end" yes no
        sed 's/^/    /' "$tmp/eval.err" | head -20
    fi
fi

echo ""
if [ "$fails" -eq 0 ]; then echo "all checks passed"; else echo "$fails check(s) failed"; exit 1; fi
