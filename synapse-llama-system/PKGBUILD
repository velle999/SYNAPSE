# Maintainer: Velle Sinclair <brncomputerhelp@gmail.com>
#
# synapse-llama-system — Arch's llama.cpp, wearing the name synapd asks for.
#
# ── Why this exists ─────────────────────────────────────────────────────────
#
# synapd links libllama directly and its PKGBUILD says `depends=('synapse-llama')`.
# On SynapseOS that name is satisfied by synapse-llama-<backend>, which packages
# a llama.cpp tree archiso/build.sh stages with the CUDA or Vulkan recipe baked
# in. There is no staging tree on a machine that is not being built into an ISO,
# so that package cannot be produced anywhere else — and syn-update says so
# itself, which is why synapse-llama is the one component it refuses to update.
#
# Arch has shipped the same libraries since llama-cpp 0.3.0: /usr/lib/libllama.so
# and /usr/include/llama.h from `llama-cpp`, libggml from `ggml`, and the GPU
# backends as separate ggml-cuda / ggml-vulkan / ggml-hip / ggml-sycl packages
# that ggml loads out of /usr/lib/ggml at run time. synapd builds and links
# against them unmodified — verified, not assumed.
#
# So this package is the mapping and nothing else: it installs no files and
# claims the name.
#
# ⛔ IT DOES NOT CHANGE synapd's depends, AND THAT IS THE WHOLE POINT.
#
# Pointing synapd at `llama-cpp` instead would have been one line and would have
# stranded every installed SynapseOS machine. syn-update rebuilds synapd but
# never synapse-llama, so the new synapd would arrive asking for a name the
# installed libraries do not provide, pacman would refuse it, and the fix would
# be a hand-built package on every box. The dependency string stays exactly as
# it was; only the set of things that can satisfy it grows.
#
# ⚠ NOT FOR SYNAPSEOS, EVER. It conflicts with the real synapse-llama, is in no
# build list, on no ISO and in no installer roster, and preflight's UNREGISTERED
# table records that as deliberate.
pkgname=synapse-llama-system
pkgver=0.1.0
pkgrel=1
pkgdesc="Satisfies synapse-llama with the distribution's own llama.cpp — for machines that are not SynapseOS"
arch=('any')
url="https://github.com/velle999/SYNAPSE"
license=('GPL-2.0-or-later')

# ⚠ ggml IS NAMED EXPLICITLY even though llama-cpp already pulls it. synapd's
# own NEEDED list is libllama.so.0, libggml.so.0 AND libggml-base.so.0 — a
# direct link deserves a direct dependency, and llama-cpp's own depends are
# llama-cpp's business to change.
depends=('llama-cpp' 'ggml')

optdepends=('ggml-cuda: GPU offload on NVIDIA — ggml loads it from /usr/lib/ggml at run time'
            'ggml-vulkan: GPU offload on AMD and Intel, and on NVIDIA without CUDA'
            'ggml-hip: GPU offload through ROCm instead of Vulkan'
            'ggml-blas: faster prompt processing on the CPU')

# ⛔ PROVIDES *AND* CONFLICTS. Providing the name without conflicting would let
# both this and a real synapse-llama be installed, and they would fight over
# /usr/lib/libllama.so — pacman would stop the second one with a file conflict
# rather than a legible "these two cannot both be here".
provides=('synapse-llama')
conflicts=('synapse-llama')

# No source and no package(): there are no files. `makepkg` is happy with an
# empty package() and this is one of the few times that is the honest answer.
package() {
    :
}
