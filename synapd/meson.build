project('synapd', 'c', version: '0.1.0',
  default_options: ['c_std=gnu17', 'warning_level=1'])

cc = meson.get_compiler('c')
dep_threads = dependency('threads')
dep_systemd = dependency('libsystemd', required: false)

# llama.cpp — look in llama-staging (symlinked into srcdir by PKGBUILD),
# then fall back to system paths.
_staging = join_paths(meson.project_source_root(), '..', 'llama-staging', 'usr')
_has_staging = run_command('test', '-d', join_paths(_staging, 'lib'), check: false).returncode() == 0

_lib_dirs = []
if _has_staging
    _lib_dirs += [join_paths(_staging, 'lib')]
    # Pass the staging include as a compile arg: include_directories()
    # rejects absolute paths that point near the source tree.
    add_project_arguments('-I' + join_paths(_staging, 'include'), language: 'c')
endif

dep_llama = cc.find_library('llama', dirs: _lib_dirs, required: false)
llama_inc = include_directories('include')

if dep_systemd.found()
  add_project_arguments('-DHAVE_LIBSYSTEMD', language: 'c')
endif
if dep_llama.found()
  add_project_arguments('-DHAVE_LLAMA', language: 'c')
endif

deps = [dep_threads]
if dep_llama.found()
  deps += [dep_llama]
endif
if dep_systemd.found()
  deps += [dep_systemd]
endif

add_project_arguments('-D_GNU_SOURCE', language: 'c')

executable('synapd',
  files('src/main.c','src/inference.c','src/socket_server.c',
        'src/context.c','src/scheduler.c','src/log.c'),
  include_directories: llama_inc,
  dependencies: deps,
  install: true)

install_data('systemd/synapd.service',
  install_dir: '/usr/lib/systemd/system')
install_data('config/synapd.conf',
  install_dir: '/etc/synapd')
