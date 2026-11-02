# stonefly

Wayland compositor seeded from **wlroots 0.20
[tinywl](https://gitlab.freedesktop.org/wlroots/wlroots/-/tree/0.20.0/tinywl)**.

## Why “stonefly”?

There was once a little pebble by a river.

It sat very still. Water polished it. Seasons argued over it. Fish ignored it.
Compositors and window managers thundered past upstream like bigger rocks with
opinions. The pebble listened.

One spring morning it noticed the insects that skim the surface — stoneflies —
hatching, lifting, refusing to stay where the current put them. The pebble
thought: *I could do that.*

So it grew wings. Not fancy ones. Just enough to leave the riverbed without
forgetting it.

That is this project. A small stone that learned to fly: still close to the
water (wlroots, Wayland, the usual current), still humble about what it is,
but no longer content to only be sat upon.

If you prefer something quieter, *dipper* was the runner-up — a bird that
bobs along the same streams. We kept the louder name. The lore stays; the
binary is `stonefly`.

## Layout

```
meson.build          # project root
Makefile             # make apk
protocol/            # vendored wlr XML → wayland-scanner *-protocol.h
src/
  main.c             # tiny entry (+ windows subcommand)
  util/json.*        # fixed-buffer JSON writer + scanner
  log/               # structured JSON logging (peeble slog shape)
  ipc/               # unix-socket IPC + windows/layers [--json]
  config/hardcoded.h # day-zero knobs
  server/            # display, backend, scene, loop
  output/            # outputs + scale
  layer/             # wlr-layer-shell scene + exclusive zones
  seat/              # seat, keys, pointer, cursor
  xdg/               # xdg-shell toplevels/popups
  wm/                # focus, max, snap, minimize, spawn, scratchpads
dist/testing/stonefly/APKBUILD  # Alpine aport (repo name = testing)
```

Day-zero binds and scale live in `src/config/hardcoded.h`.
Layer-shell (`wlr-layer-shell-unstable-v1` v5) is implemented for
external clients (bars, OSD, wallpapers): exclusive zones shrink the
usable area used by maximize/snap/cascade.

Super tap / double-tap (peeble `bind_tap`): single →
`noctalia msg panel-toggle launcher`; double → named scratchpad `term`
(`ghostty --class=stonefly.scratch.term`). Super+t claims the focused
window onto that pad. Named pads are omitted from Super+Tab.

## Build

```sh
make apk
doas apk upgrade --available   # if /home/apk/testing is already a repo
# or:
doas apk add --allow-untrusted /home/apk/testing/x86_64/stonefly-*.apk
```

`make apk` packs a source tarball, runs `abuild checksum`, then `abuild -r`.
The aport lives at `dist/testing/stonefly/` so:

- project `src/` does not collide with abuild’s `$srcdir` (`$aport/src`)
- abuild names the repo after the aport’s **parent** → `testing`
- with default `REPODEST` (`/home/apk` on this host) the APK lands in
  `/home/apk/testing/x86_64/`

If you already have the same `pkgver-rpkgrel` installed, `apk upgrade` is a
no-op — bump `pkgrel` (or `--force-reinstall` the `.apk`).
## Run

```sh
stonefly -s foot
```

`-s` runs a command after the compositor is up (`WAYLAND_DISPLAY` set).
Spawn binds (Super+w Ghostty, Super+Space foot, …) also work once you are in
the session.

## Logging and IPC

Compositor logs are structured JSON lines (peeble-shaped) on stderr and
`~/.var/log/stonefly/wm.log` (on start the previous file becomes
`wm.last.json`; only current + last session are kept). wlroots messages
go through the same handler.

Same binary speaks IPC when given a subcommand. Socket path:
`$STONEFLY_SOCKET`, else `$XDG_RUNTIME_DIR/stonefly-$WAYLAND_DISPLAY.sock`.

```sh
stonefly windows           # human: "* id  app_id  title"
stonefly windows --json    # JSON array (id, app_id, title, geom, flags)
stonefly layers            # human: namespace / layer / output / geom / excl / kbd
stonefly layers --json     # JSON array (namespace, layer, output, geom, zones)
```

Wire format matches peeble: one JSON object per line, e.g. `{"cmd":"windows"}`.

## Keybindings (peeble-shaped, hardcoded)

| Bind | Action |
|---|---|
| **Super+Esc** / **Super+Shift+Q** | Quit |
| **Super+q** | Close focused window |
| **Super+Tab** / **Super+Shift+Tab** | Focus next / prev |
| **Alt+Tab** | `noctalia msg window-switcher` (window snapshots when capture protocols are present) |
| **Alt+Shift+Tab** | Focus prev including minimized |
| **Super+f** | Toggle maximize to edges |
| **Super+m** | Toggle maximize with gaps |
| **Ctrl+Esc** / **Ctrl+Shift+Esc** | Minimize / unminimize (unnamed scratchpad) |
| **Super+Left/Right/Up/Down** | Snap to half (with gaps) |
| **Super+u** | Unsnap / restore |
| **Super+LMB** | Move window (magnetic edge snap + preview) |
| **Super+RMB** | Resize window (edges nearest pointer) |
| **Super+w** | spawn `ghostty +new-window` |
| **Super+Space** | spawn `foot` |
| **Super+z** | spawn `nautilus` |
| **Super+e** | spawn text editor |
| **Alt+Enter** | spawn Ghostty quick terminal |

Output scale is hardcoded to **1.5** with `wp_fractional_scale_v1` + viewporter.

## Notes

- SceneFX 0.5 (`scenefx` / `scenefx-dev`): `fx_renderer_create`, per-window blur + drop shadow + 12px rounded corners, 3px border (neon-blue focused / silver idle / amber urgent), client opacity **0.90**.
- Presentation: peeble-style slide in/out (`ease_out`, 200ms, closest usable edge) on map, close, minimize/unminimize, and scratchpad show/hide. Client Ctrl+W / CSD close uses a last-frame **ghost** slide (FORK.md close-ghost).
- Urgency: `xdg-activation-v1` → amber border until focus. System bell: `xdg-system-bell-v1` → 200ms amber flash.
- Usable area: layer exclusive zones (`exclusive_zone != 0`, including `-1`) shrink the box used by max/snap/cascade. When the bar’s zone arrives after map, floaters are EnsureOnScreen’d (gap-max if oversized).
- Drag-snap (peeble parity): Super+LMB / CSD move has magnetic edge resistance (`snap_threshold` 26, push-through `4×`), neon half-snap preview on `layers.overlay` (SceneFX rects), release-to-snap / push-through cancel, PrepareDrag float from snap/max keeping PreSnap.
- No Xwayland or real config file yet (layer-shell is implemented).
- `LICENSE.tinywl` is upstream’s MIT notice for the seed.

## FORK.md → stonefly (remaining gaps)

River-fork items that stonefly already owns in-process (no River protocol needed): vsynced slides, map park/appear, opacity, close-ghost, SceneFX blur/corners/shadows, layer z-order, urgency, system bell.

Still open relative to the old River-fork brief / peeble feature set:

| Gap | FORK / notes | Status |
|---|---|---|
| Foreign-toplevel for docks/taskbars | [#1449](https://codeberg.org/river/river/issues/1449) / [#1537](https://codeberg.org/river/river/pulls/1537) | ✅ `zwlr_foreign_toplevel_management_v1` v3 + `ext-foreign-toplevel-list-v1` |
| Capture privacy (block window from screenshots) | [#1481](https://codeberg.org/river/river/issues/1481) | Not implemented |
| Workspaces / tags | [#1402](https://codeberg.org/river/river/issues/1402) `ext-workspace-v1`, [#1390](https://codeberg.org/river/river/issues/1390) toplevel-tag | Not implemented |
| Window previews / destination size | [#1384](https://codeberg.org/river/river/issues/1384) | ✅ ext toplevel image-capture + image-copy-capture for Noctalia switcher (`ext_capture.c`) |
| Xwayland | peeble/river day-to-day | Not implemented |
| Screenshots / screencopy / image-copy-capture | peeble | Protocol ❌; Print binds → noctalia 🌙 |
| Session lock / idle / gamma / output-mgmt / clipboard | Noctalia shell needs | Not implemented — see `MATRIX.md` §9 |
| Real config (vs `hardcoded.h`) | — | Not implemented |
| In-process bar/OSD/media | peeble; noctalia covers bar/launcher/notifier | Deferred to shell |
