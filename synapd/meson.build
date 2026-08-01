project('synapd', 'c', version: '0.1.0',
  default_options: ['c_std=gnu17', 'warning_level=1'])

cc = meson.get_compiler('c')
dep_threads = dependency('threads')
# libm: inference_embed() L2-normalises with sqrt(). Linked explicitly because
# --no-undefined means an implicit libm from another dependency is not enough.
dep_m       = meson.get_compiler('c').find_library('m', required: true)
dep_systemd = dependency('libsystemd', required: false)

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

deps = [dep_threads, dep_m]
if dep_llama.found()
  deps += [dep_llama, dep_ggml, dep_ggml_base]
endif
if dep_systemd.found()
  deps += [dep_systemd]
endif

add_project_arguments('-D_GNU_SOURCE', language: 'c')

executable('synapd',
  files('src/main.c','src/inference.c','src/socket_server.c','src/wire.c',
        'src/context.c','src/scheduler.c','src/log.c'),
  include_directories: llama_inc,
  dependencies: deps,
  install: true)

# Unit test for the wire-protocol input gate. wire.c is pure (no llama, no
# sockets), so the test links just it — no model, no daemon, runs in a blink.
wire_test = executable('wire_test',
  files('tests/wire_test.c','src/wire.c'),
  include_directories: include_directories('include'))
test('wire', wire_test)

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

install_data('systemd/synapd.service',
  install_dir: '/usr/lib/systemd/system')
install_data('config/synapd.conf',
  install_dir: '/etc/synapd')
