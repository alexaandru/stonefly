# Stonefly features

Shipped capabilities in this tree. Open and partial work lives in
[`ROADMAP.md`](ROADMAP.md). When a roadmap item lands, move it here in the
same turn (feature + short notes; drop the open-row from the roadmap).

`peeble/` is historical reference only — not a product spec.

## Architecture

| | Stonefly |
|---|---|
| Role | Compositor + WM (tinywl / wlroots 0.20 seed) |
| Language | C23 (C11 fallback), Meson |
| Scene / FX | SceneFX 0.5 scene graph |
| Chrome | Layer-shell clients; day-zero → **Noctalia** |
| Config | LuaJIT + Fennel-first defaults (§10) |
| Packaging | Alpine APK via `make apk` |

---

## 1. Window management

| Feature | Notes |
|---|---|
| Stacking (no layout engine) | Stacking-first; layout engines reserved on roadmap |
| Focus next / prev (visible) | Super+Tab |
| Close focused | Super+q |
| Maximize to edges | Super+f — `maximize_to_edges` / `toggle_maximize_to_edges` (client maximized hint). Live morph (`morph_duration_ms`) |
| Maximize with gaps (WM-only) | Super+m — `maximize` / `toggle_maximize`. No client maximized → rounder CSD. Gap-max/snap content box insets by `sf_ssd_top_inset` so SSD stays below exclusive zones. Live morph |
| Unmaximize / restore | `unmaximize` clears either mode; toggles morph-restore. Interactive resize leaves max/snap flags; `float_for_resize` is for window_rule float only |
| Fullscreen (client + WM) | Saved geom + full output box; client `request_fullscreen` + foreign set/unset (no WM keybind; Super+f is edge-max) |
| Minimize / unminimize LIFO | Ctrl+Esc — unnamed scratchpad. Shared `apply_hidden`/`apply_shown` (LIFO vs scratch). `xdg request_minimize` → CSD/SSD buttons |
| Keyboard half-snap (arrows) | Super+arrows — gap math ports `snapBorderBox` |
| Unsnap / restore PreSnap | Super+u |
| Drag move (Super+LMB) | WM xcursor via `sf_seat_apply_grab_cursor`. Clear client pointer focus before set; re-assert each grab motion. SSD titlebar hover shows move face |
| Drag resize (Super+RMB) | Edges nearest pointer. Does not clear edge-max / gap-max / snap — only the dragged edge moves |
| Border/corner mouse resize | Hover + LMB on hit band (`resize_border_in` 3 / `resize_border_out` 1); edge cursors. Keep enter/motion on surface in inner band; honor implicit grab until release |
| Magnetic drag-snap + resistance | `drag_snap.c` — threshold 26, 4× push-through |
| Snap preview overlay | SceneFX overlay rects — border 1, fill 40% active |
| `set_tiled` on snap/max | `wlr_xdg_toplevel_set_tiled` on snap / edge-max / restore / unsnap; gap-max stays untiled |
| Cascade initial place | Gap + `(n%10)*25`, cursor output |
| Nested client disconnect (parent UAF) | Clear `xdg_toplevel->base->data` before destroying scene tree |
| Nested `ipc exit` teardown | `workspace_finish` destroys per-output ws scene trees before root scene destroy |
| EnsureOnScreen / usable relayout | On exclusive-zone change |
| Per-output usable area | `exclusive_zone != 0` (incl. −1) |
| Named scratchpads | `scratchpads.fnl` + `sf.scratchpad` / `scratch_toggle`/`scratch_claim`. Default ships `term` only. Fake `app_id` (`stonefly.scratch.term`) for claim vs normal windows. `auto_hide` (default true) hides when focus moves to another window on the same output |
| Scratchpad claim focused | Super+t |
| Scratchpad omit from Alt-Tab | Super+Tab too |
| SSD titlebars | `ssd_mode` (`auto`/`prefer_ssd`/`always`/`never`) + `ssd_height` (28) + `ssd_font_size` (17, fcft). Honors client `request_mode` within policy; hide on fullscreen/edge-max; TopInset in ensure-on-screen / cascade / gap-max / snap. Defer `set_mode` until `xdg_surface` initialized. Outer `corner_radii_top` on SSD; border hole punches content+SSD |
| Transient / dialog placement | Center on visible `set_parent` (+ stack above); late parent/size re-place while `auto_place`. No size-based dialog heuristic. Cleared on move/resize/max/snap |
| Window rules (app-id/title) | `sf.window_rule(match, actions [, parent])`. Match patterns via Lua `string.find`. Declarative `center`/`float`/`width`/`height`/`ssd`/`borderless`; WM `:action` shared with binds. SoftPlace appear-hold for late title. IPC `stonefly config window_rules` |
| Workspaces | Dynamic per-output strip (`workspace.c`): trailing empty, compact on empty, Super+Page/1–9 + Shift move-follow, IPC `workspaces`/`workspace`/`move-to-workspace`, windows `{output,workspace}`. `ext-workspace-v1` sync. 3-finger vertical swipe (`swipe_workspace_threshold`, `swipe_workspace_natural`). Vertical switch slide (shared by keybind / IPC / swipe; `slide_duration_ms`, 0 = instant). `xdg-toplevel-tag` stored + IPC |
| Cursor follows focus / pointer center | `sf.opt.cursor_follows_focus` (default true): warp pointer to window center on Super+Tab / workspace switch / move-follow. Click-to-focus does not warp. Action `pointer_center` warps on demand (ignores the opt). Skips MOVE/RESIZE grabs and locked pointer constraints |
| Multi-monitor focus / send | Actions `focus_monitor` / `send_to_monitor` with `:cmd` target: `left`\|`right`\|`up`\|`down`, `next`\|`prev`, or connector name (`eDP-1`). Focus warps to head center then top window (+ cursor_follows_focus). Send assigns to the target's active workspace, re-fits max/fullscreen, centers floaters, focuses + `pointer_center`. Defaults: Super+Ctrl+arrows focus, Super+Shift+arrows send |
| Keyboard nudge move / resize | Actions `nudge` / `resize_edge` with `:cmd` `left`\|`right`\|`up`\|`down`. Step from `sf.opt.nudge_step` (default 25). Resize: right/down grow, left/up shrink (top-left anchored, min 50px). No-op when max/snap/fullscreen. Defaults: Alt+arrows move, Alt+Shift+arrows resize |
| Always-on-top / pin | Flag + restack: pinned windows stay above normal ones after focus/raise; hide slides stay above pins. Actions `toggle_always_on_top` / `always_on_top` (`:cmd` true\|false). Window rule `always_on_top`. IPC `windows[].always_on_top`. Default Super+Shift+p |

