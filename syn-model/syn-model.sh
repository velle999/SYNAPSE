#!/usr/bin/env bash
# syn model — SynapseOS model manager
set -euo pipefail

VERSION="0.1.0-synapse"
MODEL_DIR="/var/lib/synapd/models"
MODEL_PATH="$MODEL_DIR/synapse.gguf"
SYNAPD_SERVICE="synapd"

# Where `fetch` reads its job from and reports back to. Created by tmpfiles.d
# as root:synapse 0770 — the group synui's user is already in, because the
# panel has to be able to drop a request and read the progress back.
RUN_DIR="/run/syn-model"
REQ_DIR="$RUN_DIR/req"

# Redirectable so tests/fetch_validate_test.sh can drive `fetch` against a
# scratch directory. Honoured ONLY when not root: the whole point of the
# request validation is that root is handed untrusted input, and an
# environment variable that moved root's destination directory would hand it
# the one thing it refuses to take from the request. systemd starts the unit
# with a clean environment regardless; this makes it not matter.
if [ "$(id -u)" != 0 ]; then
    MODEL_DIR="${SYN_MODEL_DIR:-$MODEL_DIR}"
    MODEL_PATH="$MODEL_DIR/synapse.gguf"
    RUN_DIR="${SYN_MODEL_RUN_DIR:-$RUN_DIR}"
    REQ_DIR="$RUN_DIR/req"
fi

# Download robustness knobs
RETRIES=3                              # download attempts before giving up
MIN_MODEL_BYTES=$((10 * 1024 * 1024))  # 10MB floor — anything smaller is an
                                       # error page, not a model
ASSUME_YES=0                           # -y/--yes: never prompt (first-boot)

# Default model — Mistral 7B Q4_K_M (~4.1GB)
DEFAULT_MODEL_URL="https://huggingface.co/TheBloke/Mistral-7B-Instruct-v0.2-GGUF/resolve/main/mistral-7b-instruct-v0.2.Q4_K_M.gguf"

# Smaller option — Phi-3 Mini Q4 (~2.2GB)
PHI3_URL="https://huggingface.co/microsoft/Phi-3-mini-4k-instruct-gguf/resolve/main/Phi-3-mini-4k-instruct-q4.gguf"

# Tiny option — Qwen2 0.5B (~400MB, fast on CPU)
TINY_URL="https://huggingface.co/Qwen/Qwen2-0.5B-Instruct-GGUF/resolve/main/qwen2-0_5b-instruct-q4_k_m.gguf"

usage() {
    cat << HELP
syn-model $VERSION — SynapseOS Model Manager

Usage:
  syn-model download [MODEL] [-y]   Download a model (default: mistral-7b)
  syn-model list                    List available models
  syn-model status                  Show current model status
  syn-model remove [-y]             Remove installed model
  syn-model fetch TOKEN             Run a queued download request (root; the
                                    control panel's downloader calls this
                                    through syn-model-download@TOKEN.service)
  syn-model help                    This help

Options:
  -y, --yes    Assume yes — never prompt (used by first-boot)

Models:
  mistral-7b   Mistral 7B Instruct Q4_K_M (~4.1GB) — recommended
  phi3         Phi-3 Mini 4K Instruct Q4 (~2.2GB) — good balance
  tiny         Qwen2 0.5B Q4 (~400MB) — fast, lower quality

HELP
}

status() {
    echo "SynapseOS Model Status"
    echo "────────────────────────────────"
    if [ -f "$MODEL_PATH" ]; then
        SIZE=$(du -sh "$MODEL_PATH" | cut -f1)
        echo "  Model:    $MODEL_PATH"
        echo "  Size:     $SIZE"
        echo "  Status:   ✓ installed"
    else
        echo "  Model:    not installed"
        echo "  Status:   ✗ AI features disabled"
        echo ""
        echo "  Run: syn-model download"
    fi
    echo ""
    echo "  synapd:   $(systemctl is-active $SYNAPD_SERVICE 2>/dev/null || echo unknown)"
    journalctl -t synapd -n 1 --no-pager 2>/dev/null | grep -o "model=.*" || true
}

