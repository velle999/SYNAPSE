#!/usr/bin/env bash
# syn-resolve — get DaVinci Resolve running on SynapseOS with less hand-fixing.
#
# MOST OF THE ARCH-SIDE WORK IS ALREADY DONE AND IS NOT HERE. The AUR
# davinci-resolve PKGBUILD deletes the bundled libglib/libgio/libgmodule/
# libgobject/libc++ that shadow the system copies and symlinks the real ones in,
# patchelfs an RPATH onto every ELF it ships, and installs the Blackmagic udev
# rules for panels, keyboards and DeckLink cards. Those are the fixes every
# "Resolve on Arch" guide is about, they are upstream, and duplicating them here
# would only fight the package.
#
# What is left, checked against the real /opt/resolve on the dev box rather than
# against the guides — which cost two of these their place in this file:
#
#   opencl-driver         THE ONE THAT ACTUALLY BREAKS INSTALLS. `resolve` has
#                         libOpenCL.so.1 in its NEEDED, so OpenCL is not optional
#                         acceleration, it is a link-time dependency — and the
#                         package asks for it as the VIRTUAL `opencl-driver`,
#                         which has several providers. Exactly the trap
#                         syn-install.sh already documents for vulkan-driver and
#                         libgl: with --noconfirm pacman takes the first provider
#                         rather than asking, so a box can end up holding an ICD
#                         for a GPU it does not have. It fails late and quietly —
#                         the package installs clean and Resolve then refuses to
#                         start, blaming the GPU.
#
#   XDG_SESSION_TYPE      "wayland" session-wide. Resolve is X11-only Qt5 (it
#                         links libQt5OpenGL/libGL, and the AUR patchelfs an
#                         xcbglintegrations path into it), so it lands on
#                         Xwayland either way — but by fallback rather than by
#                         decision. Pinned to xcb so every launch is the same
#                         launch.
#
#   QT_QPA_PLATFORMTHEME  Exported session-wide — "kde" where that plugin exists,
#                         else "xdgdesktopportal" — so Qt apps pick up the theme;
#                         Dolphin's icon view drawing black on black is what it
#                         fixed. Resolve bundles its own Qt 5.15.2 and ships NO
#                         platformthemes plugin, so it is being pointed at a
#                         plugin only the system has (5.15.19 here).
#                         MEASURED, NOT ASSUMED, AND WEAKER THAN THE GUIDES SAY:
#                         the bundled Qt's compiled-in prefix is a dead
#                         Blackmagic build path (/media/datastore1/build/...) and
#                         QT_PLUGIN_PATH is unset, so it almost certainly cannot
#                         reach the system plugin dir and the variable does
#                         nothing today. Cleared anyway because it costs one line
#                         and the day it DOES resolve — anything exporting
#                         QT_PLUGIN_PATH, a future Resolve build — it means two
#                         different Qt 5.15 builds in one process. Cheap
#                         insurance, not a fix for a bug seen here.
#
# NOT DONE, deliberately: clearing MANGOHUD. profile.d exports MANGOHUD=1
# session-wide and it looks like it must reach a GPU application this heavy, but
# MangoHud's implicit layer is VULKAN-only (its layer JSON keys off MANGOHUD=1),
# and Resolve links libGL and libOpenCL with no libvulkan anywhere under
# /opt/resolve. profile.d says as much itself: "OpenGL titles still need the
# mangohud preload that synui-game-run applies", and nothing applies it here. So
# the overlay cannot attach, and a line clearing it would be a fix for nothing —
# left out on purpose so it does not get added back on the same hunch.
#
# The launch environment is applied by shadowing the packaged desktop entry
# rather than by editing it: a copy in /usr/local/share/applications wins over
# /usr/share/applications by XDG_DATA_DIRS order (unset here, so the spec
# default /usr/local/share:/usr/share applies — synui-open-folder.sh assumes the
# same), which means a Resolve upgrade cannot revert it and removing one file
# undoes all of it.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
# https://github.com/velle999/SYNAPSE

