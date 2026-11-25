# Stonefly roadmap

**Pending and partial work only**, except the **Protocols (running inventory)**
table below (yes / partial / no vs peers — update in place; do not delete
shipped rows). Shipped product notes live in [`FEATURES.md`](FEATURES.md).
When a non-protocol item lands, move it there in the same turn and **delete**
the matching roadmap row (do not leave ✅ rows).

`umbriel/` is the primary reference; `peeble/` is secondary for Umbriel gaps —
neither is a line-for-line checklist. C↔Lua border and deferred opt inventory:
**C ↔ Lua border** below. Peer framing vs mango / niri / Hyprland / Umbriel:
**Peer catch-up** and the protocols table.

---

## Peer catch-up

Where stonefly stands relative to mango, niri, Hyprland, and Umbriel. Shipped
parity lives in FEATURES; this section only frames the remaining gaps that feed
**TODO NEXT** and the after-stack. Per-protocol wire status is the running
table next.

### At parity (or ahead) — see FEATURES

Core Wayland desktop plumbing (`xdg-shell` v7, `wlr-layer-shell` v5,
`zwlr-foreign-toplevel-v1` + `ext-foreign-toplevel-list-v1`, `ext-workspace-v1`,
`ext-image-copy-capture-v1` with per-toplevel privacy block, `ext-session-lock-v1`,
output power / gamma / management, fractional scale, viewporter,
`ext-background-effect-v1` (layers+toplevels per-rect; layer popup inherit),
shortcuts inhibit, security context, lazy on-demand Xwayland). WM & UX:
scrollable `columns` + `stacking`, workspace overview filmstrip, touchpad
gestures, SceneFX blur/shadows/rounded corners,
`sf.opt.anim`, dim-inactive spotlight, per-window/rule opacity & `no_dim`,
LuaJIT + Fennel config.

### Remaining gap buckets

| Bucket | Gaps | Feeds |
|---|---|---|
| **App-compatibility walls** | (IME + tablet shipped) | — |
| **Daily-driver feel & polish** | `sf.on` + `sf.wins()`; columns polish (`center_focused=on_overflow`, drag-edge pan) | TODO NEXT #2; §1 columns |
| **Credibility / no-surprises** | Predictability Medium leftovers; optional SSD icon blit | TODO NEXT #1; after-stack |

Recent catch-up batch already in FEATURES: shortcuts inhibit, security context,
`sf.opt.anim` + dim-inactive + opacity/`no_dim`, lazy Xwayland, scratchpad
xdg-activation claim, IME relay.

---

## Protocols (running inventory)

Living list of globals stonefly advertises, how complete they are, and how that
compares to **Umbriel**, **niri**, and **mango**. Update this table in the same
turn as protocol work (see [`AGENTS.md`](AGENTS.md)). Product prose for shipped
behavior stays in FEATURES; open hollow-ad / missing work stays in §5 below and
TODO NEXT.

Legend: **yes** advertised and used · **partial** advertised or incomplete ·
**no** not advertised. Core `wl_*` (compositor, seat, shm, linux-dmabuf,
data-device) are assumed on all four.