---

## 2. Presentation / animations / eye candy

| Feature | Notes |
|---|---|
| Appear / map slide | Closest usable edge, 200ms ease_out; short-axis preference when several edges tie. SoftPlace `appear_pending` hold; mid-slide retarget; morph if SoftPlace centers after slide |
| Close slide (WM-initiated) | Shared `slide_offscreen` / `closest_dir` with appear, scratch, close-ghost |
| Close slide (client Ctrl+W / CSD) | Cached last-frame ghost (`close_snap`); unmap slides via `sf_anim_offscreen_for_box`. Skipped when WM hide already ran |
| Snap / unsnap / max morph | Live peeble-style morph (`morph_duration_ms`); mid-flight retarget. Fullscreen stays instant |
| Workspace switch slide | Vertical strip slide on Super+Page / 1–9, IPC `workspace`, and 3-finger swipe. Next: content up (new from below); prev: opposite. Focus + pointer follow at start. Duration `slide_duration_ms` (0 = instant). Mid-flight interrupt lands then retargets |
| Scratchpad / minimize slides | Mid-flight reverse |
| Vsync-capped animation pacing | Own scene / frame loop |
| Backdrop / window blur | Per-window SceneFX blur (`window_blur`) or client `ext-background-effect-v1` (`client_blur`). Gate: `(window_blur or client_blur) and not fullscreen`. Default `blur_xray false` |
| Soft rounded corners (opaque) | `corner_radius` 12 = CSD/hole/buffer; outer rim = `corner_radius + border_thickness` |
| Drop shadows | SceneFX |
| Compositor border (focus/idle/urgent) | Neon / charcoal inactive / amber. Static `SF_BORDER_*` float arrays (not compound literals). Hole+buffer use `corner_radius`; outer = radius+border |
| Layer z-order vs windows | Scene trees background→overlay; xdg under `layers.toplevel` |
| Window previews / destination size | Noctalia switcher via `ext-foreign-toplevel-list` + image-capture-source + image-copy-capture (`ext_capture.c`) |
| Urgency / xdg-activation | Amber until focus; never steals focus. IPC `windows.urgent` + `set-urgent` / `unset-urgent` |
| System bell | 200ms amber flash (`bell_flash_ms`); does not stick urgency |
| Capture privacy (block window) | Per-window `capture_blocked`; reject ext-image-capture accept. `opt.capture_block_app_ids`. IPC `block-capture` / `unblock-capture` |

