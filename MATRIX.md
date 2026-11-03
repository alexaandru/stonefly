# Peeble ↔ stonefly feature matrix

Comparison of **peeble** (River companion WM + in-process shell) and
**stonefly** (monolithic wlroots 0.20 + SceneFX compositor/WM in this tree).

Sources: peeble [`README.md`](peeble/README.md) Features,
[`ROADMAP.md`](peeble/ROADMAP.md) (implemented + open + Ideas/wishlist),
[`FORK.md`](peeble/FORK.md) (River-fork compositor gaps), stonefly
[`README.md`](README.md), and the live stonefly tree.

## How to read this

| Symbol | Meaning |
|---|---|
| **✅** | Shipped / implemented in that project |
| **🚧** | Partial, stub, or day-zero subset |
| **📋** | Roadmap / Ideas / FORK-only — mentioned, not shipped |
| **❌** | Not present |
| **🌙** | Intentionally deferred to an external shell (Noctalia under stonefly) |
| **N/A** | Does not apply given architecture |

**Peeble** columns split **shipped** vs **roadmap** so “mentioned only”
items stay visible. **Stonefly** is a greenfield compositor: many peeble
shell features are covered by Noctalia rather than in-process code; FORK
items that needed River protocol land *in-process* on stonefly instead.

### Architecture (context)

| | Peeble | Stonefly |
|---|---|---|
| Role | Wayland **client** WM + shell on River 0.5 | **Compositor + WM** (tinywl seed) |
| Language | Go + Risor config | C23 (C11 fallback), Meson |
| Scene / FX | River draws; peeble proposes geometry | SceneFX 0.5 scene graph |
| Chrome | In-process bar / OSD / launcher / wallpaper | Layer-shell clients; day-zero → **Noctalia** |
| Config | `config.risor` + live reload | LuaJIT + Fennel-first defaults (§10); `hardcoded.h` is a shim |
| Packaging | Go binary `~/.local/share/go/bin/peeble` | Alpine APK via `make apk` |

---

## 1. Window management

