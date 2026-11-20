# Lessons

Working notes from peer compositors and from stonefly bugs. Prefer these over
guessing “parity with X.”

## Umbriel (cloned under `./umbriel`)

Noctalia’s compositor sibling: C++/wlroots, layouts **scrolling · dwindle ·
master · stacking**. Stonefly keeps **stacking + columns** only (breadth tax).
Scrolling is niri-inspired; Umbriel’s docs and action catalog are the UX source
of truth for columns. Shell stays out of process (Noctalia).

### Columns are first-class; windows are rows

Scrolling is a strip of fixed-extent columns. A column can hold several windows
stacked vertically. That unit drives consume, tabs, column-maximize, and
focus-within-column. Treating “one window = one column” is too weak for a
usable strip.

Stonefly model (stolen from Umbriel `Column` + `ScrollingLayout::m_columns`):

- `ws->columns` — ordered strip of `struct sf_column`
- each column: `rows` (wl_list), `width_frac`, `width_frac_saved` + `full_width`,
  `top_gap_weight` / `bottom_gap_weight`
- each row (`toplevel`): `column`, `column_link`, `column_row_weight`,
  `column_extent_saved` (primary extent remembered across consume → restored
  on expel)
- pack math: `sf_arrange_math_column_strip` + `sf_arrange_math_column_rows`
- tabs deferred

### Float is sticky membership, not “broke out once”

`window-toggle-floating` only applies on tiling layouts (stacking is already
all-float). Entering a tiling layout re-tiles layout-floated windows; **rule /
user floats stay out**. Visible non-floats must all join the tile set; float is
the only opt-out. Hide/show must not leave unmanaged ghosts under the strip.

### Consume / expel reshape the strip

Default: Mod+, / Mod+. = consume left/right (stack into the neighbor). The
richer pair is consume-or-expel: stack if a neighbor exists, otherwise peel out
of a multi-window column. Also: consume-**from**-left/right (pull the neighbor
in). Move-left/right is a different verb (reorder / new column).

### Maximize is split on purpose

- `toggle-maximize` → **column** fills usable width (still tiled).
- `toggle-maximize-to-edges` → leave gaps/struts/borders (break-out).
- `client_maximize = "column"|"edges"` wires the client button.

Stonefly: Super+m = column-wide in columns; Super+f = edges break-out.
Under columns, edge-max is **transient**: `arrange_max_edges_rejoin` without
sticky `arrange_floating`. Focus change / Tab / focus_dir / Super+m releases
it back into its cell **only while the window is still visible**. Hide →
`focus_top` must not demote (`release_transient_max_edges` no-ops when
`minimized` / `anim_hide`); otherwise unminimize joins a floater as a tile.
Marking it `arrange_floating` made Super+Tab walk only tiles (focus “behind”
the overlay) and Super+m look like a pink float.

### Focus means visible (reject floats-always-above)

Umbriel/niri keep an independent floating layer always above tiles; Tab must
include floats / `window-focus-switch-floating` or a covering max-float is a
death trap (focus lands on a tile you cannot see). Stonefly product rule:
**select window → see window.** `sf_focus_toplevel` raises; arrange flush must
**not** re-raise every `arrange_floating` window. Super+Tab’s columns ring
appends sticky workspace floats into the same spatial cycle so tile↔float
round-trips. Pinned / hide-slide still win via `sf_pinned_restack`.

### Show slide resting vs hide park

Hide finish leaves the scene node at the off-screen pose and keeps
`resting_x/y` as the on-screen return target. `sf_anim_start_show` must slide
to `resting_*`, never `scene_tree->node` after a completed hide — otherwise
floats, QT (taskbar path), and edge-max come back focused but painted off
screen (Super+m / scratch reassert looked like the “fix” because they
`apply_box` before show).

### Minimize is float-only (Umbriel)

Tiled windows do not minimize into a hole under the strip. A hidden tile must
not keep a ghost packing slot. Stonefly still allows tile minimize but skips
minimized windows in packing; foreign must keep advertising them. Expel keys
off **row count** (any sibling), not visible count — otherwise a sole visible
row stuck with minimized ghosts cannot peel out. Cell-drag INSERT/NEW_COL
indices are **painted** (visible-only) for live spacer reflow and mapped to
structural on commit; raw `sf_column_row_of` as a reflow slot disagrees with
minimized prefixes.

