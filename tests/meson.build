sf_test_inc = include_directories('.')
sf_src_inc = include_directories('../src')
libm = meson.get_compiler('c').find_library('m', required: false)

sf_test_lib = static_library(
  'sf_test',
  'sf_test.c',
  include_directories: sf_test_inc,
  dependencies: [libm],
)

log_stub = static_library(
  'log_stub',
  'stubs/log_stub.c',
  include_directories: sf_src_inc,
)

util_math_test = executable(
  'util_math_test',
  'util/math_test.c',
  include_directories: [sf_test_inc, sf_src_inc],
  link_with: sf_test_lib,
  dependencies: [libm],
)
test('util_math', util_math_test)

util_color_test = executable(
  'util_color_test',
  'util/color_test.c',
  include_directories: [sf_test_inc, sf_src_inc],
  link_with: sf_test_lib,
  dependencies: [libm],
)
test('util_color', util_color_test)

util_time_test = executable(
  'util_time_test',
  'util/time_test.c',
  include_directories: [sf_test_inc, sf_src_inc],
  link_with: sf_test_lib,
)
test('util_time', util_time_test)

util_str_test = executable(
  'util_str_test',
  ['util/str_test.c', '../src/util/str.c'],
  include_directories: [sf_test_inc, sf_src_inc],
  link_with: [sf_test_lib, log_stub],
)
test('util_str', util_str_test)

util_json_test = executable(
  'util_json_test',
  ['util/json_test.c', '../src/util/json.c'],
  include_directories: [sf_test_inc, sf_src_inc],
  link_with: sf_test_lib,
)
test('util_json', util_json_test)

ssd_layout_test = executable(
  'ssd_layout_test',
  ['ssd/layout_test.c', '../src/ssd/ssd_layout.c'],
  include_directories: [sf_test_inc, sf_src_inc],
  link_with: sf_test_lib,
)
test('ssd_layout', ssd_layout_test)

ssd_policy_test = executable(
  'ssd_policy_test',
  ['ssd/policy_test.c', '../src/ssd/ssd_policy.c'],
  include_directories: [sf_test_inc, sf_src_inc],
  link_with: sf_test_lib,
  dependencies: [dependency('wlroots-0.20', required: true)],
)
test('ssd_policy', ssd_policy_test)

config_action_test = executable(
  'config_action_test',
  ['config/action_test.c', '../src/config/action.c', '../src/util/str.c'],
  include_directories: [sf_test_inc, sf_src_inc],
  link_with: [sf_test_lib, log_stub],
  dependencies: [
    dependency('wlroots-0.20', required: true),
    dependency('xkbcommon', required: true),
    dependency('wayland-server', required: true),
  ],
)
test('config_action', config_action_test)

ipc_windows_json_test = executable(
  'ipc_windows_json_test',
  ['ipc/windows_json_test.c', '../src/ipc/windows_json.c', '../src/util/json.c'],
  include_directories: [sf_test_inc, sf_src_inc],
  link_with: sf_test_lib,
)
test('ipc_windows_json', ipc_windows_json_test)

ipc_outputs_json_test = executable(
  'ipc_outputs_json_test',
  ['ipc/outputs_json_test.c', '../src/ipc/outputs_json.c', '../src/util/json.c'],
  include_directories: [sf_test_inc, sf_src_inc],
  link_with: sf_test_lib,
  dependencies: [libm],
)
test('ipc_outputs_json', ipc_outputs_json_test)

ipc_workspaces_json_test = executable(
  'ipc_workspaces_json_test',
  ['ipc/workspaces_json_test.c', '../src/ipc/workspaces_json.c', '../src/util/json.c'],
  include_directories: [sf_test_inc, sf_src_inc],
  link_with: sf_test_lib,
)
test('ipc_workspaces_json', ipc_workspaces_json_test)

ipc_layers_json_test = executable(
  'ipc_layers_json_test',
  ['ipc/layers_json_test.c', '../src/ipc/layers_json.c', '../src/util/json.c'],
  include_directories: [sf_test_inc, sf_src_inc],
  link_with: sf_test_lib,
)
test('ipc_layers_json', ipc_layers_json_test)

util_shell_quote_test = executable(
  'util_shell_quote_test',
  ['util/shell_quote_test.c', '../src/util/shell_quote.c'],
  include_directories: [sf_test_inc, sf_src_inc],
  link_with: sf_test_lib,
)
test('util_shell_quote', util_shell_quote_test)

wm_geom_test = executable(
  'wm_geom_test',
  ['wm/geom_test.c', '../src/wm/geom.c'],
  include_directories: [sf_test_inc, sf_src_inc],
  link_with: sf_test_lib,
)
test('wm_geom', wm_geom_test)

wm_layout_test = executable(
  'wm_layout_test',
  ['wm/layout_test.c', '../src/wm/layout.c'],
  include_directories: [sf_test_inc, sf_src_inc],
  link_with: sf_test_lib,
  dependencies: [dependency('wayland-server', required: true)],
)
test('wm_layout', wm_layout_test)

wm_snap_test = executable(
  'wm_snap_test',
  ['wm/snap_test.c'],
  include_directories: [sf_test_inc, sf_src_inc],
  link_with: sf_test_lib,
)
test('wm_snap', wm_snap_test)

ipc_parse_test = executable(
  'ipc_parse_test',
  ['ipc/parse_test.c', '../src/ipc/parse.c', '../src/util/json.c'],
  include_directories: [sf_test_inc, sf_src_inc],
  link_with: sf_test_lib,
)
test('ipc_parse', ipc_parse_test)

util_path_test = executable(
  'util_path_test',
  ['util/path_test.c', '../src/util/path.c'],
  include_directories: [sf_test_inc, sf_src_inc],
  link_with: sf_test_lib,
)
test('util_path', util_path_test)

config_chord_test = executable(
  'config_chord_test',
  ['config/chord_test.c', '../src/config/chord.c'],
  include_directories: [sf_test_inc, sf_src_inc],
  link_with: sf_test_lib,
  dependencies: [
    dependency('xkbcommon', required: true),
    dependency('wlroots-0.20', required: true),
  ],
)
test('config_chord', config_chord_test)