# Overridable only so resolve_test.sh can point the whole thing at a temp tree —
# there is no other reason to set these, and nothing in SynapseOS does.
RESOLVE_BIN="${SYN_RESOLVE_BIN:-/opt/resolve/bin/resolve}"
PKG_DESKTOP="${SYN_RESOLVE_PKG_DESKTOP:-/usr/share/applications/DaVinciResolve.desktop}"
OUR_DESKTOP="${SYN_RESOLVE_OUR_DESKTOP:-/usr/local/share/applications/DaVinciResolve.desktop}"
ICD_DIR="${SYN_RESOLVE_ICD_DIR:-/etc/OpenCL/vendors}"

ok()   { printf '  \033[32m✓\033[0m %s\n' "$*"; }
bad()  { printf '  \033[31m✗\033[0m %s\n' "$*"; }
warn() { printf '  \033[33m!\033[0m %s\n' "$*"; }
info() { printf '    %s\n' "$*"; }

# ── The machine ──────────────────────────────────────────────

# Same PCI sweep syn-install.sh uses to choose a graphics driver: class 0x03 is
# a display controller, and the vendor id says whose.
detect_gpu() {
    HAS_NVIDIA= HAS_AMD= HAS_INTEL=
    local dev
    for dev in /sys/bus/pci/devices/*; do
        case "$(cat "$dev/class" 2>/dev/null)" in 0x03*) ;; *) continue ;; esac
        case "$(cat "$dev/vendor" 2>/dev/null)" in
            0x10de) HAS_NVIDIA=1 ;;
            0x1002) HAS_AMD=1 ;;
            0x8086) HAS_INTEL=1 ;;
        esac
    done
}

# The concrete OpenCL runtime for this GPU. Discrete wins on a hybrid laptop:
# Resolve wants the card that can actually grade, and Intel's iGPU being present
# is not a reason to hand it the iGPU's ICD.
icd_package() {
    if   [ -n "$HAS_NVIDIA" ]; then echo opencl-nvidia
    elif [ -n "$HAS_AMD"    ]; then echo rocm-opencl-runtime
    elif [ -n "$HAS_INTEL"  ]; then echo intel-compute-runtime
    fi
}

# Is there an ICD at all? Asked of /etc/OpenCL/vendors rather than of pacman:
# the file is what the loader reads, and it is what is missing when Resolve says
# it cannot find a GPU. A package can be installed and its ICD still absent.
have_icd() {
    local f
    for f in "$ICD_DIR"/*.icd; do [ -e "$f" ] && return 0; done
    return 1
}

# Installed, and by what? A pacman-managed install came from the AUR package and
# has the patchelf/udev work done; a bare .run extracted by Blackmagic's own
# installer has none of it, which changes what advice is honest.
resolve_owner() {
    [ -x "$RESOLVE_BIN" ] || return 1
    pacman -Qoq "$RESOLVE_BIN" 2>/dev/null || echo "(not a pacman package)"
}

# ── doctor ───────────────────────────────────────────────────

cmd_doctor() {
    detect_gpu
    local gpu icd owner rc=0
    gpu=$( [ -n "$HAS_NVIDIA" ] && echo NVIDIA || true
           [ -n "$HAS_AMD"    ] && echo AMD    || true
           [ -n "$HAS_INTEL"  ] && echo Intel  || true )
    gpu=$(echo $gpu)

    echo ""
    echo "  +- DaVinci Resolve on SynapseOS --------------------+"
    echo ""

    owner=$(resolve_owner)
    if [ -n "$owner" ]; then
        ok "Resolve installed at /opt/resolve  [$owner]"
    else
        bad "Resolve is not installed"
        info "syn resolve install   — after downloading the Linux .zip from"
        info "                        blackmagicdesign.com (free registration)"
        rc=1
    fi

    # OpenCL. The one that fails late and says nothing useful when it is wrong.
    icd=$(icd_package)
    if [ -z "$gpu" ]; then
        warn "No PCI display controller found — a VM? Resolve needs a real GPU."
        rc=1
    elif have_icd; then
        ok "OpenCL runtime present  [$gpu: $(ls "$ICD_DIR"/*.icd 2>/dev/null | xargs -n1 basename | tr '\n' ' ')]"
        if [ -n "$icd" ] && ! pacman -Qq "$icd" >/dev/null 2>&1; then
            warn "…but $icd is not installed, so that ICD may be for another GPU."
            info "sudo syn resolve setup"
        fi
    else
        bad "No OpenCL runtime — Resolve will refuse to start on a $gpu GPU"
        info "sudo syn resolve setup   (installs $icd)"
        rc=1
    fi
    command -v clinfo >/dev/null 2>&1 \
        && info "clinfo reports $(clinfo -l 2>/dev/null | grep -c 'Device' || echo 0) OpenCL device(s)"

    # The session environment, which is the SynapseOS-specific half.
    if [ -f "$OUR_DESKTOP" ]; then
        ok "Launch override in place  [$OUR_DESKTOP]"
    elif [ -n "$owner" ]; then
        bad "No launch override — Resolve will inherit the session's Qt environment"
        info "sudo syn resolve setup"
        rc=1
    fi

    case ":${XDG_DATA_DIRS:-/usr/local/share:/usr/share}:" in
        *:/usr/local/share:*|*:/usr/local/share/:*) ;;
        *) warn "XDG_DATA_DIRS excludes /usr/local/share, so the override cannot shadow"
           info "the packaged entry. Current: ${XDG_DATA_DIRS:-<unset, spec default>}"
           rc=1 ;;
    esac

    [ -n "${QT_QPA_PLATFORMTHEME:-}" ] \
        && info "QT_QPA_PLATFORMTHEME=$QT_QPA_PLATFORMTHEME in this shell — the override clears it"

    # The thing people actually report as "Resolve is broken".
    echo ""
    echo "  Codecs"
    if [ -n "$owner" ] && [ "${owner#davinci-resolve-studio}" != "$owner" ]; then
        ok "Studio edition — H.264/H.265 decode is supported"
    else
        warn "The FREE edition on Linux cannot decode H.264/H.265."
        info "Footage from phones and most cameras imports as media offline."
        info "syn resolve transcode <files>   — rewraps to DNxHR, which it reads"
    fi

    echo ""
    echo "  +---------------------------------------------------+"
    echo ""
    return $rc
}

# ── setup ────────────────────────────────────────────────────

# Write the shadowing desktop entry from the packaged one, so Name, Icon,
# MimeType and any actions stay whatever Blackmagic shipped and only the command
# changes. Generated rather than shipped in the package: it describes a file
# that belongs to another package and may not exist.
write_override() {
    [ -f "$PKG_DESKTOP" ] || return 1
    mkdir -p "$(dirname "$OUR_DESKTOP")" || return 1

    {
        echo "# Generated by 'syn resolve setup'. Do not edit — rewritten on every"
        echo "# Resolve install/upgrade by the 74-syn-resolve pacman hook."
        echo "#"
        echo "# This shadows $PKG_DESKTOP"
        echo "# by XDG_DATA_DIRS order. Delete this file to go back to launching"
        echo "# Resolve with the session's own environment."
        # TryExec must keep pointing at the real binary: it is the "is this app
        # installed" test, and pointing it at our launcher would answer yes on a
        # machine where Resolve has been removed.
        # Anchored to ^Exec= so TryExec is untouched, and `.*` before the binary
        # so a wrapped Exec (`env FOO=1 /opt/resolve/bin/resolve`) is rewritten
        # too rather than silently left alone. Everything after the binary — the
        # %u a .resolveproj double-click arrives on — is kept.
        sed "s|^Exec=.*/opt/resolve/bin/resolve|Exec=/usr/lib/syn/syn-resolve launch|" \
            "$PKG_DESKTOP"
    } > "$OUR_DESKTOP" || return 1
    chmod 0644 "$OUR_DESKTOP"

    # A copy that still launches the raw binary is worse than none: it looks
    # applied and changes nothing.
    grep -q '^Exec=/usr/lib/syn/syn-resolve launch' "$OUR_DESKTOP"
}

