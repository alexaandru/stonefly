# Unit suites are discovered by tests/drive.py (/* sf-suite … */ in *_test.c).
# Host `make test` and APKBUILD check() invoke drive.py directly so the
# per-file listing is visible. This meson test() remains for `meson test`
# convenience; meson's default runner hides stdout of passing tests.
unit_runner = find_program('drive.py')
test(
  'unit',
  unit_runner,
  workdir: meson.project_source_root(),
  timeout: 300,
  is_parallel: false,
  # No coverage in meson/abuild convenience path (host `make test` defaults cover).
  env: ['OPTS='],
)
