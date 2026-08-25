#!/usr/bin/env bash
#
# syn_gfn_test.sh — the launcher's contract with the dock, the browser and the
# profile.
#
# ⛔ WHAT THIS GUARDS IS THREE FLAGS AND A FILE, all of which are invisible
# when wrong. A missing --class renames the window and synui's dock, which
# resolves a pin through a direct <app_id>.desktop lookup and RUNS THE APP_ID
# as a command on a miss, launches something else entirely with a perfectly
# normal-looking icon. --app=, which is the tempting way to get a window with
# no tab strip, is exactly what takes --class away. --start-fullscreen core-
# dumps Vivaldi 8.1 under Wayland. And the three site permissions have to be
# in the profile BEFORE the first launch, because the prompt they replace is
# raised while the page is full screen with the pointer captured — where
# nobody can see it, let alone click it.
#
# A stub for EVERY candidate browser on PATH, so nothing here starts a real
# one: the launcher `exec`s whatever it picked, and a script that records its
# argv is a browser as far as that goes.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
GFN=${1:-$HERE/../syn-gfn.sh}
[ -r "$GFN" ] || { echo "not readable: $GFN" >&2; exit 1; }

TMP=$(mktemp -d) || exit 1
trap 'rm -rf "$TMP"' EXIT

pass=0 fail=0
ok()   { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad()  { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail + 1)); }
check(){ if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 (want '$2', got '$3')"; fi; }
has()  { if printf '%s' "$2" | grep -qF -- "$3"; then ok "$1"; else bad "$1 (not in: $2)"; fi; }
hasnt(){ if printf '%s' "$2" | grep -qF -- "$3"; then bad "$1 ($3 is in: $2)"; else ok "$1"; fi; }

# ── Browsers that only write down what they were asked ──────────────────────
#
# ⚠ EVERY CANDIDATE IS STUBBED, not just one. PATH still has to carry /usr/bin
# (mkdir, python3), so a real browser of the same name would be found there and
# a test that means to record a command line would instead OPEN A BROWSER on
# the desktop of whoever ran the suite — which is exactly what the first draft
# of this file did.
mkdir -p "$TMP/bin"
for b in vivaldi-stable chromium chromium-browser google-chrome-stable \
         google-chrome brave brave-browser microsoft-edge-stable; do
    cat > "$TMP/bin/$b" <<EOF
#!/usr/bin/env bash
printf '%s\n' "$b"        >  "$TMP/who"
printf '%s\n' "\$*"        >  "$TMP/argv"
printf '%s\n' "\$MANGOHUD" >  "$TMP/mangohud"
printf '%s\n' "\$DISABLE_MANGOHUD" > "$TMP/disable_mangohud"
EOF
    chmod +x "$TMP/bin/$b"
done

export PATH="$TMP/bin:/usr/bin:/bin"
export XDG_DATA_HOME="$TMP/share"
PROFILE="$TMP/share/syn-gfn"

run() { bash "$GFN" "$@" >"$TMP/out" 2>"$TMP/err"; echo $?; }

echo "syn-gfn — $GFN"

# ── The command it builds ───────────────────────────────────────────────────
rc=$(run)
check "a plain run exits 0" 0 "$rc"
argv=$(cat "$TMP/argv" 2>/dev/null)
check "…in the first browser of the search order" \
      "vivaldi-stable" "$(cat "$TMP/who" 2>/dev/null)"

has  "…passing --class=syn-gfn, which is what the dock pins by" \
     "$argv" "--class=syn-gfn"
has  "…its own profile, never the browsing one" \
     "$argv" "--user-data-dir=$PROFILE"
has  "…and the service's own URL" "$argv" "https://play.geforcenow.com"

# ⛔ The two flags that look like improvements and are not.
hasnt "no --app=: it renames the window and drops --class" "$argv" "--app="
hasnt "no --start-fullscreen: Vivaldi core-dumps on it"    "$argv" "--start-fullscreen"

# Wayland cannot take ANGLE's Vulkan backend. MangoHud's layer is turned off
# the way wpengine and synstudio turn it off — it segfaults Vulkan clients in
# vkCreateDevice on AMD — and DISABLE_MANGOHUD is the half that actually holds.
has   "ANGLE is pinned to GL"        "$argv" "--use-angle=gl"
has   "…and Vulkan is off with it"   "$argv" "DefaultANGLEVulkan"
check "MANGOHUD is off for the browser" "0" "$(cat "$TMP/mangohud" 2>/dev/null)"
check "…and DISABLE_MANGOHUD is set, which is the half that beats the enable" \
      "1" "$(cat "$TMP/disable_mangohud" 2>/dev/null)"

# ── The permissions, in the profile, before the browser ever ran ────────────
if command -v python3 >/dev/null 2>&1; then
    P="$PROFILE/Default/Preferences"
    [ -r "$P" ] && ok "a Preferences file is seeded on first run" \
                || bad "no Preferences written to $P"
    for key in keyboard_lock pointer_lock automatic_fullscreen; do
        got=$(python3 - "$P" "$key" <<'PY'
import json, sys
try:
    d = json.load(open(sys.argv[1]))
except Exception:
    print("no-file"); raise SystemExit
ex = d.get("profile", {}).get("content_settings", {}).get("exceptions", {})
b = ex.get(sys.argv[2], {})
print(b.get("https://play.geforcenow.com:443,*", {}).get("setting", "unset"))
PY
)
        check "…$key is allowed for the site" 1 "$got"
    done

    # ⚠ It must ADD to a profile, not replace one: a second run happens after
    # the browser has written a session, a cookie jar and every preference the
    # user changed, and a seeder that clobbers those is worse than one that
    # never ran.
    python3 - "$P" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
d["syn_gfn_canary"] = "keep me"
json.dump(d, open(sys.argv[1], "w"))
PY
    run >/dev/null
    kept=$(python3 -c "
import json,sys
print(json.load(open('$P')).get('syn_gfn_canary','GONE'))")
    check "a second run leaves everything else in the profile alone" "keep me" "$kept"
else
    ok "python3 absent — the permission seeding is not checked here"
fi

# ── Saying no clearly ───────────────────────────────────────────────────────
rc=$(run --browser=definitely-not-installed)
check "a browser that is not there fails" 1 "$rc"
grep -q "not installed" "$TMP/err" \
    && ok "…and says so" || bad "no message about the missing browser"

rc=$(run --nonsense)
check "an unknown option is refused" 2 "$rc"

rc=$(run --help)
check "--help exits 0" 0 "$rc"
grep -qi "fullscreen button, not f11" "$TMP/out" \
    && ok "…and carries the rule that makes Escape work" \
    || bad "--help does not explain the Escape/fullscreen rule"

rc=$(run --list-browsers)
check "--list-browsers exits 0" 0 "$rc"
grep -q "would use this one" "$TMP/out" \
    && ok "…and marks the one it would pick" \
    || bad "--list-browsers marked nothing"

# ── A browser it was told to use wins over the search order ─────────────────
rc=$(run --browser=chromium)
check "--browser= is honoured" 0 "$rc"
check "…and it is the one that ran" "chromium" "$(cat "$TMP/who")"

printf '\n  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" = 0 ] || exit 1
echo "syn_gfn: PASS"
