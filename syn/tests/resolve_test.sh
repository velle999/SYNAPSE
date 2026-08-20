#!/usr/bin/env bash
# resolve_test.sh — the DaVinci Resolve launch override is the whole feature
#
# `syn resolve` mostly reports, and reporting wrong is annoying. One part of it
# is not: the generated desktop entry in /usr/local/share/applications is what
# every menu launch of Resolve goes through once it exists. Two ways for it to
# be silently wrong, and both look identical to a working one from the outside:
#
#   - it still runs /opt/resolve/bin/resolve, so the environment is never
#     applied and nothing says so; or
#   - it loses Name/Icon/MimeType off the packaged entry it was generated from,
#     so Resolve appears in the menu as a nameless, iconless row.
#
# There is no Resolve on a build machine and none on the ISO, so the packaged
# entry is faked here. That is honest rather than a shortcut: the thing under
# test is a transformation of that file, and the file is a fixed format.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
sr="$here/../syn-resolve.sh"

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

[ -f "$sr" ] || { echo "  FAIL  syn-resolve.sh is missing"; exit 1; }

echo "=== syntax ==="
if bash -n "$sr" 2>"$tmp/syntax"; then
    echo "  ok    parses"
else
    echo "  FAIL  does not parse:"; sed 's/^/        /' "$tmp/syntax"; exit 1
fi

# The packaged entry, as the AUR package writes it: RESOLVE_INSTALL_LOCATION
# already substituted, %u on the Exec, and the StartupWMClass the PKGBUILD
# appends. Every field but Exec has to survive.
mkdir -p "$tmp/usr/share/applications" "$tmp/usr/local/share/applications"
cat > "$tmp/usr/share/applications/DaVinciResolve.desktop" <<'ENTRY'
[Desktop Entry]
Type=Application
Name=DaVinci Resolve
GenericName=Video Editor
Exec=/opt/resolve/bin/resolve %u
TryExec=/opt/resolve/bin/resolve
Icon=davinci-resolve
Categories=AudioVideo;AudioVideoEditing;
MimeType=application/x-resolveproj;
StartupWMClass=resolve
ENTRY

run_setup() {
    SYN_RESOLVE_PKG_DESKTOP="$tmp/usr/share/applications/DaVinciResolve.desktop" \
    SYN_RESOLVE_OUR_DESKTOP="$tmp/usr/local/share/applications/DaVinciResolve.desktop" \
    SYN_RESOLVE_ICD_DIR="$tmp/icd" \
    SYN_RESOLVE_BIN="$tmp/opt/resolve/bin/resolve" \
        bash "$sr" setup "$@" >"$tmp/out" 2>&1
}

out="$tmp/usr/local/share/applications/DaVinciResolve.desktop"

echo ""
echo "=== the override is generated from the packaged entry ==="
run_setup --quiet
check "override written" "yes" "$([ -f "$out" ] && echo yes || echo no)"

# THE assertion. A generated file that still launches the raw binary applies no
# environment at all and looks completely normal.
check "Exec goes through the launcher" "1" \
      "$(grep -c '^Exec=/usr/lib/syn/syn-resolve launch' "$out")"
check "no Exec still points at the raw binary" "0" \
      "$(grep -c '^Exec=/opt/resolve/bin/resolve' "$out")"

# The arguments have to survive: %u is how a .resolveproj double-click reaches
# it, and dropping it makes the file association silently do nothing.
check "%u kept on the Exec line" "1" "$(grep -c '^Exec=.*launch %u$' "$out")"

# TryExec must NOT be rewritten — it is the "is this installed" probe, and
# pointing it at our launcher answers yes on a machine with no Resolve.
check "TryExec still probes the real binary" "1" \
      "$(grep -c '^TryExec=/opt/resolve/bin/resolve$' "$out")"

echo ""
echo "=== everything else off the packaged entry survives ==="
for f in "Name=DaVinci Resolve" "GenericName=Video Editor" "Icon=davinci-resolve" \
         "Categories=AudioVideo;AudioVideoEditing;" \
         "MimeType=application/x-resolveproj;" "StartupWMClass=resolve"; do
    check "kept: ${f%%=*}" "1" "$(grep -cxF "$f" "$out")"
done

echo ""
echo "=== it is idempotent ==="
# The pacman hook runs this on every Resolve upgrade AND every syn upgrade, so a
# second run appending or double-rewriting would corrupt it over time.
cp "$out" "$tmp/first"
run_setup --quiet
check "second run is byte-identical" "same" \
      "$(cmp -s "$tmp/first" "$out" && echo same || echo differs)"

