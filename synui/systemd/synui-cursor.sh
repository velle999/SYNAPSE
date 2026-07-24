#!/bin/sh
# synui-cursor — install and switch X cursor themes (the Super+C picker's helper).
#
# Cursor themes from opendesktop.org / gnome-look.org come in two shapes that
# look identical from the outside, and telling them apart is most of this tool:
#
#   ready-to-use   Theme/cursors/left_ptr, Theme/cursors/watch, …   -> just move it
#   source tree    makefile, src/*.in, config/*.in                  -> needs building
#
# Dropping a source tree into ~/.local/share/icons (the obvious thing to do, and
# what happened here with retrosmart-x11-cursors-3.1a) leaves something that is
# not a cursor theme at all: no cursors/ dir, so nothing will ever see it, and
# nothing says why. `install` detects that case and says so instead of silently
# creating a dud.
#
# WHAT MAKES A VALID THEME: a directory with a cursors/ subdirectory containing
# at least one cursor file. Deliberately NOT index.theme — plenty of themes ship
# without one, and some source trees ship one while having no cursors at all, so
# testing index.theme gets both cases backwards.
#
# SECURITY: these are untrusted archives off the internet, so `install` never
# runs anything from them, and extraction is treated as hostile — see the notes
# on traversal and symlinks in cur_extract/cur_adopt. Building a source tree runs
# that archive's OWN makefile, which is arbitrary code execution as you, so it is
# never automatic: `synui-cursor build` is a separate, explicit command.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
# https://github.com/velle999/SYNAPSE
set -u

ICONS_USER="${XDG_DATA_HOME:-$HOME/.local/share}/icons"
STATE_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/synui"
STATE="$STATE_DIR/cursor.state"
ENVFILE="$STATE_DIR/cursor.env"

DEFAULT_THEME=Adwaita
DEFAULT_SIZE=24

die()  { printf 'synui-cursor: %s\n' "$*" >&2; exit 1; }
warn() { printf 'synui-cursor: %s\n' "$*" >&2; }
note() { command -v notify-send >/dev/null 2>&1 && notify-send -a synui "$@" || true; }

