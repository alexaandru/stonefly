# Unit suites are discovered by tests/drive.lua (/* sf-suite … */ in *_test.c).
# Maintain tests/drive.fnl; `make drive-lua` regenerates the committed Lua.
# Host `make test` and APKBUILD check() invoke drive.lua only (no Fennel).
# This meson test() remains for `meson test` convenience; meson's default
# runner hides stdout of passing tests.
unit_runner = find_program('drive.lua')
test(
  'unit',
  unit_runner,
  workdir: meson.project_source_root(),
  timeout: 300,
  is_parallel: false,
  # No coverage in meson/abuild convenience path (host `make test` defaults cover).
  env: ['OPTS='],
)