| Feature | Peeble shipped | Peeble roadmap / Ideas | Stonefly | Notes |
|---|---|---|---|---|
| Stacking (no layout engine) | ✅ | — | ✅ | Both stacking-first |
| Focus next / prev (visible) | ✅ Super+Tab | — | ✅ | Same bind shape |
| Focus next / prev including minimized | ✅ Alt+Tab | — | 🚧 | Alt+Tab → `noctalia msg window-switcher`; Alt+Shift+Tab still `FOCUS_PREV_ALL` (unhides). Super+Tab visible-only cycle. |
| Close focused | ✅ | — | ✅ Super+q | |
| Maximize to edges | ✅ | — | ✅ Super+f | Client maximized hint |
| Maximize with gaps (WM-only) | ✅ | — | ✅ Super+m | No client maximized → rounder CSD |
| Unmaximize / restore | ✅ | — | ✅ | Toggle paths |
| Fullscreen (client + WM) | ✅ | — | ✅ | Saved geom + full output box; client `request_fullscreen` + foreign set/unset (no WM keybind; Super+f is edge-max) |
| Minimize / unminimize LIFO | ✅ Ctrl+Esc | — | ✅ | Unnamed scratchpad. Shared `apply_hidden`/`apply_shown` (LIFO vs scratch). `xdg request_minimize` → CSD/SSD buttons. |
| Always-on-top | ✅ | — | ❌ | Peeble winmenu / state |
| Keyboard half-snap (arrows) | ✅ Super+arrows | — | ✅ | Gap math ports `snapBorderBox` |
| Unsnap / restore PreSnap | ✅ Super+u | — | ✅ | |
| Drag move (Super+LMB) | ✅ | — | ✅ | |
| Drag resize (Super+RMB) | ✅ | — | ✅ | Edges nearest pointer |
| Magnetic drag-snap + resistance | ✅ threshold 26, 4× push-through | — | ✅ | `drag_snap.c` parity |
| Snap preview overlay | ✅ SHM layer-shell | Configurable fill color/opacity 📋 | ✅ SceneFX overlay rects | Border 1, fill 40% active |
| `set_tiled` on snap/max | ✅ | — | ✅ | `wlr_xdg_toplevel_set_tiled` on snap / edge-max / restore / unsnap (`wm.c`); gap-max stays untiled (peeble). |
| Cascade initial place | ✅ | — | ✅ | Gap + `(n%10)*25`, cursor output |
| EnsureOnScreen / usable relayout | ✅ | — | ✅ | On exclusive-zone change |
| Per-output usable area | ✅ `UsableOn` / `UsableAreaChanged` | — | ✅ | `exclusive_zone != 0` (incl. −1) |
| Named scratchpads | ✅ multi + claim + auto_hide | Click-outside catcher 📋 | 🚧 | Pad `term` (+ config). Spawn uses fake `app_id` (`stonefly.scratch.term`) so claim does not steal normal Super+w windows — that breaks taskbar icons (shell keys icons off app_id). Real app_id + PID claim is not enough for single-instance multi-window clients (one Ghostty PID, many windows). Proper fix later: xdg-activation / startup token (or accept first-map races); leave icon/claim as-is for now. Hide/show via `apply_hidden(SCRATCH)` / `apply_shown` (same foreign/defocus as LIFO; `focus_top` skips `anim_hide`). **`auto_hide`:** peeble `OnFocusChange` — hide when focus moves to another window on the same output (click / Super+Tab); other-head or nil (layer/empty) leaves pad shown. Spec key `auto_hide` (default true). |
| Scratchpad claim focused | ✅ Super+t | — | ✅ | |
| Scratchpad omit from Alt-Tab | ✅ | — | ✅ | Super+Tab too |
| Window menu | ✅ | — | ❌ | |
| SSD titlebars | ✅ River `use_ssd` | Polish / force-SSD rules 📋 | ✅ | `ssd_mode` (`auto`/`prefer_ssd`/`always`/`never`, default `auto`) + `ssd_height` (28). Honors client `request_mode` within policy; fcft+pixman titlebar (title, min/max/close); hide on fullscreen/edge-max; TopInset in ensure-on-screen. Pointer: drag, dblclick gap-max, buttons. Per-app overrides / winmenu still open. **Fixed:** Chrome early bind — defer `set_mode` until `xdg_surface` initialized. |
| Geometry persist across restart | ✅ `windows.json` by river id | Schema / blacklist / MRU polish 📋 | ❌ | |
| Window rules (app-id/title) | 🚧 center + modal parent | auto-float, size, scratchpad, AOT, force SSD 📋 | ❌ | |
| Keyboard nudge move/resize | ❌ | 📋 high-value | ❌ | Snap exists; step nudge does not |
| Workspaces / tags | ❌ | 📋 + river#1402 / #1390 | 🚧 | Dynamic per-output strip (`workspace.c`): trailing empty, compact on empty, Super+Page/1–9 + Shift move-follow, IPC `workspaces`/`workspace`/`move-to-workspace` (`strtol` index parse), windows `{output,workspace}`. ✅ `ext-workspace-v1` group/handle sync. ✅ 3-finger vertical swipe (`swipe_workspace_threshold`, `swipe_workspace_natural` default true = content tracks finger). ❌ persist / named rules / switch anim |
| Layout engine (tile/columns/…) | ❌ | 📋 | 🚧 | Resolve `workspace → output → global` (`stonefly_layout`); only `stacking` implemented. `columns`/`tile` enum + `apply_layout` hook reserved. Global `stonefly.opt.layout`. |
| Focus cycle policy (MRU vs stack) | 🚧 stacking default | Decide MRU Alt+Tab 📋 | 🚧 | Stacking / MRU list front |
| Multi-monitor focus / send | ✅ `focus_monitor` / `send_to_monitor` | — | ❌ | |
| Pointer center / cursor-follows-focus | ✅ | — | ❌ | |
| Picture-in-picture float | ❌ | Ideas 📋 | ❌ | |
| Dim inactive / spotlight | ❌ | Ideas 📋 (needs opacity) | 🚧 | `client_opacity` (focused) + `inactive_opacity` (unfocused); both default 0.96 (opt-in like hypr/mango/niri). Lower inactive (e.g. 0.85) to dim. Applied on focus + exclusive layer steal (`decor.c`). No black dim-overlay / spotlight yet |

---

## 2. Presentation / animations / eye candy

These map heavily to [`FORK.md`](../../FORK.md). On peeble they need River
hooks; on stonefly they are owned in-process.

