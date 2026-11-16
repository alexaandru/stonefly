# From C to Lua — Neovim for Wayland WMs

Stonefly’s product aim: a compositor whose **window-management brain** is as
scriptable as Neovim — C owns the kernel; Lua owns policy and packs.

This note is the border contract: what lives where **today**, what must stay in
C, and what to push into the Lua runtime so the WM stays flexible without
becoming surprising or unsafe.

Related: [`ROADMAP.md`](ROADMAP.md) TODO NEXT #8 ✅ (hardcoded → Lua audit);
#7 gestures ✅ (incl. Mod+Wheel); #9 live reload ✅; [`LESSONS.md`](LESSONS.md);
Predictability audit in the roadmap (round-trips stay boring even when Lua
decides). **Deferred knobs + Tier 2–3:** §9 below (no longer blocks TODO NEXT).

---

## 1. The Neovim analogy

| Layer | Owns |
|---|---|
| **C** | Buffer/scene model, input, protocol, jobs — fast, correct, boring primitives |
| **Lua** | Policy, UX composition, plugins — anything a user or pack might redefine |

Neovim without `autocmd` / `vim.api` is just a big `init.lua` that fills
options. Stonefly today is closer to that: a **load-time config compiler** into
C tables. The target is a **live runtime** that can observe events and call
primitives.

```text
TODAY                              TARGET
─────                              ──────
Lua: fill tables at load           Lua: live policy + packs
C:   interpret tables forever      C:   primitives + invariants + Wayland

Config ──compile──► C state        Events ──► Lua ──► actions ──► C
Gestures / mouse / wheel ✅        Maximize matrix / focus / insert → opts
SSD buttons / drop seam still C    One opt/Lua matrix → C verbs
```

**Rule of thumb:** if two reasonable users would disagree, or a pack would want
to change it, it belongs in Lua or opts. If getting it wrong corrupts the scene
graph or Wayland state, it stays in C.

---

## 2. What C must keep (forever)

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

Predictability (see ROADMAP **Predictability audit**) is enforced here. Lua may
*request* “unmax” or “pan”; C *does* it in a round-trippable way.

---

## 3. What Lua can do today

In-process **LuaJIT** + Fennel-first defaults (`src/config/`, `config_lua.c`).
The VM stays open after a successful load (pattern match uses live
`string.find`). Almost everything is still **declarative at init** — not a
session brain.

| API | Role |
|---|---|
| `sf.opt` | Numbers / bools / colors / enums → `struct sf_config` |
| `sf.bind` / `sf.tap` / `sf.chain` | Key / mouse / Mod+Wheel chords → `sf_action_step[]` |
| `sf.gesture` | Touchpad swipe + pinch → workspace / columns_pan / actions |
| `sf.window_rule` | Match + declarative chrome/float/size + WM actions |
| `sf.scratchpad` | Named pads |
| `sf.input` | Libinput / keyboard device rules |
| `sf.exec` | Session spawn list (after `WAYLAND_DISPLAY`) |
| `sf.reload` | Re-run init mid-session (same as `:reload` / IPC / inotify) |
| `sf.packadd` / `sf.stdpath` / rtp | Pack path scaffolding (Neovim-shaped) |

**Not today**

- `sf.on` / autocmd-style event handlers
- Querying or mutating windows from Lua during the session (`sf.wins`, …)
- Replacing focus / insert / maximize / SSD chrome policy with Lua *functions*
  (still hardcoded or only partially opt’d — see §9)
- Packs that react to map / focus / arrange without new C

Summary: **strong static config**, **live reload of that config**, still a
**weak runtime** until `sf.on` / window query land. Bind-shaped input policy
(gestures, mouse grabs, Mod+Wheel) is already out of C.

Default tree: `src/config/default/` (`options.fnl`, `binds.fnl`, `gestures.fnl`,
`rules.fnl`, `inputs.fnl`, `scratchpads.fnl`, `execs.fnl`, …). Docset:
`dist/fennel-ls/docsets/sf.lua`.

---

## 4. What to push from C → Lua

Ordered by leverage. Each tier assumes the one above is honest (no new
hardcoded policy while a bind/opt/Lua surface exists). Full file-level
inventory: **§9**.

### Tier 1 — bind/opt-shaped policy (TODO NEXT #8 ✅)

Same smell as the old hardcoded Super+LMB and 3-finger swipe. Expose as data or
thin Lua; C only runs the chosen verb. **#8 closed** on the surfaces below;
smaller knobs stay deferred in §9.2.

