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
  'src/io.c',
  'src/timeline.c',
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
     timeout: 300)
