# Stonefly roadmap

Open and partial work. Shipped items live in [`FEATURES.md`](FEATURES.md).
When something ships, move it there in the same turn and drop (or shrink) the
open row here.

`peeble/` remains reference-only for behavior/port ideas — not a checklist to
match line-for-line.

## TODO NEXT

QoL stack — little things that make the WM feel finished (tiling excluded).
Knock these out in order when picking “what next”; chapter tables below hold
the detail rows. When an item ships, check it off here and move the row to
[`FEATURES.md`](FEATURES.md).

| # | Item | Status | Why |
|---|---|---|---|
| 1 | Cursor follows focus + warp on workspace / monitor change | ✅ | Shipped (`cursor_follows_focus` + `pointer_center`); monitor focus via #2 |
| 2 | Multi-monitor focus + send-to-monitor | ✅ | Shipped (`focus_monitor` / `send_to_monitor` + Super+Ctrl/Shift+arrows) |
| 3 | Keyboard nudge move / resize | ✅ | Shipped (`nudge` / `resize_edge` + `nudge_step`; Alt / Alt+Shift+arrows) |
| 4 | Always-on-top / pin | ✅ | Shipped (`toggle_always_on_top` / rule / Super+Shift+p) |
| 5 | Workspace switch animation | ✅ | Shipped (vertical strip slide; Super+Page / 1–9 + IPC + swipe share `sf_workspace_switch`) |
| 6 | Real `inputs.fnl` API | ✅ | Shipped (`sf.input` + libinput apply; tap/natural/accel + left_handed/dwt/repeat) |
| 7 | Configurable pointer gestures | ✅ | Shipped (`sf.gesture` swipe + pinch; default 3-finger V/H; pinch→max opt-in in `gestures.fnl`) |
| 8 | **Hardcoded → config / Lua audit** | ❌ | **Next.** Sweep C for knobs and *policy* stuck in the compositor that belong in opts, binds, or Lua. Goal: C owns primitives + events; Lua composes behavior — keeping policy in C starves the runtime we want. Full border contract: [`FROM_C_TO_LUA.md`](FROM_C_TO_LUA.md). Seed list below |
| 9 | Live config reload (binds + opts first) | ✅ | Shipped: staging Lua re-run, inotify watch, `:reload` / `sf.reload` / IPC; keep previous on failure |
| 10 | Dim-inactive / spotlight polish | 🚧 | Opacity knobs exist; stronger dim or overlay so focus reads at a glance |
| 11 | Popin / popout appear+close | ❌ | Umbriel-style scale+fade; see §2. Slide ships (tiled = right inter-gap; scratch closest) |
| 12 | IME **or** Xwayland (pick by apps) | ❌ | fcitx/CJK/OSK → IME; Steam/legacy → Xwayland — real projects, not afternoon bites |

Seed findings for **#8** (not exhaustive — audit while implementing):