| Shipped | Surface |
|---|---|
| 3-finger V/H + Super+pinch | `sf.gesture` (#7) |
| Super+Wheel / Super+Shift+Wheel | `sf.bind` wheel + `wheel_bind_cooldown_ms` |
| Super+LMB/RMB/Shift+RMB grabs | `sf.bind` + `move_window` / `resize_window` / `resize_center` |
| Chrome/protocol maximize | `sf.opt.client_maximize` (default `gaps` = Super+m) |
| Unminimize workspace | `sf.opt.unminimize_workspace` (default `active`) |

**Maximize verbs (keep both binds):**

| Verb | Meaning | Default bind / callers |
|---|---|---|
| **edge-max** | Zen / usable-fill, gapless | Super+f, `:maximize_to_edges`, Super+pinchout |
| **gap-max** / columns **full_width** | Column / gap-inset max | Super+m; CSD / SSD max+dblclick / foreign follow `client_maximize` (default `gaps`) |

### Tier 2 — event runtime (`sf.on`) — the real leap

Without this, packs cannot replace ad-hoc C branches.

```text
sf.on("map" | "unmap" | "focus" | "arrange" | "workspace"
    | "fullscreen" | "output" | …, handler)
```

Handlers call primitives only, for example:

- focus / float / unfloat / join / edge-max / FS
- spawn / notify
- read opts, schedule user logic

Then “Slack stays float+edge-max”, custom insert rules, “when FS then …”, live
in Fennel packs instead of `wm/*.c`. SoftPlace late title/app_id and
scratchpad click-outside catcher also belong here once events exist.

### Tier 3 — introspect + mutate

| Need | Why |
|---|---|
| `sf.wins()` / focused / on workspace | Packs decide from state, not only from load-time rules |
| Read layout mode, column/row, geom, floating/FS flags | Same |
| Safe setters through existing C verbs | No raw scene writes from Lua |

### Tier 4 — stay in C even when Lua is rich

- Strip pack / appear glue / exclusive FS / slot restore
- Anything frame-rate or protocol-correctness sensitive
- Invariant checks after Lua-requested actions

---

## 5. Predictability vs flexibility

Scriptability must not reintroduce surprises. Lua policy still goes through C
verbs that preserve:

- Strip slot round-trips (leave_tiling / float / max / WS move)
- Exclusive fullscreen + no Tab onto covered windows
- Sticky float vs transient tile edge-max
- Arrange switch that does not silently demote max/snap/FS

When adding an `sf.on` hook, define whether it runs before or after the C
default, and whether it can cancel. Prefer **compose** (Lua runs, then C
invariant pass) over **replace** for lifecycle.

---

## 6. Practical sequence

Matches ROADMAP TODO NEXT; do not grow new hardcoded policy in parallel.

1. **#7 Configurable gestures** — ✅ `sf.gesture` swipe + pinch; Mod+Wheel ✅
   via `sf.bind`.
2. **#8 Hardcoded → config/Lua audit** — ✅ closed (gestures/mouse/wheel,
   `client_maximize`, `unminimize_workspace`, contract). Deferred knobs §9.2.
3. **#9 Live reload** — ✅ shipped (re-run Lua init; inotify; keep previous on
   failure). Edit Fennel → session updates without logout.
4. **`sf.on` + window query API** — Tier 2–3; packs become real (next big leap).
5. **Deferred Tier 1 knobs** — pick from §9.2 when pain shows (focus/insert,
   drop seam, border LMB, anim table, …).

Shell UI (bar, launcher, OSD) stays **Noctalia**. Compositor Lua is WM policy,
not a second shell.

---

## 7. Done when

**#8 bar (met):** gestures, mouse/wheel grabs, chrome maximize mapping, and
unminimize WS policy are opts/binds; contract + inventory exist; live reload
updates them without dropping the session.

**Full Neovim-WM bar (still open):**
- Focus cycle / insert / SSD close-min map / drop seam / … without a C patch
  (§9.2 deferred).
- A Fennel pack can subscribe to map/focus/arrange (`sf.on`) and call
  documented primitives (`sf.wins`) safely.
- C still guarantees boring round-trips (Predictability audit High stays green).

---

## 8. File map (today)

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

---

## 9. Deferred inventory (after TODO NEXT #8 ✅)

#8 shipped the bind/opt surfaces and this contract. Items below are **optional
follow-ups** — document here, pick when pain shows. Prefer opts/binds before
`sf.on`. Rule unchanged: other than §2 primitives, UX-disputed policy should
not grow new C hardcoding.

### 9.1 Shipped with #8 (do not re-litigate)

| Surface | Notes |
|---|---|
| `sf.opt` chrome / gaps / snap / tap / wheel cooldown / swipe / SSD size / resize hitband / opacity / blur / `cursor_follows_focus` / `config_watch` / `client_maximize` / `unminimize_workspace` | `options.fnl` + C defaults |
| `sf.bind` keys / mouse / Mod+Wheel | `binds.fnl`; C seeds recovery-only (§9.4) |
| `sf.gesture` | 3-finger V/H + Super+pinch → max/unmax |
| `sf.tap` / scratchpads / rules / inputs / execs / live reload | default tree |

### 9.2 Deferred Tier 1 knobs (opt/bind; no `sf.on` required)