cmd_setup() {
    local quiet=""
    [ "${1:-}" = "--quiet" ] && quiet=1

    # No blanket root check. The two halves need root for different reasons and
    # fail at different times: writing the override needs whoever owns
    # /usr/local/share/applications, installing an ICD needs pacman. Refusing up
    # front on uid would also mean the hook — which IS root — and the test rig —
    # which writes to a temp dir it owns — could not share this path.
    detect_gpu
    local icd rc=0
    icd=$(icd_package)

    [ -n "$quiet" ] || echo ""

    if [ ! -f "$PKG_DESKTOP" ]; then
        [ -n "$quiet" ] || warn "no $PKG_DESKTOP yet — nothing to shadow."
    elif write_override; then
        [ -n "$quiet" ] || ok "launch override: $OUR_DESKTOP"
        update-desktop-database "$(dirname "$OUR_DESKTOP")" >/dev/null 2>&1
    else
        bad "could not write $OUR_DESKTOP"
        [ "$(id -u)" = 0 ] || info "run it as root: sudo syn resolve setup"
        rc=1
    fi

    # NEVER run pacman from here when the hook called us: pacman holds its
    # database lock for the whole transaction, so an install would deadlock or
    # be refused. The hook path reports; a human running `syn resolve setup`
    # installs.
    if have_icd; then
        [ -n "$quiet" ] || ok "OpenCL runtime already present"
    elif [ -z "$icd" ]; then
        warn "no supported GPU detected — install an OpenCL runtime yourself"
    elif [ -n "$quiet" ]; then
        # Advice, not a failure. This branch IS the pacman hook, and the hook
        # did its job — a non-zero exit here would make pacman print "command
        # failed to execute correctly" after a transaction that went fine.
        warn "Resolve needs an OpenCL runtime and none is installed."
        info "Run:  sudo syn resolve setup"
    elif [ "$(id -u)" != 0 ]; then
        warn "Resolve needs an OpenCL runtime and none is installed."
        info "Run:  sudo pacman -S --needed $icd"
        rc=1
    else
        echo "  Installing $icd for the detected GPU..."
        # Named concretely rather than letting the virtual `opencl-driver` dep
        # pick — see the header.
        pacman -S --needed --noconfirm "$icd" \
            && ok "$icd installed" \
            || { bad "$icd failed to install"; rc=1; }
    fi

    [ -n "$quiet" ] || { echo ""; echo "  Done. Check with: syn resolve doctor"; echo ""; }
    return $rc
}