list_models() {
    cat << LIST
Available models:

  mistral-7b   Mistral 7B Instruct v0.2 Q4_K_M   ~4.1GB   ★★★★★ recommended
  phi3         Phi-3 Mini 4K Instruct Q4          ~2.2GB   ★★★★☆ good balance
  tiny         Qwen2 0.5B Instruct Q4_K_M         ~400MB   ★★★☆☆ fast/low RAM

LIST
}

# ── Download helpers ──────────────────────────────────────────
die() { echo "✗ $*" >&2; exit 1; }

human() { numfmt --to=iec --suffix=B "${1:-0}" 2>/dev/null || echo "${1:-0} bytes"; }

# Expected size in bytes from a HEAD request (follows redirects to the CDN).
# Echoes a number, or empty if the server doesn't report Content-Length.
remote_size() {
    local sz
    sz=$(curl -sIL "$1" 2>/dev/null \
         | awk 'BEGIN{IGNORECASE=1} /^content-length:/{v=$2} END{gsub(/\r/,"",v); print v}') || true
    [[ "$sz" =~ ^[0-9]+$ ]] && echo "$sz" || echo ""
}

# Free bytes on the filesystem holding $1.
free_bytes() { df -PB1 "$1" 2>/dev/null | awk 'NR==2{print $4}'; }

filesize() { stat -c%s "$1" 2>/dev/null || echo 0; }

# A real GGUF model begins with the ASCII magic "GGUF".
is_gguf() { [ "$(head -c4 "$1" 2>/dev/null)" = "GGUF" ]; }

# One download attempt; resumes a partial $2 if present. Returns curl/wget rc.
fetch_once() {
    local url="$1" dest="$2"
    if command -v curl &>/dev/null; then
        curl -fL -C - --progress-bar -o "$dest" "$url"
    elif command -v wget &>/dev/null; then
        wget -c --progress=bar:force -O "$dest" "$url"
    else
        die "curl or wget required"
    fi
}

# Classify the downloaded temp file:
#   0 = complete and valid     1 = incomplete (resume)     2 = corrupt (restart)
validate_download() {
    local dest="$1" expected="$2" actual
    [ -f "$dest" ] || return 1
    actual=$(filesize "$dest")

    if [ -n "$expected" ]; then
        [ "$actual" -lt "$expected" ] && return 1          # partial → resume
        [ "$actual" -gt "$expected" ] && return 2          # overshoot → corrupt
    else
        [ "$actual" -lt "$MIN_MODEL_BYTES" ] && return 2   # too small → error page
    fi
    is_gguf "$dest" || return 2                            # wrong content
    return 0
}

