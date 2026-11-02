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
| Focus next / prev including minimized | ✅ Alt+Tab | — | ✅ | Unhides on stonefly |
| Close focused | ✅ | — | ✅ Super+q | |
| Maximize to edges | ✅ | — | ✅ Super+f | Client maximized hint |
| Maximize with gaps (WM-only) | ✅ | — | ✅ Super+m | No client maximized → rounder CSD |
| Unmaximize / restore | ✅ | — | ✅ | Toggle paths |
| Fullscreen (client + WM) | ✅ | — | ✅ | Saved geom + full output box; client `request_fullscreen` + foreign set/unset (no WM keybind; Super+f is edge-max) |
| Minimize / unminimize LIFO | ✅ Ctrl+Esc | — | ✅ | Unnamed scratchpad |
| Always-on-top | ✅ | — | ❌ | Peeble winmenu / state |
| Keyboard half-snap (arrows) | ✅ Super+arrows | — | ✅ | Gap math ports `snapBorderBox` |
| Unsnap / restore PreSnap | ✅ Super+u | — | ✅ | |
| Drag move (Super+LMB) | ✅ | — | ✅ | |
| Drag resize (Super+RMB) | ✅ | — | ✅ | Edges nearest pointer |
| Magnetic drag-snap + resistance | ✅ threshold 26, 4× push-through | — | ✅ | `drag_snap.c` parity |
| Snap preview overlay | ✅ SHM layer-shell | Configurable fill color/opacity 📋 | ✅ SceneFX overlay rects | Border 1, fill 40% active |
| `set_tiled` on snap/max | ✅ | — | ❌ | Out of day-zero scope |
| Cascade initial place | ✅ | — | ✅ | Gap + `(n%10)*25`, cursor output |
| EnsureOnScreen / usable relayout | ✅ | — | ✅ | On exclusive-zone change |
| Per-output usable area | ✅ `UsableOn` / `UsableAreaChanged` | — | ✅ | `exclusive_zone != 0` (incl. −1) |
| Named scratchpads | ✅ multi + claim + auto_hide | Click-outside catcher 📋 | 🚧 | Hardcoded pad `term` only |
| Scratchpad claim focused | ✅ Super+t | — | ✅ | |
| Scratchpad omit from Alt-Tab | ✅ | — | ✅ | Super+Tab too |
| Window menu | ✅ | — | ❌ | |
| SSD titlebars | ✅ River `use_ssd` | Polish / force-SSD rules 📋 | ❌ | Stonefly draws SceneFX border/blur; clients keep CSD |
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
| Scratchpad / minimize slides | ✅ | — | ✅ | Mid-flight reverse |
| Vsync-capped animation pacing | ❌ busy manage→render | river#1385 P0 📋 | ✅ | Own scene / frame loop |
| Fade / scale appear | ❌ | river#1373 + Ideas 📋 | ❌ | Opacity static 0.96, not animated fade |
| WM-settable per-window opacity | ❌ | river#1373 P1 📋 | 🚧 | Global `STONEFLY_CLIENT_OPACITY` 0.96; no per-window API |
| Backdrop / window blur | ❌ | SceneFX P3 📋 | 🚧 | Optimized blur node exists; per-window blur off (`STONEFLY_WINDOW_BLUR 0`) |
| Soft rounded corners (opaque) | ❌ | SceneFX P3 📋 | ✅ | 12px |
| Drop shadows | ❌ | SceneFX P3 📋 | ✅ | |
| Compositor border (focus/idle/urgent) | ✅ via River `SetBorders` | Urgency needs #1281 | ✅ SceneFX | Neon / charcoal inactive / amber |
| Layer z-order vs windows | 🚧 PlaceTop fights | river#1375 P0 📋 | ✅ | Scene trees background→overlay; xdg under `layers.toplevel` |
| Window previews / destination size | ❌ | river#1384 📋 | ❌ | |
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
| Taskbar / app icons | ✅ | Middle-click close, group-by-app, urgency badge 📋 | 🌙 | |
| Unread title-regex badges | ✅ Slack / Gmail | Live notify unread semantics 📋 | 🌙 | |
| Launcher | ✅ fuzzy + plugins | Grid / categories / emoji Ideas 📋 | 🌙 | Super tap → `noctalia msg panel-toggle launcher` |
| OSD cards (vol/bright/…) | ✅ | Richer network OSD 📋 | 🌙 | XF86* / media / mic / brightness binds → `noctalia msg` |
| Notification toasts | ✅ FDO server | Notification center UI Ideas 📋 | 🌙 | Noctalia notifier |
| Wallpaper engine | ✅ slideshow + transitions + cache | Video/shader Ideas 📋 | 🌙 / ❌ | Layer-shell wallpaper clients OK; no in-process engine |
| Window snap preview (WM) | ✅ | — | ✅ | In compositor (not shell) |
| SSD / winmenu chrome | ✅ | — | ❌ | |
| Idle ceremony / lock / DPMS | ✅ | Lock polish Ideas 📋 | ❌ | |
| Caffeine (idle-inhibit) | ✅ | — | ❌ | |
| Night light (gamma) | ✅ | Schedule timer 📋 | ❌ | |
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
| logind power + PrepareForSleep | ✅ | — | ❌ | |
| ScreenSaver Inhibit | ✅ | — | ❌ | |
| UPower kbd / power profiles | ✅ | Richer battery 📋 | ❌ / 🌙 | |
| Network / BT / MPRIS / weather status | ✅ | Real SSID + proxy 📋 | 🌙 | |
| Portal FileChooser (wallpaper) | ✅ | Wallpaper URI / Settings theme 📋 | ❌ | |
| Systray StatusNotifierItem | ❌ | 📋 largest chrome gap | ❌ / 🌙 | |

