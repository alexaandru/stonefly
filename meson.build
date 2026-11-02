project(
  'stonefly',
  'c',
  version: '0.0.1',
  meson_version: '>=1.4.0',
  default_options: [
    'c_std=c23,c11',
    'warning_level=2',
    'werror=true',
  ],
)

# SceneFX before wlroots so wlr_scene_* comes from the FX scene.
scenefx = dependency('scenefx-0.5', version: '>=0.5.0')
wlroots = dependency('wlroots-0.20', version: '>=0.20.0')
wayland_server = dependency('wayland-server')
xkbcommon = dependency('xkbcommon')

add_project_arguments(
  '-DWLR_USE_UNSTABLE',
  '-D_POSIX_C_SOURCE=200809L',
  language: 'c',
)

subdir('protocol')
subdir('src')

stonefly_datadir = get_option('prefix') / get_option('datadir') / 'stonefly'
install_subdir(
  'runtime',
  install_dir: stonefly_datadir,
  strip_directory: true,
)
