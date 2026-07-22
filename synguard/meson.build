# meson.build — synguard
project('synguard', 'c',
  version: '0.1.0',
  default_options: ['c_std=gnu17', 'warning_level=2', 'buildtype=debugoptimized']
)

dep_threads = dependency('threads')
# libsystemd: sd_notify() for Type=notify readiness. Replaces a
# system("systemd-notify") shell-out that could not satisfy NotifyAccess=main.
dep_systemd = dependency('libsystemd')

add_project_arguments('-D_GNU_SOURCE', '-DSYNGUARD_VERSION="@0@"'.format(meson.project_version()), language: 'c')

sources = files(
  'src/synguard_main.c',
  'src/rule_engine.c',
  'src/event_processor.c',
  'src/ai_classifier.c',
  'src/action_engine.c',
  'src/isolation.c',
  'src/secfeed.c',
  'src/core.c',
)

inc = include_directories('include')

executable('synguard', sources,
  include_directories: inc,
  dependencies: [dep_threads, dep_systemd],
  install: true,
  install_dir: get_option('bindir'),
)

# Verdict-feed slot management (the leak that once wedged the feed at capacity).
# secfeed.c depends only on the header-only logger + types, so the test links
# just it — no daemon, no kmod. The socket path is overridden to a temp file so
# the test needs neither root nor /run.
secfeed_test = executable('secfeed_test',
  files('tests/secfeed_test.c', 'src/secfeed.c'),
  include_directories: inc,
  dependencies: [dep_threads, dep_systemd],
  c_args: ['-DSYNGUARD_SECFEED_SOCKET="@0@/synguard-secfeed-test.sock"'.format(
    meson.current_build_dir())])
test('secfeed', secfeed_test, timeout: 60)

# The connect() wire format and the worm/C2 thresholds. netwatch_connect() and
# kmod_parse_event() live in event_processor.c, which pulls in the pipeline, so
# the test stubs the daemon entry points it never calls.
netwatch_test = executable('netwatch_test',
  files('tests/netwatch_test.c', 'src/event_processor.c', 'src/core.c'),
  include_directories: inc,
  dependencies: [dep_threads, dep_systemd])
test('netwatch', netwatch_test, timeout: 60)

# The unit is installed by the PKGBUILD, to /usr/lib/systemd/system — NOT here
# and NOT to /etc.
#
# This used to install to /etc/systemd/system as well, so the package owned two
# copies of the same unit. /etc wins (systemctl resolves FragmentPath there),
# which means every hardening change shipped in /usr/lib was inert until the
# /etc copy happened to be overwritten too. A packaged unit belongs in
# /usr/lib; /etc/systemd/system is the ADMIN's space, and a package writing
# there both shadows itself and clobbers local overrides on upgrade.

install_subdir('rules', install_dir: '/etc/synguard', strip_directory: false)

message('synguard @0@ configured.'.format(meson.project_version()))
