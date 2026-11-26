# Agent Notes

Rules for work in this repository. Read this before editing, building, or
diagnosing stonefly.

## Scope

- **Product code lives in `./src`** (and root Meson/`protocol/`/`runtime/`/
  `dist/`). That is what you change. Do not edit `umbriel/` or `peeble/`.
- **Arrange set is stacking + columns only.** Umbriel remains the behavior
  reference for columns; do not chase Umbriel’s full layout/overview/HDR
  sandwich (master/dwindle/monocle hard-cut; parse aliases → columns).
- **`umbriel/` is the reference compositor** — Noctalia’s C++/wlroots sibling
  (cloned under `./umbriel`). Prefer its docs, action catalog, layouts,
  scratchpads, foreign UX, and effects model when shaping stonefly. Steal
  abstractions and behavior from Umbriel first; do not import Umbriel C++
  style into `./src` (house style stays wlroots/tinywl C below).
- **`peeble/` is secondary reference only** — the old Go/River companion.
  Use it **only for features Umbriel does not cover** (e.g. in-process bar,
  launcher plugins, services, other peeble-only shell/helpers). Never prefer
  peeble over Umbriel when both have an answer. Do not apply peeble Go style
  or packaging habits to stonefly.
- Shell UI (bar, launcher, OSD, wallpaper chrome) is day-zero **Noctalia**,
  not an in-process peeble-style shell. Prefer compositor/protocol work in
  `./src` over reinventing shell features.
- **In-compositor UI stack** lives under `src/ui/` (cheatsheet first;
  quit-confirm later). fcft-first text + scene buffers/rects under
  `layers.overlay` — no Pango/Cairo unless forced; SSD still owns
  `fcft_init`/`fcft_fini`.
- **Steal first, then implement.** Short Umbriel (then niri/mango only if
  Umbriel lacks it) read → ship. Reject long formal design-doc /
  write-review loops when a peer already solved it.
- **Predictability is a product value.** Arrange switch, float/unfloat,
  max/unmax, fullscreen, hide/show, workspace/monitor send must round-trip
  without silent demotion, forgotten cells, or focus onto covered windows.
  See ROADMAP Predictability audit and **WM lessons** below.
- **An idle window must settle.** We fed every xdg surface commit back into
  wlroots configure setters even when caps, bounds, suspended state and edges
  were unchanged. The client acknowledged and committed again. Two idle foot
  terminals ran at 54°C after the fix (previously 92°C); blur was not the fix.
  Send required initial/remap state, then compare desired values against the
  **last scheduled** state, not just the last ack. After touching commit/layout
  paths, trace an idle client with `WAYLAND_DEBUG=client`: after initial placement
  configure serials must stop until something actually changes. Don't suppress
  frame callbacks or disable effects to mask a configure loop.
- **C ↔ Lua border:** C owns Wayland/scene/lifecycle/geometry kernels/action
  primitives/invariants; Lua owns policy, UX composition, and packs. Rule of
  thumb: user/pack disagreement → Lua/opts; scene/Wayland corruption risk →
  C. Contract: [`ROADMAP.md`](ROADMAP.md) **C ↔ Lua border**.

## WM lessons (Umbriel / traps)

Hard-won rules. Prefer these over guessing “parity with X.” Shipped detail
stays in FEATURES; open gaps in ROADMAP.

### Columns & membership

- **Columns are first-class; windows are rows.** Strip of `sf_column` with
  `rows`, `width_frac` / `full_width`, gap weights. One window = one column is
  too weak for consume, tabs, column-max, focus-within-column. Multi-row only
  after user consume/drag (or restored slots) — never invent groups on join.
- **Arrange leave is two-pass.** Remember every `arrange_slot_*` before any
  `remove_row` (same as `workspace_park_members`); one-pass remember+unlink
  renumbers indices and invents multi-row columns on the next join. Columns →
  stacking uses preferred/natural float box + cascade — not packed cell geom.
- **Float is sticky membership.** User/rule floats stay out across tiling
  re-entry; layout-floated windows re-tile. Hide/show must not leave unmanaged
  ghosts under the strip. `leave_tiling` / float / edges break-out must
  `sf_arrange_slot_remember` before unlink.
- **Arrange ≠ `layout_sm`.** Workspace arrange (stacking/columns) packs;
  per-window `layout_sm` owns float/max/snap/FS chrome. Column-max stays tiled;
  edges-max breaks out.
