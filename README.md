# stonefly

Wayland compositor seeded from **wlroots 0.20
[tinywl](https://gitlab.freedesktop.org/wlroots/wlroots/-/tree/0.20.0/tinywl)**.

## Build (abuild)

```sh
cd testdata/testing/stonefly
abuild -P "$PWD/.repo" -r
doas apk add --allow-untrusted .repo/*/x86_64/stonefly-*.apk
```

The `.apk` lands under `.repo/testing/x86_64/` when you pin `-P` like that.
`pkg/` is only fakeroot staging.

## Run

Nested (inside an existing Wayland session — opens a window as the “display”):

```sh
stonefly -s foot
```

Or from a TTY (real DRM session).

`-s` runs a shell command after the compositor is up, with `WAYLAND_DISPLAY`
set. You need that (or clients launched some other way): stonefly has **no
launcher keybind**.

Without `-s`, the compositor still starts; point clients at its
`WAYLAND_DISPLAY` (printed in the log) yourself.

## Keybindings (from tinywl)

| Bind           | Action                         |
| -------------- | ------------------------------ |
| **Alt+Escape** | Quit the compositor            |
| **Alt+F1**     | Cycle focus to the next window |

Pointer: click to focus; interactive move/resize via xdg toplevel requests
(CSD / client decorations), same as tinywl.

## Notes

- Multiple windows work; cycle with Alt+F1.
- No HiDPI, layer-shell, Xwayland, or config — same omissions as tinywl.
- `LICENSE.tinywl` is upstream’s MIT notice for the seed.
