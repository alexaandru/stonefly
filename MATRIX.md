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
| Config | `config.risor` + live reload | `src/config/hardcoded.h` only |
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
| Named scratchpads | ✅ multi + claim + auto_hide | Click-outside catcher 📋 | 🚧 | Hardcoded pad `term` only. Spawn uses fake `app_id` (`stonefly.scratch.term`) so claim does not steal normal Super+w windows — that breaks taskbar icons (shell keys icons off app_id). Real app_id + PID claim is not enough for single-instance multi-window clients (one Ghostty PID, many windows). Proper fix later: xdg-activation / startup token (or accept first-map races); leave icon/claim as-is for now. Hide/show via `apply_hidden(SCRATCH)` / `apply_shown` (same foreign/defocus as LIFO; `focus_top` skips `anim_hide`). |
| Scratchpad claim focused | ✅ Super+t | — | ✅ | |
| Scratchpad omit from Alt-Tab | ✅ | — | ✅ | Super+Tab too |
| Window menu | ✅ | — | ❌ | |
| SSD titlebars | ✅ River `use_ssd` | Polish / force-SSD rules 📋 | 🚧 | `xdg-decoration` advertised, prefers CSD. SceneFX border/blur only; no SSD titlebar paint yet. **Fixed:** Chrome binds deco before first commit; early `set_mode` → `schedule_configure` asserted `surface->initialized` (session abort). Now defer CSD until initialized / initial commit. |
| Geometry persist across restart | ✅ `windows.json` by river id | Schema / blacklist / MRU polish 📋 | ❌ | |
| Window rules (app-id/title) | 🚧 center + modal parent | auto-float, size, scratchpad, AOT, force SSD 📋 | ❌ | |
| Keyboard nudge move/resize | ❌ | 📋 high-value | ❌ | Snap exists; step nudge does not |
| Workspaces / tags | ❌ | 📋 + river#1402 / #1390 | ❌ | FORK gap |
| Layout engine (tile/columns/…) | ❌ | 📋 | ❌ | |
| Focus cycle policy (MRU vs stack) | 🚧 stacking default | Decide MRU Alt+Tab 📋 | 🚧 | Stacking / MRU list front |
| Multi-monitor focus / send | ✅ `focus_monitor` / `send_to_monitor` | — | ❌ | |
| Pointer center / cursor-follows-focus | ✅ | — | ❌ | |
| Picture-in-picture float | ❌ | Ideas 📋 | ❌ | |
| Dim inactive / spotlight | ❌ | Ideas 📋 (needs opacity) | 🚧 | Flat opacity 0.96 (`STONEFLY_CLIENT_OPACITY`); no dim-unfocused policy |

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
| WM-settable per-window opacity | ❌ | river#1373 P1 📋 | 🚧 | Global `STONEFLY_CLIENT_OPACITY` 0.96; no per-window API |
| Backdrop / window blur | ❌ | SceneFX P3 📋 | 🚧 | Optimized blur node exists; per-window blur off (`STONEFLY_WINDOW_BLUR 0`) |
| Soft rounded corners (opaque) | ❌ | SceneFX P3 📋 | ✅ | 12px |
| Drop shadows | ❌ | SceneFX P3 📋 | ✅ | |
| Compositor border (focus/idle/urgent) | ✅ via River `SetBorders` | Urgency needs #1281 | ✅ SceneFX | Neon / charcoal inactive / amber. **Fixed:** `STONEFLY_BORDER_*` were compound literals; pointers into them across `else if` blocks → ASan `stack-use-after-scope` in `border_rgba_premul` on first map/focus (boot loop). Now `static const float[4]`. |
| Layer z-order vs windows | 🚧 PlaceTop fights | river#1375 P0 📋 | ✅ | Scene trees background→overlay; xdg under `layers.toplevel` |
| Window previews / destination size | ❌ | river#1384 📋 | ✅ | Noctalia switcher via `ext-foreign-toplevel-list` + image-capture-source + image-copy-capture (`ext_capture.c`); frozen snapshot per open |
| Urgency / xdg-activation | ❌ | river#1281 → border/badge 📋 | ✅ | Amber until focus |
| System bell | ❌ | river#1389 📋 | ✅ | 200ms amber flash |
| Capture privacy (block window) | ❌ | river#1481 📋 | ❌ | |