| Protocol | What it is | stonefly | niri | mango | umbriel |
|---|---|---|---|---|---|
| **xdg-shell** | App windows, popups, configure/fullscreen | yes **v7** (bounds, caps, suspended, constrained) | yes (v6–7) | yes | yes (v6) |
| **zxdg-decoration** | Client vs server-side titlebars | yes (SSD in `ssd_mode`) | yes | yes | yes |
| **org_kde_kwin_server_decoration** | Old GTK SSD hint | no | yes | no | yes |
| **xdg_wm_dialog_v1** | Modal dialog + parent | **yes** (attach + shade + focus trap) | no* | no* | yes |
| **xdg-toplevel-tag-v1** | Stable window id for restore/rules | **yes** (store + IPC + `match.tag` rules) | no* | no* | yes |
| **xdg-toplevel-icon-v1** | Client-set window icon | yes | no* | no* | no* |
| **xdg-toplevel-drag-v1** | Tab/window drag between clients | no | no* | no | yes |
| **xdg-foreign-v2** | Embed another client’s surface | no | yes | no | yes |
| **xdg-activation-v1** | Focus/urgency tokens | yes (never steal; amber urgent) | yes | yes | yes |
| **xdg-system-bell-v1** | Visual/audio bell | yes (flash) | no* | no* | no* |
| **zwlr-layer-shell-v1** | Bar, wallpaper, OSD, launcher | yes **v5** + exclusive_edge | yes | yes | yes **v4** |
| **ext-workspace-v1** | Shell workspace list/activate | yes (per-output groups, stable trailing handle) | yes | yes | yes |
| **zwlr-foreign-toplevel-v1** | Taskbar window list + activate | yes **v3**; pin to workspace output | yes | yes | yes |
| **ext-foreign-toplevel-list-v1** | Stable ids for capture/shell | yes | yes | yes | yes |
| **ext-session-lock-v1** | Screen lock surfaces | yes (presentation-gated, dynamic layout coverage) | yes | yes | yes |
| **ext-idle-notify-v1** | Idle timeout for lockers | yes | yes | yes | yes |
| **zwp-idle-inhibit-v1** | Video/fullscreen keeps session awake | yes | yes | yes | yes |
| **zwlr-output-power-v1** | DPMS / output off | yes (does not remove logical head; wake before changing backend properties) | yes* | yes | no* |
| **zwlr-gamma-control-v1** | Night-light LUT | yes | yes | yes* | yes |
| **wp-color-manager-v1** | HDR / ICC / transfer | no | no | **hdr.c** custom | yes (if renderer supports) |
| **zwlr-output-management-v1** | kanshi / wlr-randr layout | yes (group test/apply; layout after commit) | yes | yes | yes |
| **zxdg-output-v1** | Logical size/name under fractional scale | yes | yes | yes | yes |
| **wp-fractional-scale-v1** | Non-integer scale | yes | yes | yes* | yes |
| **wp-viewporter** | Crop/scale buffer | yes | yes | yes* | yes |
| **wp-presentation** | Present timestamps | yes (v2) | yes | ? | yes (v2) |
| **wp-tearing-control-v1** | Async page-flip hint | yes (output `:tearing` + FS hint/rule; recovery) | no* | yes | yes |
| **wp-fifo-v1** | Commit after a refresh | no | no | no | no* |
| **wp-content-type-v1** | Game/video/photo hint | no | no | no | yes |
| **wp-alpha-modifier-v1** | Client surface opacity | no | no | no | yes |
| **wp-linux-drm-syncobj-v1** | Explicit GPU sync | no | smithay DRM path | ? | yes |
| **ext-data-control-v1** | Clipboard managers | yes | yes | ? | yes |
| **zwlr-data-control-v1** | Legacy clipboard mgr | yes | yes | ? | yes |
| **wp-primary-selection** | Middle-click paste | yes | yes | yes | yes |
| **zwlr-screencopy-v1** | grim / old portal capture | yes (dual-pass omits `capture_blocked`) | yes | ? | yes |
| **ext-image-copy-capture-v1** | New output capture | yes | yes | yes | yes |
| **ext-*-image-capture-source-v1** | Per-output / per-toplevel capture | yes (toplevel + privacy block) | yes | yes | yes |
| **ext-background-effect-v1** | Client blur regions (Noctalia) | yes (layers+toplevels per-rect; layer popup inherit) | yes | no* | compositor blur (no protocol) |
| **wp-cursor-shape-v1** | Named cursors | yes v2 + WM grab faces | yes | yes | yes |
| **zwp-pointer-gestures-v1** | Touchpad swipe/pinch | yes (`sf.gesture`) | yes | yes | yes |
| **zwp-relative-pointer-v1** | FPS look | yes | yes | yes | yes |
| **zwp-pointer-constraints-v1** | Lock/confine pointer | yes | yes | yes | yes |
| **zwp-virtual-keyboard-v1** | OSK / wtype | yes | yes | yes | yes |
| **zwlr-virtual-pointer-v1** | Synthetic mouse | no | yes | yes | yes |
| **zwp-tablet-v2** | Stylus | yes (tools + pads + pointer emulate) | yes | yes | yes |
| **zwp-text-input-v3** + **zwp-input-method-v2** | IME | yes (relay + grab + popups; sandbox gets text-input only) | yes | yes | yes |
| **zwp-keyboard-shortcuts-inhibit-v1** | Games grab Super | yes (active on focused surface; VT switch escapes) | yes | yes | yes |
| **wp-security-context-v1** | Flatpak restricted globals | yes (allowlist; blocks nested contexts & privileged globals) | yes | no | yes |
| **wp-drm-lease-v1** | VR / non-desktop DRM | no | ? | yes | yes |
| **Xwayland** | X11 apps | yes (on-demand lazy in-process; unified `sf_toplevel` + overlay override-redirect) | xwayland-satellite | in-process | on-demand in-process |
| **xdg-desktop-portal** | screencast UX | generic wlr/gnome | gnome portal | xdp-wlr | xdg-desktop-portal-umbriel |

\* = not confirmed in the sources used for the last pass; treat as “not advertised”
until a live dump or tree read says otherwise.

`./protocol/` only vendors XML this tree must wayland-scanner itself. Absence
from that directory does not mean unimplemented (`xdg-shell` via
`wlr_xdg_shell_create`).

### xdg-shell versions (stonefly v7)