download() {
    local model="${1:-mistral-7b}"
    local url

    case "$model" in
        mistral-7b|mistral) url="$DEFAULT_MODEL_URL" ;;
        phi3|phi)           url="$PHI3_URL" ;;
        tiny|qwen)          url="$TINY_URL" ;;
        http*)              url="$model" ;;  # custom URL
        *)
            echo "Unknown model: $model"
            echo "Run: syn-model list"
            exit 1
            ;;
    esac

    echo "SynapseOS Model Downloader"
    echo "────────────────────────────────"
    echo "  Model:  $model"
    echo "  URL:    $url"
    echo "  Dest:   $MODEL_PATH"

    if [ -f "$MODEL_PATH" ]; then
        if [ "$ASSUME_YES" = 1 ]; then
            :
        elif [ -t 0 ]; then
            read -r -p "Model already installed. Replace? [y/N] " confirm
            [[ "$confirm" =~ ^[Yy]$ ]] || exit 0
        else
            echo "Model already installed; pass --yes to replace."
            exit 0
        fi
    fi

    mkdir -p "$MODEL_DIR"
    local tmp="$MODEL_PATH.tmp"

    # Pre-flight: make sure there's room for what's left to fetch (+5% slack).
    local expected free have needed
    expected=$(remote_size "$url")
    if [ -n "$expected" ]; then
        echo "  Size:   $(human "$expected")"
        have=$(filesize "$tmp")
        needed=$(( expected - have + expected / 20 ))
        (( needed < 0 )) && needed=0
        free=$(free_bytes "$MODEL_DIR")
        if [ -n "$free" ] && [ "$needed" -gt "$free" ]; then
            die "insufficient disk space in $MODEL_DIR: need ~$(human "$needed"), have $(human "$free")"
        fi
    fi
    echo ""

    # Fetch with resume + retries, validating after each attempt.
    local attempt rc v ok=0 before last_before=-1
    for (( attempt = 1; attempt <= RETRIES; attempt++ )); do
        # If the previous attempt didn't grow the file, the server likely
        # ignored our resume request — restart from scratch instead of looping
        # on a stuck partial.
        before=$(filesize "$tmp")
        if [ "$before" -gt 0 ] && [ "$before" = "$last_before" ]; then
            echo "  resume made no progress — restarting download from scratch" >&2
            rm -f "$tmp"; before=0
        fi
        last_before=$before

        echo "Downloading (attempt $attempt/$RETRIES)..."
        rc=0; fetch_once "$url" "$tmp" || rc=$?
        v=0; validate_download "$tmp" "$expected" || v=$?

        if [ "$v" = 0 ]; then ok=1; break; fi
        if [ "$v" = 2 ]; then
            echo "  ✗ downloaded data is not a valid model — discarding and retrying" >&2
            rm -f "$tmp"
        else
            echo "  download interrupted (rc=$rc) — will resume" >&2
        fi
        sleep 2
    done

    [ "$ok" = 1 ] || die "download failed after $RETRIES attempts (see messages above)"

    mv "$tmp" "$MODEL_PATH"
    echo ""
    echo "✓ Model downloaded and verified: $(human "$(filesize "$MODEL_PATH")")"
    echo ""

    if command -v systemctl &>/dev/null; then
        echo "Restarting synapd..."
        systemctl restart "$SYNAPD_SERVICE" 2>/dev/null \
            || echo "  (could not restart $SYNAPD_SERVICE — start it manually)"
        sleep 3
        systemctl status "$SYNAPD_SERVICE" --no-pager -l 2>/dev/null | tail -5 || true
        echo ""
    fi
    echo "✓ SynapseOS AI is ready. Run: synsh"
}

# ── Request-driven fetch ──────────────────────────────────────
#
# `syn-model fetch TOKEN` is the privileged half of the control panel's model
# downloader. It is never run by hand: syn-model-download@TOKEN.service runs
# it as root, and a polkit rule lets the `synapse` group start that unit.
#
# The caller is therefore NOT trusted, even though it had to be in the synapse
# group to get here. Everything the request file carries is re-validated below,
# because the alternative is a group member handing root an arbitrary URL and
# an arbitrary path to write it to:
#
#   - the URL must be huggingface.co over https, with no whitespace, quotes or
#     control characters — it is passed to curl as one argv element, never
#     through a shell;
#   - the destination is a BARE FILENAME ending .gguf and is joined to
#     $MODEL_DIR here, so no request can name a path. This is the same rule
#     synapd enforces on SYN_MSG_RELOAD, for the same reason;
#   - an existing file is never overwritten. A model already on disk needs no
#     download, and this is what stops a request replacing the model synapd is
#     running with something else.
#
# The request is read once and unlinked immediately: the directory is group-
# writable, so re-reading it after validating would be a window in which the
# contents could change under us.

# Atomic, because synui polls this file and a half-written line would read as
# a finished download at 0%.
progress_write() {
    local token="$1" state="$2" got="$3" total="$4" msg="${5:-}"
    local pct=0
    [ -n "$total" ] && [ "$total" -gt 0 ] 2>/dev/null && pct=$(( got * 100 / total ))
    local dest="$RUN_DIR/$token.progress"
    printf 'state=%s\npct=%d\ngot=%d\ntotal=%d\nmsg=%s\n' \
        "$state" "$pct" "${got:-0}" "${total:-0}" "$msg" > "$dest.tmp"
    chmod 0640 "$dest.tmp" 2>/dev/null || true
    chgrp synapse "$dest.tmp" 2>/dev/null || true
    mv -f "$dest.tmp" "$dest"
}