| ID | Where | Default today | Proposed surface | Priority |
|---|---|---|---|---|
| `column_drop_seam` | `wm/arrange.c` `SF_COLUMN_DROP_SEAM` | 0.15 (mid=swap, fringe=insert, outer=new col) | `sf.opt.column_drop_seam` | high |
| `tiled_grab_remap` | `xdg/xdg.c` `sf_begin_interactive` | tiled move→`ARRANGE_SWAP`; tiled resize→`width_frac` | verbs (`swap_window` / `column_resize`) or opt | high |
| `border_lmb` | `seat/pointer.c` | bare LMB on hitband → resize (sizes already opts; both 0 disables) | `sf.opt.border_resize` bool, or mousebind mods==0 | high |
| `focus_cycle` | `arrange.c` atan2; `wm.c` MRU | tiling=spatial; stacking=MRU | `sf.opt.focus_cycle` = `spatial`\|`mru`\|`strip` | high |
| `tile_insert` | `arrange.c` join `after + 1` | after focused column | `sf.opt.tile_insert` = `after_focus`\|`end`\|`begin` | high |
| `arrange_cycle` | `arrange.c` `arrange_cycle_order[]` | stacking ↔ columns | `sf.opt.arrange_cycle` list | med |
| `appear_give_up` | `wm/wm.c` `arrange_prop_waits >= 1` | 1 post-propose commit (tol 2px) | `sf.opt.appear_give_up_commits` | med |
| `pinch_threshold` | `seat/gestures.c` | 0.15 | `sf.opt.pinch_scale_threshold` | med |
| `ssd_buttons` | `ssd/ssd.c` | close / min hardcoded; max → `client_maximize` | declarative close/min → action | med |
| `ssd_dblclick` | `ssd/ssd.c` | 400ms → `toggle_client_maximize` | `sf.opt.ssd_dblclick_ms` / custom action | low |
| `tiled_slide` | `anim/anim.c` | tiled appear/hide always **right** inter-gap | `sf.opt.tiled_slide` / Umbriel `sf.opt.anim` | med |
| `anim_ease` | `util/math.h` | `sf_ease_out_cubic` only | per-op style in `sf.opt.anim` | low |
| `ipc_quick_term` | `ipc/ipc.c` | pad `"term"` | opt or IPC arg | low |
| `config_watch_debounce` | `config_watch.c` | 150ms | `sf.opt.config_watch_debounce_ms` | low |
| `column_min_frac` | arrange_math / column | 0.05 floor | `sf.opt.column_min_frac` | low |
| `resize_center_min` | `pointer.c` | 50 | `sf.opt.resize_center_min` | low |
| `snap_resist_mul` | `drag_snap.c` | 4 × `snap_threshold` | fold into snap opts | low |
| `ssd_chrome_layout` | `ssd_layout.c` | fixed min+max+close boxes | button set / layout opts | low |
| `pan_focus` | `arrange.c` columns pan | centre-most visible; no warp | keep / warp / under-pointer | low |
| `focus_on` | `pointer.c` | click-to-focus | `click`\|`sloppy` (careful enter) | low |
| `map_focus` | `xdg/xdg.c` map | always focus new map | opt, or Tier 2 `sf.on("map")` | low |
| `transient_edge_max` | focus / Tab | tile edge-max is transient | document; sticky = float+rule | docs / Stay |

### 9.3 Predictability Medium knobs (same opt pass as #8)

| Gap | Direction |
|---|---|
| Maximize entry-point matrix | ✅ `client_maximize` (default gaps); Super+f remains zen |
| Unminimize → active WS | ✅ `unminimize_workspace` (default active) |
| Transient tile edge-max | Keep transient; document; sticky = float+edge-max / rule |
| Columns pan focus retarget | `pan_focus` opt if users disagree |

### 9.4 Seed hygiene (C vs Fennel)

Day-zero truth is `src/config/default/*.fnl`. C seeds exist for
`STONEFLY_NOCONFIG` / Lua boot failure.

| Issue | Direction |
|---|---|
| C keybind seed omits F11, Super+Prior/Next, Super+1–9, Alt+Space, … that `binds.fnl` has | Shrink C seed to a tiny recovery set, or sync it — do not pretend C is product defaults |
| First `sf.bind` / mouse / wheel / `sf.gesture` clears that C table only | Keep (replace pattern); document |
| C numeric defaults (opacity 0.96, corner 12, …) differ from `options.fnl` (0.90 / 8, …) | Align C with Fennel **or** document NOCONFIG as “minimal recovery”, not day-zero chrome |
| Legacy `master_*` / `dwindle_*` still in `sf.opt` | Dormant while product arrange is stacking+columns; drop when unused |

### 9.5 Tier 2–3 gate (next big leap; not TODO NEXT #8)

Do not invent more one-off C branches for pack-shaped policy:

1. `sf.on(event, handler)` — map / unmap / focus / arrange / workspace / fullscreen / output
2. `sf.wins()` / focused / safe setters through existing verbs
3. SoftPlace / scratchpad catcher / dim-spotlight packs live in Fennel

### 9.6 Forever C

See §2. Strip membership, exclusive fullscreen, slot restore, scene enable
timing for appear, and protocol correctness stay in C even when Lua is rich.