### SoftPlace soft bit vs float-pending

Do not `drop_soft` while a float rule is still possible. Soft false + early
`consider_place` return skips `rules_apply` / join on late title, leaving an
unmanaged cascade under columns. Mirror center hold: keep soft until center
and float identity are decided; when soft is already false, still apply rules
and join/float on identity change.

### Cell-drag home and drop pointers

Home must be a **column pointer** (plus preferred insert index), not a strip
index alone — destroying an earlier column shifts indices and abort restores
into the wrong column. Validate `drop_peer` / `drop_column` on finish (and
clear on unlink of the last row / peer). Mid-drag `break_out` / `arrange_set`
must abort the grab first so finish cannot re-tile an `arrange_floating`
window.

### Scroll policy is “reveal enough,” not always center

On focus, scroll just far enough to show the column. Optional
`center_focused = on_overflow` centers only when the focused column and its
previous neighbor cannot share the viewport. Underfull strips can center as a
strip. Edge-drag during rearrange pans offscreen columns. Stonefly now keeps
the prior `columns_view_offset` and nudges just-enough (no re-center on every
focus). `on_overflow` / underfull-center / edge pan still open.

### Column width is a slot, not `usable * frac`

Umbriel: `width = round(frac × (viewport + gap)) - gap`. Two columns at 0.5
fit the viewport with one inter-column gap. Stonefly used `usable * frac` and
overflowed by exactly `gap`, so focus scrolled by that gap and the strip
wobbled (one side glued to the edge). Resize span must use `usable + gap` as
the unit so Super+RMB matches the slot.

### New windows insert beside focus

New column after the focused one, not append-at-MRU-end. Drag shows insertion
preview into a new or existing column.

### Close / focus inside a stacked column

Close a column → nearest survivor; if that column is multi-row, restore the
**MRU row**, not always the first. Missing in stonefly.

### Tabs are a display mode on a column

`column-toggle-tabbed` — same boxes, one visible, tab bar. Optional later.

### Foreign output follows the workspace, not the paint rect

Umbriel pins `zwlr_foreign_toplevel` `output_enter` to the window’s workspace
output. Scrolling columns routinely sit off-screen; geom∩output would
`output_leave` and taskbars drop icons. Same trap as fullscreen mid-configure
0×0 buffers. Stonefly hit this: N windows → fewer taskbar icons only in
columns.

### Scope discipline

Umbriel ships four layouts; stonefly keeps **two** (stacking + columns) for
polish. Shell is Noctalia; “parity with X” is not a reason. Sharpen scrolling
membership / consume / float / drag / foreign before inventing deck/grid.

### Borrowed vs still rough (stonefly)