echo ""
echo "=== with no Resolve installed it does nothing, quietly ==="
rm -f "$tmp/usr/share/applications/DaVinciResolve.desktop" "$out"
run_setup --quiet
rc=$?    # captured NOW: the next check overwrites $?
check "no override invented" "no" "$([ -f "$out" ] && echo yes || echo no)"
# The hook runs this on every syn upgrade, including on the overwhelming
# majority of machines that will never install Resolve. Exiting non-zero there
# makes pacman report a failed hook after a transaction that was fine.
check "exits clean so the hook stays silent" "0" "$rc"

echo ""
echo "=== the hook contract ==="
hook="$here/../syn-resolve.hook"
check "hook exists" "yes" "$([ -f "$hook" ] && echo yes || echo no)"
# A hook that shells out to pacman deadlocks against the transaction that
# invoked it. The guard is that setup's hook path never installs, so the hook
# must use it.
check "hook calls the non-installing path" "1" \
      "$(grep -c 'Exec = /usr/lib/syn/syn-resolve setup --quiet' "$hook")"
check "hook runs after the transaction" "1" \
      "$(grep -c '^When = PostTransaction' "$hook")"
# Triggering on syn alone would miss a box where Resolve is installed later;
# triggering on Resolve alone would miss one where syn is.
check "triggers on Resolve and on syn" "5" \
      "$(grep -c '^Target = ' "$hook")"

echo ""
echo "=== transcode conforms variable-rate sources ==="
# Every synui screen recording is variable rate, and a VFR source reaching the
# dnxhd encoder untouched is not a loud failure — it is a mezzanine at a rate no
# timeline has, with frames dropped wherever the source was dense. Measured on a
# real 313-frame capture: 67.2 fps out, 87 frames gone, no warning.

# nearest_fps is self-contained awk, so it can be lifted out and exercised
# directly rather than inferred from the ffmpeg line.
eval "$(sed -n '/^nearest_fps()/,/^}/p' "$sr")"
check "67.2 fps (a synui capture) snaps to 60" "60"          "$(nearest_fps 67.2)"
check "59.94 keeps its fraction"       "60000/1001"          "$(nearest_fps 59.94)"
check "23.98 keeps its fraction"       "24000/1001"          "$(nearest_fps 23.98)"
check "30.1 snaps to 30"               "30"                  "$(nearest_fps 30.1)"
check "an exact rate is left alone"    "25"                  "$(nearest_fps 25)"

check "the encode forces constant rate" "1" \
      "$(grep -c -- '-fps_mode cfr -r' "$sr")"
# r_frame_rate on a VFR file is the TIMEBASE (wf-recorder's is 90000/1), so
# deciding the output rate from it asks for a 90000 fps mezzanine.
check "rate comes from avg_frame_rate" "1" \
      "$(grep -c 'srcfps=.*awk -v v="\${afr:-0}"' "$sr")"

if command -v ffmpeg >/dev/null 2>&1 && command -v ffprobe >/dev/null 2>&1 &&
   ffmpeg -hide_banner -encoders 2>/dev/null | grep -q ' dnxhd ' &&
   ffmpeg -hide_banner -encoders 2>/dev/null | grep -q ' libx264 '; then
    # A source whose r_frame_rate and avg_frame_rate disagree — the same shape
    # as a capture, without needing one.
    ffmpeg -v error -f lavfi -i testsrc2=size=640x480:rate=60 -t 2 \
           -vf "select='not(mod(n,3))+not(mod(n,7))'" -fps_mode vfr \
           -c:v libx264 -pix_fmt yuv420p "$tmp/vfr.mp4" 2>/dev/null

    r=$(ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate \
                -of default=nw=1:nk=1 "$tmp/vfr.mp4")
    a=$(ffprobe -v error -select_streams v:0 -show_entries stream=avg_frame_rate \
                -of default=nw=1:nk=1 "$tmp/vfr.mp4")
    check "fixture really is variable rate" "differ" \
          "$([ "$r" != "$a" ] && echo differ || echo "same:$r")"

    bash "$sr" transcode --out "$tmp/dnx" "$tmp/vfr.mp4" >/dev/null 2>&1
    out="$tmp/dnx/vfr.mov"
    if [ -f "$out" ]; then
        ro=$(ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate \
                     -of default=nw=1:nk=1 "$out")
        ao=$(ffprobe -v error -select_streams v:0 -show_entries stream=avg_frame_rate \
                     -of default=nw=1:nk=1 "$out")
        check "output is constant rate" "equal" \
              "$([ "$ro" = "$ao" ] && echo equal || echo "$ro vs $ao")"
        check "output lands on a standard rate" "25/1" "$ro"
        # Resolve reads neither H.264 nor AAC on Linux; both have to be gone.
        check "video is DNxHR" "dnxhd" \
              "$(ffprobe -v error -select_streams v:0 -show_entries stream=codec_name \
                         -of default=nw=1:nk=1 "$out")"
        check "audio is not left as AAC" "" \
              "$(ffprobe -v error -select_streams a:0 -show_entries stream=codec_name \
                         -of default=nw=1:nk=1 "$out" | grep '^aac$')"

        bash "$sr" transcode --fps 30 --out "$tmp/dnx30" "$tmp/vfr.mp4" >/dev/null 2>&1
        check "--fps overrides the snap" "30/1" \
              "$(ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate \
                         -of default=nw=1:nk=1 "$tmp/dnx30/vfr.mov" 2>/dev/null)"
    else
        echo "  FAIL  transcode produced no output"
        fails=$((fails + 1))
    fi
