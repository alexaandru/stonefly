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
| 7 | **Configurable pointer gestures** | ❌ | **Next.** 3-finger V=workspaces / H=columns pan are hardcoded in `seat/gestures.c` (only `swipe_workspace_*` opts). Want `sf.bind`/`sf.gesture` (fingers, axis, action) like chords — same class of smell as hardcoding Super+LMB |
| 8 | **Hardcoded → config / Lua audit** | ❌ | **Right after #7.** Sweep C for knobs and *policy* stuck in the compositor that belong in opts, binds, or Lua. Goal: C owns primitives + events; Lua composes behavior — keeping policy in C starves the runtime we want. Seed list below; expand while doing #7 |
| 9 | Live config reload (binds + opts first) | ❌ | Tweak chords without logout; mango/Hypr baseline |
| 10 | Dim-inactive / spotlight polish | 🚧 | Opacity knobs exist; stronger dim or overlay so focus reads at a glance |
| 11 | Popin / popout appear+close | ❌ | Umbriel-style scale+fade; see §2. Slide ships (tiled = right inter-gap; scratch closest) |
| 12 | IME **or** Xwayland (pick by apps) | ❌ | fcitx/CJK/OSK → IME; Steam/legacy → Xwayland — real projects, not afternoon bites |

Seed findings for **#8** (not exhaustive — audit while implementing):

| Kind | Where today | Push toward |
|---|---|---|
| Gesture policy | `gestures.c` 3-finger V/H | #7 `sf.gesture` |
| Mouse grabs | `pointer.c` Super+LMB move, Super+RMB resize, Super+Shift+RMB center-lock; border LMB resize | `sf.bind` mouse chords / actions |
| Column cell-drag | `arrange.c` Super+LMB swap/insert; Super+RMB `width_frac`; `SF_COLUMN_DROP_SEAM` 0.15 | binds + opts |
| Focus cycle policy | tiling = clockwise atan2; stacking = MRU | opt or Lua |
| New-tile insert | always after focused column | opt / Lua |
| Arrange cycle set | `arrange_cycle_order[]` stacking↔columns only | opt list |
| Appear wait | give-up after 3 commits (`arrange_prop_waits`) | opt |
| SSD title buttons | close / min / max wired in C | actions from Lua / rules |
| Event hooks | config is load-time declarative only (`bind`/`opt`/`rule`/`scratchpad`) | `sf.on` map/focus/arrange/… callbacks so packs extend WM without new C |

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
| Configurable swipe / pinch binds | ❌ | TODO NEXT #7. Today: hardcoded 3-finger V/H in `gestures.c` |
| Tablet | ❌ | Touch / gestures / virtual-keyboard / constraints shipped |
| Full color-management | ❌ | Gamma LUT shipped |
| xdg-toplevel-tag consumers | 🚧 | Protocol + IPC stored; no rules/persist consumer |
| xdg-toplevel-icon buffers in shell | 🚧 | Protocol + IPC `icon_name`; Noctalia unused buffers yet |

---

## 6. Config, IPC, tooling

| Feature | Status | Notes |
|---|---|---|
| Config validate CLI | ❌ | Check without applying |
| Nano syntax highlight | ❌ | |
| IPC `action <name>` allowlist | ❌ | |
| IPC inputs | ❌ | Live devices dump; `config inputs` rules ✅ |
| APK release buildtype | 🚧 | **TEMP:** clang `debugoptimized` + ASan/UBSan (`!strip`); release `abuild-meson --buildtype=release` kept commented until crash hunting ends |
| Exit CPU/heap profiles | ❌ | |

---

## 7. Still-open FORK / gap crumbs

| Pri | Item | Status |
|---|---|---|
| P1 | Configurable gestures | ❌ TODO NEXT #7 (hardcoded 3-finger swipe) |
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
4. Input: tablet (IME is TODO NEXT #12; gestures #7; hardcoded→Lua audit #8)
5. IPC: action allowlist
6. Packaging: flip APK back to release buildtype when ASan hunting ends
7. Protocol polish: `wp_fifo_v1`, full color-mgmt

Shell/OSD/media stay **🌙 Noctalia**.

---

## 10. Config — open

| Open | Status | Notes |
|---|---|---|
| Configurable pointer gestures | ❌ | TODO NEXT #7. `sf.gesture` / bind-shaped swipe+pinch; retire hardcoded 3-finger paths |
| Hardcoded → config / Lua audit | ❌ | TODO NEXT #8. Knobs + policy out of C; `sf.on` event hooks; C keeps primitives |
| Live reload mid-session | ❌ | TODO NEXT #9. Loads once at init; start with binds + opts; `sf.exec` `reload true` waits on this |
| Config validate CLI | ❌ | Check without applying |
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