| Feature | Peeble shipped | Peeble / FORK | Stonefly | Notes |
|---|---|---|---|---|
| Appear / map slide | ✅ `window_effect` + AppearHold hack | Map-hold fork feature 📋 | ✅ | Closest edge, 200ms ease_out; no resting flash needed |
| Close slide (WM-initiated) | ✅ Super+q etc. | — | ✅ | |
| Close slide (client Ctrl+W / CSD) | ❌ blocked | Last-frame ghost FORK P1 📋 | ✅ | Ghost buffer slide |
| Snap / unsnap morph | ✅ `animation("morph")` + window_effect | — | ✅ | Live peeble-style: each tick `set_position` + `set_size` (`STONEFLY_MORPH_DURATION_MS`); SceneFX chrome follows commits. Keybind snap + Super+u unsnap. Mid-flight retarget. |
| Scratchpad / minimize slides | ✅ | — | ✅ | Mid-flight reverse |
| Vsync-capped animation pacing | ❌ busy manage→render | river#1385 P0 📋 | ✅ | Own scene / frame loop |
| Fade / scale appear | ❌ | river#1373 + Ideas 📋 | ❌ | Opacity static 0.96, not animated fade |
| WM-settable per-window opacity | ❌ | river#1373 P1 📋 | 🚧 | Global focused/inactive opacity knobs; no per-window / rules API yet |
| Backdrop / window blur | ❌ | SceneFX P3 📋 | 🚧 | Per-window SceneFX blur (`window_blur`). Default `blur_xray false` → blur real backdrop (other clients); `true` → wallpaper/optimized underlay only (mango xray). Edge-max / quick terminal keeps blur+opacity (border/shadow still off); fullscreen drops blur |
| Soft rounded corners (opaque) | ❌ | SceneFX P3 📋 | ✅ | `corner_radius` 12 = CSD/hole/buffer; outer rim = `corner_radius + border_thickness` |
| Drop shadows | ❌ | SceneFX P3 📋 | ✅ | |
| Compositor border (focus/idle/urgent) | ✅ via River `SetBorders` | Urgency needs #1281 | ✅ SceneFX | Neon / charcoal inactive / amber. **Fixed:** `STONEFLY_BORDER_*` were compound literals; pointers into them across `else if` blocks → ASan `stack-use-after-scope` in `border_rgba_premul` on first map/focus (boot loop). Now `static const float[4]`. **Fixed:** crescent/hairline vs CSD — hole+buffer use `corner_radius` (match client round); outer = radius+border (dwl); hole inset 1px under the rim for AA/scale seams. |
| Layer z-order vs windows | 🚧 PlaceTop fights | river#1375 P0 📋 | ✅ | Scene trees background→overlay; xdg under `layers.toplevel` |
| Window previews / destination size | ❌ | river#1384 📋 | ✅ | Noctalia switcher via `ext-foreign-toplevel-list` + image-capture-source + image-copy-capture (`ext_capture.c`); frozen snapshot per open |
| Urgency / xdg-activation | ❌ | river#1281 → border/badge 📋 | ✅ | Amber until focus |
| System bell | ❌ | river#1389 📋 | ✅ | 200ms amber flash |
| Capture privacy (block window) | ❌ | river#1481 📋 | ✅ | Per-window `capture_blocked` on toplevel; reject ext-image-capture accept. Config `opt.capture_block_app_ids` (comma list / Lua table) on map/app_id. IPC `block-capture` / `unblock-capture` + windows JSON field. |

---

## 3. Shell chrome

Under stonefly, day-zero desktop chrome is **Noctalia** (bar, launcher,
notifier) on layer-shell. Peeble owns these in-process.

