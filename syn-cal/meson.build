project('syn-cal', 'c',
  version: '0.1.0',
  license: 'GPL-2.0-or-later',
  default_options: ['c_std=c11', 'warning_level=2', 'werror=false'])

# ⚠ NO EXTERNAL DEPENDENCIES IN THE ENGINE, ON PURPOSE. The store is a folder of
# .ics files and the sync algorithm moves opaque blobs between it and a server —
# neither needs to understand iCalendar. libcurl arrives with caldav.c and
# libical with event.c, and both are additions to this list rather than
# conditions on it: the engine and its tests build and run without either, which
# is what keeps the part that can lose data testable on any machine.
engine = files(
  'src/util.c',
  'src/ics.c',
  'src/store.c',
  'src/sync.c',
)

inc = include_directories('include')

foreach t : ['ics', 'store', 'sync']
  exe = executable(t + '_test', ['tests/' + t + '_test.c'] + engine,
                   include_directories: inc, build_by_default: false)
  test(t, exe, timeout: 60)
endforeach
