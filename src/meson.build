stonefly_src = files(
  'main.c',
  'util/json.c',
  'log/log.c',
  'ipc/ipc.c',
  'ipc/windows.c',
  'server/server.c',
  'output/output.c',
  'seat/seat.c',
  'xdg/xdg.c',
  'wm/wm.c',
)

stonefly_inc = include_directories('.')

executable(
  'stonefly',
  stonefly_src,
  include_directories: stonefly_inc,
  dependencies: [wlroots, wayland_server, xkbcommon],
  install: true,
)