| Ver | Events / states | Stonefly use |
|---|---|---|
| 1–3 | Toplevel/popup, tiled edges, ping | Core path |
| 4 | `configure_bounds` | Usable box (`sf_xdg_sync_toplevel`) |
| 5 | `wm_capabilities` | maximize / fullscreen / minimize (no window-menu until a menu exists) |
| 6 | `suspended` | Minimize / scratch hide, hidden workspace (not mid-slide), session lock |
| 7 | `constrained_*` edges | Tile/max/FS: all edges; snap: flush edges; float: geom∩usable |

Send capabilities and available bounds on initial configure, including remap.
Later syncs compare against the last scheduled values so an unchanged client
commit does not request another configure.

Do not suspend columns that are only scrolled off-strip: they stay in the
scene and foreign-toplevel still pins them to the output.

### Highest-leverage protocol gaps

1. Full `wp_color_manager_v1` (HDR/output) — multi-day; leave unadvertised until then (§5)
2. `wp_fifo_v1` — no `wlr_fifo_v1` in wlroots 0.20 (stable tip); leave
   unadvertised until upstream helper or a hand-rolled surface-blocker path

---

## C ↔ Lua border

Stonefly’s product aim: a compositor whose **window-management brain** is as
scriptable as Neovim — C owns the kernel; Lua owns policy and packs. This
section is the border contract: what lives where **today**, what must stay in
C, and what to push into the Lua runtime. Shipped #8 audit notes live in
FEATURES; open knobs feed med/low deferred inventory below and **TODO NEXT** #3
(`sf.on` / `sf.wins`).

**Rule of thumb:** if two reasonable users would disagree, or a pack would want
to change it, it belongs in Lua or opts. If getting it wrong corrupts the scene
graph or Wayland state, it stays in C.

```text
TODAY                              TARGET
─────                              ──────
Lua: fill tables at load           Lua: live policy + packs
C:   interpret tables forever      C:   primitives + invariants + Wayland

Config ──compile──► C state        Events ──► Lua ──► actions ──► C
Gestures / mouse / wheel ✅        Maximize matrix / focus / insert → opts
SSD buttons / drop seam still C    One opt/Lua matrix → C verbs
```

### What C must keep (forever)

Not “policy stuck in C” — the substrate. Lua calls these; it does not
reimplement them.

| Domain | Examples |
|---|---|
| Protocol / backend | Wayland, wlroots, SceneFX, outputs, seats, layers, foreign-toplevel |
| Scene & time | Scene graph, damage, animation clocks, morph/slide apply |
| Lifecycle | Map/unmap, configure, place_sm / layout_sm step+apply |
| Geometry kernels | Column strip math, usable box, snap boxes, ensure-on-screen |
| Action *primitives* | Focus, raise, join/break column, set FS / edge-max / gap-max / float, spawn, hide/show |
| Invariants | Slot memory, exclusive fullscreen, sticky float vs transient edge-max, no focus onto covered windows |
| Implementation caps | `SF_ARRANGE_MAX`, IPC reply buffers — not UX policy |

Predictability (audit below) is enforced here. Lua may *request* “unmax” or
“pan”; C *does* it in a round-trippable way.

### What Lua can do today

In-process **LuaJIT** + Fennel-first defaults (`src/config/`, `config_lua.c`).
The VM stays open after a successful load. Almost everything is still
**declarative at init** — not a session brain.

| API | Role |
|---|---|
| `sf.opt` | Numbers / bools / colors / enums → `struct sf_config` |
| `sf.bind` / `sf.tap` / `sf.chain` | Key / mouse / Mod+Wheel chords → `sf_action_step[]` |
| `sf.gesture` | Touchpad swipe + pinch → workspace / columns_pan / actions |
| `sf.window_rule` | Match + declarative chrome/float/size + WM actions |
| `sf.scratchpad` | Named pads |
| `sf.input` | Libinput / keyboard device rules |
| `sf.output` | Per-monitor arrange (`stacking`/`columns`/`inherit`) |
| `sf.exec` | Session spawn list (after `WAYLAND_DISPLAY`) |
| `sf.reload` | Re-run init mid-session (same as `:reload` / IPC / inotify) |
| `sf.packadd` / `sf.stdpath` / rtp | Pack path scaffolding (Neovim-shaped) |

**Not today:** `sf.on` / autocmd-style handlers; querying or mutating windows
from Lua mid-session (`sf.wins`, …); replacing focus / insert / maximize / SSD
chrome policy with Lua *functions* (still hardcoded or only partially opt’d —
see deferred inventory); packs that react to map / focus / arrange without new
C. Summary: **strong static config**, **live reload**, still a **weak runtime**
until TODO NEXT #2.