- **Width is a slot:** `round(frac × (usable + gap)) - gap` so two 0.5 columns
  fit. `usable * frac` overflows by exactly one gap and wobbles the strip.
- **Scroll = reveal enough**, not always center. `sf.opt.center_focused`
  (`never`|`always`|`on_overflow`) gates centering; underfull strips stay
  left-aligned. New columns insert beside focus. Consume/expel ≠ move-left/right.

### Maximize, focus, FS

- **Two maximize verbs:** Super+m = column/gap max (still tiled); Super+f =
  edges break-out. Under columns, edge-max is **transient** (`arrange_max_edges_rejoin`)
  unless sticky float — do not demote while `minimized` / hide-animating.
- **Sticky float + edge-max** (Slack): never set `arrange_max_edges_rejoin` or
  clear `arrange_floating`; `apply_shown` must not join when rules want float.
- **Focus means visible.** `sf_focus_toplevel` raises; arrange flush must not
  re-raise every float. Tab/focus_dir skip `sf_toplevel_focus_obscured`.
- **Corner crescent (SceneFX):** `corner_radius` is content arc; outer =
  content+border so hole/outer centers coincide. Hole inset + matching
  `hole_r` shrink (`border_overlap_px`) covers CSD AA; do **not** add
  Umbriel’s makeBorderRing AA margin on a solid `wlr_scene_rect` (that pad
  is transparent only in `border.frag` — here it fattens the stroke and
  draws the crescent). No umbrielfx `border.frag` / `corner_box`.
- **looksTiled float heuristic** (Umbriel): parented, X11 DIALOG/SPLASH/
  TOOLBAR/UTILITY/modal, or fixed min==max on either axis → sticky float +
  center (parent else usable). Fullscreen requested/applied stays tiled-
  shaped (Wine). Join/rejoin/SoftPlace must consult `sf_toplevel_looks_tiled`.
- **Modal dialog attach** (`xdg_wm_dialog_v1`): reparent under parent
  `attachment_tree`, sticky float + center, `dialog_dim` shade (hit-test
  pass-through like `dim_overlay`), focus walks placement root then
  `input_delegate`. `sync_scene_layer` must skip `modal_attached_to`;
  `layout_geom` uses `wlr_scene_node_coords`. Keep `attachment_tree` above
  chrome/dim after every decor update.
- **Exclusive fullscreen:** one FS per output; FS box = `sf_output_for_toplevel`
  → exact `output->full`. Do **not** crop FS with `arrange_prop_*` present-size
  (strip cell would chop Netflix chrome). Clear `arrange_prop_*` on enter.
  Under columns, FS **parks off the strip** (`sf_arrange_slot_remember` +
  `remove_row`, `arrange_managed` stays); unFS rejoins via slot. Minimized
  rows stay members. Do not leave FS-only empty column shells.

### Appear / hide / present

- **Show slides to `resting_*`.** Hide parks the node off-screen; never target
  `scene_tree->node` after a completed hide.
- **Popin crop:** presented `source_box` + `dest_size` **after** `set_clip`
  (wlroots reconfigure overwrites dest). On leave, null the clip first, then
  resting clip — never force full-surface `dest_size` while a clip is installed.
  Target `arrange_prop` cell; re-apply crop on idle after commit; corner radii
  use the presented box. Never force `dest_size` when not in a pop.
- **Appear wait:** pack cell first; give up after 1 post-propose commit. Never
  reset `arrange_prop_waits` on same-size re-pack. SoftPlace: keep soft until
  float identity decided; empty `app_id` with a title must show (Chrome Task
  Manager). Explicit focus must unpark appear.
- **Off-strip paint:** crop content/border/SSD to `arrange_prop_*` until the
  client redraws — client geo under the next column is a bug.
- **Hide finish:** chrome pad on offscreen target, clear usable, disable scene;
  raise border after SSD on show. Super+q is close → unmap ghost (chrome-less).

### Drag, minimize, foreign, shell

- **Cell-drag home** is a column pointer (+ insert index), not a strip index.
  Validate `drop_peer` / `drop_column`; abort grab before mid-drag `arrange_set`.
  Reflow indices are painted (visible-only) → structural on commit.
- **Minimize:** packing skips minimized rows; expel keys off **row count** (any
  sibling), not visible count. Foreign still advertises minimized windows.