| Feature | Peeble shipped | Peeble roadmap / Ideas | Stonefly | Notes |
|---|---|---|---|---|
| Status bar | ✅ rich widgets | Taskbar v2, overflow modes, FuncMap 📋 | 🌙 | Noctalia |
| Taskbar / app icons | ✅ | Middle-click close, group-by-app, urgency badge 📋 | 🌙 | Scratch `term` placeholder icon — see Named scratchpads. **Crash:** taskbar tooltip `xdg_popup` with no parent scene tree. Null-check alone insufficient — `wlr_xdg_popup_destroy` inside `new_popup` still SEGVs in wlroots. Now parents orphans to `layers.overlay` (idle-destroy only on OOM). |
| Unread title-regex badges | ✅ Slack / Gmail | Live notify unread semantics 📋 | 🌙 | |
| Launcher | ✅ fuzzy + plugins | Grid / categories / emoji Ideas 📋 | 🌙 | Super tap → `noctalia msg panel-toggle launcher`. OTP provider under `dist/noctalia/launcher/provider/otp/` — path source must be additive; a lone `[[plugins.source]]` replaces default `official`+`community` git sources (restore via `plugins source add` or keep those blocks in config). |
| OSD cards (vol/bright/…) | ✅ | Richer network OSD 📋 | 🌙 | XF86* / media / mic / brightness binds → `noctalia msg` |
| Notification toasts | ✅ FDO server | Notification center UI Ideas 📋 | 🌙 | Noctalia notifier |
| Wallpaper engine | ✅ slideshow + transitions + cache | Video/shader Ideas 📋 | 🌙 / ❌ | Layer-shell wallpaper clients OK; no in-process engine |
| Window snap preview (WM) | ✅ | — | ✅ | In compositor (not shell) |
| SSD / winmenu chrome | ✅ | — | 🚧 | SSD titlebars ✅; winmenu ❌ |
| Idle ceremony / lock / DPMS | ✅ | Lock polish Ideas 📋 | ✅ | `ext-session-lock-v1` + `zwlr-output-power-management-v1` (`session/session.c`). Blank retained if lock client dies. Auto-lock / PrepareForSleep are Noctalia-side on top of idle-notify. |
| Caffeine (idle-inhibit) | ✅ | — | ✅ | `zwp_idle_inhibit_v1` → `ext-idle-notify-v1` inhibit |
| Night light (gamma) | ✅ | Schedule timer 📋 | ✅ | `zwlr_gamma_control_v1` + SceneFX `wlr_scene_set_gamma_control_manager_v1` (`server.c`). Schedule stays Noctalia-side. |
| Power confirm / power menu | 🚧 `confirm_power` | Dedicated power menu 📋 | ❌ | |
| Control center panel | ❌ | Ideas 📋 | 🌙 | Noctalia-shaped |
| Hold-modifier cheatsheet | ❌ | Ideas + `modifiers_watch` 📋 | ❌ | |
| Clipboard history panel | ❌ | Ideas 📋 | ❌ | |
| Color picker | ❌ | Ideas 📋 | ❌ | |
| Dock with magnification | ❌ | Ideas 📋 | ❌ | |
| Desktop widgets | ❌ | Ideas 📋 | ❌ | |
| Blur behind bar/OSD | ❌ | Needs compositor blur Ideas 📋 | 🚧 | SceneFX can; chrome is external |

---

## 4. D-Bus / session

| Feature | Peeble shipped | Peeble roadmap | Stonefly | Notes |
|---|---|---|---|---|
| `UpdateActivationEnvironment` after display | ✅ | — | ✅ | In-process sd-bus (was shell `dbus-update-activation-environment --all`). Ghostty `+new-window` path |
| FDO Notifications client (config warn) | ✅ | — | ✅ | sd-bus `Notify`; wait ≤20s on `NameOwnerChanged` for `org.freedesktop.Notifications` (Noctalia via `-s`), else log + bail toast-only |
| FDO Notifications server | ✅ | Live unread badge 📋 | 🌙 | Noctalia owns the server |
| logind power + PrepareForSleep | ✅ | — | 🌙 | Compositor exposes lock + idle + DPMS; Noctalia owns PrepareForSleep / lock-before-suspend |
| ScreenSaver Inhibit | ✅ | — | ❌ | |
| UPower kbd / power profiles | ✅ | Richer battery 📋 | ❌ / 🌙 | |
| Network / BT / MPRIS / weather status | ✅ | Real SSID + proxy 📋 | 🌙 | |
| Portal FileChooser (wallpaper) | ✅ | Wallpaper URI / Settings theme 📋 | ❌ | |
| Systray StatusNotifierItem | ❌ | 📋 largest chrome gap | ❌ / 🌙 | |

---

## 5. Protocols & I/O

