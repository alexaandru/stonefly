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
src/
  main.c             # tiny entry (+ windows subcommand)
  util/json.*        # fixed-buffer JSON writer + scanner
  log/               # structured JSON logging (peeble slog shape)
  ipc/               # unix-socket IPC + windows [--json]
  config/hardcoded.h # day-zero knobs
  server/            # display, backend, scene, loop
  output/            # outputs + scale
  seat/              # seat, keys, pointer, cursor
  xdg/               # xdg-shell toplevels/popups
  wm/                # focus, max, snap, minimize, spawn
dist/testing/stonefly/APKBUILD  # Alpine aport (repo name = testing)
```

Day-zero binds and scale live in `src/config/hardcoded.h`.

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
`~/.var/log/stonefly/wm.log`. wlroots messages go through the same handler.

Same binary speaks IPC when given a subcommand. Socket path:
`$STONEFLY_SOCKET`, else `$XDG_RUNTIME_DIR/stonefly-$WAYLAND_DISPLAY.sock`.

```sh
stonefly windows           # human: "* id  app_id  title"
stonefly windows --json    # JSON array (id, app_id, title, geom, flags)
```

Wire format matches peeble: one JSON object per line, e.g. `{"cmd":"windows"}`.

## Keybindings (peeble-shaped, hardcoded)

| Bind | Action |
|---|---|
| **Super+Esc** / **Super+Shift+Q** | Quit |
| **Super+q** | Close focused window |
| **Super+Tab** / **Super+Shift+Tab** | Focus next / prev |
| **Alt+Tab** / **Alt+Shift+Tab** | Focus next / prev (includes minimized) |
| **Super+f** | Toggle maximize to edges |
| **Ctrl+Esc** / **Ctrl+Shift+Esc** | Minimize / unminimize (unnamed scratchpad) |
| **Super+Left/Right/Up/Down** | Snap to half of the output |
| **Super+u** | Unsnap |
| **Super+w** | spawn `ghostty +new-window` |
| **Super+Space** | spawn `foot` |
| **Super+z** | spawn `nautilus` |
| **Super+e** | spawn text editor |
| **Alt+Enter** | spawn Ghostty quick terminal |

Output scale is hardcoded to **1.5** with `wp_fractional_scale_v1` + viewporter.

## Notes

- No layer-shell, Xwayland, or real config file yet.
- `LICENSE.tinywl` is upstream’s MIT notice for the seed.