# curl in the background so the shell can report how far along it is. Same
# flags as fetch_once (resume, follow redirects, fail on HTTP error); the
# progress bar is dropped because nothing is watching stdout here.
fetch_with_progress() {
    local url="$1" dest="$2" expected="$3" token="$4"
    local pid rc=0 got

    curl -fsSL -C - -o "$dest" "$url" &
    pid=$!
    while kill -0 "$pid" 2>/dev/null; do
        got=$(filesize "$dest")
        progress_write "$token" running "$got" "$expected" ""
        sleep 1
    done
    wait "$pid" || rc=$?
    return $rc
}

fetch() {
    local token="${1:-}"

    # The token is half of two file paths below, so it is checked before it is
    # ever interpolated — no dots-and-slashes, no leading dash.
    [[ "$token" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$ ]] \
        || die "invalid request token"

    local req="$REQ_DIR/$token.request"
    [ -f "$req" ] || die "no such request: $token"

    local url="" file="" line key val
    while IFS= read -r line; do
        key="${line%%=*}"; val="${line#*=}"
        case "$key" in
            url)  url="$val"  ;;
            file) file="$val" ;;
        esac
    done < "$req"
    rm -f "$req"

    [[ "$url" =~ ^https://huggingface\.co/[A-Za-z0-9._~:/?#@!$\&()*+,\;=%-]+$ ]] \
        || { progress_write "$token" failed 0 0 "refused: not a huggingface.co https URL"
             die "refused URL: $url"; }

    [[ "$file" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,120}\.gguf$ ]] \
        || { progress_write "$token" failed 0 0 "refused: bad destination filename"
             die "refused destination: $file"; }

    local path="$MODEL_DIR/$file"
    if [ -e "$path" ]; then
        progress_write "$token" failed 0 0 "already installed"
        die "already installed: $path"
    fi

    mkdir -p "$MODEL_DIR"
    local tmp="$path.part"
    progress_write "$token" running 0 0 "starting"

    local expected free have needed
    expected=$(remote_size "$url")
    if [ -n "$expected" ]; then
        have=$(filesize "$tmp")
        needed=$(( expected - have + expected / 20 ))
        (( needed < 0 )) && needed=0
        free=$(free_bytes "$MODEL_DIR")
        if [ -n "$free" ] && [ "$needed" -gt "$free" ]; then
            progress_write "$token" failed 0 "$expected" \
                "not enough disk space: need $(human "$needed"), have $(human "$free")"
            die "insufficient disk space in $MODEL_DIR"
        fi
    fi

    # Same resume/validate/retry shape as download(), reporting to the progress
    # file instead of a terminal.
    local attempt rc v ok=0 before last_before=-1
    for (( attempt = 1; attempt <= RETRIES; attempt++ )); do
        before=$(filesize "$tmp")
        if [ "$before" -gt 0 ] && [ "$before" = "$last_before" ]; then
            rm -f "$tmp"; before=0
        fi
        last_before=$before

        rc=0; fetch_with_progress "$url" "$tmp" "$expected" "$token" || rc=$?
        v=0; validate_download "$tmp" "$expected" || v=$?

        if [ "$v" = 0 ]; then ok=1; break; fi
        if [ "$v" = 2 ]; then
            rm -f "$tmp"
            progress_write "$token" running 0 "$expected" \
                "server sent something that is not a model — retrying"
        else
            progress_write "$token" running "$(filesize "$tmp")" "$expected" \
                "interrupted (rc=$rc) — resuming"
        fi
        sleep 2
    done

    if [ "$ok" != 1 ]; then
        progress_write "$token" failed "$(filesize "$tmp")" "$expected" \
            "download failed after $RETRIES attempts"
        die "download failed after $RETRIES attempts"
    fi

    mv "$tmp" "$path"
    # synapd runs unprivileged and has to be able to read what root just wrote.
    chown synapd:synapd "$path" 2>/dev/null || true
    chmod 0644 "$path"

    local size; size=$(filesize "$path")
    progress_write "$token" done "$size" "$size" "$file"
    echo "✓ Downloaded $path ($(human "$size"))"
}

