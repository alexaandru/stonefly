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
- Use tagged structs: `struct sf_foo`, not `typedef ... foo_t`.
- Forward-declare structs in headers; keep includes minimal.
- Header guards: `#ifndef SF_*_H` / `#define` / `#endif` (no
  `#pragma once`).

### Naming

- Public compositor API: `sf_*` functions, `struct sf_*`, `enum sf_*`,
  `SF_*` constants (same prefix as `SF_LOG*` / `sf_str` in `src/log/`).
- Lua/Fennel global: `sf` (`sf.opt`, `sf.bind`, `sf.tap`, `sf.scratchpad`,
  `sf.window_rule`, `sf.stdpath`, `sf.packadd`, …).
- Product / process / env stay `stonefly` / `STONEFLY_*`: binary name,
  datadir paths, socket `stonefly-*.sock`, env `STONEFLY_CONFIG` /
  `STONEFLY_SOCKET` / `STONEFLY_NOCONFIG`, scratchpad app_ids like
  `stonefly.scratch.term`, docs/product prose.
- Snake_case everywhere; no CamelCase in new C.

### Modules and file layout

C has no packages — directories only group related TUs. Keep that flat and
predictable:

- **One directory per module:** `src/<module>/`. Entry point is usually
  `<module>.c` + `<module>.h` (public API).
- **Split large TUs by feature name in the same folder**, matching neighbors:
  `seat/keyboard.c`, `seat/pointer.c`, `wm/drag_snap.c`, `ipc/windows.c`.
  Prefer that over deeper nests (`seat/pointer/foo.c`) or a single growing
  god-file.
- **Module-prefixed names** when the files are inseparable parts of one
  surface (`ssd/ssd_layout.c`, `ssd/ssd_paint.c`). Feature names when they
  are distinct subsystems that share a folder.
- **Module-private cross-file decls** go in `<module>/internal.h` (or a
  similarly named private header). Do not grow the public `<module>.h` for
  helpers only other files in that module need.
- **Type homes:** put durable types where their domain lives, not inside a
  protocol adapter. Window state is `toplevel/toplevel.h`; `xdg/` sets up
  xdg-shell and fills that type. Consumers that only need the window type
  include `toplevel/toplevel.h`.
- **Shared helpers:** cross-module math/string/time/JSON live under
  `src/util/` (`math.h`, `str.c`/`str.h`, `time.h`, `json.c`/`json.h`). Do
  not copy static `clamp01` / `xstrdup` / timespec helpers into new files —
  extend `util/` instead.
- **`struct sf_server`:** nest related protocol/state blobs
  (`lock`, `idle`, `foreign`, `constraints`, `tap`, `swipe`, …). Keep DRM
  `session` and scene `layers.lock` distinct from session-lock state.
- Add new `.c` files to `src/meson.build` `sf_src` in the same change.

### Control flow and APIs

- Early returns for errors; `bool` success or `NULL` failure are common.
- `goto` is rare (cleanup paths only). Prefer matching existing neighbors.
- Listeners and scene nodes follow wlroots patterns already in
  `server/`, `xdg/`, `output/`, etc.
- Prefer **tables** for name↔enum / option setters / parallel switch arms
  (see `config/action.c`, `config_lua.c` `opt_*` tables) over duplicated
  `strcmp` ladders or mirrored `switch`es in several TUs.
- Prefer **generic actions + data** (`EXEC` + `cmd`, `SCRATCH_TOGGLE` + pad
  name) over new specialized enum values for each app or pad.
- Drop deprecated shims and legacy config forms once defaults and docs use
  the current API — do not keep parallel parse paths “just in case.”

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

## FEATURES.md / ROADMAP.md / LESSONS.md

Shipped work lives in [`FEATURES.md`](FEATURES.md); open and partial work in
[`ROADMAP.md`](ROADMAP.md). Peer-compositor UX notes and hard-won traps live in
[`LESSONS.md`](LESSONS.md) (start with Umbriel columns / foreign / float). After
a change, fix, feature, or notable diagnosis, update the matching rows **in the
same turn**. When something ships, move it from the roadmap into features (and
drop or shrink the open row). Capture durable peer lessons in `LESSONS.md`.

## Done checklist

Before calling work finished:

1. Code matches wlroots/tinywl style (`.clang-format` / neighboring `./src`),
   including module layout and `util/` reuse above.
2. No host library/pkg-config probing; verify via abuild (`make apk`).
3. No new internal shell-outs.
4. `FEATURES.md` / `ROADMAP.md` rows for the touched feature are current
   (shipped items moved to features).
5. `pkgrel` / `pkgver` untouched unless requested.
6. Do not run `make fmt` unless the user asks — formatting is their call.
7. New sources are listed in `src/meson.build` when applicable.
