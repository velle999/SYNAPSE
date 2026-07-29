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
  'src/sg_log.c',
)

inc = include_directories('include')

# ── BPF-LSM enforcement gate ────────────────────────────────────────────
# clang and bpftool are BUILD-time only: the CO-RE object is compiled once
# here and loaded by libbpf at runtime, so an installed system needs no
# compiler. That is the whole reason this path is preferable to growing the
# DKMS kmod — no vermagic to go stale against the running kernel.
bpf_opt   = get_option('bpf_lsm')
cc_clang  = find_program('clang',   required: bpf_opt.enabled())
cc_bpftool= find_program('bpftool', required: bpf_opt.enabled())
dep_bpf   = dependency('libbpf', required: bpf_opt.enabled())

bpf_ok = not bpf_opt.disabled() and cc_clang.found() \
         and cc_bpftool.found() and dep_bpf.found()

bpf_sources = []
bpf_deps    = []
bpf_args    = []

if bpf_ok
  # vmlinux.h from the BUILD host's BTF. CO-RE relocations keep the resulting
  # object portable to other kernels, so this is not a build-host pin.
  vmlinux_h = custom_target('vmlinux.h',
    output: 'vmlinux.h',
    command: [cc_bpftool, 'btf', 'dump', 'file', '/sys/kernel/btf/vmlinux',
              'format', 'c'],
    capture: true)

  bpf_o = custom_target('sg_lsm.bpf.o',
    input: 'bpf/sg_lsm.bpf.c',
    output: 'sg_lsm.bpf.o',
    depends: vmlinux_h,
    command: [cc_clang, '-O2', '-g', '-target', 'bpf', '-D__TARGET_ARCH_x86',
              '-I' + meson.current_build_dir(),
              '-I' + meson.current_source_dir() / 'include',
              '-c', '@INPUT@', '-o', '@OUTPUT@'])

  # The skeleton is what lets the daemon reference maps and programs by name
  # instead of by index.
  bpf_skel = custom_target('sg_lsm.skel.h',
    input: bpf_o,
    output: 'sg_lsm.skel.h',
    command: [cc_bpftool, 'gen', 'skeleton', '@INPUT@'],
    capture: true)

  bpf_sources = [files('src/bpf_loader.c', 'src/policy_lower.c'), bpf_skel]
  bpf_deps    = [dep_bpf]
  bpf_args    = ['-DSYNGUARD_HAVE_BPF_LSM=1']

  # The failsafes, exercised against a REAL attached hook rather than a mock:
  # each gate is driven until it opens, and the suite also proves the gate can
  # close, so a permanently-inert layer cannot pass by failing open everywhere.
  # Needs root; reports meson SKIP otherwise instead of a false pass.
  bpf_test_cmdline = meson.current_build_dir() / 'failsafe-cmdline.txt'
  bpf_failsafe_test = executable('bpf_failsafe_test',
    files('tests/bpf_failsafe_test.c', 'src/bpf_loader.c', 'src/sg_log.c'),
    bpf_skel,
    include_directories: inc,
    dependencies: [dep_threads, dep_bpf],
    c_args: [
      '-DSG_BPF_CMDLINE_PATH="@0@"'.format(bpf_test_cmdline),
      '-DSG_BPF_CMDLINE_PATH_TEST="@0@"'.format(bpf_test_cmdline),
    ])
  test('bpf_failsafe', bpf_failsafe_test, timeout: 120)

  # The kernel matcher, driven with real open() calls. lower_test proves the
  # lowering agrees with fnmatch in userspace; this proves the kernel agrees
  # too. The load-bearing assertion is negative: "/dir/*" must NOT reach a file
  # one level deeper, because a deny rule matching more than it should is the
  # dangerous direction.
  bpf_policy_test = executable('bpf_policy_test',
    files('tests/bpf_policy_test.c', 'src/bpf_loader.c', 'src/policy_lower.c',
          'src/sg_log.c'),
    bpf_skel,
    include_directories: inc,
    dependencies: [dep_threads, dep_bpf])
  test('bpf_policy', bpf_policy_test, timeout: 120)
endif

executable('synguard', sources, bpf_sources,
  include_directories: inc,
  dependencies: [dep_threads, dep_systemd] + bpf_deps,
  c_args: bpf_args,
  install: true,
  install_dir: get_option('bindir'),
)

