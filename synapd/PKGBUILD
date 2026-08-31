pkgname=synapd
pkgver=0.1.0
# 43: THE CLIENT SAID WHO IT WAS AND WE BELIEVED IT. The accept loop did
#   `w->client_pid = hdr.client_pid` — identity supplied by the thing being
#   observed, in the daemon whose whole job is security telemetry. The socket
#   is 0660 root:synapse so it was never open to the world, but any member of
#   that group could claim any PID, and the claim reached two places that
#   matter: context_push() files the event under that PID in the rolling
#   context the model is later shown, and the anomaly log names it — so
#   attacker-controlled text could be filed against another process and read
#   back as that process's history. Not a privilege escalation: nothing here
#   kills anything, and synguard re-verifies identity itself before it acts.
#   SO_PEERCRED now decides, and a header that CONTRADICTS the kernel is logged
#   rather than smoothed over. ⚠ No credentials means attribution to NOBODY,
#   never a fallback to the claim — that would hand the decision back to the
#   sender by another route. ⚠ `client_uid` was in the work item and had NEVER
#   been assigned: always 0 from calloc, which reads as root. Nothing consumed
#   it, but the next thing that did would have believed every client was root.
#   The header's own comment said "sender PID for privilege checks", which is
#   an invitation; it now says advisory.
#   Found by an outside review of the repo (GPT-5, via velle). The finding was
#   real and correctly sized.
# 44: the default model named a file nothing ever creates.
#   SYNAPD_DEFAULT_MODEL and the shipped synapd.conf both said
#   synapse-7b-q4_k_m.gguf. syn-model — the downloader the installer runs —
#   writes synapse.gguf. So on a FRESH INSTALL synapd looked for a model that
#   was not there and came up in "no model loaded, shell-assist mode only",
#   with the reason in the journal and nowhere else.
#   ⚠ INVISIBLE ON ANY BOX THAT HAS EVER PICKED A MODEL BY HAND, because
#   /var/lib/synapd/model.selected outranks the config — which is why it
#   survived this long on the machine it was written on.
pkgrel=50
pkgdesc="SynapseOS AI inference daemon — persistent llama.cpp backend"
arch=('x86_64')
url="https://github.com/velle999/SYNAPSE"
license=('GPL-2.0-or-later')
# synapse-llama provides libllama/libggml in /usr/lib. It is a real dependency:
# synapd previously resolved these through an /etc/ld.so.conf.d entry pointing
# at a build dir in $HOME, so pacman had no idea the daemon needed them and
# nothing stopped them being deleted or shadowed. synapse-llama-cuda `provides`
# this, so a GPU box satisfies the dep without pulling the CPU build in.
#
# ORDER MATTERS, and getting it wrong produces a SEGFAULT, not a build error.
#
# synapd is compiled against the llama.h that is INSTALLED (see meson.build on
# why system headers are preferred). llama.cpp adds fields to
# llama_context_params and llama_model_params between releases, so a synapd
# built against an OLD header and run against a NEW libllama passes a
# short struct into llama_init_from_model() and the context constructor reads
# off the end of it. That is what happened at the b8272 -> b10241 bump: synapd
# was packaged BEFORE the new synapse-llama was installed, and the daemon
# crash-looped on SIGSEGV inside llama_context::llama_context. Nothing warns —
# the soname did not change, so it links and resolves perfectly.
#
# So: install synapse-llama FIRST, then build this. build-all.sh already does
# that (build_script_pkg pacman -U's synapse-llama before build_component
# synapd); a hand-run makepkg here is the path that can get it wrong. Wipe
# build/ and src/synapd-0.1.0/ too — a stale meson cache pins the OLD library
# by absolute path and fails with "libggml-base.so.0.9.7 ... missing".
#
depends=('glibc' 'systemd-libs' 'gcc-libs' 'libgomp' 'synapse-llama' 'json-c')

# ⚠ THE GPU HALF IS THE PROVIDER'S, NOT SYNAPD'S. On SynapseOS the backend is
# baked into synapse-llama-<backend>; anywhere else ggml loads one out of
# /usr/lib/ggml at run time, so naming them here is how somebody installing
# synapse-llama-system finds out that CPU-only is a choice they can change.
optdepends=('ggml-cuda: GPU offload on NVIDIA, with synapse-llama-system'
            'ggml-vulkan: GPU offload on AMD and Intel, with synapse-llama-system')
