# Stonefly features

What ships in this tree. Open and partial work lives in
[`ROADMAP.md`](ROADMAP.md). Implementation traps live in
[`AGENTS.md`](AGENTS.md) **WM lessons**. Protocol yes/partial/no vs peers:
ROADMAP **Protocols (running inventory)**.

When a roadmap item lands, describe it here in plain language and delete the
open row from the roadmap in the same turn.

---

## At a glance

| | |
|---|---|
| **Role** | Wayland compositor + window manager (wlroots 0.20 / tinywl seed) |
| **Language** | C23 (C11 fallback), Meson |
| **Scene** | SceneFX 0.5 (blur, shadows, rounded corners, borders) |
| **Arrange** | `columns` (default) and `stacking` |
| **Shell** | Day-zero **Noctalia** on layer-shell (bar, launcher, OSD, …) |
| **Config** | In-process LuaJIT + Fennel-first defaults |
| **X11** | Lazy on-demand Xwayland (unified toplevels) |
| **Packaging** | Alpine APK via `make apk` |

---

## Window management

### Arrange

- **Columns** — scrollable strip of first-class columns; windows are rows inside
  a column (consume/expel, consume-from, column width, focus-within-column).
  Super+,/. consume (or expel); Super+Ctrl+,/. pull from the neighbor column.
