# Stonefly features

Shipped capabilities in this tree. Open and partial work lives in
[`ROADMAP.md`](ROADMAP.md). When a roadmap item lands, move it here in the
same turn (feature + short notes; drop the open-row from the roadmap).

`umbriel/` is the primary reference compositor; `peeble/` is secondary only for
features Umbriel lacks — neither is a product spec. See `AGENTS.md`.

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
| Workspace arrange engines | **`columns` default** · `stacking` only. First-class strip: `ws->columns` of `struct sf_column` (ordered rows, `width_frac`, `full_width`, row height weights, end gap weights) — umbriel `Column`/`m_columns`. Windows hold `toplevel->column` + `column_row_weight` + `column_extent_saved` (consume→expel) + **`arrange_slot_*`** (column/row/frac remembered across leave_tiling, float, and max-edges break-out so stacking↔columns / unfloat / unmax restore the same cell — not one-client-per-column). Pack: `sf_arrange_math_column_strip` / `_rows`. Column width = umbriel slot `round(frac×(usable+gap))-gap`; scroll is **just-enough**. Super+] / [ toggle. Non-floats join (`arrange_floating` opt-out). Lone **visible** column fills usable; 2+ use `width_frac`. Insert after focused column when no slot; Super+, / . consume/expel; Super+LMB cell swap / seam-insert / gap new-col; Super+v float (`arrange_float_custom`: float→resize→unfloat→float restores last float box; first float keeps cell); pink `border_float_*`; Super+m column full-width; Super+RMB resizes that column’s frac; Super+c rotates columns. Half-snap stacking-only |
| Arrange-aware keybinds | `sf.bind` `:arrange` filter (`stacking` / `tiling` / exact engine / **`overview`**). Prefer arrange-specific over unset. Defaults: Super+arrows snap while stacking, `focus_dir` while tiling, Super+v `toggle_float` while tiling, Super+c / Super+Shift+c `arrange_rotate`. Overview-only: Escape close, Return/Space confirm, bare Wheel / Shift+Wheel filmstrip scroll. Context via `sf_bind_context_arrange` (overview interactive → `SF_ARRANGE_OVERVIEW`). Generalized `:when` / submaps → ROADMAP Bind context |
| Focus next / prev (visible) | Super+Tab / Super+Shift+Tab. Stacking: MRU. Columns: **stable clockwise ring** by angle from usable centre (independent of column/row membership order after drag-reorder); Super+c still rotates columns |
| Close focused | Super+q |
| Maximize to edges | Super+f — `maximize_to_edges` / `toggle_maximize_to_edges` (client maximized hint). Live morph (`morph_duration_ms`). **Columns:** transient overlay (`arrange_max_edges_rejoin`) — breaks out + raises without sticky `arrange_floating` / pink float chrome; remembers strip slot and rejoins that cell on unmax / focus release. Hide/minimize keeps the overlay; unminimize restores edge-max **while still edged**. User resize/nudge/drag demotes via `break_edged_for_user_geom` and hide/show keeps that size (transient overlay becomes sticky float). **Sticky floats** (rule `:float` / Super+v, e.g. Slack): edge-max is an overlay on the float — never arms tile rejoin or clears `arrange_floating`. **Scratch/QT:** fresh spawn/claim edge-maxes; after user resize, toggle keeps the custom box (show only REAPPLYs when still edged) |
| Maximize with gaps (WM-only) | Super+m — `maximize` / `toggle_maximize`. No client maximized → rounder CSD. Gap-max/snap content box insets by `sf_ssd_top_inset` so SSD stays below exclusive zones. Live morph. **Columns:** widens that column to full usable and centers the strip — stays tiled; persists across Tab. **Stacking:** normal gap-max |
| Unmaximize / restore | `unmaximize` clears either mode; toggles morph-restore. `float_for_resize` / `layout_float` for window_rule / drag break |
| Layout mode state machine | Pure `wm/layout_sm` owns float/max/snap/fullscreen + PreMax on `toplevel->layout` (no mirror bools). Commands + geometry observe → state + effects; WM applies effects only. Resize/nudge/commit reconcile both ways. Unit suite `tests/wm/layout_sm_test.c` |
| Place / SoftPlace / appear SM | Pure `wm/place_sm` owns managed vs settled, SoftPlace soft bit, and appear-hold on `toplevel->place` (replaces `auto_place` / `soft_place` / `appear_pending`). Map begin, user/layout own, drop-soft, soft-center, arm/try appear. Unit suite `tests/wm/place_sm_test.c` |
| Action verb catalog | `wm/action_run.c` — table of handlers + `SF_ACTION_F_WIN` / `SF_ACTION_F_CMD` protocol. Binds/taps/rules/IPC share `sf_run_action`. `SF_ACTION_COUNT` keeps name table and slot table aligned (unit-checked) |
| Fullscreen (client + WM) | Saved geom + full output box via `sf_output_for_toplevel` → cached `output->full` (not geom-centre layout lookup — that missed scrolled columns / some Chrome floats and left FS mode with the old size: Netflix “90% top-left”). After enter, `fullscreen_ensure_box` force-applies if geom ≠ full. Client `request_fullscreen` + foreign set/unset stay SET. WM actions `toggle_fullscreen` / `set_fullscreen` / `unset_fullscreen`; default **F11** → toggle (C seed + `binds.fnl`; Super+f remains edge-max). Scene parent `layers.fullscreen` (above top/bar; re-enabled on enter if overview had disabled it). **Exclusive per output.** Tab / focus_dir skip buried; explicit focus exits FS then focuses. **Strip:** FS keeps membership, no cell paint; reflow on enter/exit. GEOM sticky + no full-output regain |
| Minimize / unminimize LIFO | Ctrl+Esc — unnamed scratchpad. Shared `apply_hidden`/`apply_shown` (LIFO vs scratch). `xdg request_minimize` → CSD/SSD buttons. Show slide targets `resting_*` (hide parks the scene node off-screen; reading node.x made floats/QT/edge-max recall slide to an off-screen pose — taskbar activate looked focused but invisible) |
| Keyboard half-snap (arrows) | Super+arrows while stacking — gap math ports `snapBorderBox`. Tiling uses `focus_dir` |
| Unsnap / restore PreSnap | Super+u |
| Drag move (`move_window`, default Super+LMB) | `sf.bind` mouse chord → `SF_ACTION_MOVE_WINDOW`. Stacking: free move + magnetic edge-snap. Columns tiles: cell swap / seam-insert / gap new-col with live make-room (INSERT/NEW_COL use painted indices → structural on apply; home pinned by column pointer; drop targets invalidated on unlink; mid-drag float/arrange-set aborts grab). **Columns floats: free move only** (no magnetic half-snap — floated on purpose). WM xcursor via `sf_seat_apply_grab_cursor` |
| Tile factor resize (`resize_window`, default Super+RMB) | Columns: `SF_CURSOR_ARRANGE_RESIZE` adjusts that column’s `width_frac` only (session `column_ratio` stays at config); clears full-width. Stacking: free edge resize unchanged |
| Drag resize (`resize_window`) | Edges nearest pointer; undragged edges stay put. Layout SM observes the proposed box so max/snap/fs status tracks the live geometry. Moving edge(s) clamped so geometry + SSD titleband + compositor border stay in usable∩gaps. Grab/apply use scene origin as geometry TL (no ±geo.x/y) |
| Center-lock resize (`resize_center`, default Super+Shift+RMB) | Grow/shrink around a fixed layout center: propose Δw = 2·Δx, Δh = 2·Δy (min 50). Cap with the same usable box. Scene position follows the *committed* buffer; re-sync on xdg commit. `SF_CURSOR_RESIZE_CENTER` |
| Border/corner mouse resize | Hover + LMB on hit band (`resize_border_in` 3 / `resize_border_out` 1); edge cursors. Same usable clamp as Super+RMB. Keep enter/motion on surface in inner band; honor implicit grab until release. Disabled while `arrange_managed`. Leaving the band / titleband clears the WM face (`pointer_wm_cursor`) so the resize glyph cannot stick |
| Magnetic drag-snap + resistance | `drag_snap.c` — threshold 26, 4× push-through; **disabled under columns** (floats stay free) |
| Snap preview overlay | SceneFX overlay rects — border 1, fill 40% active |
| `set_tiled` on snap/max | `wlr_xdg_toplevel_set_tiled` on snap / edge-max / restore / unsnap / tile apply; cleared on break_out, leave_tiling, and arrange unlink (unmap) so remap is not stuck tiled |
| Cascade initial place | Gap + `(n%wrap)*cascade_step` (defaults wrap 10 / step 25), cursor usable. SoftPlace morph only after appear (disabled-on-map path keeps cascade; ensure_on_screen clamps resting, not a stale 0×0 pose) |
| Nested client disconnect (parent UAF) | Clear `xdg_toplevel->base->data` before destroying scene tree |
| Nested `ipc exit` / output teardown | `workspace_finish` destroys per-output ws scene trees before root scene destroy. `workspace_destroy` detaches member scene chrome (`ssd` / decor) before freeing `ws->tree` so cursor-shape border hit-tests cannot UAF dangling `scene_tree`. Late xdg commits no-op when `xdg_scene_tree` is already NULL (`sf_decor_update_geom` / commit) |
| EnsureOnScreen / usable relayout | On exclusive-zone change |
| Per-output usable area | `exclusive_zone != 0` (incl. −1) |
| Named scratchpads | `scratchpads.fnl` + `sf.scratchpad` / `scratch_toggle`/`scratch_claim`. Default ships `term` only. Fake `app_id` (`stonefly.scratch.term`) for claim vs normal windows. `auto_hide` (default true) hides when focus moves to another window on the same output. Detach unlinks columns; QT starts edge-max on spawn/claim; user-resized size survives hide/show until explicit max-edges again |
| Scratchpad claim focused | Super+t |
| Scratchpad omit from Alt-Tab | Super+Tab too |
| SSD titlebars | `ssd_mode` (`auto`/`prefer_ssd`/`always`/`never`) + `ssd_height` (28) + `ssd_font_size` (17, fcft). Honors client `request_mode` within policy; hide on fullscreen/edge-max; TopInset in ensure-on-screen / cascade / gap-max / snap. Defer `set_mode` until `xdg_surface` initialized. Outer `corner_radii_top` on SSD; border hole punches content+SSD. Max button + title double-click follow `client_maximize` |
| Client maximize policy | `sf.opt.client_maximize` (`gaps` default / `edges`). CSD `request_maximize`, SSD max/dblclick, foreign taskbar → that verb. Super+m = gaps; Super+f = edges (zen) — two binds |
| Unminimize workspace | `sf.opt.unminimize_workspace` (`active` default = carry to current WS; `home` = stay on hide WS) |
| Transient / dialog placement | Center on visible `set_parent` (+ stack above); late parent/size re-place while `place` is managed. No size-based dialog heuristic. Cleared on move/resize/max/snap |
| Window rules (app-id/title) | `sf.window_rule(match, actions [, parent])`. Match patterns via Lua `string.find`. Declarative `center`/`float`/`width`/`height`/`ssd`/`borderless`; `:float true` = sticky `arrange_floating` (stays out of columns; **does not inherit lone-column full usable** — SoftPlace keeps soft + defers tile join while float *or* center still possible; late identity still `rules_apply` after soft drop; rule float restores pre-tile size). WM `:action` shared with binds. SoftPlace appear-hold for late title (center or float). Defaults float gcr-prompter, dosbox, Chrome `^Google Chrome$`. IPC `stonefly config window_rules` |
| Workspaces | Dynamic per-output strip (`workspace.c`): trailing empty, compact on empty, Super+Page/1–9 + Shift move-follow, IPC `workspaces`/`workspace`/`move-to-workspace`, windows `{output,workspace}`. `ext-workspace-v1` sync. Default 3-finger swipe via `sf.gesture`: **vertical** = workspace switch; **horizontal** under columns = pan (`sf_arrange_columns_pan`; threshold axis-lock; `swipe_workspace_natural`). Vertical switch slide (shared by keybind / IPC / swipe; `slide_duration_ms`, 0 = instant). `xdg-toplevel-tag` stored + IPC |
| Cursor follows focus / pointer center | `sf.opt.cursor_follows_focus` (default true): warp pointer to window center on Super+Tab / workspace switch / move-follow. Click-to-focus does not warp. Action `pointer_center` warps on demand (ignores the opt). Skips MOVE/RESIZE grabs and locked pointer constraints |
| Multi-monitor focus / send | Actions `focus_monitor` / `send_to_monitor` with `:cmd` target: `left`\|`right`\|`up`\|`down`, `next`\|`prev`, or connector name (`eDP-1`). Focus warps to head center then top window (+ cursor_follows_focus). Send assigns to the target's active workspace, re-fits max/fullscreen, centers floaters, focuses + `pointer_center`. Defaults: Super+Ctrl+arrows focus, Super+Shift+arrows send |
| Keyboard nudge move / resize | Actions `nudge` / `resize_edge` with `:cmd` `left`\|`right`\|`up`\|`down`. Step from `sf.opt.nudge_step` (default 25). Resize: right/down grow, left/up shrink (top-left anchored, min 50px). No-op when max/snap/fullscreen. Defaults: Alt+arrows move, Alt+Shift+arrows resize |
| Always-on-top / pin | Flag + restack: pinned windows stay above normal ones after focus/raise; hide slides stay above pins. Actions `toggle_always_on_top` / `always_on_top` (`:cmd` true\|false). Window rule `always_on_top`. IPC `windows[].always_on_top`. Default Super+Shift+p |
| Input device rules | `sf.input(match, opts)` in `inputs.fnl`. Match type keywords (`keyboard`/`pointer`/`mouse`/`touchpad`/`touch`) or case-insensitive name substring; later rules override. Libinput: `tap_to_click`, `natural_scroll`, `left_handed`, `dwt`, `accel_speed` (−1..1), `accel_profile` (`none`\|`flat`\|`adaptive`). Keyboard: `repeat_rate` / `repeat_delay`. Applied on device add via `seat/input_config.c`. Default: touchpad tap + natural scroll + dwt. IPC `stonefly config inputs` |
| Predictability (High) | Strip `arrange_slot_*` survives leave_tiling / float / max-edges / WS·monitor send; exclusive FS + focus-obscured (no Tab onto covered); sticky float+edge-max (Slack); arrange switch does not silently demote edged. Open Medium gaps: ROADMAP Predictability |

---

## 2. Presentation / animations / eye candy

| Feature | Notes |
|---|---|
| Appear / map slide | 200ms ease_out. **Tiled columns:** pack cell first (`sf_arrange_flush`), hold appear until the client commits near that size (give up after 1 post-propose commit — terminals rarely match exact cell height), then slide at final height. Same-size re-pack must not reset `arrange_prop_waits` (or the scene stays disabled forever while foreign/taskbar still list the window). Appear peels with the strip: glue to the **next** column’s current left (pushed right), else previous column’s right; neighbors morph on the **same clock** (`sf_anim_sync_clock`). `arrange_appear_pending` is set on join / before the arm_appear flush so neighbors cannot morph into the gap first. Commit tries appear on pending+disabled, not only SoftPlace hold. Mid-slide arrange retargets only. Hide/close still exit to the column’s right inter-gap. `join_toplevel` must not enable the scene. Park off-screen before scene enable. Floats/scratchpads keep closest-edge + short-axis when `slide_dir_closest`. SoftPlace appear-hold via `place_sm`; mid-slide retarget; morph if SoftPlace centers after slide. GEOM observe uses resting/morph target mid-slide so edge-max scratch show does not demote to float |
| Close slide (WM-initiated) | Shared `slide_offscreen` with appear/scratch/close-ghost: tiled → right inter-gap (floored past usable + border/shadow pad); floats → `closest_dir`. Scene disabled on hide finish (no chrome strip while awaiting client unmap). Tiled WM close unlinks + `sf_arrange_flush` before hide so survivors morph while the closer slides out |
| Close slide (client Ctrl+W / CSD) | Cached last-frame ghost (`close_snap`); unmap slides via `sf_anim_offscreen_for_box` with **strip_column** (right inter-gap, not closest/top). Survivors `sf_arrange_flush` on the same unmap so morph runs with the ghost. Skipped when WM hide already ran |
| Snap / unsnap / max morph | Live peeble-style morph (`morph_duration_ms`); mid-flight retarget. Fullscreen stays instant |
| Tile present-size crop | Border/shadow/SSD/subsurface clip follow the packed cell (`arrange_prop_*`) or morph endpoint while the committed buffer is still larger — scrolled-off columns cannot leave a content chunk under the next cell |
| Workspace switch slide | Vertical strip slide on Super+Page / 1–9, IPC `workspace`, and 3-finger swipe. Next: content up (new from below); prev: opposite. Focus + pointer follow at start. Duration `slide_duration_ms` (0 = instant). Mid-flight interrupt lands then retargets |
| Scratchpad / minimize slides | Mid-flight reverse. Edge-max reassert runs while still hidden so REAPPLY cannot cancel the show slide |
| Vsync-capped animation pacing | Own scene / frame loop |
| Backdrop / window blur | Per-window SceneFX blur (`window_blur`) or client `ext-background-effect-v1` (`client_blur`). Gate: `(window_blur or client_blur) and not fullscreen`. Default `blur_xray false`. **Layer-shell** (any client; Noctalia bar/launcher/switcher/panels via `compositor_blur`): protocol region → one SceneFX plate per rect (`sf_layer_set_client_blur`; switcher panel+tiles); single full-surface region falls back to tightest exclusive/opaque/input box; transparency mask; follows `blur_xray` |
| Soft rounded corners (opaque) | `corner_radius` 12 = CSD/hole/buffer; outer rim = `corner_radius + border_thickness` |
| Drop shadows | SceneFX |
| Compositor border (focus/idle/urgent) | Neon / charcoal inactive / amber. Static `SF_BORDER_*` float arrays (not compound literals). Hole+buffer use `corner_radius`; outer = radius+border. **Flush heuristic (non-tiled):** border/shadow/SSD only when content clears usable by ≥ `border_thickness` on **all** four sides — flush on any side → chrome off (all-or-nothing). Tiled strip cells keep borders |
| Layer z-order vs windows | Scene trees background→bottom→blur→toplevel→overview→top→fullscreen→overlay→lock; normal xdg under workspace trees in `layers.toplevel`; fullscreen xdg reparented to `layers.fullscreen`; overview filmstrip above windows / below the bar |
| Workspace overview | Umbriel-shaped vertical filmstrip (thin v1). Actions `overview_toggle` / `open` / `close` / **`overview_confirm`** / **`overview_scroll`**; default **Super+o**. Continuous **4-finger** vertical swipe via `sf.gesture` `:overview` (natural: finger up opens). While open: **3-finger** V/H `:overview_pan` outranks workspace/columns_pan; bare Wheel / Shift+Wheel (`:arrange "overview"`); **selection** highlights the window that had seat focus at open (seat cleared so clients do not eat keys); **Super+Tab / Super+Shift+Tab** cycle cards across all workspaces (spatial order) and scroll the filmstrip; Enter/Space confirm selection + close; Escape close. Live root-surface cards (stack tiled→floating→fullscreen like Umbriel); click card → focus + close; empty click closes. Hides scratchpads; dims backdrop; `sf.opt.overview_zoom` (0.5), `overview_scroll_factor` (1.0). No card-drag / shortcut badges (deferred). `wm/overview.c` |
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
| xdg-shell toplevels / popups | `request_minimize` → minimize / unminimize. Popups `wlr_xdg_popup_unconstrain_from_box` into parent output **usable** (flip/slide/resize); layer `get_popup` reparents and re-constrains only if `xdg_surface` already initialized (else `xdg_popup_commit` on first commit). Early configure aborted wlroots 0.20 on Noctalia taskbar tooltips |
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
| xdg_wm_dialog_v1 | Tracks `toplevel->modal`; placement keys off parent (modal focus/trap still ROADMAP §5) |
| Idle-inhibit / idle-notify / session-lock / output-power | `session/session.c` |
| Gamma control | `zwlr_gamma_control_v1` + SceneFX hook |
| ext-workspace-v1 | Per-output group + per-workspace handle |
| xdg-toplevel-tag | Stored on toplevel; IPC `windows` (`tag` / `description`) |
| xdg-toplevel-icon-v1 | Preferred sizes 16–64; IPC `icon_name` |
| ext-background-effect-v1 | Hand protocol + `src/effect/background_effect.c`; capability `blur`; xdg toplevels + all layer-shell surfaces. Layers: one plate per effect rect (else exclusive/opaque/input shrink); toplevels still full-surface plate when region non-empty (true region mask ROADMAP) |
| Touch / gestures / virtual-keyboard | `seat/`: touch, `pointer_gestures_v1` + `sf.gesture` swipe binds, `zwp_virtual_keyboard_v1`, relative-pointer + constraints; `sf.input` libinput apply on new keyboard/pointer/touch |
| Presentation-time | `wp_presentation` via `wlr_presentation_create`. Tearing control advertised (hint log only) |

---

## 6. Config, IPC, tooling

| Feature | Notes |
|---|---|
| Scriptable config | LuaJIT in-process VM (§10): rtp/packpath, Fennel 1.6.1 shim, Fennel-first embedded default |
| C↔Lua border / hardcoded→config audit | ✅ TODO NEXT #8. C owns primitives; Lua/opts own policy. Contract + deferred knobs: [`FROM_C_TO_LUA.md`](FROM_C_TO_LUA.md) (§9.2 leftovers, Tier 2 `sf.on` still open) |
| `--write-config` | C23 `#embed` tree, refuse overwrite. Meson `embed_defaults_stamp.h` so default `*.fnl` edits rebuild `config_embed.c` |
| Keybind / mousebind / wheelbind table | `sf.bind("Super+w", …)` / `sf.bind("Super+LMB", {:action :move_window})` / `sf.bind("Super+WheelUp", …)` / `sf.tap("Super", …)`. Mouse tokens: `LMB`/`RMB`/`MMB` (preferred), `BTN_*`, Umbriel `MouseLeft`, Hyprland `mouse:272`. Wheel: `WheelUp`/`Down`/`Left`/`Right` (+ Hyprland `mouse_up`/`mouse_down`); **mods required** except while overview (`:arrange "overview"` bare Wheel / Shift+Wheel filmstrip). First mouse/wheel chord clears that C seed only. IPC `config mousebinds` / `wheelbinds`. Cooldown: `wheel_bind_cooldown_ms` (default 150). Actions via `sf_action_parse` |
| Ctrl+Alt+Fn VT switch | `wlr_session_change_vt` on `XF86Switch_VT_*` |
| Super N-tap (`sf.tap` + `:count`) | Default: count 1 → launcher, count 2 → term pad. Same mod merges levels; gap = `double_tap_gap_ms` |
| Spawn / exec / exec_once | Single helper `sf_spawn_cmd` (`util/spawn.c`): fork + `/bin/sh -c`, child `setsid` + SIGCHLD/mask reset; parent SIGCHLD reaper (`sf_spawn_install_reaper`, not `SIG_IGN`). Used by bind/rule `exec`, `sf.exec`, scratchpad `cmd`, CLI `-s`. Default `execs.fnl`: Slack flatpak. IPC `config execs` |
| Live config reload | `sf_config_reload`: staging Lua re-run; keep previous on failure + FDO Notify with exact Lua error. Action `:reload` / `sf.reload` / `stonefly reload` / Super+Shift+r. inotify: config dir or nearest existing ancestor (so `--write-config` mid-session arms + reloads). `sf.opt.config_watch`, 150ms debounce. Side effects: scratchpad rebind-by-name, rules/input/decor, `sf.exec {:reload true}` |
| Config validate | `stonefly validate` — load user init (or embedded) without applying; exact Lua error on stderr; exit 0/1 |
| IPC `windows` [--json] | Peeble wire shape |
| IPC `layers` [--json] | Layer-shell debug snapshot |
| IPC focus / close by id | JSON + CLI |
| IPC toggle-quick-terminal | Toggles scratchpad `"term"` |
| IPC `reload` | Live config reload (`{"ok":true}` or error) |
| IPC `exit` / `quit` | `wl_display_terminate` on owning display |
| IPC `config` [section] | Live dump: options/binds/mousebinds/wheelbinds/taps/scratchpads/window_rules/execs/inputs (rules array)/gestures/outputs (`{}` until API) |
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
| Configurable touchpad gestures (`sf.gesture`) | ✅ Hyprland-shaped `(sf.gesture {:fingers N :axis DIR :mods?} ACTION)`. Swipe built-ins `:workspace` / `:columns_pan` / `:overview` / **`:overview_pan`** (filmstrip while open; skipped when closed, +10 score when open); pinch dirs `pinch`/`pinchin`/`pinchout` fire actions once \|scale−1\| ≥ 0.15. `:mods` exact match (e.g. Super+pinch). Default: 3-finger V/H + overview_pan + 4-finger overview + Super+pinchout→`maximize_to_edges` / Super+pinchin→`unmaximize`. IPC `config gestures`. Unit: `tests/config/gesture_test.c` |
| 3-finger vertical swipe workspaces | ✅ via `sf.gesture` `:workspace` (default); opts: threshold / natural |
| 3-finger horizontal swipe columns pan | ✅ via `sf.gesture` `:columns_pan` (default). Live-pan `columns_view_offset` (view only). Axis lock after threshold; natural follows `swipe_workspace_natural`. Focus: keep while mostly on-screen, else centre-most visible cell (continuous; no pointer warp) |
| 4-finger vertical swipe overview | ✅ via `sf.gesture` `:overview` (default). Continuous progress (Umbriel ~300 px travel); release commits open/close at 0.5. Natural follows `swipe_workspace_natural` |
| Configurable mouse grabs (`sf.bind` + `move_window` / `resize_window` / `resize_center`) | ✅ Umbriel-shaped same `sf.bind` table; Hyprland bindm-shaped grab verbs. Default: Super+LMB/RMB / Super+Shift+RMB. Border LMB hit-test stays C/opt. Unit: `tests/config/mousebind_test.c` + chord mouse parse |
| Mod+Wheel binds (`sf.bind` + `WheelUp`/`Down`/`Left`/`Right`) | ✅ Same `sf.bind` table; mods required outside overview. Default: Super+Wheel → workspace prev/next; Super+Shift+Wheel → `focus_dir` left/right (tiling); overview bare Wheel / Shift+Wheel → `overview_scroll`. Consume-on-match + `wheel_bind_cooldown_ms` (150). Unit: `tests/config/wheelbind_test.c` + chord wheel parse |
| Super+LMB/RMB move/resize + magnetic snap + preview | ✅ via defaults above |
| Super+Shift+RMB center-lock resize | ✅ via `resize_center` default |
| Border/corner LMB resize | ✅ (hit-test / `resize_border_*` opts; not an `sf.bind`) |
| Ctrl+Esc minimize LIFO | ✅ |
| Super N-tap launcher / double-tap term / Super+t claim | ✅ `sf.tap` `:count` 1..9 (gesture-parallel); defaults count 1→launcher, 2→term |
| Spawns Ghostty / foot / nautilus / editor / QT | ✅ |
| Named scratchpads beyond `term` | ✅ via `scratchpads.fnl` (default only `term`) |
| Fullscreen / SSD | ✅ |
| set_tiled | ✅ |
| Screenshots, clipboard, idle/lock/gamma | 🌙 noctalia binds + ✅ screencopy/ext-capture / ✅ idle+lock+DPMS / ✅ clipboard+gamma |
| Real config file | ✅ LuaJIT + Fennel-first + `--write-config` + IPC `config` |
| Workspaces (stacking) | ✅ |
| Super+] / Super+[ arrange cycle | ✅ stacking ↔ columns |
| Foreign-toplevel for docks/taskbars | ✅ workspace-output pin (columns off-strip no longer drop icons) |

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
`scratchpad` / `window_rule` / `packadd` / `stdpath` / `fs` / `exec` /
`input` / `chain` / `reload`); `sf.fs.mkdir_p` / `sf.fs.mtime` wrap C
`sf_mkdir_p` / `stat` (fennel-shim cache dir uses `mkdir_p`, no
`os.execute`); chord/tap strings via libxkbcommon; action names shared with
IPC via `config/action.h`; `opt` setters table-driven (incl. `config_watch`);
live reload + ancestor inotify (`config_watch.c`); `stonefly validate`; IPC
`stonefly config [section]` / `stonefly reload`; consumers read
`sf_cfg(server)`. fennel-ls docset `dist/fennel-ls/docsets/sf.lua` + default
`flsproject.fnl`.

Entry points: `src/config/`, `runtime/pack/dist/opt/fennel-shim/`,
`stonefly --write-config`, `stonefly validate`, `stonefly config`,
`stonefly reload`.

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

Shipped pure suites include `wm/layout_sm` / `wm/place_sm` (layout + SoftPlace
appear machines), geom/snap/layout name parse, IPC JSON/parse, util/*, ssd
policy/layout, config action/chord.

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