- **Foreign output pins to the workspace output**, not geom∩paint — off-strip
  columns must not `output_leave` and drop taskbar icons. Taskbar is membership,
  not visibility.
- **Layer blur:** one SceneFX plate per effect rect via shared
  `sf_blur_plates_*` (same as toplevel); layer keeps exclusive/opaque/input
  full-region box policy. Do not destroy blur plates in
  `layer_surface_destroy` (tree already freed) — detach straggler plate
  listeners only. Wire every layer surface, not a bar special case. Toplevel
  client regions use plates under `client_blur_tree` (sibling of
  `xdg_scene_tree` — never inside it, or `wlr_scene_subsurface_tree_set_clip`
  asserts). Chrome `window_blur` disabled while client plates are active.
  Layer popups inherit a live non-xray plate when the parent layer has
  `client_blur`.
- **xdg_popup:** `wlr_xdg_popup_unconstrain_from_box` into usable; layer popups
  reparent on `get_popup`; defer unconstrain until initialized.
- **Derived shell policy** (e.g. Noctalia bar opacity on edge-max): recompute
  from window list on layout/unmap/workspace — never toggle on a single
  max/unmax (needs `sf.on` + `sf.wins`).
- **Live reload:** staging config + fresh VM; on failure restore previous +
  Notify; `sf_scratchpad_rebind` by name; disarm tap aliases before swap.
- **Wayland send buffer:** `wl_display_set_default_max_buffer_size` (1 MiB) after
  create; idle-coalesce foreign title/state/outputs into one `done` per handle
  (default 4 KiB ring disconnects Noctalia under burst).
- **Session lock** is a presentation guarantee: wait for per-output protected
  buffer feedback; keep blank/lock surfaces synced with the output layout.

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
  `sf.window_rule`, `sf.stdpath`, `sf.fs`, `sf.packadd`, …).
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
- **Shared helpers:** cross-module math/string/time/JSON/path/spawn live under
  `src/util/` (`math.h`, `str.c`/`str.h`, `time.h`, `json.c`/`json.h`,
  `path.c`, `spawn.c`, `shell_quote.c`). Do not copy static `clamp01` /
  `xstrdup` / timespec / fork+`execl` helpers into new files — extend `util/`
  instead.
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
- Prefer concrete pain (bugs, hard features, navigation while editing) over
  house-style file-shape refactors. Renaming a switch into a “handler table”
  is cleanup, not a substitute for fixing membership/scroll/float policy.
- The compositor is **single-threaded** on the Wayland `wl_event_loop`. No
  pthreads in product code; only fork+exec for user commands. IPC, D-Bus,
  idle arrange, and anims use the main loop.

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

**`make apk` works in the agent sandbox.** After compositor / Meson / protocol
edits that should compile, run it to verify the Alpine pack (compile +
`check()`: style-check + `tests/drive.lua`). Host unit tests (`make test` /
`tests/drive.lua`) remain fine for faster C harness iteration. Driver source
of truth is `tests/drive.fnl` → `make drive-lua` (Fennel only for that regen;
never required for `make test` / APK check).

**Do not look up libraries, headers, or pkg-config on the host.**

1. Compositor deps live in the **abuild** chroot (`make apk`), not a host
   `ninja` / `meson compile` / `cc` tree. Prefer `make apk` over probing
   `/usr` or inventing a host Meson dir.
2. If a symbol or `.pc` file is unclear, read Meson/`APKBUILD` makedepends and
   vendored headers under `protocol/` — do not probe `/usr` on the host.
3. When the user pastes `@dist/alpine/build.log` or says “reread,” read that
   log; do not relaunch abuild only to “confirm” a log they already have.
4. Do **not** run `abuild-keygen` or invent signing-key workarounds. Missing
   keys → stop and hand off; signing stays on the host with the user’s keys.
5. `make apk` runs `abuild checksum` (may dirty `APKBUILD` hashes). Do not
   run checksum by itself, and do not edit `Makefile` / `APKBUILD` otherwise.

## Prefer in-process C

Do not shell out (`system`, `popen`, `sh -c` helpers, `mkdir -p`,
`notify-send`, `dbus-update-activation-environment`, etc.) for work that
belongs in the compositor.

- Session bus / notifications / activation environment: `src/dbus/dbus.c`
  (sd-bus / elogind).