# Verdict-feed slot management (the leak that once wedged the feed at capacity).
# secfeed.c depends only on the header-only logger + types, so the test links
# just it — no daemon, no kmod. The socket path is overridden to a temp file so
# the test needs neither root nor /run.
secfeed_test = executable('secfeed_test',
  files('tests/secfeed_test.c', 'src/secfeed.c', 'src/sg_log.c'),
  include_directories: inc,
  dependencies: [dep_threads, dep_systemd],
  c_args: ['-DSYNGUARD_SECFEED_SOCKET="@0@/synguard-secfeed-test.sock"'.format(
    meson.current_build_dir())])
test('secfeed', secfeed_test, timeout: 60)

# The stale-pid guard on the DENY/QUARANTINE path. isolation.c needs only libc
# and the header-only logger, so the test links just it — no daemon, no kmod,
# no root, and nothing in it kills or stops a process: the guard is a pure
# predicate over /proc, driven against the test's own pid and a child it reaps
# itself.
isolation_test = executable('isolation_test',
  files('tests/isolation_test.c', 'src/isolation.c', 'src/sg_log.c'),
  include_directories: inc)
test('isolation', isolation_test, timeout: 60)

# Verdict census + enforcement reachability: whether "mode=ENFORCE" actually
# means anything for the loaded policy. Links rule_engine.c only (the real
# parser is in the loop, via a temp rules dir), and additionally loads the
# shipped rules/ tree to assert the stock policy stays detect-only — so
# arming every install is a change that has to be made on purpose.
policy_test = executable('policy_test',
  files('tests/policy_test.c', 'src/rule_engine.c', 'src/sg_log.c'),
  include_directories: inc,
  c_args: ['-DSYNGUARD_SHIPPED_RULES_DIR="@0@/rules"'.format(
    meson.current_source_dir())])
test('policy', policy_test, timeout: 60)

# Lowering rules to a form a BPF hook can run. The load-bearing assertion is
# differential: every lowerable pattern must agree with the real fnmatch() call
# rules_evaluate() makes, over a corpus of paths. Two engines that disagree
# means the policy an admin wrote is not the policy the machine runs, and
# nothing at runtime would report it. Also guards that the SHIPPED policy keeps
# lowering to zero enforceable rules.
lower_test = executable('lower_test',
  files('tests/lower_test.c', 'src/policy_lower.c', 'src/rule_engine.c',
        'src/sg_log.c'),
  include_directories: inc,
  dependencies: [dep_threads],
  c_args: ['-DSYNGUARD_SHIPPED_RULES_DIR="@0@/rules"'.format(
    meson.current_source_dir())])
test('lower', lower_test, timeout: 60)

# The `access read|write|any` filter. Same shape as policy_test — real parser,
# real shipped rules/ — and it asserts both halves: the persistence surfaces
# stay quiet on a read, and the credential/device paths were NOT narrowed
# along with them (reading /etc/shadow or /dev/input IS the attack).
access_test = executable('access_test',
  files('tests/access_test.c', 'src/rule_engine.c', 'src/sg_log.c'),
  include_directories: inc,
  c_args: ['-DSYNGUARD_SHIPPED_RULES_DIR="@0@/rules"'.format(
    meson.current_source_dir())])
test('access', access_test, timeout: 60)

# Repeat-alert aggregation. The point of the test is the things aggregation
# must NOT do: delay a first occurrence, let one noisy pair mask another,
# collapse a CRITICAL, or go quiet when its table is full.
aggregate_test = executable('aggregate_test',
  files('tests/aggregate_test.c', 'src/action_engine.c', 'src/sg_log.c'),
  include_directories: inc,
  dependencies: [dep_threads])
test('aggregate', aggregate_test, timeout: 60)

# The syscall return value on the wire, and the two cases where acting on a
# DENY prevents nothing. Same link set as netwatch_test: kmod_parse_event() and
# sg_deny_suppression_reason() both live in event_processor.c.
syscall_ret_test = executable('syscall_ret_test',
  files('tests/syscall_ret_test.c', 'src/event_processor.c', 'src/core.c',
        'src/sg_log.c'),
  include_directories: inc,
  dependencies: [dep_threads, dep_systemd])
test('syscall_ret', syscall_ret_test, timeout: 60)

# The connect() wire format and the worm/C2 thresholds. netwatch_connect() and
# kmod_parse_event() live in event_processor.c, which pulls in the pipeline, so
# the test stubs the daemon entry points it never calls.
netwatch_test = executable('netwatch_test',
  files('tests/netwatch_test.c', 'src/event_processor.c', 'src/core.c', 'src/sg_log.c'),
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