Default tree: `src/config/default/` (`options.fnl`, `binds.fnl`, `gestures.fnl`,
`rules.fnl`, `inputs.fnl`, `outputs.fnl`, `scratchpads.fnl`, `execs.fnl`, …).
Docset: `dist/fennel-ls/docsets/sf.lua`.

### Push tiers (C → Lua)

Ordered by leverage. Each tier assumes the one above is honest (no new
hardcoded policy while a bind/opt/Lua surface exists).

**Tier 1 — bind/opt-shaped policy (FEATURES #8 ✅).** Same smell as the old
hardcoded Super+LMB and 3-finger swipe. Expose as data or thin Lua; C only runs
the chosen verb. High leftovers shipped; med/low stay in deferred inventory below.

| Shipped | Surface |
|---|---|
| 3-finger V/H + Super+pinch | `sf.gesture` |
| Super+Wheel / Super+Shift+Wheel | `sf.bind` wheel + `wheel_bind_cooldown_ms` |
| Super+LMB/RMB/Shift+RMB grabs | `sf.bind` + `move_window` / `resize_window` / `resize_center` |
| Chrome/protocol maximize | `sf.opt.client_maximize` (default `gaps` = Super+m) |
| Unminimize workspace | `sf.opt.unminimize_workspace` (default `active`) |
| Per-op anim + dim/opacity | `sf.opt.anim`, `dim_inactive`, rule `opacity` / `no_dim` |
| Modal dialog attach | `sf.opt.attach_modal_dialogs`, `dialog_dim` |

**Maximize verbs (keep both binds):**

| Verb | Meaning | Default bind / callers |
|---|---|---|
| **edge-max** | Zen / usable-fill, gapless | Super+f, `:maximize_to_edges`, Super+pinchout |
| **gap-max** / columns **full_width** | Column / gap-inset max | Super+m; CSD / SSD max+dblclick / foreign follow `client_maximize` (default `gaps`) |

**Tier 2 — event runtime (`sf.on`) — TODO NEXT #2.** Without this, packs cannot
replace ad-hoc C branches.

```text
sf.on("map" | "unmap" | "focus" | "arrange" | "workspace"
    | "fullscreen" | "output" | …, handler)
```

Handlers call primitives only (focus / float / join / edge-max / FS / spawn /
notify / read opts). SoftPlace late title/app_id and scratchpad click-outside
catcher also belong here once events exist.

**Tier 3 — introspect + mutate (with #6).** `sf.wins()` / focused / on
workspace; read layout mode, column/row, geom, floating/FS flags; safe setters
through existing C verbs (no raw scene writes from Lua).

**Tier 4 — stay in C even when Lua is rich.** Strip pack / appear glue /
exclusive FS / slot restore; anything frame-rate or protocol-correctness
sensitive; invariant checks after Lua-requested actions.

### Predictability vs flexibility

Scriptability must not reintroduce surprises. Lua policy still goes through C
verbs that preserve strip slot round-trips, exclusive fullscreen, sticky float
vs transient tile edge-max, and arrange switch that does not silently demote
max/snap/FS. When adding an `sf.on` hook, define whether it runs before or after
the C default, and whether it can cancel. Prefer **compose** (Lua runs, then C
invariant pass) over **replace** for lifecycle.

### File map (today)

| Path | Role |
|---|---|
| `src/config/config_lua.c` | `sf.*` Lua API, load/reload, VM lifetime |
| `src/config/config_watch.c` | inotify + 150ms debounce → `sf_config_reload` |
| `src/config/default/*.fnl` | Embedded Fennel defaults |
| `src/config/action.c` | Action name ↔ enum (bind/rule/IPC shared) |
| `src/wm/action_run.c` | C verb catalog |
| `src/seat/gestures.c` | Consumes `sf.gesture` swipe + pinch table |
| `src/config/mousebind.c` | Seed + match for `sf.bind` mouse chords |
| `src/config/wheelbind.c` | Seed + match for `sf.bind` Mod+Wheel chords |
| `src/seat/pointer.c` | Mousebinds → grabs; wheelbinds on axis; border LMB hit-test |
| `src/ssd/ssd.c` | Title buttons + double-click; max → `client_maximize` |
| `src/config/policy.c` | `client_maximize` / `unminimize_workspace` parse |
| `src/wm/arrange.c` | Drop seam, insert-after-focus, focus ring, arrange cycle |
| `runtime/pack/` | Pack path / fennel-shim |
| `dist/fennel-ls/docsets/sf.lua` | fennel-ls docset for `sf.*` |

### Deferred inventory

#8 shipped the bind/opt surfaces and this contract. Items below are **optional
follow-ups** — pick when pain shows. Prefer opts/binds before `sf.on`. Rule
unchanged: other than forever-C primitives, UX-disputed policy should not grow
new C hardcoding.

#### Shipped with #8 (do not re-litigate)

| Surface | Notes |
|---|---|
| `sf.opt` chrome / gaps / snap / tap / wheel cooldown / swipe / SSD size / resize hitband / opacity / blur / `cursor_follows_focus` / `config_watch` / `client_maximize` / `unminimize_workspace` / `anim` / `dim_inactive` / `focus_cycle` / `tile_insert` / `column_drop_seam` / `tiled_grab_remap` / `border_resize` | `options.fnl` + C defaults |
| `sf.bind` keys / mouse / Mod+Wheel | `binds.fnl`; C seeds recovery-only (seed hygiene below) |
| `sf.gesture` | 3-finger V/H + Super+pinch → max/unmax |
| `sf.tap` / scratchpads / rules / inputs / execs / live reload | default tree |

#### Deferred Tier 1 knobs (opt/bind; no `sf.on` required)

High rows shipped (FEATURES). Med/low are not pick-queue blockers.

| ID | Where | Default today | Proposed surface | Priority |
|---|---|---|---|---|
| `arrange_cycle` | `arrange.c` `arrange_cycle_order[]` | stacking ↔ columns | `sf.opt.arrange_cycle` list | med |
| `appear_give_up` | `wm/wm.c` `arrange_prop_waits >= 1` | 1 post-propose commit (tol 2px) | `sf.opt.appear_give_up_commits` | med |
| `pinch_threshold` | `seat/gestures.c` | 0.15 | `sf.opt.pinch_scale_threshold` | med |
| `ssd_buttons` | `ssd/ssd.c` | close / min hardcoded; max → `client_maximize` | declarative close/min → action | med |
| `ssd_dblclick` | `ssd/ssd.c` | 400ms → `toggle_client_maximize` | `sf.opt.ssd_dblclick_ms` / custom action | low |
| `ipc_quick_term` | `ipc/ipc.c` | pad `"term"` | opt or IPC arg | low |
| `config_watch_debounce` | `config_watch.c` | 150ms | `sf.opt.config_watch_debounce_ms` | low |
| `column_min_frac` | arrange_math / column | 0.05 floor | `sf.opt.column_min_frac` | low |
| `resize_center_min` | `pointer.c` | 50 | `sf.opt.resize_center_min` | low |
| `snap_resist_mul` | `drag_snap.c` | 4 × `snap_threshold` | fold into snap opts | low |
| `ssd_chrome_layout` | `ssd_layout.c` | fixed min+max+close boxes | button set / layout opts | low |
| `pan_focus` | `arrange.c` columns pan | centre-most visible; no warp | keep / warp / under-pointer | low (also Predictability Medium) |
| `focus_on` | `pointer.c` | click-to-focus | `click`\|`sloppy` (careful enter) | low |
| `map_focus` | `xdg/xdg.c` map | always focus new map | opt, or Tier 2 `sf.on("map")` | low |
| `transient_edge_max` | focus / Tab | tile edge-max is transient | document; sticky = float+rule | docs / Stay |

`tiled_slide` / `anim_ease` folded into shipped `sf.opt.anim` (FEATURES).

#### Predictability Medium knobs

| Gap | Direction |
|---|---|
| Maximize entry-point matrix | ✅ `client_maximize` (default gaps); Super+f remains zen |
| Unminimize → active WS | ✅ `unminimize_workspace` (default active) |
| Transient tile edge-max | Keep transient; document; sticky = float+edge-max / rule (TODO NEXT #1) |
| Columns pan focus retarget | `pan_focus` opt if users disagree (TODO NEXT #1) |

#### Seed hygiene (C vs Fennel)

Day-zero truth is `src/config/default/*.fnl`. C seeds exist for
`STONEFLY_NOCONFIG` / Lua boot failure.

| Issue | Direction |
|---|---|
| C keybind seed omits F11, Super+Prior/Next, Super+1–9, Alt+Space, … that `binds.fnl` has | Shrink C seed to a tiny recovery set, or sync it — do not pretend C is product defaults |
| First `sf.bind` / mouse / wheel / `sf.gesture` clears that C table only | Keep (replace pattern); document |
| C numeric defaults (opacity 0.96, corner 12, …) differ from `options.fnl` (0.90 / 8, …) | Align C with Fennel **or** document NOCONFIG as “minimal recovery”, not day-zero chrome |
| Legacy `master_*` / `dwindle_*` still in `sf.opt` | Dormant while product arrange is stacking+columns; drop when unused |

#### Tier 2–3 gate (TODO NEXT #2)

Do not invent more one-off C branches for pack-shaped policy:

1. `sf.on(event, handler)` — map / unmap / focus / arrange / layout / workspace / fullscreen / output
2. `sf.wins()` / focused / safe setters through existing verbs
3. SoftPlace / scratchpad catcher / dim-spotlight packs live in Fennel
4. Derived screen policy example: Noctalia bar opacity from “any visible edge-max on output” (recompute on layout/unmap/workspace — never toggle on a single max/unmax)

#### Done when

**#8 bar (met):** gestures, mouse/wheel grabs, chrome maximize mapping, and
unminimize WS policy are opts/binds; contract + inventory exist; live reload
updates them without dropping the session.

**Full Neovim-WM bar (still open):** focus cycle / insert / SSD close-min map /
drop seam / … without a C patch (deferred high knobs); a Fennel pack can
subscribe to map/focus/arrange (`sf.on`) and call documented primitives
(`sf.wins`) safely; C still guarantees boring round-trips (Predictability High
stays green).

---

## TODO NEXT

Fresh pick-queue (tiling overview / big engines excluded). Knock these out in
order when choosing “what next”; chapter tables below hold detail. Carry-overs
from the previous stack plus high-value leftovers and low-hanging fruit.

| # | Item | Status | Why |
|---|---|---|---|
| 1 | **Predictability Medium leftovers** | 🚧 | FS unFS→pack; snap↔max one “normal” box; document transient tile edge-max; pan-focus opt |
| 2 | **`sf.on` + window query** | ❌ | Tier 2–3 Neovim leap (C↔Lua border). Packs subscribe to map/unmap/focus/arrange/layout/workspace and **recompute** from `sf.wins()` — not one-shot max/unmax. Example: Noctalia bar opaque iff any visible edge-max on that output (`noctalia msg config-set bar.default.background_opacity`); naive bind-side toggle breaks with 2+ maxed windows |

**Shipped this stack:** scratchpad claim via xdg-activation; IME relay; tablet
v2; deferred WM opts high (`focus_cycle`, `tile_insert`, `column_drop_seam`,
`tiled_grab_remap`, `border_resize`); `sf.output` / `outputs.fnl` + IPC
`config outputs` — all FEATURES.

**After this stack (not numbered):** IPC `action` allowlist; APK flip to release
buildtype when ASan hunting ends; `--write-config-lua`; protocol hollow-ads
optional SSD icon blit; `wp_fifo_v1` / full color-mgmt (unadvertised);
geometry persist;
scratchpad click-outside catcher; winmenu (or Noctalia); columns polish leftovers.
Queue is #1–#2.

---

## Predictability audit — still open

**Principle:** arrange switch, float/unfloat, max/unmax, FS, hide/show, WS/monitor
send must round-trip. No silent demotion, forgotten cell, or focus onto covered
windows. High-severity gaps are fixed (see FEATURES). Arrange switch:
columns→stacking cascades at preferred/natural size; stacking→columns restores
slots or one window per column (FEATURES).

### Medium

| Gap | What surprises | Direction |
|---|---|---|
| **Transient tile edge-max** | Focus/Tab drops overlay back to cell — right for “peek”, wrong if user wanted sticky | Keep transient; sticky = float+edge-max / rule. Document in defaults (TODO NEXT #1) |
| **FS saved box vs tile cell** | UnFS restores layout_sm saved geom; column pack may overwrite | If `arrange_managed`, pack instead of saved-box morph |
| **Snap ↔ max ↔ unmax** | PreMax / PreSnap stacking after toggle chains | One saved “normal” box; modes are overlays |
| **Pan focus vs click focus** | 3-finger pan retargets centre-most; pointer may sit over another cell | Optional: warp or focus-under-pointer on swipe end |

### Low

| Gap | Notes |
|---|---|
| Empty column shells after FS-only column | Membership kept, pack skips FS — watch empty-shell leaks |
| SoftPlace late title / app_id | Appear-hold is late-title only (empty app_id no longer parks forever — Chrome Task Manager). Watch late app_id morph still |
| Output disconnect / refuge | Live validation: unplug/replug with tiled, minimized and fullscreen clients; verify recovery when the last head was lost |

---

## How to read

| Symbol | Meaning |
|---|---|
| **🚧** | Partial, stub, or day-zero subset |
| **📋** | Mentioned / planned, not shipped |
| **❌** | Not present |
| **🌙** | Deferred to external shell (Noctalia) |

---

## 1. Window management

| Feature | Status | Notes |
|---|---|---|
| Focus next / prev including minimized | 🚧 | Alt+Tab → noctalia switcher; Alt+Shift+Tab `FOCUS_PREV_ALL` (unhides). Super+Tab visible-only (columns ring includes sticky floats; focus raises so targets stay visible) |
| Window menu / winmenu | ❌ | SSD titlebars shipped |
| Geometry persist across restart | ❌ | Tag protocol shipped; no persist consumer |
| Workspaces polish | 🚧 | Named rules / geometry persist open. Switch slide + strip shipped (FEATURES) |
| Columns polish | 🚧 | Still open: `center_focused=on_overflow`, consume-from bind, tabs, drag edge pan; deck/grid declined |
| Focus cycle opt (MRU / spatial / strip) | ✅ | `sf.opt.focus_cycle`; FEATURES |
| Picture-in-picture float | ❌ | Related to always-on-top |
| Scratchpad click-outside catcher | ❌ | `auto_hide` on focus change ships |
| Scratchpad claim vs icons | ✅ | xdg-activation token claim; optional app_id fallback; default Ghostty keeps real icons |
| SSD per-app overrides / winmenu | 🚧 | Rule `ssd` ships; winmenu open |

---

## 2. Presentation / animations / eye candy

Blur region plates + layer popup inherit shipped (FEATURES). Optional later:
Alpine SceneFX `ignore_alpha` threshold for transparency-mask blur.

---

## 3. Shell chrome

| Feature | Status | Notes |
|---|---|---|
| Winmenu | ❌ | |
| Power confirm / power menu | ❌ | |
| Hold-modifier cheatsheet | ❌ | |
| Clipboard history panel | ❌ | Protocols shipped; panel is shell |
| Color picker | ❌ | |
| Dock with magnification | ❌ | |
| Desktop widgets | ❌ | |
| In-process wallpaper engine | ❌ | Layer-shell wallpaper clients OK |

Bar / launcher / OSD / notifier / control center stay **🌙 Noctalia**.

---

## 4. D-Bus / session

| Feature | Status | Notes |
|---|---|---|
| ScreenSaver Inhibit | ❌ | |
| UPower kbd / power profiles | ❌ / 🌙 | |
| Portal FileChooser (wallpaper) | ❌ | |
| Systray StatusNotifierItem | ❌ / 🌙 | |

---

## 5. Protocols & I/O — open gaps

Full peer matrix and versions: **Protocols (running inventory)** above. This
chapter is **open / hollow / missing only** (advertise ≠ full effect).

| Feature | Status | Notes |
|---|---|---|
| `xdg-toplevel-icon-v1` SSD blit | ❌ | Name + buffers stored; IPC `icon_name`. No theme lookup; SSD does not paint buffers (Noctalia uses app_id). Optional thin consumer later |
| Screencast / capture_sessions UI | ❌ | Screencopy + ext-image-copy-capture wire shipped; portal session UX open |
| `wp_fifo_v1` | ❌ | Protocol exists (staging); wlroots 0.20 has no `wlr_fifo_v1` helper (MR existed, not in 0.20.2). SceneFX unrelated. Do not hollow-advertise. Presentation-time ships |
| Full color-management | ❌ | Not advertised (hollow ad worse than silence). Gamma LUT ships; Umbriel-class HDR is multi-day |

**Intentional subsets** (documented in FEATURES, not gaps to “finish”):
`xdg-activation` = urgency / never focus-steal; `xdg-system-bell` = visual
flash only; layer-shell keyboard auto-focus = **exclusive** only (`on_demand`
stays click-to-focus); foreign has no URGENT state (IPC urgency instead).

---

## 6. Config, IPC, tooling

| Feature | Status | Notes |
|---|---|---|
| `--write-config-lua` | ❌ | Offload compiled/plain Lua instead of `.fnl` |
| IPC `action <name>` allowlist | ❌ | |
| IPC inputs (live devices) | ❌ | `config inputs` rules ✅ |
| Nano syntax highlight | ❌ | |
| APK release buildtype | 🚧 | **TEMP:** clang debugoptimized + ASan/UBSan; flip when crash hunting ends |
| Exit CPU/heap profiles | ❌ | |
| Neovim API compat / fake `vim.*` | ❌ | Non-goal |
| Fennel as C dependency | ❌ | Non-goal |
| Remote plugin fetch | ❌ | Non-goal for v1 |

Deferred bind/opt knobs (med/low): **C ↔ Lua border** deferred inventory.
High knobs shipped (FEATURES).

---

## 7. Still-open FORK / gap crumbs

| Pri | Item | Status |
|---|---|---|
| P1 | `sf.on` + window query | ❌ TODO NEXT #2 |
| P2 | Workspaces polish | ❌ rules / geometry persist |
| P2 | SSD toplevel-icon blit (optional) | ❌ §5 |
| P2 | Columns polish leftovers | 🚧 §1 (`center_focused=on_overflow`, drag-edge pan) |
| P2 | `wp_fifo_v1` | ❌ |
| P2 | Tablet v2 | ✅ FEATURES |

---

## 8. Day-zero holes still open

| Habit | Status |
|---|---|
| Winmenu | ❌ |
| Geometry persist | ❌ |
| Alt+Tab cycle policy polish | 🚧 noctalia switcher + `FOCUS_PREV_ALL` |

---

## 9. Suggested attack order

Primary queue is **TODO NEXT** at the top (#1–#3). After that stack:

1. IPC action allowlist; APK release buildtype; `--write-config-lua`
2. Capture: portal capture_sessions UX; optional SSD icon blit
3. Not-yet-advertised: `wp_fifo_v1` (no wlroots 0.20 helper — hand-roll or wait), full color-mgmt (multi-day)
5. Optional WM: columns polish leftovers; geometry persist; overview polish (card drag, multi-surface cards, shortcut badges)
6. **Bind context redesign** (own chapter below) — after overview/arrange filters feel cramped

Shell/OSD/media stay **🌙 Noctalia**.

---

## 10. Bind context (own chapter)

**Status:** 📋 Preferential `:arrange` filter ships today (including `"overview"`).
A generalized context / `:when` / submap surface needs a careful redesign — do
not grow more boolean axes onto `sf.bind` ad hoc.

### What ships now

- `sf.bind` optional `:arrange` — `stacking` / `tiling` (any tile engine) /
  exact engine / **`overview`**. Match prefers arrange-specific over unset
  (`sf_arrange_bind_matches` + keyboard/mouse/wheel loops).
- Overview interactive forces context via `sf_bind_context_arrange` →
  `SF_ARRANGE_OVERVIEW`, so tiling-only binds stay dormant and Escape /
  Return / Space / bare Wheel can be overview-only without eating normal
  client input when the filmstrip is closed.
- Gestures use a parallel rule: `:overview_pan` is skipped when closed and
  score-boosted when open (same fingers as workspace / columns_pan).

### Why a redesign

`:arrange` is a layout/engine tag overloaded as a modal UI gate. That works
for stacking↔tiling and for overview, but it does not compose cleanly with
session lock, move/resize grabs, confirmation dialogs, scratchpad catchers,
or user-defined modal layers (screenshot chords, resize-chord submaps).

### Peer models (steal from)

| Peer | Model | Notes for stonefly |
|---|---|---|
| **Umbriel** | Explicit **submaps** (`submap name` / `submap reset`); overview keeps normal keybinds active + Escape/shortcut badges | Best reference for modal layers. Overview is *not* a submap — actions adapt in-place |
| **Hyprland** | Submaps (enter/reset); binds can be submap-scoped | Same shape as Umbriel; widely known pack pattern |
| **Sway / i3** | Named `mode` with mode-local binds; Escape → default | Classic; modes replace the whole bind set while active |
| **niri** | Mostly global binds; overview / actions no-op or hardcode Escape | Simpler; less pack power |

Stonefly should steal Umbriel/Hyprland submaps for *user* modal layers, and
keep overview as a first-class context (like today) rather than forcing packs
to enter a submap on Super+o.

### Design sketch (when picked up)

1. **Contexts as a small enum / bitset owned by C** — at least: default,
   overview, session-lock, grab (move/resize), maybe confirm. Lua reads via
   something like `sf.context` / bind `:when`.
2. **Preferential match** stays (specific context beats unset) — same
   keyboard loop shape as `:arrange` today.
3. **User submaps** as a Lua/pack layer on top (enter/reset action +
   `submap[name],` prefix or `:submap` field), not as more arrange enums.
4. **Do not** make every layout engine a full bind namespace forever — keep
   `:arrange "tiling"` as the coarse filter; exact engines stay optional.
5. Gesture match already takes `overview_open`; generalize only if a second
   gesture context appears (unlikely soon).

### Non-goals for the first cut

- Full i3-style mode replacement of the entire bind table
- Per-window or per-app bind contexts (window rules already cover actions)
- Replacing `:arrange` before packs depend on overview filters

---

## 11. Testing — open

| Suite | Status | Scope | Gate |
|---|---|---|---|
| Unit (more pure) | 📋 | Remaining config decode / rules match without Lua VM; deeper JSON paths | Extend `make test` |
| Integration | 📋 later | Headless / virtual WL + client/IPC drivers | Separate Meson tag; skip by default |
| Coverage floors / HTML | 📋 optional | gcovr/`llvm-cov`; fail under a floor | Dev/CI only — not APK `check()` |

Shipped unit surface: [`FEATURES.md`](FEATURES.md) (Testing and packaging).

### Non-goals (for now)

- Third-party C unit frameworks as `checkdepends`
- Mocking / linker `--wrap` as the default story
- Full compositor under every `meson test`
- Coverage % gate inside APK `check()`

---

## Legend for future edits

When a non-protocol row ships: update **FEATURES.md**, **delete** the roadmap
row (and any TODO NEXT entry). For protocols: update the **Protocols (running
inventory)** cell in place (and §5 / TODO NEXT when still open). Prefer
stonefly source paths (`src/wm/…`) in notes.