Focused/inactive opacity knobs ship as a day-zero subset (`client_opacity` /
`inactive_opacity`, default 0.96); per-window / rules API and fade/scale
appear remain on the roadmap.

---

## 3. Shell chrome (compositor-owned + Noctalia)

Day-zero desktop chrome is **Noctalia** on layer-shell. Compositor-owned
pieces that already ship:

| Feature | Notes |
|---|---|
| Window snap preview (WM) | In compositor |
| SSD titlebars | See §1; winmenu still open |
| Idle ceremony / lock / DPMS | `ext-session-lock-v1` + `zwlr-output-power-management-v1` (`session/session.c`). Nested `server->lock` / `idle` / `output_power`. Blank retained if lock client dies |
| Caffeine (idle-inhibit) | `zwp_idle_inhibit_v1` → `ext-idle-notify-v1` inhibit |
| Night light (gamma) | `zwlr_gamma_control_v1` + SceneFX scene hook. Schedule stays Noctalia-side |

Intentionally deferred to Noctalia (🌙): status bar, taskbar, unread badges,
launcher (Super tap → `noctalia msg panel-toggle launcher`), OSD cards,
notification toasts, control center. Layer-shell wallpaper clients OK; no
in-process wallpaper engine.

---

## 4. D-Bus / session

| Feature | Notes |
|---|---|
| `UpdateActivationEnvironment` after display | In-process sd-bus (`src/dbus/dbus.c`) |
| FDO Notifications client (config warn) | sd-bus `Notify`; wait ≤20s on `NameOwnerChanged` for `org.freedesktop.Notifications`, else log + bail toast-only |

Noctalia owns the FDO Notifications server, PrepareForSleep / lock-before-suspend
policy, and most status widgets (network / BT / MPRIS / weather).

---

## 5. Protocols & I/O

| Feature | Notes |
|---|---|
| xdg-shell toplevels / popups | `request_minimize` → minimize / unminimize |
| wlr-layer-shell | v5 server; exclusive zones → usable. `set_exclusive_edge` via wlroots scene configure |
| wp_fractional_scale + viewporter | DRM: scale 1.5 (`SF_OUTPUT_SCALE`). Nested/headless: scale 1.0 |
| zxdg_output_v1 | Logical size for Noctalia floating-panel margins under fractional scale |
| wp_cursor_shape_v1 | v2. WM grab cursors via `sf_seat_apply_grab_cursor` |
| XCURSOR_THEME / SIZE | `seat/pointer.c`; nested uses config `cursor_size` and strips `-NNpx` theme suffixes |
| wlr-output-management | apply/test + layout pos; `set_configuration` on new/destroy/apply/request_state |
| ext-data-control clipboard | `ext-data-control-v1` + legacy `wlr-data-control-v1` + `wp_primary_selection` |
| Screenshots / capture | `ext-image-copy-capture` + toplevel capture + `zwlr_screencopy_manager_v1`; Print binds → noctalia. Capture privacy for toplevel image-capture |
| Foreign-toplevel management | `zwlr_foreign_toplevel_management_v1` v3 + `ext-foreign-toplevel-list-v1` |
| xdg-activation (urgency) | Amber until focus; IPC set/unset |
| xdg-system-bell | Visual flash only (`bell_flash_ms`) |
| xdg_wm_dialog_v1 | Tracks `toplevel->modal`; placement keys off parent |
| Idle-inhibit / idle-notify / session-lock / output-power | `session/session.c` |
| Gamma control | `zwlr_gamma_control_v1` + SceneFX hook |
| ext-workspace-v1 | Per-output group + per-workspace handle |
| xdg-toplevel-tag | Stored on toplevel; IPC `windows` (`tag` / `description`) |
| xdg-toplevel-icon-v1 | Preferred sizes 16–64; IPC `icon_name` |
| ext-background-effect-v1 | Hand protocol + `src/effect/background_effect.c`; capability `blur` |
| Touch / gestures / virtual-keyboard | `seat/`: touch, `pointer_gestures_v1` + workspace swipe, `zwp_virtual_keyboard_v1`, relative-pointer + constraints |
| Presentation-time | `wp_presentation` via `wlr_presentation_create`. Tearing control advertised (hint log only) |

