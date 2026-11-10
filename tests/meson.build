sf_test_inc = include_directories('.')
sf_src_inc = include_directories('../src')
libm = meson.get_compiler('c').find_library('m', required: false)

sf_test_lib = static_library(
  'sf_test',
  'sf_test.c',
  include_directories: sf_test_inc,
  dependencies: [libm],
)

# Pure-header util math — no compositor objects, no wlroots.
util_math_test = executable(
  'util_math_test',
  'util/math_test.c',
  include_directories: [sf_test_inc, sf_src_inc],
  link_with: sf_test_lib,
  dependencies: [libm],
)
test('util_math', util_math_test)