makedepends=('meson' 'ninja' 'gcc' 'pkg-config')
backup=('etc/synapd/synapd.conf')
install=synapd.install
# ⛔ THE RELEASE URL, AND IT CARRIES THE pkgrel. The filename before `::` is
# what makepkg looks for on disk, so a build from this checkout uses the tarball
# build-all.sh just collected and never downloads. The URL after it is for
# everybody else, and it names <pkgver>-<pkgrel> because that tag is the only
# thing that makes a published source unambiguously the one this PKGBUILD was
# written against.
#
# ⛔ AND sha256sums STAYS 'SKIP' — a real checksum breaks every local build the
# moment the tree changes, which is every build that matters here.
source=("$pkgname-$pkgver.tar.gz::https://github.com/velle999/$pkgname/releases/download/$pkgver-$pkgrel/$pkgname-$pkgver.tar.gz")
sha256sums=('SKIP')

prepare() {
    # Make llama-staging accessible for meson.build's library search.
    #
    # -n (--no-dereference) matters: this symlink survives in src/ from the last
    # build, and plain `ln -sf` *follows* an existing symlink-to-a-directory and
    # tries to create the link inside it — which fails, so every re-run of
    # makepkg died in prepare() with "failed to create symbolic link
    # '.../src/llama-staging/llama-staging'". Only a clean src/ ever worked.
    if [ -d "$startdir/llama-staging/usr" ]; then
        ln -sfn "$startdir/llama-staging" "$srcdir/llama-staging"
    elif [ -d "$startdir/../llama-staging/usr" ]; then
        ln -sfn "$startdir/../llama-staging" "$srcdir/llama-staging"
    fi
}

build() {
    cd "$srcdir/synapd-0.1.0"
    meson setup build --buildtype=release --prefix=/usr
    meson compile -C build
}

package() {
    cd "$srcdir/synapd-0.1.0"
    DESTDIR="$pkgdir" meson install -C build
    install -dm750 "$pkgdir/var/lib/synapd/models"
    install -dm750 "$pkgdir/var/log/synapd"
    install -Dm644 systemd/synapd.service \
        "$pkgdir/usr/lib/systemd/system/synapd.service"
    install -Dm644 systemd/synapd.socket \
        "$pkgdir/usr/lib/systemd/system/synapd.socket"
    install -Dm644 systemd/synapd-setup.service \
        "$pkgdir/usr/lib/systemd/system/synapd-setup.service"
    # ⛔ SHIPPED, NOT ENABLED. The loopback port for llama.cpp-shaped frontends
    # is an unauthenticated model on a port every local process can reach, so it
    # exists on disk and does nothing until somebody runs
    #   systemctl enable --now synapd-http-proxy.socket
    # Neither unit is in synapd.install's enable list, deliberately.
    install -Dm644 systemd/synapd-http-proxy.socket \
        "$pkgdir/usr/lib/systemd/system/synapd-http-proxy.socket"
    install -Dm644 systemd/synapd-http-proxy.service \
        "$pkgdir/usr/lib/systemd/system/synapd-http-proxy.service"

    # Re-asserts synapd's ownership of the kmod's writable sysfs nodes, which a
    # module reload silently resets to root:root. See the unit for why this is
    # a timer and not an event.
    install -Dm644 systemd/synapd-kmod-perms.service \
        "$pkgdir/usr/lib/systemd/system/synapd-kmod-perms.service"
    install -Dm644 systemd/synapd-kmod-perms.timer \
        "$pkgdir/usr/lib/systemd/system/synapd-kmod-perms.timer"

    # Restarts the daemon on upgrade. A long-lived daemon does not pick up a new
    # binary on its own, and nothing in a desktop session restarts one — the
    # upgraded package sat on disk while the old process served from memory.
    install -Dm644 hooks/73-synapd-restart.hook \
        "$pkgdir/usr/share/libalpm/hooks/73-synapd-restart.hook"

    # Creates the unprivileged `synapd` user and owns its state dirs, so the
    # daemon that parses socket input is not root. The tmpfiles Z lines also
    # re-own a model that an older, root-running synapd left behind.
    install -Dm644 sysusers/synapd.conf \
        "$pkgdir/usr/lib/sysusers.d/synapd.conf"
    install -Dm644 tmpfiles/synapd.conf \
        "$pkgdir/usr/lib/tmpfiles.d/synapd.conf"
}