- **Stacking** — free float / snap / gap-max desktop.
- Switch with Super+] / [ (`arrange_rotate`). Non-floats join the tile set;
  user and rule floats stay out (`arrange_floating`).
- Per-monitor default via `sf.output` / `outputs.fnl`; resolve workspace
  override → output → global (`sf.opt.arrange`).
- **Arrange switch:** columns → stacking restores preferred/natural size
  (pre-tile float box) and cascades so windows are usable; stacking → columns
  restores prior strip slots when the user had grouped rows, otherwise **one
  window per column**. Slot memory round-trips consume/drag across switches.
- Column width uses Umbriel slot math so equal fractions fit the usable box.
  Scroll reveals just enough; new windows insert beside the focused column.

### Focus, chrome modes, fullscreen

- Super+Tab / Super+Shift+Tab cycle visible windows (columns: spatial ring
  including sticky floats; stacking: MRU). Focus raises the target.
- Alt+Tab → Noctalia window switcher; Alt+Shift+Tab includes minimized.
- **Super+f** — maximize to edges (zen). Under columns this is a transient
  peek (`arrange_max_edges_rejoin`) that drops back to the cell on focus/Tab;
  sticky edge-max = float first (Super+v) then Super+f (or a float rule).
  Documented next to the default binds.
- **Super+m** — maximize with gaps / column full-width (stays tiled in columns).
- Client / SSD / foreign maximize follow `sf.opt.client_maximize` (default
  `gaps`).
- **F11** — exclusive fullscreen (one per output; exact output box). Leaving
  FS on a strip-managed tiled window packs the cell (skips saved-box morph);
  stacking / sticky-float still restore the saved box.
- Half-snap with Super+arrows while stacking; Super+u restores. Snap↔max↔FS
  overlays share one “normal” float box.

### Move, resize, drag

- Super+LMB move, Super+RMB resize, Super+Shift+RMB center-lock resize
  (`sf.bind` mouse chords).
- Under columns, tiled Super+LMB does cell swap / seam insert / new column
  with live make-room; floats move freely (no magnetic half-snap).
- Border/corner LMB hit-band resize; magnetic drag-snap + preview in stacking.
- Keyboard nudge / resize-edge; multi-monitor focus and send actions.

### Workspaces, scratchpads, rules

- Per-output workspace strip with `ext-workspace-v1`, IPC, keybinds, and
  3-finger vertical swipe. Horizontal 3-finger pans the columns strip.
- Named scratchpads (`sf.scratchpad`); default `term` (real Ghostty
  `app_id` / taskbar icons). Spawn claim uses a compositor-issued
  `xdg-activation` token; optional `:app_id` is only a soft fallback.
  Super N-tap: 1 → Noctalia launcher, 2 → term pad. Super+t toggles QT
  membership on the focused window (claim / unclaim; steals the pad).
- Window rules: match `app_id` / title → float, center, size, SSD, opacity,
  actions. Embedded defaults (`rules.fnl`) seed Umbriel float/size/opacity
  peers (Noctalia, PiP, GNOME utils, portals, halloy, …) plus Slack edge-max
  and Chrome/Chromium profile float.
- Always-on-top / pin; unminimize can carry to the active workspace
  (`sf.opt.unminimize_workspace`).
- SSD titlebars via `ssd_mode` / height / font; hidden on fullscreen and
  edge-max.

### Overview

- Super+o and 4-finger vertical swipe open an Umbriel-shaped workspace
  filmstrip (thin v1: live cards, click to focus, Escape / Enter / Space,
  overview-only binds and pan gestures).

---

## Look and motion

- Tiled appear/close: Umbriel-style popin/popout (scale + fade at the packed
  cell). Floats and scratchpads use closest-edge slides.
- Live morph for snap / max / unmax; workspace switch slides vertically.
- Close (client or Super+q after unmap) uses a last-frame ghost animation.
- Per-op animation table: `sf.opt.anim` (enable, duration, style, scale).
- SceneFX blur (window and layer `ext-background-effect-v1`), soft shadows,
  rounded corners, focus/idle/urgent borders. Content-radius corners: outer
  = content+border (concentric hole), hole inset + matching `hole_r` shrink
  so CSD/SceneFX AA does not leave crescent wallpaper gaps. Umbriel
  `sf_nested_radius` kept for reference; SceneFX solid rects stay concentric.
- Dim-inactive spotlight (`sf.opt.dim_inactive`) and per-window/rule opacity /
  `no_dim`.
- **looksTiled float heuristic** (Umbriel): parented, X11 dialog/splash/
  toolbar/utility/modal, or fixed min==max size → sticky float centered on
  parent or usable (GIMP/Krita splash).
- **Modal dialog attach** (`xdg_wm_dialog_v1`): with a parent, sticky float +
  center on parent, scene child under `attachment_tree`, parent shade
  (`sf.opt.dialog_dim`), and focus trap (`sf.opt.attach_modal_dialogs`,
  default true).
- Urgency via `xdg-activation` (amber border, never steals focus). System bell
  flashes amber.

---

## Shell and session

Day-zero chrome is **Noctalia** (bar, taskbar, launcher, OSD, notifications,
control center). The compositor provides the protocols and WM hooks those
need.

Compositor-owned session pieces:

- Session lock, idle notify/inhibit, output power (DPMS)
- Night-light gamma LUT (schedule stays Noctalia-side)
- In-process D-Bus: activation environment + FDO Notify client for config
  warnings

---

## Protocols and input

Full peer matrix and versions: ROADMAP **Protocols (running inventory)**.

Highlights that ship for a usable Noctalia desktop:

- **xdg-shell v7** — bounds, capabilities, suspended, constrained; idle
  clients stop getting redundant configures
- **Layer-shell v5** — exclusive zones shrink usable area
- Foreign toplevel v3 + ext list (taskbar; output pin follows workspace)
- Workspaces, session lock, idle, output power, gamma, output management
- Fractional scale + viewporter + xdg-output; cursor-shape
- Screencopy + ext-image-copy-capture — per-toplevel privacy block; full-output
  captures omit `capture_blocked` windows (dual-pass on stock SceneFX: filtered
  commit for clients, then full scene for display)
- **Tearing** — `wp_tearing_control_v1` async page-flips when
  `sf.output name {:tearing true}`, the window is fullscreen, and the client
  hints async (or `window_rule` `:tearing true`/`false` forces/vetoes). Vetoed
  during lock, overview, workspace anim, or active capture; recovers after a
  failed async flip/present
- Clipboard (ext + legacy data-control, primary selection)
- Background-effect blur — layers and toplevels use per-rect plates from
  `set_blur_region`; layer-shell popups inherit a live plate when the parent
  layer requested blur
- Gestures, relative pointer, constraints, virtual keyboard, touch
- **IME** — `zwp_text_input_v3` + `zwp_input_method_v2` relay (fcitx/ibus;
  keyboard grab + candidate popups). Sandbox allowlist exposes text-input;
  input-method stays privileged.
- **Tablet v2** — `zwp_tablet_v2` for Wacom One & co: pressure, tilt, distance,
  rotation, slider, wheel; tip/buttons; pad ExpressKeys / rings / strips /
  modes; pointer emulation when the client has no tablet support.
- Shortcuts inhibit, security context (Flatpak), presentation-time
- **xdg_wm_dialog_v1** — modal attach policy (float/center/shade/focus trap)
- **xdg-toplevel-tag-v1** — stored + IPC; `window_rule` `match.tag` (alias
  `xdg_tag`) for rules; geometry persist still open
- Lazy **Xwayland** — on-demand; managed windows share `sf_toplevel`

Intentional subsets (not unfinished bugs): activation never focus-steals;
system bell is visual only; layer keyboard auto-focus is exclusive-only.

---

## Config and IPC

### Config

- LuaJIT VM in-process; Fennel-first embedded defaults under
  `src/config/default/`
- WM feel knobs: `sf.opt.focus_cycle` (`spatial`|`mru`|`strip`),
  `pan_focus` (`keep`|`center`|`warp`|`under_pointer`; columns strip pan),
  `tile_insert` (`after_focus`|`end`|`begin`), `column_drop_seam`,
  `tiled_grab_remap`, `border_resize` (defaults match prior C hardcoding)
- `sf.opt`, `sf.bind` (keys / mouse / Mod+Wheel), `sf.tap`, `sf.gesture`,
  `sf.window_rule`, `sf.scratchpad`, `sf.input`, `sf.output`, `sf.exec`,
  `sf.reload`, packs via `sf.packadd` / `sf.stdpath`
- `sf.output` name + `{:arrange …}`: per-monitor stacking/columns/inherit;
  resolve workspace → output → global; IPC `config outputs`
- `stonefly --write-config`, `stonefly validate`, live reload (inotify +
  staging; failed reload keeps the previous config and notifies)
- C owns Wayland/scene/primitives; Lua owns policy — see ROADMAP **C ↔ Lua
  border**

### IPC / CLI

Same binary; socket `$STONEFLY_SOCKET` or
`$XDG_RUNTIME_DIR/stonefly-$WAYLAND_DISPLAY.sock`.

`windows`, `layers`, `outputs`, `workspaces`, `workspace` /
`move-to-workspace`, `config` [section], `focus` / `close`,
`toggle-quick-terminal`, `toggle-claim-quick-terminal`, `reload`,
`action` `<name>` `[data…]` (allowlisted bind verbs; pointer-grab denied;
`quit` allowed — same terminate as `exit`), `exit` — human or `--json`.

Structured JSON logs on stderr and `~/.var/log/stonefly/wm.log`.

---

## Testing and packaging

- Owned C unit harness under `tests/` (`make test` on host; APK `check()`
  runs style-check + the same driver)
- Alpine package via `make apk` (`dist/alpine/APKBUILD`)

---

## Doc map

| Doc | Role |
|---|---|
| **FEATURES.md** (this file) | What ships |
| **ROADMAP.md** | What is open; peer catch-up; protocol inventory; C↔Lua inventory |
| **AGENTS.md** | House rules + WM lessons |
| **README.md** | Quick start, binds, build |
