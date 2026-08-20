#!/bin/sh
# view_image.sh — the image viewer's Exec, and the association it is half of
#
# Three things here are silent when they break, and all three end the same way:
# a double-click on a photograph does nothing at all.
#
#   1. THE RECURSION. synui-view-image is the registered handler for image/png,
#      so its own last-resort `xdg-open` comes straight back to it. Without the
#      SYNUI_VIEW_FALLBACK guard that is an infinite loop of processes, kicked
#      off by opening a picture.
#   2. THE PATH IT DISPATCHES. The panel opens exactly the path it is handed and
#      the compositor's working directory is not the caller's, so a relative
#      argument has to be made absolute before it goes down the socket.
#   3. THE TWO HALVES OF THE ASSOCIATION. The vendor mimeapps.list (in synfiles)
#      names synui-view.desktop for image/png and image/jpeg; this entry has to
#      declare those types and to name the wrapper in Exec. A mimeapps.list
#      entry pointing at a .desktop that does not declare the type is ignored by
#      some implementations and honoured by others, which is the worst kind of
#      "it works on my machine".
#
# Everything runs against fakes on PATH — no synui, no viewer, no browser is
# started by this test, and nothing outside its scratch directory is touched.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

helper=${1:?usage: view_image.sh <systemd/synui-view-image.sh> <data/synui-view.desktop> <src/crop.c>}
entry=${2:?missing data/synui-view.desktop}
cropc=${3:?missing src/crop.c}

fails=0
ok()   { printf '  ok    %s\n' "$1"; }
bad()  { printf '  FAIL  %s\n' "$1"; fails=$((fails + 1)); }
check() { if [ "$2" -eq 0 ]; then ok "$1"; else bad "$1"; fi; }

tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM

mkdir -p "$tmp/bin" "$tmp/pics"
: > "$tmp/pics/cat.png"

# ── The fakes ────────────────────────────────────────────────
#
# synctl answers according to SYNCTL_OK, which is how "there is a synui
# listening" and "there is not" are both testable without either being true.
cat > "$tmp/bin/synctl" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$SYNCTL_LOG"
[ -n "${SYNCTL_OK:-}" ] && exit 0
exit 1
EOF

# A viewer that might be installed. Records and exits; it must never run while
# synui is answering.
cat > "$tmp/bin/feh" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$FEH_LOG"
EOF

# xdg-open, which on a system where this wrapper IS the default handler comes
# straight back to the wrapper. Faithfully re-entrant, so the guard is what
# stops it and not the fake.
cat > "$tmp/bin/xdg-open" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$XDG_LOG"
exec "$HELPER" "$@"
EOF

chmod +x "$tmp/bin/synctl" "$tmp/bin/feh" "$tmp/bin/xdg-open"

SYNCTL_LOG=$tmp/synctl.log
FEH_LOG=$tmp/feh.log
XDG_LOG=$tmp/xdg.log
HELPER=$(cd "$(dirname "$helper")" && pwd)/$(basename "$helper")
export SYNCTL_LOG FEH_LOG XDG_LOG HELPER

run() {  # run <PATH-extras…> — always with the fakes first
    : > "$SYNCTL_LOG"; : > "$FEH_LOG"; : > "$XDG_LOG"
    PATH="$tmp/bin:$PATH" "$@"
}

# ── 1. synui is up: the panel gets it, nothing else runs ─────
SYNCTL_OK=1 run sh "$HELPER" "$tmp/pics/cat.png" >/dev/null 2>&1
grep -q "dispatch view $tmp/pics/cat.png" "$SYNCTL_LOG"
check "the viewer is dispatched to synui" $?

[ ! -s "$FEH_LOG" ] && ok "no other viewer is started when synui answers" \
                    || bad "it started a second viewer as well"

# A RELATIVE argument still reaches the panel as an absolute path — the
# compositor's working directory is not this process's.
: > "$SYNCTL_LOG"
( cd "$tmp/pics" && PATH="$tmp/bin:$PATH" SYNCTL_OK=1 sh "$HELPER" cat.png ) >/dev/null 2>&1
grep -q "dispatch view /" "$SYNCTL_LOG"
check "a relative path is made absolute before it is dispatched" $?

# ── 2. no synui: an installed viewer takes it ────────────────
run sh "$HELPER" "$tmp/pics/cat.png" >/dev/null 2>&1
grep -q "$tmp/pics/cat.png" "$FEH_LOG"
check "with no synui the file goes to an installed viewer" $?

# ── 3. no synui and no viewer: xdg-open ONCE ─────────────────
#
# The whole point of the guard. Without it the fake above re-enters the wrapper
# for ever; with it the second pass declines and exits.
rm -f "$tmp/bin/feh"
run sh "$HELPER" "$tmp/pics/cat.png" >/dev/null 2>&1
calls=$(wc -l < "$XDG_LOG")
if [ "$calls" -eq 1 ]; then
    ok "the last-resort xdg-open is not re-entered"
else
    bad "xdg-open ran $calls times — the recursion guard is gone"
fi

# ── 4. arguments ─────────────────────────────────────────────
if run sh "$HELPER" >/dev/null 2>&1; then
    bad "no argument should be a usage error"
else
    ok "no argument is a usage error"
fi

if run sh "$HELPER" "$tmp/pics/missing.png" >/dev/null 2>&1; then
    bad "a file that is not there should be an error"
else
    ok "a file that is not there is an error"
fi

# ── 5. the two halves of the association ─────────────────────
grep -q '^Exec=synui-view-image %f$' "$entry"
check "the entry runs the wrapper, with one file" $?

grep -q '^MimeType=.*image/png.*$' "$entry"
check "the entry declares image/png" $?

grep -q '^MimeType=.*image/jpeg.*$' "$entry"
check "the entry declares image/jpeg" $?

grep -q '^OnlyShowIn=synui;SynapseOS;$' "$entry"
check "the entry is hidden outside synui, under BOTH desktop names" $?

# ⚠ AND THE TYPES HAVE TO BE ONES THE VIEWER CAN DECODE. crop.c accepts .png,
# .jpg and .jpeg and nothing else; a MimeType line offering image/gif would be
# an association that opens "Cannot open that image", which is worse than the
# browser it replaced. Checked against the decoder's own extension list rather
# than against a copy of it.
# ⚠ THE MimeType LINE ALONE, not the file: the entry's own comment names the
# formats it deliberately leaves out, and a grep over the whole file reads that
# explanation as the thing it warns against.
mimeline=$(grep '^MimeType=' "$entry")

for t in gif webp tiff svg+xml bmp; do
    if printf '%s\n' "$mimeline" | grep -q "image/$t"; then
        bad "the entry claims image/$t, which crop.c cannot decode"
    else
        ok "the entry does not claim image/$t"
    fi
done

grep -q '"\.png"' "$cropc" && grep -q '"\.jpeg"' "$cropc"
check "crop.c still decodes the types the entry claims" $?

if [ "$fails" -gt 0 ]; then
    printf 'view_image: %d failure(s)\n' "$fails" >&2
    exit 1
fi
printf 'view_image: ok\n'