---

## 6. Config, IPC, tooling

| Feature | Notes |
|---|---|
| Scriptable config | LuaJIT in-process VM (§10): rtp/packpath, Fennel 1.6.1 shim, Fennel-first embedded default |
| `--write-config` | C23 `#embed` tree, refuse overwrite. Meson `embed_defaults_stamp.h` so default `*.fnl` edits rebuild `config_embed.c` |
| Keybind table | `sf.bind("Super+w", …)` / `sf.tap("Super", …)` via `parse_chord` / `parse_tap_mod`. Actions via `sf_action_parse` |
| Ctrl+Alt+Fn VT switch | `wlr_session_change_vt` on `XF86Switch_VT_*` |
| Super tap / double-tap | Launcher / term pad |
| Spawn / exec / exec_once | Bind/rule `exec` + `cmd`; session `sf.exec` (variadic, optional `{:reload bool}`). Default `execs.fnl`: Slack flatpak. IPC `config execs`. CLI `-s` |
| IPC `windows` [--json] | Peeble wire shape |
| IPC `layers` [--json] | Layer-shell debug snapshot |
| IPC focus / close by id | JSON + CLI |
| IPC toggle-quick-terminal | Toggles scratchpad `"term"` |
| IPC `exit` / `quit` | `wl_display_terminate` on owning display |
| IPC `config` [section] | Live dump: options/binds/taps/scratchpads/window_rules/execs/inputs/outputs |
| IPC `outputs` | `ipc/outputs.c` |
| Structured JSON logs | `wm.log` + `wm.last.json` rotate |
| APK / meson `check` | `check()`: `make style-check` then `OPTS= ./tests/drive.py` (same listing as host `make test`, no coverage). Dist tarball packs `tests/` |

---

## 7. Closed FORK / River-gap map

Former River-fork blockers that stonefly owns in-process:

| Pri | Item | Stonefly |
|---|---|---|
| P0 | vsynced animations / `render_dirty` | Owned scene / frame loop |
| P0 | Map-hold / no resting flash | Slide from off-screen |
| P0 | Foreign-toplevel activate | `zwlr_foreign_toplevel_management_v1` |
| P0 | Nodes vs layer-shell z-order | Scene layers |
| P1 | Close-ghost last-frame | Cached `close_snap` → ghost slide; `sf_ease_out_cubic` / `sf_lerp_i` |
| P1 | Urgency / xdg-activation | Shipped |
| P1 | System bell | Shipped |
| P1 | Capture privacy | Shipped |
| P2 | Workspaces + toplevel-tag | Dynamic workspaces + `ext-workspace-v1` + tag protocol + switch slide (rules/persist still open) |
| P2 | Layer exclusive_edge + presentation-time | Via scene configure + `wp_presentation` |
| P3 | SceneFX blur / corners / shadows | Shipped |

---

## 8. Day-zero WM parity (shipped)

| Habit | Status |
|---|---|
| Super+Tab focus; Alt+Tab → Noctalia switcher | ✅ (Alt+Tab bind → noctalia; see roadmap for full cycle policy) |
| Super+q close | ✅ |
| Super+f / Super+m max | ✅ |
| Super+arrows snap + Super+u | ✅ |
| Super+Page / Super+1–9 workspaces; Shift moves follow | ✅ |
| 3-finger vertical swipe workspaces | ✅ |
| Super+LMB/RMB move/resize + magnetic snap + preview | ✅ |
| Border/corner LMB resize | ✅ |
| Ctrl+Esc minimize LIFO | ✅ |
| Super tap launcher / double-tap term / Super+t claim | ✅ |
| Spawns Ghostty / foot / nautilus / editor / QT | ✅ |
| Named scratchpads beyond `term` | ✅ via `scratchpads.fnl` (default only `term`) |
| Fullscreen / SSD | ✅ |
| set_tiled | ✅ |
| Screenshots, clipboard, idle/lock/gamma | 🌙 noctalia binds + ✅ screencopy/ext-capture / ✅ idle+lock+DPMS / ✅ clipboard+gamma |
| Real config file | ✅ LuaJIT + Fennel-first + `--write-config` + IPC `config` |
| Workspaces (stacking) | ✅ |
| Foreign-toplevel for docks/taskbars | ✅ |

