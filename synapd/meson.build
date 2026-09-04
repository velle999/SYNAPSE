project('synapd', 'c', version: '0.1.0',
  default_options: ['c_std=gnu17', 'warning_level=1'])

cc = meson.get_compiler('c')
dep_threads = dependency('threads')
# libm: inference_embed() L2-normalises with sqrt(). Linked explicitly because
# --no-undefined means an implicit libm from another dependency is not enough.
dep_m       = meson.get_compiler('c').find_library('m', required: true)
dep_systemd = dependency('libsystemd', required: false)
# json-c for the llama.cpp-compatible face. Hand-rolling JSON for an API
# other people's clients call is how a quote inside a title becomes a parse
# error on their side — the same reason syn-cal's oauth.c uses a real one.
dep_jsonc   = dependency('json-c')

# llama.cpp — prefer the installed synapse-llama package (/usr/lib), and only
# fall back to llama-staging for a dev tree that hasn't installed it yet.
#
# System first, deliberately: llama used to be found ONLY via llama-staging, a
# build dir in $HOME wired in through /etc/ld.so.conf.d. That made a root daemon
# depend on a user's build tree, and let a stale /usr/lib copy shadow the real
# build — synapd ran months-old CPU-only libllama while claiming GPU was on.
# synapse-llama now owns those files; this fallback is only a convenience.
_system_llama = cc.find_library('llama', required: false)

_staging = join_paths(meson.project_source_root(), '..', 'llama-staging', 'usr')
_has_staging = not _system_llama.found() and run_command('test', '-d', join_paths(_staging, 'lib'), check: false).returncode() == 0

_lib_dirs = []
if _has_staging
    _lib_dirs += [join_paths(_staging, 'lib')]
    # Pass the staging include as a compile arg: include_directories()
    # rejects absolute paths that point near the source tree.
    add_project_arguments('-I' + join_paths(_staging, 'include'), language: 'c')
    # ld resolves libllama's own NEEDED libs (libggml, libggml-base,
    # libggml-cpu) via -rpath-link, not -L — without this the link fails
    # with undefined ggml_* references.
    add_project_link_arguments('-Wl,-rpath-link,' + join_paths(_staging, 'lib'), language: 'c')
endif

dep_llama = cc.find_library('llama', dirs: _lib_dirs, required: false)

# detect_gpu_layers() asks ggml which backends are actually compiled in, and
# reads the model's layer count from its GGUF metadata, so synapd needs the ggml
# symbols directly — libllama does not re-export them. They are split across two
# libs: the backend *registry* (ggml_backend_dev_count/_get) lives in libggml,
# while the device queries (ggml_backend_dev_memory) and gguf_* live in
# libggml-base. Link both or the link fails with undefined references.
dep_ggml      = cc.find_library('ggml',      dirs: _lib_dirs, required: false)
dep_ggml_base = cc.find_library('ggml-base', dirs: _lib_dirs, required: false)

llama_inc = include_directories('include')

if dep_systemd.found()
  add_project_arguments('-DHAVE_LIBSYSTEMD', language: 'c')
endif
if dep_llama.found()
  add_project_arguments('-DHAVE_LLAMA', language: 'c')
endif

deps = [dep_threads, dep_m, dep_jsonc]
if dep_llama.found()
  deps += [dep_llama, dep_ggml, dep_ggml_base]
endif
if dep_systemd.found()
  deps += [dep_systemd]
endif

add_project_arguments('-D_GNU_SOURCE', language: 'c')

executable('synapd',
  files('src/main.c','src/inference.c','src/socket_server.c','src/wire.c',
        'src/http_server.c',
        'src/context.c','src/scheduler.c','src/log.c','src/profile.c',
        'src/selected.c','src/pressure.c','src/offload.c','src/sysload.c'),
  include_directories: llama_inc,
  dependencies: deps,
  install: true)

# Unit test for the wire-protocol input gate. wire.c is pure (no llama, no
# sockets), so the test links just it — no model, no daemon, runs in a blink.
wire_test = executable('wire_test',
  files('tests/wire_test.c','src/wire.c'),
  include_directories: include_directories('include'))
test('wire', wire_test)

# Sampling-profile matching. profile.c is pure too — it reads files and compares
# strings — so this links it plus log.c for syn_log() and nothing else. The
# longest-match rule is the part worth pinning down: it is what stops a new
# [mistral] profile from quietly capturing Nemo.
# The llama.cpp-compatible face. Links http_server.c and log.c and nothing
# else — inference, context and the logger are stubbed in the test, so it runs
# on a
# machine with no model and no GPU, which is where a suite has to run to be
# worth having. SYNAPD_HTTP_SOCKET_PATH is redirected at the build directory:
# binding the real one would displace the daemon on a developer's own desktop.
http_test = executable('http_test',
  files('tests/http_test.c','src/http_server.c'),
  include_directories: include_directories('include'),
  c_args: ['-DSYNAPD_HTTP_SOCKET_PATH="' + meson.current_build_dir() + '/http_test.sock"'],
  dependencies: [dep_threads, dep_jsonc] +
                (dep_systemd.found() ? [dep_systemd] : []))