| Umbriel idea | Stonefly |
|---|---|
| Explicit float membership | `arrange_floating` + rejoin strays |
| Float chrome distinct | pink `border_float_*` |
| Consume L/R (+ expel when stacked) | Super+, / . |
| Column max vs edges max | Super+m / Super+f |
| Columns default | `arrange = columns` |
| Lone visible column fills usable | yes (skip columns with only minimized rows); 2+ use `width_frac` |
| Tiled appear/close | Popin/popout at the packed cell (scale+fade); neighbors morph on the opener/closer clock. Legacy strip edge-slide kept for floats/scratch only |
| Hide remnant strip | Super+q hide can leave a 2–3% content/chrome strip on the exit edge: offscreen TL ignored border/shadow pad, strip target stopped at `resting_x+w` short of usable clear, and the scene stayed enabled until async client unmap. Fix: chrome pad on offscreen, strip hide clears usable, disable scene on hide finish; ghosts destroy when they leave usable. **Live Super+q pop vs ghost:** content fade did not touch border/shadow, so the window hung as a rim then snapped off. Super+q now spawns the same snapshot ghost as Ctrl+W (no chrome) and disables the live tree immediately. `anim_hide` still skips a second ghost on unmap |
| Off-strip oversized buffer | Scrolling a column off-screen moves the scene node to the packed slot immediately, but the client may still have its previous/natural buffer (e.g. Ghostty ~1148 while the cell is 1044). Clip/border followed **client geo**, so ~100px of real content painted under the next column. Umbriel presents at layout size (stretch/crop); stonefly crops subsurface clip + border/shadow/SSD to `arrange_prop_*` / morph `to_*` via `sf_decor_present_content_size` until the client redraws. Not “scroll working as designed” — the leftover under the visible cell is a bug |
| Tiled appear size | pack cell + wait for configure before slide — natural buffer mid-slide looks like a short square growing in the column. **Never reset `arrange_prop_waits` on same-size re-pack** (second map / idle arrange); terminals rarely hit exact cell height → waits never reached give-up → scene stayed disabled (taskbar yes, screen no). Give up after 1 post-propose commit; commit path must try appear on `arrange_appear_pending`, not only SoftPlace hold |
| SoftPlace empty app_id | Appear-hold must wait for **late title** only (Chrome `app_id` then `^Google Chrome$`). A known title with empty `app_id` must show: Chrome Task Manager never sets `app_id`, and any app_id float rule (`dosbox` / Slack / …) kept `float_still_possible` → scene disabled forever while foreign + overview still listed it. Explicit `sf_focus_toplevel` must unpark (`appear_start_show`) — raise alone does not enable. IPC `scene_enabled` for diagnosis |
| Tiled push/pull sync | defer neighbor morph while opener **parked** (scene still disabled); set `arrange_appear_pending` on join and **before** the arm_appear flush. Release on show start **on the opener’s clock** (`sf_anim_sync_clock`). Once the pop is **running**, a later consume/repack must apply neighbors immediately and retarget the opener’s full cell (xy+wh) — treating `appear_pending` as “still hold” left stacked rows at the old column x with the new 1/N height (gaps + orphan in the next column) until the next spawn flushed them. True Umbriel rigid strip is one `LayoutMotion` + SceneFX `windows_move` shader — our morph+slide sync is the in-tree approximation. Close: unlink+flush with ghost/hide. `join_toplevel` must not enable the scene |
| Appear chrome flash | never enable at resting before parking off-screen; preappear keeps scene disabled |
| Hide/show border gone | The rim is a SceneFX rect **under** the xdg tree unless it is raised. wlroots 0.20 `set_clip` no-ops on an equal box, so the pop `dest_size` survives hide/show and the client buffer covers that rim until a later commit (new client, max/unmax). Only the transition out of a stretched pop must nudge and clear the clip before installing the real box; settled commits must leave an unchanged clip alone, and `raise_to_top` the border after SSD. Do not `set_dest_size` on the settled path (font scale / geometry shift). Live hide keeps the rim and fades it via `sf_decor_refresh_border`. Super+q stays a chrome-less ghost. Do not destroy/recreate the rect on unhide. |
| Scratch/QT vs layout | detach must `arrange_unlink`; edge-max is sticky (never `arrange_max_edges_rejoin` / focus demote) |
| Hide → show resting | show slide uses `resting_*`; hide parks node off-screen |
| Column edge-max across hide | do not `release_transient_max_edges` while minimized/hiding |
| Popin / popout | Umbriel: fade + presented box `lerp(scale→1)` centered in slot. Stonefly: tiled `anim.pop` (scale **0.5** in / **0.8** out + fade; no client `set_size`). **Must** use umbriel presented-crop (`source_box` + `dest_size` + clip). Bare full-surface `dest_size` → wallpaper gap. **Target `arrange_prop` cell**, not the mid-appear commit (else Ghostty “70% then pop again”). **Re-apply crop on idle after commit** — scene xdg helper resets `dest_size` and terminals damage often → 50%↔full thrash on the prompt. Undersized geo vs cell: stretch to `arrange_prop` (same crop) so border/content stay locked while kitty/ghostty catch up; oversized still 1:1 clip (scroll). **Corner radii during present:** `corners_for_buffer` must use the presented box, not geo — else mid-pop radii drop and square client corners thin the rounded border. **wlroots 0.20.2** (`types/scene/subsurface_tree.c`): a clip parks the buffer at `(clip.x, clip.y)` and `wlr_scene_subsurface_tree_set_clip` returns without `surface_reconfigure` when the box is unchanged. Apply the scaled crop **after** `set_clip` (the reconfigure overwrites source and dest). On leave, do **not** write a full-surface `dest_size` while that clip is still installed — the geometry content then sits `geometry.x/y` to the right and down until the next commit (Ghostty flashes and snaps; Nautilus stays there until a click). Null the clip first, then set the resting clip, so `surface_reconfigure` writes the same source and dest a commit would. Foot’s titlebar is inside the window geometry, so that margin is ~0 and the same bug does not show. **Never** force `dest_size` on a window that is not in a pop. Duration still `slide_duration_ms` — FIXME → TODO NEXT #2 `sf.opt.anim` |
| Configurable swipe/pinch binds | `sf.gesture` (fingers+axis+mods+action); built-ins `workspace` / `columns_pan` / `overview` need continuous/step paths. Exact dir beats axis; mods exact like keybinds. Super+pinch max leaves plain pinch for clients — better default than bare pinch→max |
| Overview (thin v1) | Umbriel: hide real trees + shared-texture cards + filmstrip + continuous 4-finger progress (~300 px) + click-to-focus. Full Umbriel overview is ~4.5k LOC (drag, badges, wallpaper mirrors, subsurface cards). Stonefly v1: same UX contract (Mod+O, 4-finger, click card, Escape/Enter/Space, Wheel pan) with single root-surface cards + dim; no card-drag / shortcut badges. Prefer progress gesture over threshold ACTION open/close. Filmstrip pan = `:overview_pan` gesture (outranks workspace/columns_pan while open) + `:arrange "overview"` bare wheel — do not hardcode Escape in seat code. Seat focus is cleared on open so clients do not eat keys — keep an overview **selection** (seeded from pre-clear focus) for highlight / Tab / confirm; do not highlight via `sf_focused_toplevel` after clear. Super+Tab while open cycles all cards across workspaces (not arrange/MRU current-WS only). Card z-order: Umbriel `populateCards` tiled→floating→fullscreen (oldest-first within pass); naive MRU create puts floats under tiles |
| Bind context vs submaps | Preferential `:arrange` filter (incl. overview) is enough for layout + one modal UI. Umbriel/Hyprland use explicit submaps for user modal layers; Umbriel overview keeps normal keybinds and adapts actions in-place. Do not overload `:arrange` forever — ROADMAP §10 Bind context for `:when` / submaps |
| Mod+Wheel binds | Same `sf.bind` table as keys/mouse; **mods required** (bare wheel steals client scroll). Umbriel `WheelUp`/`Down`/`Left`/`Right` + Hyprland `mouse_up`/`mouse_down` only (`mouse_left`/`right` stay LMB/RMB). Consume-on-match + `wheel_bind_cooldown_ms` (default 150) so high-res wheels do not spam workspace switches. `delta<0` → Up/Left |
| Configurable mouse grabs | Umbriel: mouse buttons live in the same keybind table (`Mod+MouseLeft`); Mod+LMB/RMB grabs still hardcoded in cursor. Hyprland: separate `bindm` + `mouse:272` + `movewindow`/`resizewindow`. Stonefly: Umbriel table shape (`sf.bind`) + Hyprland grab verbs (`move_window` / `resize_window` / `resize_center`). Prefer `LMB`/`RMB` tokens; never alias bare `Left`/`Right` (arrow keys). First mouse chord clears mouse seed only (keys untouched) |
| Policy stuck in C | Remaining #8: cell-drag knobs, focus-cycle shape, insert-after-focus, SSD button wiring, no `sf.on` hooks — starves the Lua runtime. Gestures + mouse grabs done |
| Layer-shell client blur | Any layer client may publish `ext-background-effect-v1` (Noctalia bar/launcher/switcher/panels via `compositor_blur`). Wire every layer surface — not a bar special case. **One SceneFX plate per effect-region rect** (Noctalia switcher: full-output overlay + `blurRects` for panel+tiles — extents AABB frosts gaps/dimmer). Single full-surface region → tightest of `exclusive_zone` / `opaque_region` / `input_region`. Transparency mask only on the single-plate path (bar): mask `dst` is the blur node box, so a full-surface mask stretched into a sub-rect plate mis-stencils. Stock SceneFX 0.5 sets `ignore_transparent` when mask ≠ NULL; Umbriel adds `ignore_alpha` threshold. Soft-shadow pad still needs geometry shrink because shadow alpha passes the stencil. **Do not** `wlr_scene_node_destroy` blur plates in `layer_surface_destroy` — scene_layer frees the tree first on the same destroy signal |
| xdg_popup unconstrain | Tinywl-shaped “empty configure” leaves Chrome/GTK menus hanging half off-screen. Must `wlr_xdg_popup_unconstrain_from_box` into usable (layout → root parent surface coords; geometry offset for xdg). Layer popups: parent is often NULL until `get_popup` — reparent there; re-constrain only if already initialized (Noctalia tooltips call `get_popup` before the first commit; `schedule_configure` / unconstrain-that-schedules asserts `surface->initialized`). Else defer to `xdg_popup_commit` on `initial_commit` (Umbriel shape). Umbriel also tightens tiled popups horizontally to the window box |
| Derived shell policy | Opaque-bar-on-edge-max must recompute “any visible edge-max on output” from a window list on layout/unmap/workspace — never toggle Noctalia opacity from a single max/unmax. Needs `sf.on` + `sf.wins` (TODO NEXT #10) |
| Live config reload | Umbriel: transactional commit + selective effects + inotify debounce 150ms. Stonefly: staging `sf_config` + fresh Lua VM; on failure restore previous (no embedded fallback on reload) + Notify with exact Lua error. Scratchpad slots hold `spec*` into config — must `sf_scratchpad_rebind` by name before finishing the old config. Tap step pointers alias binds — disarm before swap. `sf.exec {:reload true}` only after success. Watch nearest existing ancestor when config dir missing so mid-session `--write-config` is seen |
| Strip slot memory | `leave_tiling` / float / max-edges break-out must `sf_arrange_slot_remember` before unlink; `join_workspace` restores in (col,row) order. Without this, stacking↔columns and unfloat/unmax become one-client-per-column |
| Exclusive fullscreen | One FS per output; demote peers on enter. Tab/focus_dir skip `sf_toplevel_focus_obscured`; explicit focus exits FS then focuses. Two FS fighting the screen is a hard bug. FS box = `sf_output_for_toplevel` → exact `output->full` (geom-centre layout lookup fails for scrolled columns / some floats — mode went FS without APPLY_BOX → Netflix Chrome “fullscreen” stuck at old ~90% size with wallpaper strips). `fullscreen_ensure_box` after enter. Umbriel opaque `fs_backdrop` (+ fractional cover on the plate only) + center letterbox while the buffer catches up. **Do not crop FS:** `sf_decor_present_content_size` / subsurface clip must ignore `arrange_prop_*` — FS keeps strip membership so the old cell present-size otherwise chops Netflix (bottom transport half-gone, sides tight). Do not +1 the client configure either (2134×1334 on 2133×1333 clips the same UI). Clear `arrange_prop_*` on FS enter. Re-enable `layers.fullscreen` on enter (overview disables it while open) |
| Sticky float + edge-max | Rule `:float` + `maximize_to_edges` (Slack): never set `arrange_max_edges_rejoin` or clear `arrange_floating`; `apply_shown` must not join when `sf_rules_wants_float` |
| Predictability (boring WM) | Round-trips must restore: arrange switch, float/unfloat, max/unmax, FS/unFS, hide/show, WS/monitor send. Audit High ✅: unlink keeps slot; FS skips pack; leave_tiling keeps edged/FS mode; send-monitor no layout_float on max. User-resized edge-max / QT keeps custom box across hide/show (`break_edged_for_user_geom`; show REAPPLY only while still edged). Float→resize→unfloat→float restores via `arrange_float_custom`. Flush floats: chrome all-or-nothing (`sf_toplevel_room_for_chrome`). Medium still open (client maximize vs Super+m, …) |
| Maximize entry points | CSD / xdg `request_maximize` / foreign taskbar → **edge-max**; SSD maximize button / Super+m → **gap-max / column full_width**. ROADMAP Predictability Medium — unify matrix before more chrome |
| Consume / rotate vs minimized | skip minimized-only neighbor columns; pull first visible row |
| Insert after focused column | yes (first-class strip index) |
| First-class Column objects | `sf_column` + `ws->columns` |
| Per-row height weights | `column_row_weight` + end gap weights |
| Consume remembers extent | `column_extent_saved` → expel restore |
| Foreign pin to workspace output | fixed (was geom∩ → taskbar flicker) |
| Drag into column / new column | Super+LMB: cell mid = swap; seam **or inter-row gap** = insert; strip gap = new col |
| Drag live make-room | insert/new-col reflow a real spacer; neighbors shrink; tint on spacer |
| Scroll just-enough / on_overflow | just-enough shipped; `on_overflow` / underfull-center open |
| Column width slot math | `frac×(usable+gap)-gap` (2×0.5 fits) |
| 3-finger strip pan | horizontal swipe pans view (`lock_offset`); vertical still switches workspaces. Focus: keep if ≥½ width still in usable, else centre-most visible cell — continuous during pan, no `pointer_follow` (warp fights the gesture). `sf_focus_toplevel` still schedules just-enough; already-visible centre focus should not yank offset |
| Tab focus order | clockwise by angle from usable centre (stable vs membership shuffle) |
| Consume-from API | `sf_columns_consume_from` (no default bind yet) |
| Drag edge pan of off-strip columns | missing |
| MRU row after close | missing |
| Column tabs | missing |
| Minimize = float-only policy | tile minimize still allowed |

## Other stonefly lessons

### Arrange vs `layout_sm`

Workspace **arrange** (stacking/columns) owns packing. Per-window `layout_sm`
owns float/max/snap/fullscreen chrome. Do not conflate them: column-max stays
tiled; edges-max breaks out.

### xdg-shell v4–v7

Advertise the wlroots cap (`WM_BASE_VERSION` 7 on 0.20.2). On initial configure/remap send
`set_wm_capabilities` without `WINDOW_MENU` until a real menu exists — wlroots
defaults to all four caps. `set_bounds` uses the usable box, not the full mode.
`suspended` is for minimize, hidden workspace, lock, and scratch hide; keep
from/to unsuspended during a workspace slide, and never suspend a column that
is only scrolled off-strip (it still paints for overview/foreign). `constrained`
is resize-handle policy (flush/strut edges); `tiled` remains CSD chrome.

Unchanged caps, bounds, suspension and edges must not schedule another
configure on a client commit. See the idle-client regression rule in AGENTS.md.

### Session lock is a presentation guarantee

Enabling an opaque scene node does not mean a monitor has shown it. As in
Umbriel, wait for per-output presentation feedback for a protected buffer;
a no-op commit, stale feedback or a discarded frame cannot satisfy the lock.
Keep the blank and lock surfaces synchronized with the output layout even
when the locker has gone away.

### Taskbar / foreign is membership, not visibility

Noctalia’s taskbar keys off foreign-toplevel `output_enter` / state. Off-strip
columns, disabled monocle peers, and mid-fs buffers are still windows. Never
tie foreign output to the current scene box for workspace members.

### Wayland 4 KiB send buffer disconnects shells

libwayland-server’s default per-client send ring is 4 KiB. When Noctalia’s
main loop stalls (calendar HTTP) or stonefly bursts foreign state (hide/show
of a fullscreen window), the compositor logs `Data too big for buffer
(4096 + 8 > 4096)`, disconnects the client, and Noctalia exits on Broken pipe
with no `[ERR]`. Fix: `wl_display_set_default_max_buffer_size(display,
1024 * 1024)` right after `wl_display_create` (Umbriel `kWaylandClientBufferSize`,
sway/labwc/hyprland). Also idle-coalesce foreign title/state/outputs into one
`done` per handle per tick so taskbar updates do not amplify the flood.

### Reference trees

- `./umbriel` — **primary** reference compositor (layouts, float, consume,
  foreign, scratchpads, animation tables, …). Prefer over peeble and over
  guessing niri.
- `./peeble` — secondary only for features Umbriel lacks (bar / launcher /
  services-shaped ideas). Do not port Go style into `./src`.
- niri — already digested by Umbriel; do not bypass Umbriel to “match niri.”