# ── What counts as a theme ───────────────────────────────────
#
# One definition, used by list, install and set alike, so the picker and the
# installer can never disagree about whether something is installed.
cur_is_theme() {
    [ -d "$1/cursors" ] || return 1
    # A cursors/ dir with nothing in it is a build that failed half way; treat it
    # as not-a-theme rather than offering a theme whose every cursor is missing.
    for f in "$1"/cursors/*; do
        [ -e "$f" ] || [ -L "$f" ] || continue
        return 0
    done
    return 1
}

# Does this look like an unbuilt source tree? Only used to explain a failure, so
# it errs toward saying yes — a wrong guess costs a misleading hint, nothing more.
cur_is_source() {
    for f in "$1"/Makefile "$1"/makefile "$1"/GNUmakefile "$1"/build.sh \
             "$1"/*.mk "$1"/src/*.in "$1"/*.in; do
        [ -e "$f" ] && return 0
    done
    return 1
}

# Every directory a cursor theme may be installed into, user first: a theme in
# ~/.local/share/icons shadows a system one of the same name, and that is the
# order libXcursor itself searches.
cur_dirs() {
    printf '%s\n' "$ICONS_USER" "$HOME/.icons" \
                  /usr/local/share/icons /usr/share/icons
}

# ── list ─────────────────────────────────────────────────────
cur_list() {
    seen=" "
    cur_dirs | while read -r d; do
        [ -d "$d" ] || continue
        for t in "$d"/*; do
            [ -d "$t" ] || continue
            name=$(basename "$t")
            case "$seen" in *" $name "*) continue ;; esac
            cur_is_theme "$t" || continue
            seen="$seen$name "
            printf '%s\t%s\n' "$name" "$t"
        done
    done
}

# ── extraction ───────────────────────────────────────────────
#
# Extract into a scratch dir. Both extractors below are told not to restore
# ownership, and neither is trusted to reject a hostile member name — instead
# nothing is ever used straight out of here: cur_adopt re-resolves every path and
# refuses anything that does not stay inside this directory, which defeats
# ../../ traversal ("zip slip") regardless of how the extractor behaves.
cur_extract() {
    src=$1 dest=$2
    case "$src" in
        *.zip|*.ZIP)
            command -v unzip >/dev/null 2>&1 || die "unzip is not installed (needed for .zip)"
            unzip -qq -o "$src" -d "$dest" || return 1 ;;
        *.tar.gz|*.tgz|*.tar.xz|*.txz|*.tar.bz2|*.tbz2|*.tar.zst|*.tar)
            tar -xf "$src" -C "$dest" --no-same-owner --no-same-permissions || return 1 ;;
        *)
            die "unsupported archive '$src' (want .zip, .tar.gz, .tar.xz, .tar.bz2, .tar.zst)" ;;
    esac
}

# Copy one validated theme dir out of the scratch area into ICONS_USER.
#
# TWO things are checked here rather than trusted to the extractor:
#
#   traversal  the theme's resolved path must still be under $root. An archive
#              member called ../../../.config/autostart/x is otherwise a way to
#              write anywhere the user can.
#   symlinks   cursor themes legitimately use relative symlinks inside cursors/
#              (default -> left_ptr, and most of the hash-named aliases), so
#              those must survive. But an ABSOLUTE symlink, or a relative one
#              that climbs out of the theme, is either a mistake or an attempt to
#              get a later read/write redirected — both are dropped, and dropping
#              one only costs that single alias.
cur_adopt() {
    root=$1 theme=$2 name=$3

    # The name is the archive's own directory name, so it is attacker-chosen.
    # It ends up in a shell command (the picker runs `synui-cursor set <name>`),
    # in a tab-separated listing, and in an ini file, so anything exotic is
    # refused outright rather than escaped three different ways. A leading dash
    # would also be read as an option by half the tools that later see it.
    case "$name" in
        ''|.|..|-*)               warn "refusing theme with unusable name '$name'"; return 1 ;;
        */*|*[\'\"\`\$\;\&\|\<\>]*) warn "refusing theme with unsafe name '$name'"; return 1 ;;
    esac
    # Control characters, including the newline that would split a listing row.
    if printf '%s' "$name" | LC_ALL=C grep -q '[[:cntrl:]]'; then
        warn "refusing theme whose name contains control characters"
        return 1
    fi

    rroot=$(realpath "$root" 2>/dev/null) || return 1
    rtheme=$(realpath "$theme" 2>/dev/null) || return 1
    case "$rtheme/" in
        "$rroot"/*) ;;
        *) warn "refusing '$name': archive member escapes the extraction directory"
           return 1 ;;
    esac

    # -type l finds symlinks without following them; a link whose target leaves
    # the theme is removed before the tree is copied anywhere. The warning is
    # emitted here rather than counted and reported afterwards: this loop runs in
    # a subshell, so a counter incremented inside it would always read 0 in the
    # caller and the warning would never fire.
    find "$rtheme" -type l 2>/dev/null | while read -r l; do
        tgt=$(readlink "$l")
        case "$tgt" in
            /*) warn "dropping absolute symlink $(basename "$l") from '$name'"
                rm -f "$l"; continue ;;
        esac
        rl=$(realpath "$l" 2>/dev/null)
        case "${rl:-}/" in
            "$rtheme"/*) ;;
            *) warn "dropping escaping symlink $(basename "$l") from '$name'"
               rm -f "$l" ;;
        esac
    done

    dest="$ICONS_USER/$name"
    if [ -e "$dest" ]; then
        warn "'$name' is already installed at $dest — replacing it"
        rm -rf "$dest" || return 1
    fi

    mkdir -p "$ICONS_USER" || return 1
    # -a keeps the internal relative symlinks as symlinks; copying them as files
    # would multiply a 1MB animated cursor across a dozen aliases.
    cp -a "$rtheme" "$dest" || return 1
    return 0
}

# ── install ──────────────────────────────────────────────────
cur_install() {
    [ $# -ge 1 ] || die "usage: synui-cursor install <archive|directory>"
    src=$1
    [ -e "$src" ] || die "no such file: $src"

    tmp=$(mktemp -d -t synui-cursor-XXXXXX) || die "cannot create a temp dir"
    trap 'rm -rf "$tmp"' EXIT INT TERM

    if [ -d "$src" ]; then
        # A directory is adopted in place (copied), not extracted.
        root=$src
    else
        cur_extract "$src" "$tmp" || die "could not extract '$src'"
        root=$tmp

        # Archives essentially always wrap everything in a single top-level
        # directory (FrierenBLZ/, retrosmart-x11-cursors-3.1a/). Descend through
        # it so that `root` is the theme or source tree itself — otherwise a
        # staged source tree ends up one level too deep and the build command
        # printed below names a directory with nothing in it but another
        # directory. Only descends while there is exactly one child, so a pack
        # holding two themes side by side is left alone.
        while :; do
            # Stop the moment this IS the theme, or the descent walks straight
            # into cursors/ and the theme ends up "above" the root — which then
            # trips the escapes-the-extraction-directory guard and rejects a
            # perfectly good theme. A theme with no index.theme has cursors/ as
            # its only child, and plenty of them ship exactly like that.
            cur_is_theme "$root" && break

            n=0; only=""
            for e in "$root"/* "$root"/.[!.]*; do
                [ -e "$e" ] || continue
                n=$((n + 1)); only=$e
            done
            [ "$n" -eq 1 ] && [ -d "$only" ] || break
            [ "$(basename "$only")" = cursors ] && break
            root=$only
        done
    fi

    # Find every theme root in the tree: the dir CONTAINING a cursors/ dir. This
    # is what handles the nested top-level directory that archives always have
    # (FrierenBLZ/cursors/…), without assuming there is exactly one theme — packs
    # that ship a light and a dark variant side by side are common.
    found=0
    themes=$(find "$root" -maxdepth 4 -type d -name cursors 2>/dev/null)

    if [ -n "$themes" ]; then
        printf '%s\n' "$themes" | while IFS= read -r c; do
            t=$(dirname "$c")
            cur_is_theme "$t" || continue
            name=$(basename "$t")
            if cur_adopt "$root" "$t" "$name"; then
                printf "installed '%s' -> %s/%s\n" "$name" "$ICONS_USER" "$name"
            fi
        done
        # The subshell above cannot set `found`, so re-test on disk: a theme is
        # installed only if it is actually there now.
        printf '%s\n' "$themes" | while IFS= read -r c; do
            [ -d "$ICONS_USER/$(basename "$(dirname "$c")")" ] && exit 42
        done
        [ $? -eq 42 ] && found=1
    fi

    if [ "$found" -eq 0 ]; then
        if cur_is_source "$root" || [ -n "$(find "$root" -maxdepth 3 \
              \( -name 'Makefile' -o -name 'makefile' -o -name '*.mk' \) \
              -print -quit 2>/dev/null)" ]; then
            # Keep the extracted tree somewhere permanent, or the build command
            # printed below would name a temp directory that this function is
            # about to delete — advice that cannot be followed.
            sdir=$src
            if [ ! -d "$src" ]; then
                base=$(basename "$src")
                base=${base%.tar.*}; base=${base%.tgz}; base=${base%.txz}
                base=${base%.tbz2}; base=${base%.zip}; base=${base%.ZIP}
                stage="${XDG_DATA_HOME:-$HOME/.local/share}/synui/cursor-src/$base"
                rm -rf "$stage"
                if mkdir -p "$(dirname "$stage")" && cp -a "$root" "$stage"; then
                    sdir=$stage
                else
                    warn "could not stage the source tree; extract it yourself to build"
                    sdir="<the extracted directory>"
                fi
            fi
            cat >&2 <<EOF
synui-cursor: '$src' is a cursor theme SOURCE TREE, not a built theme.
              It has no cursors/ directory yet — it has to be compiled first,
              which usually needs xcursorgen and imagemagick.

              Building runs the archive's OWN makefile, which is arbitrary code
              from an untrusted download, so this tool will not do it for you
              automatically. If you trust this archive:

                  synui-cursor build $sdir
                  synui-cursor install $sdir

EOF
            exit 3
        fi
        die "no cursor theme found in '$src' (nothing containing a cursors/ directory)"
    fi
}

# ── build (explicit opt-in) ──────────────────────────────────
cur_build() {
    [ $# -ge 1 ] || die "usage: synui-cursor build <directory>"
    dir=$1
    [ -d "$dir" ] || die "not a directory: $dir"

    mk=""
    for m in "$dir/Makefile" "$dir/makefile" "$dir/GNUmakefile"; do
        [ -f "$m" ] && { mk=$m; break; }
    done
    [ -n "$mk" ] || die "no makefile in '$dir' — nothing to build"

    command -v xcursorgen >/dev/null 2>&1 || \
        warn "xcursorgen is not installed; most cursor makefiles need it (pacman -S xorg-xcursorgen)"
    command -v convert >/dev/null 2>&1 || command -v magick >/dev/null 2>&1 || \
        warn "imagemagick is not installed; most cursor makefiles need it"

    cat >&2 <<EOF
synui-cursor: about to run 'make' in $dir
              This executes that archive's own makefile as you. Only do this for
              an archive you trust.
EOF
    printf 'Continue? [y/N] ' >&2
    read -r reply || reply=n
    case "$reply" in
        y|Y|yes|YES) ;;
        *) die "aborted" ;;
    esac

    ( cd "$dir" && make ) || die "make failed in '$dir'"

    # A build that succeeded should have produced a theme somewhere in the tree.
    if find "$dir" -maxdepth 3 -type d -name cursors \
         -exec sh -c 'ls -A "$1" >/dev/null 2>&1' _ {} \; -print -quit | grep -q .; then
        printf "built. now install it with:\n    synui-cursor install %s\n" "$dir"
    else
        die "make finished but produced no cursors/ directory — check the build output above"
    fi
}

# ── set ──────────────────────────────────────────────────────
#
# Writes the choice everywhere it has to be written for the whole desktop to
# agree, which is more places than it should be — each toolkit has its own idea
# of where the cursor theme lives, and disagreeing between them is exactly how
# you end up with one cursor over synui and a different one over Steam.
cur_set() {
    [ $# -ge 1 ] || die "usage: synui-cursor set <theme> [size]"
    theme=$1
    size=${2:-}

    if [ -z "$size" ]; then
        size=$(sed -n 's/^size=//p' "$STATE" 2>/dev/null | head -1)
        [ -n "$size" ] || size=$DEFAULT_SIZE
    fi
    case "$size" in
        ''|*[!0-9]*) die "size must be a number, got '$size'" ;;
    esac

    # Refuse a theme that is not actually installed: setting one silently gives
    # you the fallback cursor everywhere with nothing explaining why.
    path=""
    while IFS="$(printf '\t')" read -r n p; do
        [ "$n" = "$theme" ] && { path=$p; break; }
    done <<EOF
$(cur_list)
EOF
    [ -n "$path" ] || die "cursor theme '$theme' is not installed (try: synui-cursor list)"

    mkdir -p "$STATE_DIR" || die "cannot create $STATE_DIR"

    # 1. synui's own state. The compositor reads this at startup and on reload,
    #    and the picker panel writes it too — same file, one source of truth.
    #    Written temp + mv so a reader never sees a half-written file.
    tmpf="$STATE.tmp.$$"
    {
        printf '# written by synui-cursor; theme for the compositor and the session\n'
        printf 'theme=%s\n' "$theme"
        printf 'size=%s\n' "$size"
    } > "$tmpf" && mv -f "$tmpf" "$STATE" || die "cannot write $STATE"

    # 2. Session environment, sourced by the session wrapper at login. XCURSOR_SIZE
    #    must stay pinned: with it unset, libXcursor computes a size from the
    #    X screen, and the Xwayland virtual screen spanning several monitors is
    #    thousands of pixels wide, which is how Steam ended up with a giant
    #    pointer while synui's own was 24px.
    tmpf="$ENVFILE.tmp.$$"
    {
        printf '# written by synui-cursor; sourced by the synui session wrapper\n'
        printf 'export XCURSOR_THEME=%s\n' "$theme"
        printf 'export XCURSOR_SIZE=%s\n' "$size"
    } > "$tmpf" && mv -f "$tmpf" "$ENVFILE" || die "cannot write $ENVFILE"

    # 3. ~/.icons/default/index.theme — the X11 mechanism proper. This is what
    #    makes an Xwayland client that was started without XCURSOR_THEME (a game
    #    launched from a .desktop, say) still pick the right cursor, so it is not
    #    redundant with the env file above.
    mkdir -p "$HOME/.icons/default" && {
        printf '[Icon Theme]\nName=Default\nComment=set by synui-cursor\nInherits=%s\n' \
            "$theme" > "$HOME/.icons/default/index.theme"
    } || warn "could not write ~/.icons/default/index.theme"

    # 4. GTK. gsettings reaches running GTK apps live; settings.ini is what a GTK
    #    app started outside the session bus reads. Both, or the change appears
    #    to work until the next reboot.
    if command -v gsettings >/dev/null 2>&1; then
        gsettings set org.gnome.desktop.interface cursor-theme "$theme" 2>/dev/null || true
        gsettings set org.gnome.desktop.interface cursor-size "$size" 2>/dev/null || true
    fi
    for v in 3.0 4.0; do
        d="${XDG_CONFIG_HOME:-$HOME/.config}/gtk-$v"
        [ -d "$d" ] || continue
        f="$d/settings.ini"
        [ -f "$f" ] || printf '[Settings]\n' > "$f"
        cur_ini_set "$f" gtk-cursor-theme-name "$theme"
        cur_ini_set "$f" gtk-cursor-theme-size "$size"
    done

    # 5. KDE/Qt — Dolphin and friends read kcminputrc, not kdeglobals, for this.
    if command -v kwriteconfig6 >/dev/null 2>&1; then
        kwriteconfig6 --file kcminputrc --group Mouse --key cursorTheme "$theme" 2>/dev/null || true
        kwriteconfig6 --file kcminputrc --group Mouse --key cursorSize  "$size"  2>/dev/null || true
    elif command -v kwriteconfig5 >/dev/null 2>&1; then
        kwriteconfig5 --file kcminputrc --group Mouse --key cursorTheme "$theme" 2>/dev/null || true
        kwriteconfig5 --file kcminputrc --group Mouse --key cursorSize  "$size"  2>/dev/null || true
    fi

    # 6. Tell the running compositor, so the pointer changes now rather than at
    #    the next login. synctl is synui's own IPC; if it is not reachable (this
    #    ran outside a session) the state file above still applies at startup.
    if command -v synctl >/dev/null 2>&1; then
        synctl dispatch cursor_reload >/dev/null 2>&1 || true
    fi

    printf "cursor theme set to '%s' at %spx\n" "$theme" "$size"
    printf "already-running apps keep the old cursor until they restart\n"
}

# Set key=value in a GTK-style ini without disturbing the rest of the file.
cur_ini_set() {
    f=$1 k=$2 v=$3
    if grep -q "^$k[[:space:]]*=" "$f" 2>/dev/null; then
        sed -i "s|^$k[[:space:]]*=.*|$k=$v|" "$f"
    elif grep -q '^\[Settings\]' "$f" 2>/dev/null; then
        sed -i "0,/^\[Settings\]/s//[Settings]\n$k=$v/" "$f"
    else
        printf '[Settings]\n%s=%s\n' "$k" "$v" >> "$f"
    fi
}

# ── current ──────────────────────────────────────────────────
cur_current() {
    t=$(sed -n 's/^theme=//p' "$STATE" 2>/dev/null | head -1)
    s=$(sed -n 's/^size=//p'  "$STATE" 2>/dev/null | head -1)
    printf '%s\t%s\n' "${t:-$DEFAULT_THEME}" "${s:-$DEFAULT_SIZE}"
}

usage() {
    cat >&2 <<'EOF'
usage: synui-cursor <command>

  list                     installed cursor themes (name, path)
  current                  active theme and size
  install <archive|dir>    install a theme from a .zip/.tar.* or a directory
  build <dir>              compile a source tree (runs ITS makefile — asks first)
  set <theme> [size]       make a theme active (compositor, GTK, Qt/KDE, X11)

The picker panel (Super+C) is the graphical front end to the same state.
EOF
    exit 2
}

[ $# -ge 1 ] || usage
cmd=$1; shift
case "$cmd" in
    list)    cur_list ;;
    current) cur_current ;;
    install) cur_install "$@" ;;
    build)   cur_build "$@" ;;
    set)     cur_set "$@" ;;
    *)       usage ;;
esac