---

## 9. Day-zero Noctalia protocol surface

Enough for bar / dock / launcher / notifier: layer-shell, foreign-toplevel v3,
fractional-scale, viewporter, xdg-output, cursor-shape, xdg-activation,
system-bell.

| Pri | Protocol / surface | Status |
|---|---|---|
| P0 | `ext-session-lock-v1` | ✅ |
| P0 | `ext-workspace-v1` + IPC workspaces | ✅ both |
| P1 | `ext-foreign-toplevel-image-capture-source-v1` | ✅ |
| P1 | `zwlr_screencopy` / `ext-image-copy-capture` | ✅ both + capture privacy |
| P1 | `ext-idle-notify` + idle-inhibit | ✅ |
| P1 | `zwlr-output-power-management-v1` | ✅ |
| P2 | `zwlr_gamma_control` | ✅ (full color-mgmt still open) |
| P2 | `zxdg_output` | ✅ |
| P2 | `zwlr_output_management` | ✅ |
| P2 | `ext-data-control` / wlr-data-control | ✅ |
| P3 | xdg-decoration | ✅ Manager + SSD within `ssd_mode` |

Seat-side day-zero: touch, virtual-keyboard, relative-pointer, pointer-constraints
(`seat/{seat,keyboard,pointer,gestures,constraints}.c`).

---

## 10. Config — shipped

LuaJIT linked in-process (`config_lua.c`); XDG dirs + `STONEFLY_*` overrides;
C23 `#embed` of Fennel-first default tree (`src/config/default/`); missing user
`init.lua` → embedded default; broken user init → log + fall back + FDO
`Notify` after display up; `stonefly --write-config`; builtin pack
`fennel-shim` (Fennel 1.6.1); `sf.*` API (`opt` / `bind` / `tap` /
`scratchpad` / `window_rule` / `packadd` / `stdpath` / `exec` / `chain`);
chord/tap strings via libxkbcommon; action names shared with IPC via
`config/action.h`; `opt` setters table-driven; IPC `stonefly config [section]`;
consumers read `sf_cfg(server)`. fennel-ls docset
`dist/fennel-ls/docsets/sf.lua` + default `flsproject.fnl`.

Entry points: `src/config/`, `runtime/pack/dist/opt/fennel-shim/`,
`stonefly --write-config`, `stonefly config`.

Open config items stay in [`ROADMAP.md`](ROADMAP.md) §10.

---

## 11. Testing — shipped harness

Owns a small harness under `tests/` (no Criterion/Check/cmocka/fluent DSL).

| Piece | Path |
|---|---|
| Assertions | `tests/sf_test.c` / `tests/sf_test.h` |
| Driver | `tests/drive.py` (discover / build / run / cover) |
| Entry | `tests/drive.py`; `make test` (host) / APK `check()` runs the same driver; meson `test('unit')` remains optional |
| Layout | Mirror `src/`: `tests/<module>/<name>_test.c` |
| Unit stubs | `tests/stubs/include` ahead of system headers (host + APK check); compositor Meson uses real deps |
| Coverage | `OPTS=cover` default on `make test` (host gcov; not gated in APK) |

**Add a suite:** create `tests/…/foo_test.c` with one pragma — no Makefile /
meson edits:

```c
/* sf-suite cover=util/foo.c src=util/foo.c stub=log libs=m */
```

`cover=` is the `src/` path in the report; `src=` links extra `.c` files;
`stub=log` pulls `tests/stubs/log_stub.c`; `libs=m` adds `-lm`.

| Rule | Choice |
|---|---|
| Framework | Project-owned runner + discovery driver |
| Checks | Thin owned wrappers (`sf_test_eq_int`, …) |
| Dependencies | No new Alpine `checkdepends` for a test framework |
| Style | Same house C as `./src` |
| Build | Driver compiles selected TUs into `build/unit*` |

Host: `make test`. Abuild `check()`: `make style-check` then `OPTS= ./tests/drive.py`
(same per-file listing; no coverage footer). Integration / coverage floors stay
on the roadmap.
