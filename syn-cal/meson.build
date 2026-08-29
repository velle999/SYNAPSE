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
cc = meson.get_compiler('c')
conf = configuration_data()
conf.set_quoted('SYNCAL_VERSION', meson.project_version())
configure_file(output: 'config.h', configuration: conf)
add_project_arguments('-DSYNCAL_VERSION="' + meson.project_version() + '"', language: 'c')

libcurl   = dependency('libcurl')
libxml    = dependency('libxml-2.0')
libsecret = dependency('libsecret-1')
glib      = dependency('glib-2.0')
jsonc     = dependency('json-c')
libical   = dependency('libical')

engine = files(
  'src/util.c',
  'src/ics.c',
  'src/store.c',
  'src/sync.c',
)

inc = include_directories('include')

# The network half. Kept out of `engine` so the algorithm's tests still build
# and run on a machine with neither library — see the note above.
net = files('src/http.c', 'src/caldav.c', 'src/account.c', 'src/oauth.c', 'src/event.c')

syncal_exe = executable('syn-cal', ['src/main.c'] + engine + net,
                        include_directories: inc,
                        dependencies: [libcurl, libxml, libsecret, glib, jsonc, libical],
                        install: true)

foreach t : ['ics', 'store', 'sync']
  exe = executable(t + '_test', ['tests/' + t + '_test.c'] + engine,
                   include_directories: inc, build_by_default: false)
  test(t, exe, timeout: 60)
endforeach

caldav_test = executable('caldav_test', ['tests/caldav_test.c'] + engine + net,
                         include_directories: inc,
                         dependencies: [libcurl, libxml, libsecret, glib, jsonc, libical],
                         build_by_default: false)
oauth_test = executable('oauth_test', ['tests/oauth_test.c'] + engine + net,
                        include_directories: inc,
                        dependencies: [libcurl, libxml, libsecret, glib, jsonc, libical],
                        build_by_default: false)
event_test = executable('event_test', ['tests/event_test.c'] + engine + net,
                        include_directories: inc,
                        dependencies: [libcurl, libxml, libsecret, glib, jsonc, libical],
                        build_by_default: false)
test('event', event_test, timeout: 60)

test('oauth', find_program('tests/oauth_test.sh'),
     args: [oauth_test.full_path()],
     depends: oauth_test,
     timeout: 90)

test('cli', find_program('tests/cli_test.sh'),
     args: [syncal_exe.full_path()],
     depends: syncal_exe,
     timeout: 120)

test('caldav', find_program('tests/caldav_test.sh'),
     args: [caldav_test.full_path()],
     depends: caldav_test,
     timeout: 120)