---

## 3. Shell chrome

Under stonefly, day-zero desktop chrome is **Noctalia** (bar, launcher,
notifier) on layer-shell. Peeble owns these in-process.

| Feature | Peeble shipped | Peeble roadmap / Ideas | Stonefly | Notes |
|---|---|---|---|---|
| Status bar | ✅ rich widgets | Taskbar v2, overflow modes, FuncMap 📋 | 🌙 | Noctalia |
| Taskbar / app icons | ✅ | Middle-click close, group-by-app, urgency badge 📋 | 🌙 | Scratch `term` placeholder icon — see Named scratchpads. **Crash:** taskbar tooltip `xdg_popup` with no parent scene tree. Null-check alone insufficient — `wlr_xdg_popup_destroy` inside `new_popup` still SEGVs in wlroots. Now parents orphans to `layers.overlay` (idle-destroy only on OOM). |
| Unread title-regex badges | ✅ Slack / Gmail | Live notify unread semantics 📋 | 🌙 | |
| Launcher | ✅ fuzzy + plugins | Grid / categories / emoji Ideas 📋 | 🌙 | Super tap → `noctalia msg panel-toggle launcher` |
| OSD cards (vol/bright/…) | ✅ | Richer network OSD 📋 | 🌙 | XF86* / media / mic / brightness binds → `noctalia msg` |
| Notification toasts | ✅ FDO server | Notification center UI Ideas 📋 | 🌙 | Noctalia notifier |
| Wallpaper engine | ✅ slideshow + transitions + cache | Video/shader Ideas 📋 | 🌙 / ❌ | Layer-shell wallpaper clients OK; no in-process engine |
| Window snap preview (WM) | ✅ | — | ✅ | In compositor (not shell) |
| SSD / winmenu chrome | ✅ | — | ❌ | |
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
| `dbus-update-activation-environment` after display | ✅ | — | ✅ | Ghostty `+new-window` path |
| FDO Notifications server | ✅ | Live unread badge 📋 | 🌙 | |
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
| wlr-layer-shell | ✅ client | `set_exclusive_edge` when advertised 📋 | ✅ v5 server | Exclusive zones → usable |
| wp_fractional_scale + viewporter | ✅ all chrome (`surfscale`) | — | ✅ | DRM: scale 1.5 (`STONEFLY_OUTPUT_SCALE`). Nested/headless (`session == NULL`): scale 1.0 so Umbriel embed is not double-scaled. |
| zxdg_output_v1 | ✅ | — | ✅ | `wlr_xdg_output_manager_v1` on output layout (`server.c`). Logical size for Noctalia floating-panel margins under fractional scale. |
| wp_cursor_shape_v1 | ✅ | `all_resize` / `dnd_ask` 📋 | ✅ v2 | Named shapes for clients |
| XCURSOR_THEME / SIZE | ✅ | — | ✅ | |
| wlr-output-management | ✅ `output()` | — | ❌ | Hardcoded scale; no wdisplays path |
| ext-data-control clipboard | ✅ | Primary-selection history Ideas 📋 | ✅ | `ext-data-control-v1` + legacy `wlr-data-control-v1` + `wp_primary_selection` (`server.c` / seat). |
| Screenshots (fullscreen/window/region) | ✅ ext-image-copy-capture | Include-cursor polish 📋 | 🌙 / ✅ | Print binds → noctalia. `ext-image-copy-capture` + toplevel capture (`ext_capture.c`) + `zwlr_screencopy_manager_v1` (`server.c`). Capture privacy (#1481) still open. |
| Screencast / capture_sessions | ✅ bar widget | — | ❌ | |
| Foreign-toplevel management | 🚧 River read-only; bar uses Focus | Activate relay #1449/#1537 📋 | ✅ | `zwlr_foreign_toplevel_management_v1` v3 (`foreign.c`) + parallel `ext-foreign-toplevel-list-v1` (`ext_capture.c`) for capture handles |
| xdg-activation (urgency) | ❌ | #1281 📋 | ✅ | |
| xdg-system-bell | ❌ | #1389 📋 | ✅ | |
| Idle-inhibit / idle-notify / session-lock | ✅ | — | ✅ | `session/session.c`: ext-session-lock-v1, ext-idle-notify-v1, zwp-idle-inhibit-v1, zwlr-output-power-management-v1 |
| Gamma control | ✅ | — | ✅ | `zwlr_gamma_control_v1` + SceneFX scene hook (`server.c`) |
| Xwayland | ✅ (River host) | — | ❌ | Classic X11 apps fail |
| ext-workspace-v1 | ❌ | #1402 📋 | ❌ | |
| xdg-toplevel-tag | ❌ | #1390 📋 | ❌ | |
| Touch / gestures / virtual-keyboard | ❌ | River 0.5 items 📋 | ❌ | |
| Presentation-time / fifo / tearing | ❌ | Shared frame clock Ideas 📋 | 🚧 | Own vsync; no protocol export |

---

## 6. Config, IPC, tooling

| Feature | Peeble shipped | Peeble roadmap / Ideas | Stonefly | Notes |
|---|---|---|---|---|
| Scriptable config | ✅ Risor + live reload | Multi-file include design 📋 | 🚧 | Spec: [`docs/CONFIG.md`](docs/CONFIG.md) — LuaJIT + Neovim rtp/packpath; first pack = fennel-shim from `fennel-nvim/`. Consumers use `stonefly_cfg(server)` / `server->config`; `hardcoded.h` is a shim. |
| Validate / write-default | ✅ `--write-config` + embed | — | 🚧 | C23 `#embed` Fennel-first tree + `stonefly --write-config` (refuse overwrite); validate CLI still open |
| Nano syntax highlight | ✅ `peeble.nanorc` | — | ❌ | |
| Keybind table | ✅ Risor binds + taps | — | ✅ | Compile-time table |
| Ctrl+Alt+Fn VT switch | ✅ via session | — | ✅ | `wlr_session_change_vt` on `XF86Switch_VT_*`; was missing (unreliable switch + DRM yank / crash on half-leave). Nested backends: no-op. |
| Super tap / double-tap | ✅ | Regression keep-green 📋 | ✅ | Launcher / term pad |
| Spawn / exec / exec_once | ✅ | `spawn_one` panels QoL 📋 | 🚧 | Named SPAWN_* + `STONEFLY_ACTION_EXEC` (`sh -c`); no exec_once |
| IPC `windows` [--json] | ✅ | — | ✅ | Peeble wire shape |
| IPC `layers` [--json] | ❌ | — | ✅ | Layer-shell debug snapshot (`src/ipc/layers.c`) |
| IPC focus / close by id | ✅ | — | ✅ | `{"cmd":"focus\|close","arg":"<id>"}` + CLI (`ipc.c`) |
| IPC toggle-quick-terminal | ✅ | — | ✅ | Toggles scratchpad pad `"term"` (same as Super+Return) |
| IPC `config` [section] | ❌ | — | ✅ | Live `server->config` dump (`ipc/config.c`); sections options/binds/taps/scratchpads/cmds/inputs/outputs |
| IPC `action <name>` allowlist | ❌ | 📋 | ❌ | |
| IPC outputs / inputs | ❌ | Ideas 📋 | 🚧 | `outputs` ✅ (`ipc/outputs.c`); inputs ❌ |
| Structured JSON logs | ✅ `~/.var/log/peeble/wm.log` | — | ✅ | `wm.log` + `wm.last.json` rotate |
| APK release buildtype | ✅ | — | 🚧 | **TEMP:** clang `debugoptimized` + ASan/UBSan (`!strip`); `compiler-rt` in makedepends; release `abuild-meson --buildtype=release` kept commented until crash hunting ends |
| APK / meson `check` | ✅ Go unit tests | — | ❌ | `options="!check"` today (common for early wlroots WMs). Reasonable first gate: `meson test` on pure helpers (json, anim easing, drag-snap geom, IPC JSON round-trip) — peeble-style, no DRM. Optional later: headless/`WLR_BACKENDS=headless` smoke start+exit. Not: full protocol suites or live Noctalia/eDP tests in abuild. |
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
| P1 | #1373 WM opacity | Blocked | 🚧 global opacity |
| P1 | Close-ghost last-frame | Blocked for client close | ✅ |
| P1 | #1281 urgency / xdg-activation | Blocked | ✅ |
| P1 | #1389 system bell | Blocked | ✅ |
| P1 | #1481 capture privacy | Blocked | ❌ |
| P2 | #1402 workspaces, #1390 tags | Roadmap | ❌ |
| P2 | Layer `set_exclusive_edge`, presentation-time | Roadmap | 🚧 exclusive zones yes; edge/proto clock no |
| P3 | SceneFX blur / corners / shadows | Parked | 🚧 blur off (`WINDOW_BLUR 0`) / ✅ corners / ✅ shadows |

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
| Super+LMB/RMB move/resize + magnetic snap + preview | ✅ |
| Ctrl+Esc minimize LIFO | ✅ |
| Super tap launcher / double-tap term / Super+t claim | ✅ |
| Spawns Ghostty / foot / nautilus / editor / QT | ✅ |
| Named scratchpads beyond `term` | ❌ |
| Fullscreen / always-on-top / winmenu / SSD | ✅ fullscreen / ❌ aot / ❌ winmenu / ❌ SSD |
| Multi-mon send/focus, pointer center | ❌ |
| Geometry persist, window rules, set_tiled | ❌ persist/rules / ✅ set_tiled |
| Screenshots, clipboard, idle/lock/gamma | 🌙 noctalia binds + ✅ screencopy/ext-capture / ✅ idle+lock+DPMS / ✅ clipboard+gamma |
| Xwayland | ❌ |
| Real config file | 🚧 LuaJIT + Fennel-first embed (`docs/CONFIG.md`); rebuild to verify |
| Workspaces / tiling layouts | ❌ both |
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
| P0 | `ext-workspace-v1` **or** stonefly IPC workspaces | Workspace indicator / switcher (else compositor-specific backends) |
| P1 | `ext-foreign-toplevel-image-capture-source-v1` | ✅ Window-switcher thumbnails (`ext_capture.c`; was icons-only without it) |
| P1 | `zwlr_screencopy` / `ext-image-copy-capture` | ✅ both (`server.c` screencopy + `ext_capture.c`); capture privacy still open |
| P1 | `ext-idle-notify` + idle-inhibit | ✅ Idle dim / caffeine / lock triggers (`session/session.c`) |
| P1 | `zwlr-output-power-management-v1` | ✅ DPMS on/off for idle / lock (`session/session.c`) |
| P2 | `zwlr_gamma_control` / color-management | ✅ Gamma LUT (`server.c` + SceneFX); full color-mgmt still open |
| P2 | `zxdg_output` | ✅ Logical output size/pos (`server.c`); Noctalia panel/OSD centering |
| P2 | `zwlr_output_management` | Display settings / wdisplays (scale still hardcoded) |
| P2 | `ext-data-control` / wlr-data-control | ✅ Clipboard history (`server.c`); primary-selection wired |
| P3 | xdg-decoration | ✅ Manager advertised; always prefers **client-side** (no SSD painting yet). **Fixed:** defer `set_mode` until `xdg_surface` initialized (Chrome early bind aborted wlroots 0.20) |

Also high leverage (not a Wayland protocol): richer **stonefly IPC**.
Done crumbs: focus/close by id, outputs, toggle-quick-terminal, live `config`
dump. Still open: workspaces / action allowlist / inputs.

### Broader WM gaps (after / beside the above)

1. **Xwayland** (classic apps)
2. **Capture privacy** (#1481) — screencopy exists; policy UI still missing
3. **Real config** — 🚧 in tree: LuaJIT + `#embed` Fennel-first default + fennel-shim 1.6.1 (`docs/CONFIG.md`); abuild verify next
4. Optional: always-on-top, multi-mon send/focus, tags (#1390)

Shell/OSD/media stay **🌙 Noctalia** unless product asks for in-process
chrome again.

---

## Legend for future edits

When a row moves, update **both** the chapter table and §7/§8 if it is a
FORK or day-zero item. Prefer linking river issue numbers for peeble-blocked
rows and stonefly source paths (`src/wm/…`) for compositor-owned rows.