# ── Request-driven delete ─────────────────────────────────────
#
# The other half of the picker's privileged pair, and it exists for the same
# reason `fetch` does: /var/lib/synapd/models is 0750 synapd:synapse, so synui
# — running as the logged-in user — can LIST the directory and READ the models,
# and cannot unlink inside it. Deleting needs write on the directory, which is
# root's. syn-model-delete@TOKEN.service runs this, and a polkit rule lets the
# `synapse` group start that unit.
#
# The caller is NOT trusted, exactly as in `fetch`, and for a sharper reason:
# this one removes files. Everything the request carries is re-validated here.
#
#   - the destination is a BARE FILENAME ending .gguf, joined to $MODEL_DIR
#     here, so no request can name a path out of the directory;
#   - it must be a regular file, never a symlink — a symlink planted in the
#     spool would otherwise make root unlink its target;
#   - the request is read once and unlinked immediately, because the spool is
#     group-writable and re-reading after validating is a window in which the
#     contents could change.
#
# It does NOT refuse to delete the model synapd currently has open. That is the
# user's call and the daemon holds it in memory regardless; what would actually
# break is the NEXT start, so the remembered pick is cleared below instead.
delete_request() {
    local token="${1:-}"

    [[ "$token" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$ ]] \
        || die "invalid request token"

    local req="$REQ_DIR/$token.delete"
    [ -f "$req" ] || die "no such request: $token"

    local file="" line key val
    while IFS= read -r line; do
        key="${line%%=*}"; val="${line#*=}"
        case "$key" in
            file) file="$val" ;;
        esac
    done < "$req"
    rm -f "$req"

    [[ "$file" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,120}\.gguf$ ]] \
        || { progress_write "$token" failed 0 0 "refused: bad filename"
             die "refused delete target: $file"; }

    local path="$MODEL_DIR/$file"

    # -L before -f: `[ -f ]` follows symlinks and would say yes to a link
    # pointing at a real file elsewhere.
    if [ -L "$path" ]; then
        progress_write "$token" failed 0 0 "refused: that name is a symlink"
        die "refused delete target (symlink): $path"
    fi
    if [ ! -f "$path" ]; then
        progress_write "$token" failed 0 0 "no such model"
        die "no such model: $path"
    fi

    rm -f -- "$path" || {
        progress_write "$token" failed 0 0 "could not delete the file"
        die "failed to delete: $path"
    }

    # The remembered pick must not name a file that is gone: synapd would try
    # to load it on the next start, fail, and fall back to its --model flag
    # while logging a confusing reason. Clearing it makes that fallback the
    # deliberate path rather than an error path. Only when it named THIS file.
    local selected="/var/lib/synapd/model.selected"
    if [ -f "$selected" ] && [ "$(cat "$selected" 2>/dev/null)" = "$file" ]; then
        rm -f "$selected" 2>/dev/null \
            && echo "cleared the remembered model pick ($file)"
    fi

    progress_write "$token" done 0 0 "deleted $file"
    echo "✓ Deleted $path"
}

remove() {
    if [ ! -f "$MODEL_PATH" ]; then
        echo "No model installed at $MODEL_PATH"
        exit 0
    fi
    if [ "$ASSUME_YES" != 1 ]; then
        if [ -t 0 ]; then
            read -r -p "Remove $MODEL_PATH? [y/N] " confirm
            [[ "$confirm" =~ ^[Yy]$ ]] || exit 0
        else
            echo "Refusing to remove non-interactively without --yes"
            exit 1
        fi
    fi
    rm -f "$MODEL_PATH"
    command -v systemctl &>/dev/null && systemctl restart "$SYNAPD_SERVICE" 2>/dev/null || true
    echo "✓ Model removed"
}

# ── Argument parsing ──────────────────────────────────────────
# Collect -y/--yes from anywhere; keep remaining positional args.
ARGS=()
for arg in "$@"; do
    case "$arg" in
        -y|--yes) ASSUME_YES=1 ;;
        *)        ARGS+=("$arg") ;;
    esac
done
set -- "${ARGS[@]}"

case "${1:-help}" in
    download) download "${2:-mistral-7b}" ;;
    fetch)    fetch "${2:-}" ;;
    delete-request) delete_request "${2:-}" ;;
    list)     list_models ;;
    status)   status ;;
    remove)   remove ;;
    help|-h|--help) usage ;;
    *) echo "Unknown command: $1"; usage; exit 1 ;;
esac
