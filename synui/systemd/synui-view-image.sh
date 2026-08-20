#!/bin/bash
# synui-view-image — open an image in SynapseOS's own viewer.
#
# ⚠ THIS IS THE .desktop's Exec, NOT the normal way in. Inside synui the viewer
# is `synctl dispatch view <path>`, super+shift+i, or c/Escape between the two
# faces of the panel (src/crop.c). This wrapper exists for one reason: the
# viewer is the DISTRIBUTION DEFAULT for image/png and image/jpeg (synfiles
# ships the vendor mimeapps.list that says so), and a default handler is asked
# to open files in sessions where synui is not the compositor.
#
# Why the entry cannot simply be `synctl dispatch view %f`:
#
#   - synui-view.desktop is OnlyShowIn=synui;SynapseOS, which keeps it out of
#     the KDE and GNOME menus and makes KIO skip it when picking a default —
#     KApplicationTrader filters by show-in, so Dolphin under Plasma falls
#     through to the next candidate on its own.
#   - GLib does NOT filter the default association that way. Under GNOME,
#     g_app_info_get_default_for_type hands back this entry and runs it, and a
#     bare synctl there is a double-click that does nothing at all: no window,
#     no error the user ever sees, because the message goes to a stderr nobody
#     is reading. That is precisely the failure synui-open-folder was written
#     for, one file type over.
#
# So: dispatch when there is a synui to dispatch to, and hand the file to
# whatever this system does have when there is not.
#
# Usage: synui-view-image FILE
set -u

img=${1:-}

# ── NO FILE AT ALL ──────────────────────────────────────────────────────────
#
# Which is what happens every time somebody picks "Image Viewer" out of the
# application menu: Exec is `synui-view-image %f`, and %f with nothing selected
# expands to nothing.
#
# This used to print a usage line and exit 2 — into a stderr no launcher reads.
# So the menu entry did NOTHING AT ALL: no window, no error, no log. That is
# precisely the failure this wrapper's own header describes and was written to
# fix, one case over; it fixed the no-synui case and left the no-file one.
#
# `synctl dispatch view` with no path is already defined: the panel opens on its
# recent-images list, which is exactly what super+shift+i does and exactly what
# somebody choosing "Image Viewer" from a menu is asking for. Only when there is
# no synui to ask does the usage line make sense, and then it is a person at a
# terminal reading it.
if [ -z "$img" ]; then
    if command -v synctl >/dev/null 2>&1 &&
       synctl dispatch view >/dev/null 2>&1; then
        exit 0
    fi
    echo "usage: synui-view-image FILE" >&2
    echo "  (with no file it opens the viewer's recent-images list, which" >&2
    echo "   needs a running synui to open it in)" >&2
    exit 2
fi
if [ ! -e "$img" ]; then
    echo "synui-view-image: $img does not exist" >&2
    exit 1
fi

# Absolute, because the panel opens exactly the path it is handed and the
# compositor's working directory is not this process's. A file manager passes an
# absolute path already; a shell "synui-view-image cat.png" does not.
case $img in
    /*) ;;
     *) img=$PWD/$img ;;
esac

# The viewer proper. synctl is a short-lived socket client — it connects,
# writes, and exits — so its status genuinely answers "was there a synui to take
# this", which is the question here. (It is NOT the general shape: an exit
# status is not "did the app launch" for anything that forks a window.)
if [ -z "${SYNUI_VIEW_FALLBACK:-}" ] && command -v synctl >/dev/null 2>&1; then
    if synctl dispatch view "$img" >/dev/null 2>&1; then
        exit 0
    fi
fi

# No synui listening. Anything that is actually an image viewer, in order of how
# likely it is to be on the box: the GNOME and KDE defaults first, then the
# lightweight ones somebody installs on purpose.
for v in loupe gwenview eog eom nomacs qimgv ristretto imv feh; do
    if command -v "$v" >/dev/null 2>&1; then
        exec "$v" "$img"
    fi
done

# Last resort, and ⚠ THE RECURSION IS REAL: if this script is the registered
# default, xdg-open comes straight back here. SYNUI_VIEW_FALLBACK is what breaks
# the loop — set, the branch above is skipped and so is this one, so the second
# pass exits with a message instead of calling xdg-open a third time.
if [ -z "${SYNUI_VIEW_FALLBACK:-}" ] && command -v xdg-open >/dev/null 2>&1; then
    SYNUI_VIEW_FALLBACK=1 exec xdg-open "$img"
fi

echo "synui-view-image: no synui to show it and no image viewer installed" >&2
exit 1
