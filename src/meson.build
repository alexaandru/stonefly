sf_src = files(
  'main.c',
  'util/json.c',
  'util/str.c',
  'util/shell_quote.c',
  'util/path.c',
  'log/log.c',
  'dbus/dbus.c',
  'ipc/ipc.c',
  'ipc/parse.c',
  'ipc/windows.c',
  'ipc/windows_json.c',
  'ipc/layers.c',
  'ipc/layers_json.c',
  'ipc/outputs.c',
  'ipc/outputs_json.c',
  'ipc/config.c',
  'config/config.c',
  'config/config_embed.c',
  'config/config_lua.c',
  'config/action.c',
  'config/chord.c',
  'server/server.c',
  'output/output.c',
  'layer/layer.c',
  'seat/seat.c',
  'seat/keyboard.c',
  'seat/pointer.c',
  'seat/gestures.c',
  'seat/constraints.c',
  'session/session.c',
  'xdg/xdg.c',
  'wm/wm.c',
  'wm/monitor.c',
  'wm/nudge.c',
  'wm/geom.c',
  'wm/layout.c',
  'wm/layout_sm.c',
  'wm/workspace.c',
  'wm/scratchpad.c',
  'wm/drag_snap.c',
  'wm/snap_preview.c',
  'wm/rules.c',
  'ipc/workspaces.c',
  'ipc/workspaces_json.c',
  'anim/anim.c',
  'decor/decor.c',
  'effect/background_effect.c',
  'ssd/ssd.c',
  'ssd/ssd_policy.c',
  'ssd/ssd_layout.c',
  'ssd/ssd_paint.c',
  'toplevel/foreign.c',
  'toplevel/ext_capture.c',
)

sf_inc = include_directories('.')

luajit = dependency('luajit')
fcft = dependency('fcft')
pixman = dependency('pixman-1')

# Alpine: elogind provides libsystemd.pc → sd-bus. Prefer libsystemd name.
libsystemd = dependency('libsystemd', required: false)
if not libsystemd.found()
  libsystemd = dependency('libelogind', required: true)
endif

sf_datadir = get_option('prefix') / get_option('datadir') / 'stonefly'

# C23 #embed of default/*.fnl is not always tracked in compiler depfiles.
# Regenerate a stamp header whenever those inputs change so config_embed.c
# (which #includes it) is forced to rebuild and re-#embed fresh bytes.
sf_embed_defaults = files(
  'config/default/init.lua',
  'config/default/options.fnl',
  'config/default/binds.fnl',
  'config/default/scratchpads.fnl',
  'config/default/rules.fnl',
  'config/default/execs.fnl',
  'config/default/inputs.fnl',
  'config/default/outputs.fnl',
  'config/default/flsproject.fnl',
)
sf_embed_stamp = custom_target(
  'sf-embed-stamp',
  input: sf_embed_defaults,
  output: 'embed_defaults_stamp.h',
  # Rewrite on any default/* change (mtime bump). Content hashes inputs so a
  # no-op edit still changes the header when bytes differ.
  command: [
    'sh',
    '-c',
    'printf \'#define SF_EMBED_STAMP "%s"\\n\' "$(cksum "$@" | cksum)" > "$0"',
    '@OUTPUT@',
    '@INPUT@',
  ],
  build_by_default: true,
)

executable(
  'stonefly',
  sf_src + [sf_embed_stamp],
  include_directories: sf_inc,
  dependencies: [
    scenefx,
    wlroots,
    wayland_server,
    xkbcommon,
    luajit,
    libsystemd,
    fcft,
    pixman,
    meson.get_compiler('c').find_library('m'),
    sf_protocols_dep,
  ],
  c_args: [
    '-DSF_DATADIR="@0@"'.format(sf_datadir),
  ],
  install: true,
)