| Feature | Peeble shipped | Peeble roadmap / FORK | Stonefly | Notes |
|---|---|---|---|---|
| xdg-shell toplevels / popups | ✅ (via River) | — | ✅ | `request_minimize` → `stonefly_toplevel_minimize` / unminimize (CSD buttons). |
| wlr-layer-shell | ✅ client | `set_exclusive_edge` when advertised 📋 | ✅ v5 server | Exclusive zones → usable. `set_exclusive_edge` consumed by wlroots 0.20 `wlr_scene_layer_surface_v1_configure` via `get_exclusive_edge` (no stonefly-side extra). |
| wp_fractional_scale + viewporter | ✅ all chrome (`surfscale`) | — | ✅ | DRM: scale 1.5 (`STONEFLY_OUTPUT_SCALE`). Nested/headless (`session == NULL`): scale 1.0 so Umbriel embed is not double-scaled. |
| zxdg_output_v1 | ✅ | — | ✅ | `wlr_xdg_output_manager_v1` on output layout (`server.c`). Logical size for Noctalia floating-panel margins under fractional scale. |
| wp_cursor_shape_v1 | ✅ | `all_resize` / `dnd_ask` 📋 | ✅ v2 | Named shapes for clients |
| XCURSOR_THEME / SIZE | ✅ | — | ✅ | `seat.c`: strdup theme before `setenv(XCURSOR_SIZE)` (getenv ptr invalid after setenv). |
| wlr-output-management | ✅ `output()` | — | ✅ | `wlr_output_manager_v1` apply/test (`output.c`); `wlr_output_head_v1_state_apply` + layout pos; `set_configuration` on new/destroy/apply/request_state. |
| ext-data-control clipboard | ✅ | Primary-selection history Ideas 📋 | ✅ | `ext-data-control-v1` + legacy `wlr-data-control-v1` + `wp_primary_selection` (`server.c` / seat). |
| Screenshots (fullscreen/window/region) | ✅ ext-image-copy-capture | Include-cursor polish 📋 | 🌙 / ✅ | Print binds → noctalia. `ext-image-copy-capture` + toplevel capture (`ext_capture.c`) + `zwlr_screencopy_manager_v1` (`server.c`). Capture privacy (#1481) ✅ for toplevel image-capture. |
| Screencast / capture_sessions | ✅ bar widget | — | ❌ | |
| Foreign-toplevel management | 🚧 River read-only; bar uses Focus | Activate relay #1449/#1537 📋 | ✅ | `zwlr_foreign_toplevel_management_v1` v3 (`foreign.c`) + parallel `ext-foreign-toplevel-list-v1` (`ext_capture.c`) for capture handles |
| xdg-activation (urgency) | ❌ | #1281 📋 | ✅ | |
| xdg-system-bell | ❌ | #1389 📋 | ✅ | |
| Idle-inhibit / idle-notify / session-lock | ✅ | — | ✅ | `session/session.c`: ext-session-lock-v1, ext-idle-notify-v1, zwp-idle-inhibit-v1, zwlr-output-power-management-v1 |
| Gamma control | ✅ | — | ✅ | `zwlr_gamma_control_v1` + SceneFX scene hook (`server.c`) |
| Xwayland | ✅ (River host) | — | ❌ | Classic X11 apps fail |
| ext-workspace-v1 | ❌ | #1402 📋 | ✅ | Per-output group + per-workspace handle; activate/create commit → switch/compact (`workspace.c`) |
| xdg-toplevel-tag | ❌ | #1390 📋 | ✅ | `wlr_xdg_toplevel_tag_manager_v1` (`xdg.c`); stored on toplevel; shown in IPC `windows` (`tag` / `description`). No rules/persist consumer yet. |
| Touch / gestures / virtual-keyboard | ❌ | River 0.5 items 📋 | ✅ | Touch seat caps + cursor→`wlr_seat_touch_notify_*` (`seat.c`). Gestures: `pointer_gestures_v1` + 3-finger workspace swipe (`swipe_workspace_natural`). `zwp_virtual_keyboard_v1` (attach like keyboard; compositor binds skipped). Also: `zwp_relative_pointer_manager_v1` + `zwp_pointer_constraints_v1` (lock/confine + cursor hint). |
| Presentation-time / fifo / tearing | ❌ | Shared frame clock Ideas 📋 | 🚧 | ✅ `wp_presentation` via `wlr_presentation_create(display, backend)` (`server.c`; SceneFX samples). ✅ `wp_tearing_control_v1` advertised (hint log only; no async page-flip). ❌ `wp_fifo_v1` still open (no simple wlroots helper). |

---

## 6. Config, IPC, tooling

| Feature | Peeble shipped | Peeble roadmap / Ideas | Stonefly | Notes |
|---|---|---|---|---|
| Scriptable config | ✅ Risor + live reload | Multi-file include design 📋 | ✅ | LuaJIT in-process VM (§10): Neovim-ish rtp/packpath, builtin fennel-shim (Fennel 1.6.1), Fennel-first embedded default. Open gaps in §10. |
| Validate / write-default | ✅ `--write-config` + embed | — | 🚧 | ✅ `stonefly --write-config` (C23 `#embed` tree, refuse overwrite); validate CLI still ❌ (§10). **Build:** meson `embed_defaults_stamp.h` so default `*.fnl` edits rebuild `config_embed.c` (clang often omits `#embed` from depfiles — shipped chord parser + stale old binds once). |
| Nano syntax highlight | ✅ `peeble.nanorc` | — | ❌ | |
| Keybind table | ✅ Risor `"Super+w"` chords + taps | — | ✅ | Peeble-style chords: `stonefly.bind("Super+w", {action=…})` / `tap("Super", …)`; `xkb_keysym_from_name` (no generated keysym table). Legacy `{mods=, key=}` still accepted. Shift+Tab auto `ISO_Left_Tab`. C defaults until first bind. |
| Ctrl+Alt+Fn VT switch | ✅ via session | — | ✅ | `wlr_session_change_vt` on `XF86Switch_VT_*`; was missing (unreliable switch + DRM yank / crash on half-leave). Nested backends: no-op. |
| Super tap / double-tap | ✅ | Regression keep-green 📋 | ✅ | Launcher / term pad |
| Spawn / exec / exec_once | ✅ | `spawn_one` panels QoL 📋 | 🚧 | Named SPAWN_* + `STONEFLY_ACTION_EXEC` (`sh -c`); no exec_once |
| IPC `windows` [--json] | ✅ | — | ✅ | Peeble wire shape; reply cap is `size_t` (`ipc.c` tidy) |
| IPC `layers` [--json] | ❌ | — | ✅ | Layer-shell debug snapshot (`src/ipc/layers.c`) |
| IPC focus / close by id | ✅ | — | ✅ | `{"cmd":"focus\|close","arg":"<id>"}` + CLI (`ipc.c`) |
| IPC toggle-quick-terminal | ✅ | — | ✅ | Toggles scratchpad pad `"term"` (same as Super+Return) |
| IPC `config` [section] | ❌ | — | ✅ | Live `server->config` dump (`ipc/config.c`); sections options/binds/taps/scratchpads/cmds/inputs/outputs; argv parse de-cloned (`ipc.c`) |
| IPC `action <name>` allowlist | ❌ | 📋 | ❌ | |
| IPC outputs / inputs | ❌ | Ideas 📋 | 🚧 | `outputs` ✅ (`ipc/outputs.c`); inputs ❌ |
| Structured JSON logs | ✅ `~/.var/log/peeble/wm.log` | — | ✅ | `wm.log` + `wm.last.json` rotate |
| APK release buildtype | ✅ | — | 🚧 | **TEMP:** clang `debugoptimized` + ASan/UBSan (`!strip`); `compiler-rt` in makedepends; release `abuild-meson --buildtype=release` kept commented until crash hunting ends |
| APK / meson `check` | ✅ Go unit tests | — | 🚧 | `check()` runs `make style-check` (fmt-check/cppcheck/tidy). `ipc.c` tidy findings cleared (signed-bitwise, widening, atoi→strtol, ArrayBound, branch-clone). Still no `meson test` suite; pure-helper tests remain a reasonable next gate. |
| Exit CPU/heap profiles | ✅ | — | ❌ | |

---

## 7. FORK.md priority map → stonefly

River-fork brief items and where stonefly stands (no River needed).

| Priority | Item | Peeble today | Stonefly |
|---|---|---|---|
| P0 | #1385 vsynced animations / `render_dirty` | Busy loop workaround | ✅ owned |
| P0 | Map-hold / no resting flash | AppearHold Hide hack | ✅ slide from off-screen |
| P0 | #1449 / #1537 foreign-toplevel activate | Blocked on River | ✅ `zwlr_foreign_toplevel_management_v1` |
| P0 | #1375 nodes vs layer-shell z-order | PlaceTop fights | ✅ scene layers |
| P1 | #1373 WM opacity | Blocked | 🚧 global focused + inactive opacity |
| P1 | Close-ghost last-frame | Blocked for client close | ✅ |
| P1 | #1281 urgency / xdg-activation | Blocked | ✅ |
| P1 | #1389 system bell | Blocked | ✅ |
| P1 | #1481 capture privacy | Blocked | ✅ |
| P2 | #1402 workspaces, #1390 tags | Roadmap | 🚧 | ✅ `xdg-toplevel-tag` + ✅ dynamic workspaces + ✅ `ext-workspace-v1`; ❌ rules / geometry persist |
| P2 | Layer `set_exclusive_edge`, presentation-time | Roadmap | ✅ exclusive_edge via scene configure; ✅ presentation-time; ❌ fifo |
| P3 | SceneFX blur / corners / shadows | Parked | 🚧 blur on (wallpaper-backed) / ✅ corners / ✅ shadows |

---

## 8. Day-zero parity snapshot (WM only)

What a peeble daily driver expects from **window policy**, ignoring
in-process shell (Noctalia stands in under stonefly).

| Peeble habit | Stonefly |
|---|---|
| Super+Tab focus; Alt+Tab → Noctalia switcher | ✅ |
| Super+q close | ✅ |
| Super+f / Super+m max | ✅ |
| Super+arrows snap + Super+u | ✅ |
| Super+Page / Super+1–9 workspaces; Shift moves follow | ✅ |
| 3-finger vertical swipe workspaces (`swipe_workspace_natural`) | ✅ |
| Super+LMB/RMB move/resize + magnetic snap + preview | ✅ |
| Ctrl+Esc minimize LIFO | ✅ |
| Super tap launcher / double-tap term / Super+t claim | ✅ |
| Spawns Ghostty / foot / nautilus / editor / QT | ✅ |
| Named scratchpads beyond `term` | ❌ |
| Fullscreen / always-on-top / winmenu / SSD | ✅ fullscreen / ❌ aot / ❌ winmenu / ✅ SSD |
| Multi-mon send/focus, pointer center | ❌ |
| Geometry persist, window rules, set_tiled | ❌ persist/rules / ✅ set_tiled |
| Screenshots, clipboard, idle/lock/gamma | 🌙 noctalia binds + ✅ screencopy/ext-capture / ✅ idle+lock+DPMS / ✅ clipboard+gamma |
| Xwayland | ❌ |
| Real config file | ✅ LuaJIT + Fennel-first embed + `--write-config` + IPC `config`; open items in §10 |
| Workspaces / tiling layouts | 🚧 workspaces (stacking) / ❌ tiling engines |
| Foreign-toplevel for docks/taskbars | ✅ |

---

## 9. Suggested stonefly attack order (from gaps)

Aligned with stonefly README “remaining FORK gaps”, peeble day-zero holes,
and **Noctalia coexistence** (what the shell needs from the compositor).

### Already enough for day-zero Noctalia chrome

layer-shell, foreign-toplevel v3, fractional-scale, viewporter, xdg-output,
cursor-shape, xdg-activation, system-bell. Bar / dock / launcher / notifier
work on these (launcher centering needs xdg-output under fractional scale).

### Protocol priorities (Noctalia coexistence)

| Pri | Protocol / surface | Noctalia use |
|---|---|---|
| P0 | `ext-session-lock-v1` | ✅ Lock screen (`session/session.c`) |
| P0 | `ext-workspace-v1` **or** stonefly IPC workspaces | ✅ both — `ext-workspace-v1` + IPC `workspaces` / `workspace` / `move-to-workspace` |
| P1 | `ext-foreign-toplevel-image-capture-source-v1` | ✅ Window-switcher thumbnails (`ext_capture.c`; was icons-only without it) |
| P1 | `zwlr_screencopy` / `ext-image-copy-capture` | ✅ both (`server.c` screencopy + `ext_capture.c`); ✅ capture privacy (#1481) for toplevel image-capture |
| P1 | `ext-idle-notify` + idle-inhibit | ✅ Idle dim / caffeine / lock triggers (`session/session.c`) |
| P1 | `zwlr-output-power-management-v1` | ✅ DPMS on/off for idle / lock (`session/session.c`) |
| P2 | `zwlr_gamma_control` / color-management | ✅ Gamma LUT (`server.c` + SceneFX); full color-mgmt still open |
| P2 | `zxdg_output` | ✅ Logical output size/pos (`server.c`); Noctalia panel/OSD centering |
| P2 | `zwlr_output_management` | ✅ Display settings / wdisplays / kanshi (`output.c` manager apply/test + set_configuration) |
| P2 | `ext-data-control` / wlr-data-control | ✅ Clipboard history (`server.c`); primary-selection wired |
| P3 | xdg-decoration | ✅ Manager + SSD: honor client `request_mode` within `ssd_mode` (`auto` default); fcft titlebar when SERVER_SIDE. **Fixed:** defer `set_mode` until `xdg_surface` initialized (Chrome early bind aborted wlroots 0.20) |

Also high leverage (not a Wayland protocol): richer **stonefly IPC**.
Done crumbs: focus/close by id, outputs, workspaces, toggle-quick-terminal, live
`config` dump. Still open: action allowlist / inputs.

### Broader WM gaps (after / beside the above)

1. **Xwayland** (classic apps)
2. **Capture privacy** (#1481) — ✅ compositor policy (IPC + `capture_block_app_ids`); shell UI still optional
3. **Real config** — ✅ shipped; remaining work listed in §10.
4. Optional: always-on-top, multi-mon send/focus; tags protocol ✅ (rules/persist still open); `wp_fifo_v1` still open

Seat-side day-zero: ✅ touch, virtual-keyboard, relative-pointer, pointer-constraints
(`seat.c`). Still open among input: tablet.

Shell/OSD/media stay **🌙 Noctalia** unless product asks for in-process
chrome again.

---

## 10. Config — shipped vs open

**Shipped:** LuaJIT linked in-process (`config_lua.c`); XDG dirs + `STONEFLY_*`
overrides; C23 `#embed` of Fennel-first default tree (`src/config/default/`);
missing user `init.lua` → embedded default; broken user init → log + fall back
to embedded/C and FDO `Notify` (urgency=critical) after display up — subscribe
to `NameOwnerChanged` for `org.freedesktop.Notifications`, wait ≤20s (Noctalia
usually via `-s`), else warn and skip the toast; `stonefly --write-config` (refuse overwrite); builtin pack
`fennel-shim` (Fennel 1.6.1 + Fennel-side loader); `stonefly.*` API (`opt` /
`bind` / `tap` / `scratchpad` / `packadd` / `stdpath`); bind/tap accept
peeble-style chord strings (`"Super+Shift+Tab"`, `"Super"`) via `parse_chord` /
`parse_tap_mod` in `config_lua.c` (libxkbcommon name lookup — no Go-style
generated keysym map); IPC `stonefly config [section]`; consumers read
`stonefly_cfg(server)`.

| Open | Status | Notes |
|---|---|---|
| Live reload mid-session | ❌ | Peeble has it; stonefly loads once at init |
| Config validate CLI | ❌ | peeble-style check without applying |
| `--write-config-lua` | ❌ | Offload compiled/plain Lua instead of `.fnl` |
| `sf_*` / `sf.` rename | ❌ | Shorter C + Lua/Fennel identifiers; binary name stays `stonefly` |
| `inputs.fnl` / `outputs.fnl` APIs | 🚧 | Files are no-op stubs; no input/output policy surface yet. Output `:layout` override planned (resolve workspace→output→global; global `opt.layout` already wired) |
| IPC `config` inputs/outputs sections | 🚧 | Emit `{}` until those APIs exist |
| `exec_once` / session spawn helpers | ❌ | Named SPAWN_* + `EXEC` only |
| Neovim API compat / fake `vim.*` | ❌ | Non-goal |
| Fennel as C dependency | ❌ | Non-goal (Fennel is Lua in the pack) |
| Remote plugin fetch | ❌ | Non-goal for v1 |
| Static-link LuaJIT (drop `luajit` runtime dep) | ❌ | Optional packaging choice later |

Entry points: `src/config/`, `runtime/pack/dist/opt/fennel-shim/`,
`stonefly --write-config`, `stonefly config`.

---

## Legend for future edits

When a row moves, update **both** the chapter table and §7/§8 if it is a
FORK or day-zero item. Prefer linking river issue numbers for peeble-blocked
rows and stonefly source paths (`src/wm/…`) for compositor-owned rows.
