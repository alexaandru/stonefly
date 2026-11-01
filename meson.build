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

wlroots = dependency('wlroots-0.20', version: '>=0.20.0')
wayland_server = dependency('wayland-server')
xkbcommon = dependency('xkbcommon')

executable(
  'stonefly',
  'stonefly.c',
  dependencies: [wlroots, wayland_server, xkbcommon],
  c_args: [
    '-DWLR_USE_UNSTABLE',
    '-D_POSIX_C_SOURCE=200809L',
  ],
  install: true,
)
