# Unit suites are discovered by tests/drive.py (/* sf-suite … */ in *_test.c).
# Host `make test` and abuild `meson test` both run that driver.
unit_runner = find_program('drive.py')
test(
  'unit',
  unit_runner,
  workdir: meson.project_source_root(),
  timeout: 300,
  is_parallel: false,
  # Quiet pass/fail in abuild; host `make test` defaults OPTS to cover.
  env: ['OPTS='],
)
