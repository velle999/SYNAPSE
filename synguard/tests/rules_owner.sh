#!/bin/sh
# The shipped rules have ONE owner, and it is package(): /etc/synguard/rules.d,
# the directory the daemon reads (SYNGUARD_RULES_DIR), 0750 with every file
# 0640. Up to 51 meson installed the same files a second time, to
# /etc/synguard/rules — a directory nothing reads, 0755 with every file 0644 —
# so the policy rules.d keeps from other users was world-readable one
# directory over.
#
# Read from meson's own install manifest, so this is what `meson install` does,
# not what meson.build looks like.
#
# Usage: rules_owner.sh <build dir>
installed="$1/meson-info/intro-installed.json"
[ -r "$installed" ] || { echo "FAIL: cannot read $installed"; exit 1; }

if grep -o '"/etc/synguard/rules[^"]*"' "$installed"; then
    echo "FAIL: meson installs the rules (above); package() owns /etc/synguard/rules.d"
    exit 1
fi
echo "ok: meson installs no rules; package() is their one owner"
