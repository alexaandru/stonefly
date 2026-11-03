# Agent Notes

Rules for work in this repository. Read this before editing, building, or
diagnosing stonefly.

## Scope

- **Product code lives in `./src`** (and root Meson/`protocol/`/`runtime/`/
  `dist/`). That is what you change.
- **`peeble/` is reference only** — the old Go/River companion. Use it to
  compare behavior or port ideas. Do not treat it as the codebase under
  edit, and do not apply peeble Go style or packaging habits to stonefly.
- Shell UI (bar, launcher, OSD, wallpaper chrome) is day-zero **Noctalia**,
  not an in-process peeble-style shell. Prefer compositor/protocol work in
  `./src` over reinventing shell features.

## Style

House style is **wlroots / tinywl** (this tree was seeded from wlroots 0.20
tinywl). Match existing `./src` files; do not import GNOME, CERT-as-format,
or Go conventions.

### Formatting

- **Indent:** tabs (not spaces). `case` labels align with `switch`; body is
  one more tab.
- **Braces:** K&R — opening `{` on the same line as `if` / `for` / `while` /
  `switch` / function signature.
- **Spaces:** `if (cond)`, `for (...)`, `while (...)`; no space before `(`.
- **Pointers:** `type *name` (space before `*`, none after), including
  parameters and locals.
- **Line length:** keep at or under ~100 columns (current tree max is ~94).
- **Trailing whitespace:** none.
- **Blank lines:** no empty line right after `{` or right before `}`. Prefer
  a blank line before `return` when the block has more than a couple of
  lines (skip tiny early-return helpers). `.clang-format` `KeepEmptyLines`
  strips start-of-block blanks; it does **not** insert blank-before-return.
- **Comments:** `/* ... */` only. No `//`. Avoid `/*` inside block comments
  (clang `-Wcomment`); reword paths like `default/*` to “default config tree”.

### Language and types

- Meson: `c_std=c23,c11`, `warning_level=2`, `werror=true`.
- Prefer `stdbool.h` (`bool` / `true` / `false`) and `NULL`.
- Use tagged structs: `struct stonefly_foo`, not `typedef ... foo_t`.
- Forward-declare structs in headers; keep includes minimal.
- Header guards: `#ifndef STONEFLY_*_H` / `#define` / `#endif` (no
  `#pragma once`).

### Naming

- Public compositor API: `stonefly_*` functions, `struct stonefly_*`,
  `enum stonefly_*`, `STONEFLY_*` constants.
- Log helpers only: `sf_*` / `SF_*` in `src/log/` (structured JSON slog).
- File layout mirrors modules: `src/<module>/<module>.c` + `.h`.
- Snake_case everywhere; no CamelCase in new C.

### Control flow and APIs

- Early returns for errors; `bool` success or `NULL` failure are common.
- `goto` is rare (cleanup paths only). Prefer matching existing neighbors.
- Listeners and scene nodes follow wlroots patterns already in
  `server/`, `xdg/`, `output/`, etc.

### Lint / analyzer

Style and static analysis are enforced **inside abuild** (`make apk` →
`check()`), not on the host. Configs: `.clang-format`, `.clang-tidy`,
`.cppcheck-suppress`.

| Target | Tool | Role |
|---|---|---|
| `make fmt` | clang-format -i | Rewrite all `src/**/*.{c,h}` |
| `make fmt-check` | clang-format --dry-run -Werror | Fail if formatting drifts |
| `make cppcheck` | cppcheck --project=… | Bug/style/portability on compile DB |
| `make tidy` | clang-tidy -p … | Analyzer + bugprone (warnings as errors) |
| `make style-check` | all of the above checks | What APKBUILD `check()` runs |

`BUILDDIR` defaults to `output` (abuild-meson). Meson writes
`compile_commands.json` there; cppcheck and clang-tidy consume it.

Compiler bar remains Meson `werror=true` — new warnings are build failures.

#### Known false positive: `_POSIX_C_SOURCE`

Symptom: analyzers flag `#define _POSIX_C_SOURCE 200809L` as a reserved
identifier.

Why false: it is a standard POSIX feature-test macro that user code must
define. Meson also passes `-D_POSIX_C_SOURCE=200809L`.

Workarounds:
- clang-tidy: `bugprone-reserved-identifier.AllowedIdentifiers` allows only
  `^_POSIX_C_SOURCE$` — other reserved names still fail `make tidy`.
- clang-tidy also disables `misc-header-include-cycle` (elogind
  `sd-bus-vtable.h` ↔ `sd-bus.h` cycle under `/usr/include`).
- pi-lens / clangd: root `.pi-lens.json` suppresses the LSP noise. Harmless
  in the TUI if it still appears; it does not affect the abuild build.

## Build environment

**Do not look up libraries, headers, or pkg-config on the host.**

1. Agents almost always run in a sandbox without the compositor deps.
2. The real build is **abuild** (`make apk`): clang, Meson, wlroots, SceneFX,
   and friends exist inside that environment, not on the host.

Verify compile via `make apk` / abuild, not ad-hoc host `cc` against missing
system roots. If a symbol or `.pc` file is unclear, read Meson/`APKBUILD`
makedepends and vendored headers under `protocol/` — do not probe `/usr` on
the host.

## Prefer in-process C

Do not shell out (`system`, `popen`, `sh -c` helpers, `mkdir -p`,
`notify-send`, `dbus-update-activation-environment`, etc.) for work that
belongs in the compositor.

- Session bus / notifications / activation environment: `src/dbus/dbus.c`
  (sd-bus / elogind).
- Directories: recursive mkdir in C (already shared helpers).
- Allowed `execl("/bin/sh", ...)` uses are **user-configured spawn** only
  (`-s` startup, scratchpad `cmd`, action spawn) — not internal plumbing.

When adding a dependency, prefer libraries already in `makedepends` (or a
thin C API over an optional static C++ helper). Keep the compositor C-first.

## Packaging

- Alpine aport: `dist/alpine/APKBUILD` (invoked via `make apk`).
- **clang and `*-dev` packages are `makedepends`**, never runtime `depends`.
  Users must not need clang to install/run the APK.
- **Do not bump `pkgrel` / `pkgver`** unless the user explicitly asks.
  Build/source fixes are fine; revision numbering is the user’s call.

## MATRIX.md

Always keep [`MATRIX.md`](MATRIX.md) up to date after a change, fix, feature,
or notable diagnosis. Update the matching rows (status, notes, peeble /
stonefly columns) **in the same turn** — do not leave MATRIX stale for a
later cleanup pass.

## Done checklist

Before calling work finished:

1. Code matches wlroots/tinywl style (`.clang-format` / neighboring `./src`).
2. No host library/pkg-config probing; verify via abuild (`make apk`).
3. No new internal shell-outs.
4. `MATRIX.md` rows for the touched feature are current.
5. `pkgrel` / `pkgver` untouched unless requested.
6. Do not run `make fmt` unless the user asks — formatting is their call.