- Directories: recursive mkdir in C (`sf_mkdir_p` / Lua `sf.fs.mkdir_p`).
- Allowed `/bin/sh -c` spawn is **user-configured only** via `sf_spawn_cmd`
  (`-s`, `sf.exec`, scratchpad `cmd`, `EXEC` actions) — not internal plumbing.

When adding a dependency, prefer libraries already in `makedepends` (or a
thin C API over an optional static C++ helper). Keep the compositor C-first.

## Packaging

- Alpine aport: `dist/alpine/APKBUILD` (invoked via `make apk`).
- **clang and `*-dev` packages are `makedepends`**, never runtime `depends`.
  Users must not need clang to install/run the APK.
- **Do not bump `pkgrel` / `pkgver`** unless the user explicitly asks.
  Build/source fixes are fine; revision numbering is the user’s call.
- **Do not edit or revert** root `Makefile` or `dist/alpine/APKBUILD` unless
  the user explicitly asks. `make apk` already runs `abuild checksum`; do not
  run checksum by itself.
- **Never generate abuild signing keypairs** in the sandbox. Missing keys →
  stop and hand off; signing belongs on the host with the user’s keys.

## Docs to keep current

Shipped work lives in [`FEATURES.md`](FEATURES.md); open and partial work in
[`ROADMAP.md`](ROADMAP.md) (TODO NEXT pick-queue stays titled **TODO NEXT**).
Hard-won WM traps live in this file under **WM lessons**. C↔Lua border:
ROADMAP **C ↔ Lua border**. Wayland globals / peer matrix: ROADMAP
**Protocols (running inventory)** (update cells in place). After a change,
fix, feature, or notable diagnosis, update the matching rows **in the same
turn**. When something ships, move it from the roadmap into features (and
drop or shrink the open row). When a new durable trap is learned, add a short
bullet under **WM lessons** here. When a protocol is advertised,
version-bumped, wired, or left hollow, update the ROADMAP protocols-inventory
row (and FEATURES / §5 open gaps). When a C↔Lua knob ships or a new hardcoded
policy appears, update the ROADMAP C↔Lua deferred inventory (and TODO NEXT if
it was a pick-queue item).

## Workflow

- Answer every outstanding user prompt in the turn; do not skip earlier items
  when a later one arrives.
- Prefer implementing over drafting long design docs when Umbriel / WM lessons
  already define the abstraction.
- Host unit tests (`make test`, owned C harness under `tests/`) are fine when
  they help. After src/Meson/protocol work that should compile, run
  `make apk` (sandbox abuild) unless the user already pasted a matching
  `build.log` this turn.
- **Tests follow the compositor.** If product code uses LuaJIT, pixman, or
  other deps already in `makedepends`, unit suites may link them
  (`libs=` / pkg-config in `tests/drive.lua`). Do not invent C peels, fake
  matchers, or dep-free reshapes solely so the harness stays “pure.” Domain
  kernels that are already Wayland-free (`place_sm`, `layout_sm`,
  `arrange_math`) stay unit-tested as pure C — that is domain purity, not
  harness purism. Live / integration asserts prefer IPC snapshots
  (`windows`, `dump`) over injecting test seams into scene/WM paths.
- Do not run `make fmt` unless the user asks — formatting is their call.
- When changing `.clang-tidy`, keep useful checks; silence only known narrow
  false positives and document them here (see Lint / analyzer above).

## Done checklist

Before calling work finished:

1. Code matches wlroots/tinywl style (`.clang-format` / neighboring `./src`),
   including module layout and `util/` reuse above.
2. No host library/pkg-config probing; no `abuild-keygen`; no unsolicited
   `Makefile` / `APKBUILD` / `pkgrel` edits. `make apk` is the compile check.
3. No new internal shell-outs (user-configured spawn goes through
   `sf_spawn_cmd`).
4. `FEATURES.md` / `ROADMAP.md` rows for the touched feature are current
   (shipped items moved to features; protocol ads/versions stay in ROADMAP
   protocols inventory; open hollows in ROADMAP §5). New durable traps go in
   **WM lessons** above.
5. `pkgrel` / `pkgver`, root `Makefile`, and `dist/alpine/APKBUILD` untouched
   unless requested.
6. New sources are listed in `src/meson.build` when applicable.
7. Behavior stays predictable: round-trips for the touched WM path still hold
   (or gaps are filed on the Predictability audit).
