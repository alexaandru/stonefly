stonefly_src = files(
  'main.c',
  'util/json.c',
  'log/log.c',
  'ipc/ipc.c',
  'ipc/windows.c',
  'ipc/layers.c',
  'server/server.c',
  'output/output.c',
  'layer/layer.c',
  'seat/seat.c',
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

executable(
  'stonefly',
  stonefly_src,
  include_directories: stonefly_inc,
  dependencies: [
    scenefx,
    wlroots,
    wayland_server,
    xkbcommon,
    meson.get_compiler('c').find_library('m'),
    stonefly_protocols_dep,
  ],
  install: true,
)
