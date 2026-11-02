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

# C23 #embed of default/*.fnl is not always tracked in compiler depfiles.
# Regenerate a stamp header whenever those inputs change so config_embed.c
# (which #includes it) is forced to rebuild and re-#embed fresh bytes.
stonefly_embed_defaults = files(
  'config/default/init.lua',
  'config/default/options.fnl',
  'config/default/binds.fnl',
  'config/default/scratchpads.fnl',
  'config/default/inputs.fnl',
  'config/default/outputs.fnl',
)
stonefly_embed_stamp = custom_target(
  'stonefly-embed-stamp',
  input: stonefly_embed_defaults,
  output: 'embed_defaults_stamp.h',
  # Rewrite on any default/* change (mtime bump). Content hashes inputs so a
  # no-op edit still changes the header when bytes differ.
  command: [
    'sh',
    '-c',
    'printf \'#define STONEFLY_EMBED_STAMP "%s"\\n\' "$(cksum "$@" | cksum)" > "$0"',
    '@OUTPUT@',
    '@INPUT@',
  ],
  build_by_default: true,
)

executable(
  'stonefly',
  stonefly_src + [stonefly_embed_stamp],
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
