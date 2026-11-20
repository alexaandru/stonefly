# Stonefly roadmap

**Pending and partial work only.** Shipped capabilities live in
[`FEATURES.md`](FEATURES.md). When something lands, move it there in the same
turn and **delete** the matching row here (do not leave ✅ rows).

`umbriel/` is the primary reference; `peeble/` is secondary for Umbriel gaps —
neither is a line-for-line checklist. C↔Lua border and deferred opt inventory:
[`FROM_C_TO_LUA.md`](FROM_C_TO_LUA.md).

---

## TODO NEXT

Fresh pick-queue (tiling overview / big engines excluded). Knock these out in
order when choosing “what next”; chapter tables below hold detail. Carry-overs
from the previous stack plus high-value leftovers and low-hanging fruit.

| # | Item | Status | Why |
|---|---|---|---|
| 1 | **Dim-inactive / spotlight polish** | 🚧 | Opacity knobs exist; stronger dim or overlay so focus reads at a glance |
| 2 | **`sf.opt.anim` table** | ❌ | Per-op enable/duration/style/scale (appear, close, minimize, scratch, morph, WS). Popin/popout shipped with hard-coded Umbriel scales + `slide_duration_ms` — fold those FIXMEs here |
| 3 | **Deferred WM opts (high)** | ❌ | From [`FROM_C_TO_LUA.md`](FROM_C_TO_LUA.md) §9.2: `focus_cycle`, `tile_insert`, `column_drop_seam`, `tiled_grab_remap`, `border_lmb` — day-to-day tiling feel without new C policy |
| 4 | **`outputs.fnl` / output arrange API** | 🚧 | Workspace→output→global arrange; IPC `config outputs` still `{}` |
| 5 | **Per-window / rule opacity** | 🚧 | Global focused/inactive only; rules/API for spotlight packs |
| 6 | **Scratchpad claim → xdg-activation** | 🚧 | Fake `app_id` avoids steal but breaks taskbar icons; startup token is the real fix |
| 7 | **Predictability Medium leftovers** | 🚧 | FS unFS→pack; snap↔max one “normal” box; document transient tile edge-max; pan-focus opt |
| 8 | **IME *or* Xwayland** | ❌ | Pick by apps: fcitx/CJK/OSK → IME; Steam/legacy → Xwayland — real projects |
| 9 | **`sf.on` + window query** | ❌ | Tier 2–3 Neovim leap ([`FROM_C_TO_LUA.md`](FROM_C_TO_LUA.md) §9.5). Packs subscribe to map/unmap/focus/arrange/layout/workspace and **recompute** from `sf.wins()` — not one-shot max/unmax. Example: Noctalia bar opaque iff any visible edge-max on that output (`noctalia msg config-set bar.default.background_opacity`); naive bind-side toggle breaks with 2+ maxed windows |

**After this stack (not numbered):** IPC `action` allowlist; APK flip to release
buildtype when ASan hunting ends; `--write-config-lua`; protocol hollow-ads
(tearing flips, blur region/popup, dialog modal); tag/icon consumers; full-output
screencopy blanking; tablet; `wp_fifo_v1`; full color-mgmt; geometry persist;
scratchpad click-outside catcher; winmenu (or Noctalia). Queue is #1–#9.

---

## Predictability audit — still open

**Principle:** arrange switch, float/unfloat, max/unmax, FS, hide/show, WS/monitor
send must round-trip. No silent demotion, forgotten cell, or focus onto covered
windows. High-severity gaps are fixed (see FEATURES).

### Medium

