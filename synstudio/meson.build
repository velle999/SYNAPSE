project('synstudio', 'c',
  version: '0.1.0',
  license: 'GPL-2.0-or-later',
  default_options: ['c_std=gnu11', 'warning_level=2', 'werror=false'])

conf = configuration_data()
conf.set_quoted('SYNSTUDIO_VERSION', meson.project_version())
conf.set_quoted('SYNSTUDIO_DATADIR',
                get_option('prefix') / get_option('datadir') / 'synstudio')
configure_file(output: 'config.h', configuration: conf)

# libm and nothing else.
#
# Decoding, encoding and camera raw are all reached as SUBPROCESSES (ffmpeg,
# ffprobe, dcraw_emu) rather than as libraries. That is a deliberate trade: a
# linked libavcodec makes this binary fail to start the day ffmpeg bumps a
# SONAME, which has already happened once to a shipped SynapseOS component,
# and there is nothing an application can do about it. A pipe has no ABI.
cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required: false)

sources = files(
  'src/main.c',
  'src/image.c',
  'src/curve.c',
  'src/colour.c',
  'src/spatial.c',
  'src/geometry.c',
  'src/mask.c',
  'src/develop.c',
  'src/doc.c',
  'src/lut.c',
  'src/look.c',
  'src/io.c',
  'src/timeline.c',
  'src/fx.c',
  'src/font.c',
  'src/scope.c',
  'src/subs.c',
)

executable('synstudio', sources,
  include_directories: include_directories('include', '.'),
  dependencies: [m_dep],
  install: true)

# Both, and in the same directory: synstudio.qml finds the playback component
# with Qt.resolvedUrl, which resolves against its OWN location. Installing one
# without the other leaves a Loader pointing at nothing, which costs the play
# button and says so rather than failing the window.
install_data('data/synstudio.qml', install_dir: get_option('datadir') / 'synstudio')
install_data('data/synstudio-playback.qml',
             install_dir: get_option('datadir') / 'synstudio')
# The effects. A recipe is DATA — a text manifest naming a filter chain — so
# these install like the QML does, and a third party's own go in
# ~/.config/synstudio/effects, which is read after these and wins on a name.
#
# install_subdir rather than a list: an effect is added by writing a file, and
# a build that needed editing as well would be a build that quietly shipped
# twenty-six of the twenty-seven.
install_subdir('data/effects',
               install_dir: get_option('datadir') / 'synstudio')

# The looks, for the same reason and read the same way: a .synlook is the
# develop stack as text, and one of the user's own in
# ~/.config/synstudio/looks wins on a name.
#
# No .cube ships beside them, deliberately. A LUT is somebody's licensed work
# far more often than a slider position is, and a distribution that shipped a
# folder of them would be redistributing colour it does not own. The reader,
# the catalogue and ~/.config/synstudio/luts are all here; the tables are the
# user's to bring.
install_subdir('data/looks',
               install_dir: get_option('datadir') / 'synstudio')

install_data('data/synstudio.desktop',
             install_dir: get_option('datadir') / 'applications')

# The application icon. Scalable only: Qt and GTK both render the SVG at
# whatever size they need, and a set of rasterised copies is one more thing to
# forget to regenerate.
install_data('data/synstudio.svg',
             install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps')

test('engine', find_program('tests/run.sh'),
     args: [meson.current_build_dir() / 'synstudio'],
     workdir: meson.current_source_dir(),
     # ⚠ The suite renders real video with real ffmpeg — a slow-motion export,
     # a stabiliser analysis, a dozen frames measured against each other — and
     # a build machine is a loaded machine. 300 seconds was enough when this
     # was 344 assertions and is not at 445; a test killed by meson fails the
     # BUILD, and on the machine least able to afford it.
     timeout: 900)
