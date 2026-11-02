stonefly_src = files(
  'main.c',
  'util/json.c',
  'log/log.c',
  'ipc/ipc.c',
  'ipc/windows.c',
  'ipc/layers.c',
  'ipc/outputs.c',
  'ipc/config.c',
  'config/config.c',
  'config/config_embed.c',
  'config/config_lua.c',
  'server/server.c',
  'output/output.c',
  'layer/layer.c',
  'seat/seat.c',
  'session/session.c',
  'xdg/xdg.c',
  'wm/wm.c',
  'wm/scratchpad.c',
  'wm/drag_snap.c',
  'wm/snap_preview.c',
  'anim/anim.c',
  'decor/decor.c',
  'toplevel/foreign.c',
  'toplevel/ext_capture.c',
)

stonefly_inc = include_directories('.')

luajit = dependency('luajit')

stonefly_datadir = get_option('prefix') / get_option('datadir') / 'stonefly'

executable(
  'stonefly',
  stonefly_src,
  include_directories: stonefly_inc,
  dependencies: [
    scenefx,
    wlroots,
    wayland_server,
    xkbcommon,
    luajit,
    meson.get_compiler('c').find_library('m'),
    stonefly_protocols_dep,
  ],
  c_args: [
    '-DSTONEFLY_DATADIR="@0@"'.format(stonefly_datadir),
  ],
  install: true,
)
