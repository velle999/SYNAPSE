#!/usr/bin/env bash
#
# qml_test.sh — the scan window, as far as it can be checked without a
# compositor.
#
# ⛔ NO HEADLESS RENDER: quickshell needs a Wayland session, and every way of
# giving it one from a test ends at the developer's live desktop.
#
# ⛔ THE GAP THIS EXISTS TO CLOSE. 0.1.0-1 shipped a package holding thirteen
# generated message catalogs and NO I18n.qml — meson installed the JSON and
# never installed the singleton that reads it, so `import "qml"` failed,
# quickshell refused the file and the window did not open. Every other test was
# green, because the binary was perfect. It was found by listing the contents of
# a clean-room package, not by any check. Now a check.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

ROOT=${1:-.}
QML="$ROOT/data/syn-scan.qml"
[ -f "$QML" ] || { echo "no such file: $QML" >&2; exit 1; }

pass=0; fail=0
ok()  { pass=$((pass+1)); echo "  ok    $1"; }
bad() { fail=$((fail+1)); echo "  FAIL  $1"; [ $# -gt 1 ] && echo "        $2"; }

echo "qml"

# ── every file the window imports is INSTALLED ──────────────────────────────
#
# The bug above, as an assertion. A file can sit correctly in data/ and be
# absent from the package, and nothing about the source tree says so.
for f in data/syn-scan.qml data/qml/I18n.qml data/qml/qmldir; do
    [ -f "$ROOT/$f" ] || { bad "$f is missing from the source tree"; continue; }
    if grep -qF "'$f'" "$ROOT/meson.build"; then
        ok "$f is installed by meson.build"
    else
        bad "$f exists but meson.build never installs it" \
            "the package will be missing it and the window will not open"
    fi
done

# The singleton has to be DECLARED, or the directory is not a module and the
# import resolves to nothing.
grep -q '^singleton I18n 1.0 I18n.qml$' "$ROOT/data/qml/qmldir" 2>/dev/null \
    && ok "qmldir declares the I18n singleton" \
    || bad "qmldir does not declare 'singleton I18n 1.0 I18n.qml'"

# And the window has to actually ask for that module.
grep -q '^import "qml"' "$QML" \
    && ok "the window imports the qml module" \
    || bad "the window does not import \"qml\""

# ── the linter ──────────────────────────────────────────────────────────────
#
# ⛔ Qt 6's LINTER, BY ITS FULL PATH. /usr/bin/qmllint belongs to another
# toolkit, accepts the file, and reports nothing whatever is wrong with it.
LINT=/usr/lib/qt6/bin/qmllint

# ⛔ THESE THREE ARE PROMOTED TO ERRORS. qmllint calls an assignment to a
# property that does not exist a *Warning*, but quickshell REFUSES to load the
# file over it and the window never opens at all.
LINTARGS=(--missing-property error --unresolved-type error --unresolved-alias error)

if [ -x "$LINT" ]; then
    # ⚠ ERRORS ONLY, which is what the three flags above are for. They promote
    # the findings quickshell REFUSES to load over; what stays a Warning —
    # "Unqualified access" on modelData inside a delegate, chiefly — is legal
    # QML that loads and runs. Failing on warnings here would mean failing on
    # every delegate this window has.
    out=$("$LINT" "${LINTARGS[@]}" "$QML" 2>&1)
    errs=$(grep -c "^Error:" <<<"$out" || true)
    [ "${errs:-0}" = 0 ] && ok "qmllint (Qt 6) reports no errors" \
                         || bad "qmllint: $errs error(s)" "$(grep '^Error:' <<<"$out" | head -5)"
else
    echo "  skip  qmllint not installed"
fi

printf '\n  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