---

## 5. Protocols & I/O

| Feature | Peeble shipped | Peeble roadmap / FORK | Stonefly | Notes |
|---|---|---|---|---|
| xdg-shell toplevels / popups | ✅ (via River) | — | ✅ | |
| wlr-layer-shell | ✅ client | `set_exclusive_edge` when advertised 📋 | ✅ v5 server | Exclusive zones → usable |
| wp_fractional_scale + viewporter | ✅ all chrome (`surfscale`) | — | ✅ | Output scale 1.5 hardcoded |
| wp_cursor_shape_v1 | ✅ | `all_resize` / `dnd_ask` 📋 | ✅ v2 | Named shapes for clients |
| XCURSOR_THEME / SIZE | ✅ | — | ✅ | |
| wlr-output-management | ✅ `output()` | — | ❌ | Hardcoded scale; no wdisplays path |
| ext-data-control clipboard | ✅ | Primary-selection history Ideas 📋 | ❌ | |
| Screenshots (fullscreen/window/region) | ✅ ext-image-copy-capture | Include-cursor polish 📋 | 🌙 / ❌ | Print binds → noctalia; no in-compositor screencopy / image-copy-capture |
| Screencast / capture_sessions | ✅ bar widget | — | ❌ | |
| Foreign-toplevel management | 🚧 River read-only; bar uses Focus | Activate relay #1449/#1537 📋 | ✅ | `zwlr_foreign_toplevel_management_v1` v3 (`src/toplevel/foreign.c`) |
| xdg-activation (urgency) | ❌ | #1281 📋 | ✅ | |
| xdg-system-bell | ❌ | #1389 📋 | ✅ | |
| Idle-inhibit / idle-notify / session-lock | ✅ | — | ❌ | |
| Gamma control | ✅ | — | ❌ | |
| Xwayland | ✅ (River host) | — | ❌ | Classic X11 apps fail |
| ext-workspace-v1 | ❌ | #1402 📋 | ❌ | |
| xdg-toplevel-tag | ❌ | #1390 📋 | ❌ | |
| Touch / gestures / virtual-keyboard | ❌ | River 0.5 items 📋 | ❌ | |
| Presentation-time / fifo / tearing | ❌ | Shared frame clock Ideas 📋 | 🚧 | Own vsync; no protocol export |

---

## 6. Config, IPC, tooling

| Feature | Peeble shipped | Peeble roadmap / Ideas | Stonefly | Notes |
|---|---|---|---|---|
| Scriptable config | ✅ Risor + live reload | Multi-file include design 📋 | ❌ | `hardcoded.h` knobs only |
| Validate / write-default | ✅ | — | ❌ | |
| Nano syntax highlight | ✅ `peeble.nanorc` | — | ❌ | |
| Keybind table | ✅ Risor binds + taps | — | ✅ | Compile-time table |
| Super tap / double-tap | ✅ | Regression keep-green 📋 | ✅ | Launcher / term pad |
| Spawn / exec / exec_once | ✅ | `spawn_one` panels QoL 📋 | 🚧 | Named SPAWN_* + `STONEFLY_ACTION_EXEC` (`sh -c`); no exec_once |
| IPC `windows` [--json] | ✅ | — | ✅ | Peeble wire shape |
| IPC `layers` [--json] | ❌ | — | ✅ | Layer-shell debug snapshot (`src/ipc/layers.c`) |
| IPC focus / close by id | ✅ | — | ❌ | |
| IPC toggle-quick-terminal | ✅ | — | ❌ | Bind/scratchpad covers UX |
| IPC `action <name>` allowlist | ❌ | 📋 | ❌ | |
| IPC outputs / inputs | ❌ | Ideas 📋 | ❌ | |
| Structured JSON logs | ✅ `~/.var/log/peeble/wm.log` | — | ✅ | `wm.log` + `wm.last.json` rotate |
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
| Super+Tab / Alt+Tab focus | ✅ |
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
| Geometry persist, window rules, set_tiled | ❌ |
| Screenshots, clipboard, idle/lock/gamma | 🌙 screenshots via noctalia / ❌ clipboard+idle+lock+gamma |
| Xwayland | ❌ |
| Real config file | ❌ |
| Workspaces / tiling layouts | ❌ both |
| Foreign-toplevel for docks/taskbars | ✅ |

---

## 9. Suggested stonefly attack order (from gaps)

Aligned with stonefly README “remaining FORK gaps” and peeble day-zero
holes that hurt without Noctalia covering them:

1. **`set_tiled`** on snap/max (CSD shadow/resizer parity with peeble)
2. **Xwayland** (classic apps)
3. **Screencopy / ext-image-copy-capture** (protocol; UX binds already 🌙 noctalia) + optional privacy #1481
4. **Workspaces / tags** (#1402 / #1390 shaped)
5. **Real config** (replace `hardcoded.h` — Lua/Fennel preferred over Risor in-process)
6. Optional: always-on-top, multi-mon send/focus, richer IPC, output-management, idle/lock/gamma

Shell/OSD/media stay **🌙 Noctalia** unless product asks for in-process
chrome again.

---

## Legend for future edits

When a row moves, update **both** the chapter table and §7/§8 if it is a
FORK or day-zero item. Prefer linking river issue numbers for peeble-blocked
rows and stonefly source paths (`src/wm/…`) for compositor-owned rows.