test('http', http_test, timeout: 60)

profile_test = executable('profile_test',
  files('tests/profile_test.c','src/profile.c','src/log.c'),
  include_directories: include_directories('include'),
  dependencies: dep_systemd.found() ? [dep_systemd] : [])
test('profile', profile_test)

# The model-name boundary, and the choice that outlives the process. selected.c
# is pure (stat, stdio, syn_log) so this links it plus log.c and nothing else —
# no llama, no socket. SYNAPD_MODEL_DIR and SYNAPD_SELECTED_FILE are redirected
# at a scratch tree the test builds, which is the only way to exercise a
# confinement whose whole job is to refuse everything outside one directory.
# The offload policy. pressure.c is pure (no llama, no ggml, no sockets, no
# daemon state) so this links it alone — and it has to be testable without a
# GPU, because the rule that matters is a FEEDBACK LOOP: shedding layers raises
# the free-VRAM figure the policy is reading, and a naive threshold oscillates
# for ever. That cannot be checked by looking at a real card once.
pressure_test = executable('pressure_test',
  files('tests/pressure_test.c','src/pressure.c'),
  include_directories: include_directories('include'))
test('pressure', pressure_test)

# What the machine is short of, out of /proc. Same split and the same reason:
# the branches worth testing are a kernel with no PSI and a machine actually
# stalling, and a build machine can be asked to be neither — so sysload.c takes
# its proc directory as a parameter and the suite hands it fixture files.
sysload_test = executable('sysload_test',
  files('tests/sysload_test.c','src/sysload.c'),
  include_directories: include_directories('include'))
test('sysload', sysload_test)

selected_test = executable('selected_test',
  files('tests/selected_test.c','src/selected.c','src/log.c'),
  include_directories: include_directories('include'),
  c_args: ['-DSYNAPD_MODEL_DIR="' + meson.current_build_dir() + '/seltest/models"',
           '-DSYNAPD_SELECTED_FILE="' + meson.current_build_dir() + '/seltest/model.selected"'],
  dependencies: dep_systemd.found() ? [dep_systemd] : [])
test('selected', selected_test)

# The system-sleep hook. Links nothing but libc — no llama, no threads — so a
# wedged or unbuilt model stack cannot stop it running, and it must run: it is
# what holds the suspend until the GPU is clear.
#
# install_mode is load-bearing. systemd runs only the EXECUTABLE files in
# system-sleep/ and silently ignores the rest, so a hook installed 0644 is not
# an error anywhere — it simply never runs, and the only symptom is that sleep
# quietly goes back to being slow.
executable('synapd-sleep-hook',
  files('src/sleep_hook.c'),
  include_directories: llama_inc,
  install: true,
  install_dir: '/usr/lib/systemd/system-sleep',
  install_mode: 'rwxr-xr-x')

# Same directory, same install_mode reasoning as above: 0644 here would mean a
# hook that never runs, and the only symptom would be a suspend that fails to
# freeze again some night. This one stops OTHER processes' GPU contexts (Plex
# transcodes) and names whoever is still holding the driver — see the script.
install_data('systemd/synapse-gpu-sleep',
  install_dir: '/usr/lib/systemd/system-sleep',
  install_mode: 'rwxr-xr-x')

# The connector watcher. /usr/lib/synapd, not system-sleep: it is a long-lived
# service and must be running across a suspend the hooks mishandle, not invoked
# by the same machinery whose behaviour it is there to record.
install_data('systemd/synapse-drm-watch',
  install_dir: '/usr/lib/synapd',
  install_mode: 'rwxr-xr-x')

# Creates /dev/nvidiactl and /dev/nvidia* before synapd probes for CUDA. Same
# install_mode reasoning as the sleep hooks: installed 0644 it is not an error
# anywhere, systemd simply reports it as not executable and synapd goes back to
# running a 12B model on four CPU threads.
install_data('systemd/synapse-nvidia-nodes',
  install_dir: '/usr/lib/synapd',
  install_mode: 'rwxr-xr-x')

install_data('systemd/synapd.service',
  install_dir: '/usr/lib/systemd/system')
install_data('systemd/synapse-drm-watch.service',
  install_dir: '/usr/lib/systemd/system')

# Sandbox for NVIDIA's stock persistence daemon, which ships as an unhardened
# sample unit and scored 9.6 UNSAFE. It is synapd-setup.service's Wants= that
# starts it at all, so its posture is ours to set. A drop-in rather than a
# patched unit so a nvidia-utils upgrade cannot silently revert it.
install_data('systemd/nvidia-persistenced-hardening.conf',
  install_dir: '/usr/lib/systemd/system/nvidia-persistenced.service.d',
  rename: '50-synapse-hardening.conf')
install_data('config/synapd.conf',
  install_dir: '/etc/synapd')

# Shipped sampling profiles. /usr, not /etc, precisely so an upgrade can replace
# them — local changes belong in /etc/synapd/profiles.d, which wins ties.
install_data('config/profiles.d/10-defaults.conf',
  install_dir: '/usr/share/synapd/profiles.d')
