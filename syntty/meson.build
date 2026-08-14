project('syntty', 'c',
  version : '0.1.0',
  license : 'GPL-2.0-or-later',
  default_options : [
    'c_std=c11',
    'warning_level=3',
    'werror=false',
    'b_ndebug=if-release',
  ])

cc = meson.get_compiler('c')

# util is the only library dependency, and it is for forkpty. Nothing here
# links GL, and that is a design decision rather than an omission: of kitty's
# 264 MB resident, 188 MB is the graphics stack it maps by creating a context.
# See the file header in include/syntty.h.
util_dep = cc.find_library('util')

# Glyphs. freetype rasterises and fontconfig answers "which file is
# Monospace" — the second one ONCE, on a cold cache, because the answer is
# written to $XDG_CACHE_HOME/syntty/fonts.v1 and a later start hands the path
# straight to freetype. That lookup is one of the three things kitty pays 230 ms
# for; see the file header in src/font.c.
freetype_dep   = dependency('freetype2')
fontconfig_dep = dependency('fontconfig')

syntty_sources = files(
  'src/font.c',
  'src/grid.c',
  'src/main.c',
  'src/pty.c',
  'src/render.c',
  'src/util.c',
  'src/vt.c',
)

syntty = executable('syntty',
  syntty_sources,
  include_directories : include_directories('include'),
  dependencies : [util_dep, freetype_dep, fontconfig_dep],
  install : true)

# ⚠ The suite runs a pty and a benchmark, so it is slower than a pure unit
# test — but meson kills a test at 30 seconds by DEFAULT and reports the kill
# as a BUILD failure, which looks like a broken program rather than a slow one.
# The timeout is stated here so a passing suite cannot fail the build.
test('syntty', find_program('tests/syntty_test.sh'),
  args : [syntty.full_path()],
  timeout : 120)
