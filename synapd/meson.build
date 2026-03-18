project('synapd', 'c', version: '0.1.0',
  default_options: ['c_std=gnu17', 'warning_level=1'])

cc = meson.get_compiler('c')
dep_threads = dependency('threads')
dep_systemd = dependency('libsystemd', required: false)

# llama.cpp — look in llama-staging relative to repo root, or system paths
llama_staging = join_paths(meson.source_root(), '..', 'llama-staging', 'usr')
llama_inc = include_directories(
    'include',
    join_paths(llama_staging, 'include'),
)

dep_llama = cc.find_library('llama',
    dirs: [join_paths(llama_staging, 'lib')],
    required: false)

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
