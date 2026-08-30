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
# ⚠ THE GECKO NAMES ARE STUBBED TOO, and firefox is the one that matters: it
# is installed on every SynapseOS box, so a test that forgets it opens the
# suite-runner's real Firefox.
for b in vivaldi-stable chromium chromium-browser google-chrome-stable \
         google-chrome brave brave-browser microsoft-edge-stable \
         firefox firefox-esr firefox-developer-edition librewolf; do
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

# ── Firefox ────────────────────────────────────────────────────────────────
#
# ⛔ GEFORCE NOW SUPPORTS FIREFOX ON WINDOWS, AND THIS IS LINUX. NVIDIA and
# Mozilla shipped it on 2026-08-19 for Firefox 154 on Windows browsers; on
# Linux the site lists the library and starts no game. Firefox here also has no
# Keyboard Lock API at all — measured on 154.0.1, navigator.keyboard is absent
# — so Escape would leave full screen instead of opening the in-game menu.
#
# Which makes "pick Firefox because it is installed" the worst of the options:
# it turns a launcher that says what is missing into one that opens a page and
# fails later, looking like the account's fault.
rm -f "$TMP/who"
rc=$(run)
check "Firefox is NOT picked automatically while it cannot stream" \
      "vivaldi-stable" "$(cat "$TMP/who" 2>/dev/null)"

# Only Firefox on PATH: the stock SynapseOS box, which ticks Firefox and
# neither Chromium nor Vivaldi.
#
# ⛔ AND /usr/bin IS NOT ON THIS ONE. Prepending a directory cannot HIDE a real
# vivaldi-stable in /usr/bin — `command -v` finds it there and the launcher
# execs it, which is the very mistake this file warns about at the top and
# which it then made itself: this block opened a browser on the desktop of
# whoever ran the suite. The only PATH that proves "no streaming browser
# installed" is one that does not contain any. bash covers printf and
# command -v; mkdir and cat are the two externals the Gecko path needs.
mkdir -p "$TMP/gecko-only"
cp "$TMP/bin/firefox" "$TMP/gecko-only/firefox"
for t in mkdir cat; do ln -sf "$(command -v $t)" "$TMP/gecko-only/$t"; done
BASH_BIN=$(command -v bash)     # bash is not on that PATH either
rc=$(PATH="$TMP/gecko-only" "$BASH_BIN" "$GFN" >"$TMP/out" 2>"$TMP/err"; echo $?)
check "with only Firefox installed it refuses rather than half-working" 1 "$rc"
grep -qi "cannot stream" "$TMP/err" \
    && ok "…saying the browser it found cannot stream, not that none exists" \
    || bad "the Firefox-only message does not say why ($(cat "$TMP/err"))"
grep -qi "synpkg install chromium" "$TMP/err" \
    && ok "…and how to fix it" || bad "no fix offered"
grep -qi "browser=firefox" "$TMP/err" \
    && ok "…and how to browse the catalogue anyway" || bad "no catalogue route offered"

# Asked for by name it runs — with GECKO flags, and none of Chromium's.
# ⚠ ITS OWN PROFILE DIRECTORY. Every run above was Chromium and seeded a
# Preferences file into the default one, so a check for "no Chromium
# Preferences here" against that directory tests the earlier tests.
FFPROFILE="$TMP/ff-profile"
rc=$(run --browser=firefox --profile="$FFPROFILE")
check "--browser=firefox runs it" 0 "$rc"
check "…and it is the one that ran" "firefox" "$(cat "$TMP/who")"
argv=$(cat "$TMP/argv")

# ⛔ A CHROMIUM FLAG HANDED TO FIREFOX IS NOT AN ERROR, IT IS A TAB. Gecko
# treats an unrecognised bare argument as a URL to open, so --user-data-dir
# would not fail loudly — it would start the browser with junk tabs beside the
# game and no message anywhere.
has   "…with Gecko's own profile flag"     "$argv" "--profile $FFPROFILE"
has   "…and its own instance, not a tab in the browsing session" \
      "$argv" "--new-instance"
hasnt "no --user-data-dir: that is Chromium's" "$argv" "--user-data-dir"
hasnt "no --use-angle: that is Chromium's"     "$argv" "--use-angle"
hasnt "no --enable-features: that is Chromium's" "$argv" "--enable-features"
grep -qi "cannot start a game\|not start a game" "$TMP/err" \
    && ok "…and it says the stream will not start on Linux" \
    || bad "launching Firefox by name said nothing about the limit"

# user.js, not Chromium's Preferences JSON — a different browser keeps its
# settings somewhere else, and seeding the wrong file is a silent no-op.
UJ="$FFPROFILE/user.js"
[ -r "$UJ" ] && ok "a user.js is seeded for Gecko" || bad "no user.js at $UJ"
grep -q "dom.fullscreen.keyboard_lock.enabled" "$UJ" 2>/dev/null \
    && ok "…turning on Firefox's own keyboard lock, which is not navigator.keyboard" \
    || bad "user.js does not enable Firefox's keyboard lock"
[ -r "$FFPROFILE/Default/Preferences" ] \
    && bad "Chromium Preferences were written for a Gecko browser" \
    || ok "…and no Chromium Preferences file is written for it"

rc=$(run --list-browsers)
grep -qi "catalogue only" "$TMP/out" \
    && ok "--list-browsers separates streaming from catalogue-only" \
    || bad "--list-browsers does not distinguish the two families"

printf '\n  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" = 0 ] || exit 1
echo "syn_gfn: PASS"