# ── launch ───────────────────────────────────────────────────

cmd_launch() {
    [ -x "$RESOLVE_BIN" ] || { echo "syn resolve: $RESOLVE_BIN is not installed" >&2; exit 1; }

    # Resolve bundles its own Qt 5.15.2 and ships no platformthemes plugin, so
    # naming one can only point it at the system's. See the header: on this
    # configuration it almost certainly resolves to nothing, and this is
    # insurance against the configuration where it does not.
    unset QT_QPA_PLATFORMTHEME
    unset QT_STYLE_OVERRIDE

    # X11-only Qt5: go to Xwayland deliberately rather than by fallback.
    export QT_QPA_PLATFORM=xcb

    # Deliberately NOT touched: MANGOHUD. See the header — the implicit layer is
    # Vulkan-only and Resolve has no Vulkan.
    #
    # Deliberately NOT set: any QT_SCALE_FACTOR / QT_AUTO_SCREEN_SCALE_FACTOR.
    # Resolve has its own UI scaling preference (Preferences > User > UI
    # Settings) and Qt scaling applied underneath it fights that setting rather
    # than helping. Left to the application.

    exec "$RESOLVE_BIN" "$@"
}

# ── install ──────────────────────────────────────────────────

cmd_install() {
    [ "$(id -u)" = 0 ] && {
        echo "  Run this as your user, not root — makepkg refuses to run as root."
        return 1; }

    local zip="${1:-}"

    # The download cannot be automated and this does not try: Blackmagic put it
    # behind a registration form and a licence you have to accept. Everything
    # AFTER the download is what wastes people's time, and that is what this is.
    if [ -z "$zip" ]; then
        zip=$(ls -t "$HOME"/Downloads/DaVinci_Resolve*_Linux.zip 2>/dev/null | head -1)
    fi
    if [ -z "$zip" ] || [ ! -f "$zip" ]; then
        echo ""
        echo "  No DaVinci Resolve zip found."
        echo ""
        echo "  1. Download the LINUX version from Blackmagic (free registration):"
        echo "       https://www.blackmagicdesign.com/support/family/davinci-resolve-and-fusion"
        echo "  2. Leave it in ~/Downloads, then run this again — or pass the path:"
        echo "       syn resolve install /path/to/DaVinci_Resolve_19.0_Linux.zip"
        echo ""
        return 1
    fi

    # Studio and free are separate AUR packages that conflict with each other,
    # and the filename is what says which one was downloaded.
    local aurpkg=davinci-resolve
    case "$(basename "$zip")" in *_Studio_*) aurpkg=davinci-resolve-studio ;; esac

    # The version in the filename is the version the PKGBUILD must expect: it
    # pins both pkgver and the zip's sha256. A mismatch fails deep inside
    # makepkg with a checksum error that names neither cause, so it is checked
    # here where both numbers are in hand.
    local zipver
    zipver=$(basename "$zip" | sed -nE 's/^DaVinci_Resolve_(Studio_)?([0-9][0-9.]*)_Linux\.zip$/\2/p')

    command -v makepkg >/dev/null 2>&1 || {
        echo "  makepkg is missing. Install the toolchain first:"
        echo "      sudo pacman -S --needed base-devel git"
        return 1; }
    command -v git >/dev/null 2>&1 || {
        echo "  git is missing:  sudo pacman -S --needed git"; return 1; }

    local dir="${XDG_CACHE_HOME:-$HOME/.cache}/syn/resolve"
    mkdir -p "$dir" || return 1

    echo ""
    echo "  Edition : $aurpkg"
    echo "  Archive : $zip"
    echo "  Build in: $dir/$aurpkg"
    echo ""

    if [ -d "$dir/$aurpkg/.git" ]; then
        echo "  Updating the AUR checkout..."
        git -C "$dir/$aurpkg" pull --ff-only >/dev/null 2>&1 \
            || echo "  (pull failed — using the checkout as it is)"
    else
        echo "  Cloning https://aur.archlinux.org/$aurpkg.git ..."
        git clone --depth 1 "https://aur.archlinux.org/$aurpkg.git" "$dir/$aurpkg" \
            || { echo "  clone failed"; return 1; }
    fi

    # Already installed at this version? Say so rather than rebuilding four
    # gigabytes to arrive where we started.
    local have
    have=$(pacman -Q "$aurpkg" 2>/dev/null | awk '{print $2}')
    if [ -n "$zipver" ] && [ "${have%%-*}" = "$zipver" ]; then
        echo "  $aurpkg $have is already installed — nothing to build."
        echo ""
        cmd_doctor
        return
    fi

    # The PKGBUILD pins BOTH pkgver and the archive's sha256, so a version it
    # does not expect fails deep inside makepkg on a checksum, naming neither
    # cause. Caught here where both numbers are in hand.
    local want
    want=$(sed -nE 's/^pkgver=([0-9][0-9.]*)/\1/p' "$dir/$aurpkg/PKGBUILD" | head -1)
    if [ -n "$zipver" ] && [ -n "$want" ] && [ "$zipver" != "$want" ]; then
        echo ""
        echo "  VERSION MISMATCH — makepkg would fail this on a checksum:"
        echo "      your archive  : $zipver"
        echo "      PKGBUILD wants: $want"
        echo ""
        # Which way round matters. The AUR routinely lags Blackmagic by days,
        # and "go and download the older one" is bad advice for the common case
        # — so hand over the exact two lines to change instead. The hash is
        # computed rather than described because it is the only part nobody can
        # look up, and on a 4 GB archive it is the slow half of the answer.
        echo "  The AUR package often lags Blackmagic's releases. To build $zipver"
        echo "  anyway, edit $dir/$aurpkg/PKGBUILD:"
        echo ""
        echo "      pkgver=$zipver"
        printf "      sha256sums=('"
        printf '%s' "$(sha256sum "$zip" | cut -d' ' -f1)"
        echo "'   # first entry, the .zip"
        echo ""
        echo "  then run this again. Nothing else in it is version-specific."
        echo ""
        return 1
    fi

    cp -f "$zip" "$dir/$aurpkg/" || { echo "  could not stage the archive"; return 1; }

    # makepkg runs the PKGBUILD, which is code from the AUR. Saying so and
    # stopping for an answer is the least this can do; --noconfirm here would be
    # running a stranger's script on someone's behalf without telling them.
    echo "  This builds and installs an AUR package, which means running its"
    echo "  PKGBUILD as a script. Read it first if you like:"
    echo "      less $dir/$aurpkg/PKGBUILD"
    echo ""
    printf "  Build and install %s now? [y/N] " "$aurpkg"
    local ans; read -r ans
    case "${ans,,}" in y|yes) ;; *) echo "  Nothing done."; return 1 ;; esac

    ( cd "$dir/$aurpkg" && makepkg -si ) || { echo "  build failed"; return 1; }

    # The pacman hook has already run setup by now; this is the report.
    echo ""
    cmd_doctor
}