| Kind | Where today | Push toward |
|---|---|---|
| Gesture policy | ✅ `sf.gesture` (swipe); pinch still forward-only | done (#7) |
| Mouse grabs | `pointer.c` Super+LMB move, Super+RMB resize, Super+Shift+RMB center-lock; border LMB resize | `sf.bind` mouse chords / actions |
| Column cell-drag | `arrange.c` Super+LMB swap/insert; Super+RMB `width_frac`; `SF_COLUMN_DROP_SEAM` 0.15 | binds + opts |
| Focus cycle policy | tiling = clockwise atan2; stacking = MRU | opt or Lua |
| New-tile insert | always after focused column | opt / Lua |
| Arrange cycle set | `arrange_cycle_order[]` stacking↔columns only | opt list |
| Appear wait | give-up after 3 commits (`arrange_prop_waits`) | opt |
| SSD title buttons | close / min / max wired in C | actions from Lua / rules |
| Event hooks | config is load-time declarative only (`bind`/`opt`/`rule`/`scratchpad`) | `sf.on` map/focus/arrange/… callbacks so packs extend WM without new C |
| Appear wait note | give-up is **1** post-propose commit now (seed row above said 3 — stale) | opt later |

---

## Predictability audit (unsurprising WM)

**Principle:** navigating, switching arrange (and back), float/unfloat, max/unmax
(gaps, edges, fullscreen), hide/show, and send-to-WS/monitor must be round-trips
the user can trust. No silent demotion, forgotten home cell, or focus onto
something invisible. “Boring” here is a feature.

Shipped this session (baseline): strip `arrange_slot_*` for leave_tiling /
break_out; exclusive FS + focus-obscured; sticky float + edge-max (Slack).

### High — ✅ fixed (same family as strip slot / exclusive FS)

| Gap | Fix |
|---|---|
| **WS / monitor move drops slot** | `unlink` no longer clears `arrange_slot_*`; assign remembers before unlink; clear only on destroy/close/scratch/teardown |
| **Fullscreen still packs a cell** | `column_row_paints` skips FS (keep membership); reflow on FS enter/exit via `layout_dispatch` |
| **Arrange switch demotes edged** | `leave_tiling` keeps layout mode; restores `arrange_float_box` only for ordinary tiles; unmax rejoins when tiling + !sticky float |
| **Send-to-monitor + max** | edge/gap-max re-apply without `layout_float` (sticky float / PreMax safe) |

### Medium — product policy, then code

One “maximize” word, several entry points — unify or document before more chrome:

| Trigger today | Stonefly op |
|---|---|
| Super+f / rule `:maximize_to_edges` / taskbar foreign `set_maximized` / **CSD** maximize (xdg `request_maximize`) | **edge-max** (`set_max_edges`) |
| Super+m / **SSD** maximize button | **gap-max** / columns **full_width** (`toggle_max_gaps`) |

| Gap | What surprises | Direction |
|---|---|---|
| **Maximize entry-point matrix** | CSD / taskbar / xdg → edge-max; SSD button / Super+m → gap-max / column full_width. Same icon idea, different geometry | Pick one map (or Umbriel-style `client_maximize = column\|edges` + SSD follows the same policy). Document in defaults until then |
| **Client maximize vs Super+m** | Same as matrix row: app request ≠ Super+m under columns | Fold into maximize matrix; don’t ship a third meaning |
| **Unminimize → active WS** | `apply_shown` reassigns to output’s **current** active workspace — hide on WS1, switch to WS2, show → window follows | Opt: stay on home WS vs follow active (both valid; pick one and stick) |
| **Transient tile edge-max** | Focus / Tab releases overlay back to cell — correct for “peek”, surprising if user wanted sticky edge-max on a tile | Keep transient; sticky = float+edge-max or rule (already Slack path). Document in defaults |
| **FS saved box vs tile cell** | UnFS restores layout_sm **saved** geom; if still column-managed, next pack overwrites — usually OK, race if arrange deferred | After unFS, if `arrange_managed`, skip saved-box morph and pack instead (or pack immediately) |
| **Snap ↔ max ↔ unmax** | PreMax / PreSnap stacking: which box wins after a chain of toggles | One saved “normal” box; modes are overlays; unmax always returns to that normal |
| **Pan focus vs click focus** | 3-finger pan retargets focus to centre-most; pointer may still sit over another cell | Acceptable (chosen); optional later: warp or “focus under pointer on swipe end” |

### Low — polish / rare

| Gap | Notes |
|---|---|
| Empty column shells after FS-only column | FS skipped in pack → `visible_count==0`; strip omits column while membership kept — OK; watch empty-shell leaks |
| Scratchpad claim vs taskbar icons | §1 / FEATURES: fake `app_id` avoids steal; proper fix = xdg-activation / startup token |
| Insert-after-focus for brand-new maps | Predictable once known; slot restore covers return paths; opt/Lua later (TODO NEXT #8) |
| SoftPlace late title / app_id float | Mostly fixed; watch apps that change identity after map |
| Output disconnect / refuge | `workspace_clear` unlinks + clears slots — expected on teardown |
| Foreign unset_maximized while SSD “max” was gap-max | Taskbar may think window is maximized (edges) while SSD toggled full_width — state bits can disagree until matrix is unified |
| Double-click SSD title → max? | Not implemented; if added, must follow the same maximize matrix as the SSD button |

### Done checklist when closing a High / Medium row

Round-trip tests (manual or harness): columns multi-row → stacking → columns; float → unfloat; edge-max → unmax; FS → unFS; hide → show; send to other WS/monitor and back; sticky float+edge-max hide/show. Focus never lands on a fully covered window without an explicit exit-FS. Maximize: CSD, SSD, Super+f, Super+m, taskbar — each path matches the chosen matrix.

---

## How to read

| Symbol | Meaning |
|---|---|
| **🚧** | Partial, stub, or day-zero subset |
| **📋** | Mentioned / planned, not shipped |
| **❌** | Not present |
| **🌙** | Deferred to external shell (Noctalia) — compositor work only if noted |

---

## 1. Window management

| Feature | Status | Notes |
|---|---|---|
| Focus next / prev including minimized | 🚧 | Alt+Tab → `noctalia msg window-switcher`; Alt+Shift+Tab still `FOCUS_PREV_ALL` (unhides). Super+Tab visible-only |
| Window menu | ❌ | |
| Geometry persist across restart | ❌ | Tag protocol shipped; no persist consumer yet |
| Workspaces polish | 🚧 | ❌ geometry persist / named window rules. Switch slide shipped (FEATURES). Layout engines separate |
| Layout engine (tile/columns/…) | ✅ | **stacking + columns** only; first-class `sf_column` strip (umbriel-shaped) shipped — see FEATURES / LESSONS. Cell-drag, slot width, just-enough scroll, 3-finger strip pan, clockwise Tab ring shipped. Still open: `center_focused=on_overflow`, consume-from bind, tabs, drag edge pan; deck/grid declined for now |
| Focus cycle policy (MRU vs stack) | 🚧 | Stacking / MRU list front |
| Picture-in-picture float | ❌ | Related to always-on-top; dedicated PiP policy still open |
| Dim inactive / spotlight | 🚧 | TODO NEXT #10. Global `client_opacity` / `inactive_opacity` shipped; no black dim-overlay / spotlight; consider stronger default inactive |
| Maximize policy (CSD / SSD / binds / taskbar) | 🚧 | Predictability audit **Medium**. Today: CSD+foreign+xdg → edge-max; SSD button+Super+m → gap-max/full_width. Unify or opt |
| Unminimize workspace policy | 📋 | Predictability audit **Medium** — follow active WS vs stay on home |
| Scratchpad click-outside catcher | ❌ | `auto_hide` on focus change ships; catcher does not |
| Scratchpad claim vs icons | 🚧 | Fake `app_id` avoids stealing normal windows (breaks taskbar icons). Proper fix later: xdg-activation / startup token |
| SSD per-app overrides / winmenu | 🚧 | SSD titlebars ✅; per-app overrides / winmenu open |

---

## 2. Presentation / animations / eye candy

| Feature | Status | Notes |
|---|---|---|
| Popin / popout (scale + fade) | ❌ | TODO NEXT #11. Umbriel `windows_in`/`windows_out` popin: lerp presented box from `scale`→1 centered in slot + opacity 0→1 (reverse on close/ghost). v1 can reuse morph + buffer opacity; full crop/dest_size polish later. Intent: tiled windows popin/popout; scratchpad keeps edge slide |
| Umbriel-shaped `sf.opt.anim` table | ❌ | Per-op `enabled` / `duration_ms` / `style` (appear, close, minimize, scratchpad, morph, workspace) + master; chrome gate. Day-zero intent when picked up: scratchpad + workspace slide on; tiled appear/close → popin once shipped |
| WM-settable per-window opacity | 🚧 | Global focused/inactive knobs; no per-window / rules API yet |
| Blur region mask | 🚧 | Non-empty blur region → full-window SceneFX (no region mask yet) |

---

## 3. Shell chrome

| Feature | Status | Notes |
|---|---|---|
| Winmenu | ❌ | SSD shipped |
| Power confirm / power menu | ❌ | |
| Hold-modifier cheatsheet | ❌ | |
| Clipboard history panel | ❌ | Compositor clipboard protocols shipped; panel is shell |
| Color picker | ❌ | |
| Dock with magnification | ❌ | |
| Desktop widgets | ❌ | |
| Blur behind bar/OSD | 🚧 | SceneFX can; chrome is external |
| In-process wallpaper engine | ❌ | Layer-shell wallpaper clients OK |

Bar / launcher / OSD / notifier / control center stay **🌙 Noctalia** unless
product asks for in-process chrome again.

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

| Feature | Status | Notes |
|---|---|---|
| Screencast / capture_sessions | ❌ | Screencopy + ext-image-copy-capture shipped; session UI/widget open |
| Full-output screencopy blanking | ❌ | Per-window capture privacy shipped |
| Xwayland | ❌ | TODO NEXT #12 (pick vs IME by apps). Classic X11 apps fail |
| text-input / IME | ❌ | TODO NEXT #12 (pick vs Xwayland by apps). `zwp_text_input_v3` + `zwp_input_method_v2` — wlroots helpers exist; stonefly does not advertise. Needs seat relay + popup placement + IM client |
| `wp_fifo_v1` | ❌ | Presentation-time ✅; tearing control advertised (hint only); no fifo helper yet |
| `wp_tearing_control_v1` async flip | 🚧 | Advertised; hint log only |
| Configurable swipe / pinch binds | ✅ | `sf.gesture` swipe + pinch (`pinchin`/`pinchout`/`pinch`) |
| Tablet | ❌ | Touch / gestures / virtual-keyboard / constraints shipped |
| Full color-management | ❌ | Gamma LUT shipped |
| xdg-toplevel-tag consumers | 🚧 | Protocol + IPC stored; no rules/persist consumer |
| xdg-toplevel-icon buffers in shell | 🚧 | Protocol + IPC `icon_name`; Noctalia unused buffers yet |

---

## 6. Config, IPC, tooling

| Feature | Status | Notes |
|---|---|---|
| Config validate CLI | ✅ | `stonefly validate` — exact Lua error on stderr; exit 0/1 |
| Nano syntax highlight | ❌ | |
| IPC `action <name>` allowlist | ❌ | |
| IPC inputs | ❌ | Live devices dump; `config inputs` rules ✅ |
| APK release buildtype | 🚧 | **TEMP:** clang `debugoptimized` + ASan/UBSan (`!strip`); release `abuild-meson --buildtype=release` kept commented until crash hunting ends |
| Exit CPU/heap profiles | ❌ | |

---

## 7. Still-open FORK / gap crumbs

| Pri | Item | Status |
|---|---|---|
| P1 | Configurable gestures | ✅ `sf.gesture` swipe + pinch |
| P1 | Hardcoded → Lua/config audit | ❌ TODO NEXT #8 (policy in C starves the runtime) |
| P1 | WM opacity | 🚧 global focused + inactive; no per-window API (TODO NEXT #10) |
| P2 | Workspaces polish | ❌ rules / geometry persist (switch slide ✅) |
| P2 | `wp_fifo_v1` | ❌ |
| — | Xwayland / IME | ❌ TODO NEXT #12 |
| — | Tiling engines (`columns`/`tile`) | 🚧 reserved |

---

## 8. Day-zero holes still open

| Habit | Status |
|---|---|
| Winmenu | ❌ |
| Geometry persist | ❌ |
| Xwayland / IME | ❌ TODO NEXT #12 |
| Tiling layouts | ✅ (arrange engines; see FEATURES) |
| Alt+Tab cycle policy polish | 🚧 noctalia switcher + `FOCUS_PREV_ALL` |

---

## 9. Suggested attack order

Primary queue is **TODO NEXT** at the top (QoL #1–#12). After that stack:

1. Config leftovers (§10) — validate CLI, outputs API, `--write-config-lua`
2. Optional WM: tiling engines, geometry persist / tag-based rules, overview (niri/mango-scale — large)
3. Capture: full-output screencopy blanking; shell UI for privacy optional
4. Input: tablet (IME is TODO NEXT #12; gestures ✅ #7; hardcoded→Lua audit #8)
5. IPC: action allowlist
6. Packaging: flip APK back to release buildtype when ASan hunting ends
7. Protocol polish: `wp_fifo_v1`, full color-mgmt

Shell/OSD/media stay **🌙 Noctalia**.

---

## 10. Config — open

| Open | Status | Notes |
|---|---|---|
| Configurable pointer gestures | ✅ | `sf.gesture` swipe + pinch. [`FROM_C_TO_LUA.md`](FROM_C_TO_LUA.md) |
| Hardcoded → config / Lua audit | ❌ | TODO NEXT #8. Knobs + policy out of C; `sf.on` event hooks; C keeps primitives. [`FROM_C_TO_LUA.md`](FROM_C_TO_LUA.md) |
| Live reload mid-session | ✅ | Shipped (`sf_config_reload` + inotify; `sf.exec {:reload true}` runs on success) |
| Config validate CLI | ✅ | `stonefly validate` — exact Lua error on stderr; exit 0/1 |
| `--write-config-lua` | ❌ | Offload compiled/plain Lua instead of `.fnl` |
| `outputs.fnl` API | 🚧 | `inputs.fnl` / `sf.input` shipped (FEATURES). Output `:arrange` override planned (workspace→output→global; global `opt.arrange` wired) |
| IPC `config` outputs section | 🚧 | Inputs emit rules array; outputs still `{}` until that API exists |
| Neovim API compat / fake `vim.*` | ❌ | Non-goal |
| Fennel as C dependency | ❌ | Non-goal (Fennel is Lua in the pack) |
| Remote plugin fetch | ❌ | Non-goal for v1 |
| Static-link LuaJIT (drop `luajit` runtime dep) | ❌ | Optional packaging choice later |

Shipped surface: [`FEATURES.md`](FEATURES.md) §10.

---

## 11. Testing — open

| Suite | Status | Scope | Gate |
|---|---|---|---|
| Unit (more pure) | 📋 | Remaining config decode / rules match without Lua VM; deeper JSON field paths. Layout + place SMs shipped | Extend `make test` / `meson test` |
| Integration | 📋 later | Headless / virtual WL + client/IPC drivers | Separate Meson suite tag; skip by default until stable |
| Coverage floors / HTML | 📋 optional | gcovr/`llvm-cov` report; fail under a floor | Dev/CI only — not APK `check()` |

Shipped unit surface (layout_sm/place_sm/geom/layout/snap/IPC parse/shell_quote/path/chord + earlier util/ssd/action/JSON): see [`FEATURES.md`](FEATURES.md) §11.

### Non-goals (for now)

- Third-party C unit frameworks as `checkdepends`
- Mocking frameworks / linker `--wrap` as the default story
- Running the full compositor under every `meson test`
- Collapsing unit + headless integration into one binary
- Coverage % gate inside APK `check()`

---

## Legend for future edits

When a row ships, update **FEATURES.md**, remove or shrink the matching
roadmap row (and TODO NEXT / §7/§8/§9 if listed there). Prefer stonefly
source paths (`src/wm/…`) in notes; river issue numbers only when they still
explain historical context.