| Gap | What surprises | Direction |
|---|---|---|
| **Transient tile edge-max** | Focus/Tab drops overlay back to cell — right for “peek”, wrong if user wanted sticky | Keep transient; sticky = float+edge-max / rule. Document in defaults (TODO NEXT #7) |
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
| Focus cycle opt (MRU / spatial / strip) | 🚧 | Behavior shipped (spatial tiles+floats; focus-raise z-order). Opt is TODO NEXT #3 |
| Picture-in-picture float | ❌ | Related to always-on-top |
| Dim inactive / spotlight | 🚧 | TODO NEXT #1 |
| Scratchpad click-outside catcher | ❌ | `auto_hide` on focus change ships |
| Scratchpad claim vs icons | 🚧 | TODO NEXT #6 — fake `app_id`; want xdg-activation |
| SSD per-app overrides / winmenu | 🚧 | Rule `ssd` ships; winmenu open |

---

## 2. Presentation / animations / eye candy

| Feature | Status | Notes |
|---|---|---|
| Umbriel-shaped `sf.opt.anim` | ❌ | TODO NEXT #2. Per-op `enabled` / `duration_ms` / `style` / `scale`; fold popin hard-codes |
| WM-settable per-window opacity | 🚧 | TODO NEXT #5. Global knobs only |
| Blur region mask / popup blur | 🚧 | Layers: one plate per effect rect; full-region falls back to exclusive/opaque/input (+ transparency mask). Toplevels still treat non-empty region as full-surface bool. Layer popup blur open; no Alpine `ignore_alpha` threshold |

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

## 5. Protocols & I/O

**Advertise ≠ full effect** (same class as the old layer-blur hole). Shipped
wire + known gaps:

| Feature | Status | Notes |
|---|---|---|
| `wp_tearing_control_v1` | 🚧 | **Hollow ad:** global + `new_object` logs hint only. No async/tearing page-flips (SceneFX commit stays vsynced) |
| `ext-background-effect-v1` region / popup | 🚧 | Layers: per-rect plates + exclusive/opaque/input fallback. Toplevels still bool→full plate. No layer popup blur (§2) |
| `xdg_wm_dialog_v1` modal policy | 🚧 | Stores `toplevel->modal`; placement keys off **parent**, not modal. No modal focus / input trap |
| `xdg-toplevel-tag` consumers | 🚧 | Protocol + IPC store only; no rules / geometry-persist consumer |
| `xdg-toplevel-icon-v1` buffers | 🚧 | Preferred sizes + name/buffer ref + IPC `icon_name`; SSD/shell do not paint buffers |
| Full-output screencopy blanking | ❌ | Per-toplevel `ext-image-capture` reject + IPC `capture_blocked` ship; `zwlr_screencopy` full-output still composites blocked windows |
| Screencast / capture_sessions UI | ❌ | Screencopy + ext-image-copy-capture wire shipped; portal session UX open |
| Xwayland | ❌ | TODO NEXT #8 — not advertised |
| text-input / IME | ❌ | TODO NEXT #8 — `zwp_text_input_v3` + `zwp_input_method_v2` not advertised |
| `wp_fifo_v1` | ❌ | Not advertised. Presentation-time ships |
| Tablet (`zwp_tablet_v2`) | ❌ | Not advertised. Touch / gestures / VK / constraints ship |
| Full color-management | ❌ | Not advertised. Gamma LUT ships |

**Intentional subsets** (documented in FEATURES, not gaps to “finish”):
`xdg-activation` = urgency / never focus-steal; `xdg-system-bell` = visual
flash only; layer-shell keyboard auto-focus = **exclusive** only (`on_demand`
stays click-to-focus); foreign has no URGENT state (IPC urgency instead).

---

## 6. Config, IPC, tooling

| Feature | Status | Notes |
|---|---|---|
| `--write-config-lua` | ❌ | Offload compiled/plain Lua instead of `.fnl` |
| `outputs.fnl` API | 🚧 | TODO NEXT #4. `sf.input` shipped |
| IPC `config` outputs section | 🚧 | Still `{}` until output API |
| IPC `action <name>` allowlist | ❌ | |
| IPC inputs (live devices) | ❌ | `config inputs` rules ✅ |
| Nano syntax highlight | ❌ | |
| APK release buildtype | 🚧 | **TEMP:** clang debugoptimized + ASan/UBSan; flip when crash hunting ends |
| Exit CPU/heap profiles | ❌ | |
| Neovim API compat / fake `vim.*` | ❌ | Non-goal |
| Fennel as C dependency | ❌ | Non-goal |
| Remote plugin fetch | ❌ | Non-goal for v1 |

Deferred bind/opt knobs (not TODO NEXT blockers): [`FROM_C_TO_LUA.md`](FROM_C_TO_LUA.md) §9.2.

---

## 7. Still-open FORK / gap crumbs

| Pri | Item | Status |
|---|---|---|
| P1 | WM opacity / dim | 🚧 TODO NEXT #1 / #5 |
| P1 | `sf.on` + window query | ❌ TODO NEXT #9 |
| P2 | Workspaces polish | ❌ rules / geometry persist |
| P2 | Protocol hollow-ads (tearing / blur region / dialog modal) | 🚧 §5 |
| P2 | `wp_fifo_v1` | ❌ |
| — | Xwayland / IME | ❌ TODO NEXT #8 |

---

## 8. Day-zero holes still open

| Habit | Status |
|---|---|
| Winmenu | ❌ |
| Geometry persist | ❌ |
| Xwayland / IME | ❌ TODO NEXT #8 |
| Alt+Tab cycle policy polish | 🚧 noctalia switcher + `FOCUS_PREV_ALL` |

---

## 9. Suggested attack order

Primary queue is **TODO NEXT** at the top (#1–#9). After that stack:

1. IPC action allowlist; APK release buildtype; `--write-config-lua`
2. Protocol hollow-ads: tearing async flips; blur region/popup; dialog modal policy
3. Capture: full-output screencopy blanking; tag/icon consumers
4. Input: tablet
5. Not-yet-advertised: `wp_fifo_v1`, full color-mgmt
6. Optional WM: geometry persist / tag rules; overview polish (card drag, multi-surface cards, shortcut badges)
7. **Bind context redesign** (own chapter below) — after overview/arrange filters feel cramped

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

Shipped unit surface: [`FEATURES.md`](FEATURES.md) §11.

### Non-goals (for now)

- Third-party C unit frameworks as `checkdepends`
- Mocking / linker `--wrap` as the default story
- Full compositor under every `meson test`
- Coverage % gate inside APK `check()`

---

## Legend for future edits

When a row ships: update **FEATURES.md**, **delete** the roadmap row (and any
TODO NEXT entry). Prefer stonefly source paths (`src/wm/…`) in notes.
