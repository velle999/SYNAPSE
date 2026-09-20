#!/bin/bash
# host_test.sh — the remote-synapd setting survives the trip it has to make.
#
# ⛔ WHAT THIS EXISTS TO CATCH. `vibe host desktop.lan` is written by PYTHON
# and read back by BASH, and the two halves agree only by hand: main.py refuses
# a name that cfg.host_ok() rejects, and /usr/bin/vibe matches the file with a
# sed character class rather than sourcing it. Those are two spellings of one
# rule. If they ever drift, the setting is accepted, stored, silently skipped at
# the next start, and every answer comes from the LOCAL daemon — correctly, in
# the model's own voice, from the wrong machine. Nothing fails; the laptop is
# just slow again.
#
# ⛔ AND IT MERGES. The env file holds the backend AND the host. `vibe provider`
# wrote it with write_text() once, so choosing a backend deleted the host — the
# same silent wrong-machine answer, reached by a completely ordinary action.
#
# The launcher is run FOR REAL here, with a stub python3 first on PATH that
# prints the environment it was handed instead of starting the assistant. That
# is the only way the bash half is the thing under test rather than a second
# copy of it written into this file.
#
# Usage: tests/host_test.sh            (from the vibe source tree)
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

HERE=$(cd "$(dirname "$0")/.." && pwd)
LAUNCHER="$HERE/packaging/vibe-launcher.sh"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

fails=0
ok()  { echo "  ok    $1"; }
bad() { echo "  FAIL  $1${2:+  [$2]}"; fails=$((fails + 1)); }

export XDG_CONFIG_HOME="$TMP/config"
ENVF="$XDG_CONFIG_HOME/synui/vibe.env"

# The stub stands in for the app: it prints what the launcher exported and
# exits. Prepended, so it is what `exec python3` resolves to — this is the
# positive case, where prepending is the right tool.
mkdir -p "$TMP/bin"
cat > "$TMP/bin/python3" <<'STUB'
#!/bin/bash
echo "BACKEND=${VIBE_BACKEND-}"
echo "HOST=${VIBE_SYNAPD_HOST-}"
echo "PORT=${VIBE_SYNAPD_PORT-}"
STUB
chmod +x "$TMP/bin/python3"

# `vibe host` and `vibe provider` both go through cfg.env_set, so the writer
# under test is the real one.
write() {  # write KEY VALUE
    PYTHONPATH="$HERE" python3 -c "
import sys
import vibe.config as cfg
err = cfg.env_set(sys.argv[1], sys.argv[2])
sys.exit(err or 0)
" "$1" "$2"
}

launch() { PATH="$TMP/bin:$PATH" bash "$LAUNCHER"; }

echo "vibe remote synapd"

# ── 1. the round trip: python writes it, the launcher hands it back ─────────
write VIBE_SYNAPD_HOST desktop.lan || bad "env_set wrote a host"
out=$(launch)
case "$out" in
    *"HOST=desktop.lan"*) ok "a host written by vibe is read back by the launcher" ;;
    *) bad "a host written by vibe is read back by the launcher" "$out" ;;
esac

# ── 2. the merge: a backend choice must not take the host with it ───────────
write VIBE_BACKEND synapd || bad "env_set wrote a backend"
out=$(launch)
case "$out" in
    *"HOST=desktop.lan"*) ok "choosing a backend keeps the host" ;;
    *) bad "choosing a backend keeps the host" "$out" ;;
esac
case "$out" in
    *"BACKEND=synapd"*) ok "and the backend is what was chosen" ;;
    *) bad "and the backend is what was chosen" "$out" ;;
esac

# ── 3. the port rides along, and only when it was set ───────────────────────
case "$out" in
    *"PORT="$'\n'*|*"PORT=") ok "no port set, none exported" ;;
    *"PORT=11435"*) bad "no port set, none exported" "$out" ;;
    *) ok "no port set, none exported" ;;
esac
write VIBE_SYNAPD_PORT 11500 || bad "env_set wrote a port"
out=$(launch)
case "$out" in
    *"PORT=11500"*) ok "a port written by vibe is read back too" ;;
    *) bad "a port written by vibe is read back too" "$out" ;;
esac

# ── 4. the environment still outranks the file ──────────────────────────────
out=$(PATH="$TMP/bin:$PATH" VIBE_SYNAPD_HOST=elsewhere.lan bash "$LAUNCHER")
case "$out" in
    *"HOST=elsewhere.lan"*) ok "VIBE_SYNAPD_HOST= on the command line wins" ;;
    *) bad "VIBE_SYNAPD_HOST= on the command line wins" "$out" ;;
esac

# ── 5. writer and reader agree on what a hostname is ────────────────────────
#
# Planted by hand, because the CLI will not write it: the point is that the two
# halves REFUSE THE SAME NAMES. A value the launcher would skip must never be
# one vibe stored, or the setting goes quiet.
for bad_name in 'a b' 'x;id' '$(id)' 'quote"d'; do
    printf 'VIBE_SYNAPD_HOST=%s\n' "$bad_name" > "$ENVF"
    out=$(launch)
    accepted_by_py=$(PYTHONPATH="$HERE" python3 -c "
import sys
import vibe.config as cfg
print('yes' if cfg.host_ok(sys.argv[1]) else 'no')
" "$bad_name")
    case "$out" in
        *"HOST=$bad_name"*) read_back=yes ;;
        *)                  read_back=no  ;;
    esac
    if [ "$accepted_by_py" = no ] && [ "$read_back" = no ]; then
        ok "both halves refuse '$bad_name'"
    else
        bad "both halves refuse '$bad_name'" "python=$accepted_by_py launcher=$read_back"
    fi
done

# ── 6. `local` clears it rather than storing the word ───────────────────────
#
# ⚠ The backend is re-written first ON PURPOSE: case 5 plants a file by hand and
# overwrites everything else in it, so without this the "other keys" below are
# ones the TEST removed and the check would pass for the wrong reason.
write VIBE_BACKEND synapd || bad "env_set wrote a backend"
write VIBE_SYNAPD_HOST desktop.lan || bad "env_set wrote a host"
write VIBE_SYNAPD_HOST "" || bad "env_set cleared the host"
out=$(launch)
case "$out" in
    *"HOST=desktop.lan"*) bad "clearing the host really clears it" "$out" ;;
    *) ok "clearing the host really clears it" ;;
esac
if [ -s "$ENVF" ]; then
    ok "and the file keeps its other keys"
else
    bad "and the file keeps its other keys" "$(cat "$ENVF" 2>&1)"
fi

echo
if [ "$fails" -eq 0 ]; then
    echo "vibe remote synapd: all checks passed"
else
    echo "vibe remote synapd: $fails FAILED"
fi
exit $((fails > 0))
