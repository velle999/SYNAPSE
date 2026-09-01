#!/usr/bin/env bash
# i18n.sh — a translated string must be reachable by a translator.
#
# The two ways a UI string goes quietly English, neither of which the compiler,
# the linker or any other test can see:
#
#   1. THE FILE IS NOT IN po/POTFILES. Its _() calls compile and look up
#      normally, so nothing warns — the strings simply never reach the .pot, so
#      no translator is ever offered them, and every language shows English for
#      that file forever.
#
#   2. A TABLE ENTRY IS MARKED WITH N_() AND NEVER LOOKED UP WITH _(), or the
#      reverse. N_() alone extracts the string and translates nothing; _() alone
#      translates but is invisible to xgettext. A string needs both.
#
# Also asserts the thing that would be catastrophic rather than merely absent:
# that the control panel's SETTINGS KEYS were not swept up in the marking.
# ctl_items[] carries the synuirc key in the field beside the label, and a
# translated key does not make a German panel — it makes a panel that writes
# `Erscheinungsbild = 1` into the config and silently loses the row.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
fails=0

check() {
    if [ "$2" = "$3" ]; then printf '  ok    %s\n' "$1"
    else printf '  FAIL  %s — expected [%s], got [%s]\n' "$1" "$2" "$3"; fails=$((fails+1)); fi
}

echo "synui translations"

listed=$(grep -vE '^\s*#|^\s*$' "$root/po/POTFILES" | sort)

# ── 1. every file that uses _() is offered to xgettext ────
missing=""
while IFS= read -r f; do
    rel=${f#"$root"/}
    grep -qxF "$rel" <<<"$listed" || missing="$missing $rel"
done < <(grep -rlE '(^|[^A-Za-z_])_\(' "$root/src" --include='*.c' 2>/dev/null | sort)
check "every source using _() is in po/POTFILES" "" "$missing"

# ── 2. every file in POTFILES exists ──────────────────────
gone=""
while IFS= read -r rel; do
    [ -f "$root/$rel" ] || gone="$gone $rel"
done <<<"$listed"
check "every file in po/POTFILES exists" "" "$gone"

# ── 3. the template is not stale ──────────────────────────
# Regenerated into a temp file and compared on msgids alone: the .pot carries a
# POT-Creation-Date that changes every run, so a plain diff always differs.
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
( cd "$root" && xgettext --from-code=UTF-8 --keyword=_ --keyword=N_ --keyword=_opt \
    -o "$tmp/new.pot" $(grep -vE '^\s*#|^\s*$' po/POTFILES) 2>/dev/null )
have=$(grep -c '^msgid "' "$root/po/synui.pot" 2>/dev/null || echo 0)
now=$(grep -c '^msgid "' "$tmp/new.pot" 2>/dev/null || echo 0)
check "po/synui.pot is current ($have msgids)" "$now" "$have"

# ── 4. a marked string is actually looked up ──────────────
check "N_() is paired with a _() lookup in ctlpanel.c" yes \
      "$(grep -q 'N_("Theme")' "$root/src/ctlpanel.c" &&
         grep -q '_(ctl_items\[i\].label)' "$root/src/ctlpanel.c" && echo yes || echo no)"

# ── 5. ⛔ the settings keys were NOT translated ────────────
check "the synuirc key beside the label is untouched" yes \
      "$(grep -q 'N_("Theme"),            "theme"' "$root/src/ctlpanel.c" && echo yes || echo no)"
check "no settings key reached the template" "0" \
      "$(grep -cE '^msgid "(theme|wallpaper_accent|blur_passes|dock_opacity)"$' "$root/po/synui.pot")"

# ── 6. ⛔ NOTHING INSIDE A _name() OR _key() FUNCTION IS MARKED ──
#
# The second batch of marking was done with a regex over
# `case *_ROW*: return "…"`, and it swept up two functions that look exactly
# like label tables and are not:
#
#   widget_row_name()  returns the name synui-widgets knows a row by — an IPC
#                      name. A translated one addresses a widget that does not
#                      exist, so the row silently stops working.
#   uifx_key()         returns the synuirc key. A translated one writes
#                      `Unschärfe = 1` into the config and loses the setting.
#
# The convention already distinguishes them — _label() is read by a person,
# _name() and _key() are read by a protocol or a file — so the check is that
# convention, enforced. This is the same rule as the ctlpanel settings key one
# row over, and it is the second time it has been needed.
bad=$(awk '
    /^(static )?const char \*[a-z_]+\(/ { fn = $0 }
    /return _\(/ && fn ~ /_(name|key)\(/ { print FILENAME ":" FNR }
' "$root"/src/*.c)
check "no _() inside a _name() or _key() function" "" "$bad"

# ── 7. every language named has a catalog, or meson refuses ──
absent=""
while IFS= read -r l; do
    [ -f "$root/po/$l.po" ] || absent="$absent $l"
done < <(grep -vE '^\s*#|^\s*$' "$root/po/LINGUAS")
check "every language in LINGUAS has a .po (meson requires it)" "" "$absent"

echo
if [ "$fails" -eq 0 ]; then echo "all synui translation checks passed"; else echo "$fails failed"; fi
exit $(( fails > 0 ))
