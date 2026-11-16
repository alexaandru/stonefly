# From C to Lua — Neovim for Wayland WMs

Stonefly’s product aim: a compositor whose **window-management brain** is as
scriptable as Neovim — C owns the kernel; Lua owns policy and packs.

This note is the border contract: what lives where **today**, what must stay in
C, and what to push into the Lua runtime so the WM stays flexible without
becoming surprising or unsafe.

Related: [`ROADMAP.md`](ROADMAP.md) TODO NEXT #7 (gestures) / #8 (hardcoded →
Lua audit); #9 live reload ✅; [`LESSONS.md`](LESSONS.md); Predictability audit
in the roadmap (round-trips stay boring even when Lua decides).

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
Hardcoded gestures / mouse         Same surface as sf.bind / sf.gesture
Maximize matrix scattered in C     One opt/Lua matrix → C verbs
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
| Action *primitives* | Focus, raise, join/break column, set FS / edge-max / float, spawn, hide/show |
| Invariants | Slot memory, exclusive fullscreen, sticky float vs transient edge-max, no focus onto covered windows |

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
| `sf.bind` / `sf.tap` / `sf.chain` | Chords → `sf_action_step[]` |
| `sf.gesture` | Touchpad swipe match → workspace / columns_pan / actions |
| `sf.window_rule` | Match + declarative chrome/float/size + WM actions |
| `sf.scratchpad` | Named pads |
| `sf.input` | Libinput / keyboard device rules |
| `sf.exec` | Session spawn list (after `WAYLAND_DISPLAY`) |
| `sf.reload` | Re-run init mid-session (same as `:reload` / IPC / inotify) |
| `sf.packadd` / `sf.stdpath` / rtp | Pack path scaffolding (Neovim-shaped) |

**Not today**

- `sf.on` / autocmd-style event handlers
- Querying or mutating windows from Lua during the session
- Mouse chords as first-class binds (swipes: `sf.gesture`)
- Replacing focus / insert / maximize policy with Lua functions
- Packs that react to map / focus / arrange without new C

Summary: **strong static config**, **live reload of that config**, still a
**weak runtime** until `sf.on` / window query land.

Default tree: `src/config/default/` (`options.fnl`, `binds.fnl`, `rules.fnl`,
`inputs.fnl`, `scratchpads.fnl`, `execs.fnl`, …). Docset:
`dist/fennel-ls/docsets/sf.lua`.

---

## 4. What to push from C → Lua

Ordered by leverage. Each tier assumes the one above is honest (no new
hardcoded policy while a bind/opt/Lua surface exists).

### Tier 1 — bind-shaped policy (TODO NEXT #7 / #8)

Same smell as hardcoded Super+LMB and 3-finger swipe. Expose as data or thin
Lua; C only runs the chosen verb.

| Stuck in C today | Push toward |
|---|---|
| 3-finger V = workspaces, H = columns pan | ✅ `sf.gesture` swipe + pinch (#7) |
| Super+LMB/RMB/Shift+RMB; border LMB resize (`pointer.c`) | Mouse `sf.bind` / grab actions |
| Cell-drag seams; `SF_COLUMN_DROP_SEAM` 0.15 | Binds + opts |
| Focus cycle: tiling atan2 clockwise; stacking MRU | Opt or Lua |
| New tile always after focused column | Opt / Lua |
| `arrange_cycle_order[]` stacking↔columns | Opt list |
| Appear give-up commits | Opt |
| SSD title buttons → close / min / **max_gaps** wired in C | Actions from Lua / rules (see maximize matrix) |
| Maximize entry matrix (below) | Opt + one code path |

**Maximize entry matrix (Medium predictability)** — one word, several ops:

| Trigger today | Stonefly op |
|---|---|
| Super+f / rule `:maximize_to_edges` / foreign taskbar `set_maximized` / **CSD** (xdg `request_maximize`) | **edge-max** |
| Super+m / **SSD** maximize button | **gap-max** / columns **full_width** |

Unify (or Umbriel-style `client_maximize = column|edges` with SSD following the
same policy) before adding more chrome. Until then, document in defaults.

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
in Fennel packs instead of `wm/*.c`.

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

1. **#7 Configurable gestures** — ✅ `sf.gesture` swipe + pinch (template for mouse/SSD).
2. **#8 Hardcoded → config/Lua audit** — finish the Tier 1 table; every new UX
   is bind/opt/Lua-shaped.
3. **#9 Live reload** — ✅ shipped (re-run Lua init; inotify; keep previous on
   failure). Edit Fennel → session updates without logout.
4. **`sf.on` + window query API** — Tier 2–3; packs become real.
5. **Medium predictability knobs** — maximize matrix, unminimize→home vs active
   WS — as opts/Lua once the surface exists.

Shell UI (bar, launcher, OSD) stays **Noctalia**. Compositor Lua is WM policy,
not a second shell.

---

## 7. Done when

- A user can change focus cycle, insert policy, gestures, mouse grabs, and
  maximize mapping **without a C patch**.
- A Fennel pack can subscribe to map/focus/arrange and call documented
  primitives safely.
- Live reload updates binds/opts without dropping the session.
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
| `src/seat/pointer.c` | Hardcoded Super+mouse grabs |
| `runtime/pack/` | Pack path / fennel-shim |
| `dist/fennel-ls/docsets/sf.lua` | fennel-ls docset for `sf.*` |
