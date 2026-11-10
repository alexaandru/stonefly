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