else
    echo "  skip  end-to-end conform (ffmpeg with dnxhd+libx264 not available)"
fi

echo ""
echo "=== the PKGBUILD ships all of it ==="
pkgbuild="$here/../PKGBUILD"
for f in syn-resolve.sh syn-resolve.hook; do
    check "in source=(): $f" "1" "$(grep -c "\"$f\"" "$pkgbuild")"
done
check "source and sha256sums are the same length" "same" \
      "$(a=$(sed -n '/^source=(/,/)/p' "$pkgbuild" | grep -c '"');
         b=$(sed -n '/^sha256sums=(/,/)/p' "$pkgbuild" | grep -c "SKIP");
         [ "$a" = "$b" ] && echo same || echo "$a vs $b")"
check "hook installed under its ordered name" "1" \
      "$(grep -c 'libalpm/hooks/74-syn-resolve.hook' "$pkgbuild")"
check "launcher installed where the hook execs it" "1" \
      "$(grep -c 'usr/lib/syn/syn-resolve"' "$pkgbuild")"

# The dispatch in syn.sh has to reach it, and by the same absolute path.
check "syn.sh dispatches resolve" "1" \
      "$(grep -c 'resolve).*exec /usr/lib/syn/syn-resolve' "$here/../syn.sh")"

# ── DaVinci Doctor: the porcelain contract ──────────────────────────────────
#
# `doctor --porcelain` is what the window reads, and the KEY is the contract.
# Rewording a message is free; renaming a key silently empties a card in the
# GUI — the check still runs, the window just stops describing it. That is the
# same both-directions drift test syn-install's config_test.sh does for profile
# keys, and for the same reason: two files, one vocabulary, no compiler.
echo ""
echo "=== doctor --porcelain ==="

# `yes` when the pattern appears at all — see the note at its first use.
has() { grep -q "$1" "$2" && echo yes || echo no; }

qml="$here/../resolve.qml"
[ -f "$qml" ] || { echo "  FAIL  resolve.qml is missing"; fails=$((fails + 1)); }

# Run the doctor against a tree where NOTHING is installed. That is the state
# the window exists for, and it is the one that emits every key: an installed
# machine never emits `zip`, so testing on the developer's box would check half
# of it. rc is ignored — "nothing is set up" is a failure exit by design.
porc=$(SYN_RESOLVE_BIN="$tmp/none/resolve" \
       SYN_RESOLVE_OUR_DESKTOP="$tmp/none/DaVinciResolve.desktop" \
       SYN_RESOLVE_ICD_DIR="$tmp/none/vendors" \
       HOME="$tmp/home" bash "$sr" doctor --porcelain 2>/dev/null || true)

check "porcelain emits records" "yes" \
      "$([ -n "$porc" ] && echo yes || echo no)"

# Every line is exactly <key>\t<state>\t<text>, with a state the GUI knows.
# A stray pretty-printed line here is the failure that would have the window
# describing a machine it did not understand.
badline=$(printf '%s\n' "$porc" |
          awk -F'\t' 'NF < 3 || $2 !~ /^(ok|bad|warn|info)$/ { print; exit }')
check "every record is key/state/text with a known state" "" "$badline"

# No ANSI, no box drawing: the whole point of not parsing the pretty output.
check "porcelain carries no escape sequences" "0" \
      "$(printf '%s' "$porc" | grep -c $'\033' || true)"