# ── transcode ────────────────────────────────────────────────

cmd_transcode() {
    # Free Resolve on Linux has no H.264/H.265 decode, so ordinary camera and
    # phone footage imports as media offline. DNxHR is what it does read, and
    # rewrapping is quicker than arguing with it.
    command -v ffmpeg >/dev/null 2>&1 || {
        echo "  ffmpeg is missing:  sudo pacman -S ffmpeg"; return 1; }

    local profile=dnxhr_sq outdir=""
    while [ $# -gt 0 ]; do
        case "$1" in
            --profile) profile="${2:?--profile needs a value}"; shift 2 ;;
            --out)     outdir="${2:?--out needs a directory}"; shift 2 ;;
            --) shift; break ;;
            -*) echo "  unknown option: $1"; return 1 ;;
            *)  break ;;
        esac
    done

    [ $# -gt 0 ] || {
        cat <<'USAGE'
  Usage: syn resolve transcode [--profile <p>] [--out <dir>] <file>...

  Rewraps footage into DNxHR in a .mov, which free Resolve on Linux can read.
  Output goes next to each input in a DNxHR/ folder unless --out says otherwise.

  Profiles, smallest to largest:
    dnxhr_lb   offline/proxy       dnxhr_sq   standard quality (default)
    dnxhr_hq   high quality        dnxhr_hqx  10-bit, needs a 10-bit source
USAGE
        return 1; }

    # 10-bit profiles need a 10-bit pixel format; the 8-bit ones must NOT get
    # one or ffmpeg refuses the combination.
    local pixfmt=yuv422p
    case "$profile" in dnxhr_hqx|dnxhr_444) pixfmt=yuv422p10le ;; esac

    local f rc=0 n=0
    for f in "$@"; do
        [ -f "$f" ] || { bad "not a file: $f"; rc=1; continue; }

        local dest="${outdir:-$(dirname "$f")/DNxHR}"
        mkdir -p "$dest" || { rc=1; continue; }

        local out="$dest/$(basename "${f%.*}").mov"
        if [ -e "$out" ]; then
            warn "exists, skipped: $out"
            continue
        fi

        echo "  → $(basename "$f")"
        # PCM audio because Resolve is as fussy about audio codecs as video
        # ones, and this is a mezzanine file — size is not the point.
        if ffmpeg -nostdin -hide_banner -loglevel error -stats \
                  -i "$f" \
                  -c:v dnxhd -profile:v "$profile" -pix_fmt "$pixfmt" \
                  -c:a pcm_s16le \
                  "$out" </dev/null; then
            ok "$out"
            n=$((n + 1))
        else
            bad "failed: $f"
            rm -f "$out"          # a half-written mezzanine is not a file to keep
            rc=1
        fi
    done

    echo ""
    echo "  $n file(s) written. Import the .mov files into Resolve."
    return $rc
}

# ── dispatch ─────────────────────────────────────────────────

usage() {
    cat <<'HELP'

syn resolve — DaVinci Resolve support on SynapseOS

  syn resolve doctor              What is installed, what is missing (default)
  sudo syn resolve setup          Apply the fixes: OpenCL runtime + launch env
  syn resolve install [zip]       Build the AUR package from a downloaded zip
  syn resolve transcode <file>..  Rewrap footage to DNxHR the free edition reads
  syn resolve launch [args]       Run Resolve with the SynapseOS launch env

The download itself is not automated: Blackmagic put it behind a registration
form. Everything after it is.

HELP
}

case "${1:-doctor}" in
    doctor|status)  shift 2>/dev/null; cmd_doctor ;;
    setup)          shift; cmd_setup "$@" ;;
    install)        shift; cmd_install "$@" ;;
    transcode)      shift; cmd_transcode "$@" ;;
    launch)         shift; cmd_launch "$@" ;;
    help|-h|--help) usage ;;
    *)              echo "Unknown command: $1"; usage; exit 1 ;;
esac