# Counts are the wrong assertion for these: a key the window reads in three
# places is not a failure, and pinning the number turns an ordinary edit into a
# red test.
#
# The keys the window asks for BY NAME. Read out of the QML rather than listed
# here, so this cannot drift from what the window actually reads.
for key in $(grep -oE '(stateOf|textOf|factOf)\("[a-z.]+"\)' "$qml" |
             sed 's/.*("\(.*\)")/\1/' | sort -u); do
    check "doctor emits '$key', which resolve.qml reads" "yes" \
          "$(printf '%s\n' "$porc" | grep -q "^$key	" && echo yes || echo no)"
done

# …and the other direction: a CHECK the doctor makes that the window never
# shows is a card somebody forgot to add.
#
# ⚠ Only the checks — ok/bad/warn. An `info` record is a fact offered to
# whoever wants it (the GPU name, the ICD package), and requiring the window to
# display every one of those would be requiring it to show its own workings.
# `.hint` lines are the detail under a check and are shown with it, not
# separately.
for key in $(printf '%s\n' "$porc" |
             awk -F'\t' '$2 != "info" { print $1 }' | sort -u); do
    case "$key" in *.hint) continue ;; esac
    check "resolve.qml shows '$key', which doctor checks" "yes" \
          "$(has "\"$key\"" "$qml")"
done

echo ""
echo "=== the window ==="

# FloatingWindow, never PanelWindow: a PanelWindow needs zwlr_layer_shell_v1,
# which mutter does not implement, so under GNOME it maps nothing at all — no
# window, no error, no log. This is an ordinary app window and Resolve users are
# not all on synui.
check "the window is a FloatingWindow" "yes" "$(has '^    FloatingWindow {' "$qml")"
# Anchored: the comment above it explains WHY not a PanelWindow, and an
# unanchored grep would find that explanation and fail on it.
check "…and not a PanelWindow" "no" "$(has '^ *PanelWindow {' "$qml")"

# Closing it must quit, or `qs -n --no-duplicate` finds the windowless corpse on
# the next launch and exits 0 without drawing anything — the "closed it, now it
# will not reopen" bug, invisible because the exit status is success.
check "closing the window quits the process" "1" \
      "$(grep -c 'onClosed: Qt.quit()' "$qml")"

# The privileged half goes to a terminal, because sudo with no tty cannot
# prompt — and to syntty --hold, because a build that fails and then vanishes
# takes its log with it.
check "the build is handed to syntty --hold" "yes" \
      "$(has '"syntty", "--hold"' "$qml")"
check "nothing in the window runs sudo itself" "no" \
      "$(has 'command: \["sudo"' "$qml")"

# ⚠ ONE Process PER BUTTON. Assigning running = true to a Process that is
# already running is a SILENT no-op in quickshell, so a shared, re-pointed
# launcher does nothing on its second press and says nothing.
check "install and setup have separate processes" "2" \
      "$(grep -cE 'id: (installProc|setupProc)$' "$qml")"

gui="$here/../syn-resolve-gui.sh"
[ -f "$gui" ] || { echo "  FAIL  syn-resolve-gui.sh is missing"; fails=$((fails + 1)); }

# The app_id, the .desktop basename and StartupWMClass are one name. synui's
# dock resolves a window to its entry by "<app_id>.desktop"; a mismatch gives a
# themed fallback icon and a click that does nothing, which looks normal.
check "the launcher sets QS_APP_ID" "1" \
      "$(grep -c 'QS_APP_ID="${QS_APP_ID:-syn-resolve-gui}"' "$gui")"
check "the .desktop basename matches that app_id" "yes" \
      "$(has 'syn-resolve-gui.desktop"' "$here/../PKGBUILD")"

# A missing quickshell must produce a sentence, not a menu entry that does
# nothing when clicked — errors from `qs` land on tty1 where nobody sees them.
check "a missing quickshell is reported" "yes" \
      "$(has 'zenity --error' "$gui")"

# The icon: gdk-pixbuf sniffs the first 256 bytes, so an <svg> past that is not
# an image to GTK and the entry is blank in the GNOME grid while looking
# perfect everywhere else.
svg="$here/../syn-resolve-gui.svg"
check "the icon's <svg> is inside the sniff window" "yes" \
      "$(off=$(head -c 256 "$svg" | grep -bo '<svg' | head -1 | cut -d: -f1)
         [ -n "$off" ] && echo yes || echo no)"

echo ""
if [ "$fails" -eq 0 ]; then
    echo "all syn resolve checks passed"
else
    echo "$fails check(s) failed"
fi
exit $([ "$fails" -eq 0 ] && echo 0 || echo 1)
