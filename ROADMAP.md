# Stonefly roadmap

Open and partial work. Shipped items live in [`FEATURES.md`](FEATURES.md).
When something ships, move it there in the same turn and drop (or shrink) the
open row here.

`peeble/` remains reference-only for behavior/port ideas — not a checklist to
match line-for-line.

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
| Always-on-top | ❌ | |
| Window menu | ❌ | |
| Geometry persist across restart | ❌ | Tag protocol shipped; no persist consumer yet |
| Keyboard nudge move/resize | ❌ | Snap exists; step nudge does not |
| Workspaces polish | 🚧 | ❌ geometry persist / named window rules / switch anim. Layout engines separate row |
| Layout engine (tile/columns/…) | 🚧 | Resolve `workspace → output → global` (`sf_layout`); only `stacking` implemented. `columns`/`tile` enum + `apply_layout` hook reserved. Global `sf.opt.layout` |
| Focus cycle policy (MRU vs stack) | 🚧 | Stacking / MRU list front |
| Multi-monitor focus / send | ❌ | |
| Pointer center / cursor-follows-focus | ❌ | |
| Picture-in-picture float | ❌ | |
| Dim inactive / spotlight | 🚧 | Global `client_opacity` / `inactive_opacity` shipped; no black dim-overlay / spotlight |
| Scratchpad click-outside catcher | ❌ | `auto_hide` on focus change ships; catcher does not |
| Scratchpad claim vs icons | 🚧 | Fake `app_id` avoids stealing normal windows (breaks taskbar icons). Proper fix later: xdg-activation / startup token |
| SSD per-app overrides / winmenu | 🚧 | SSD titlebars ✅; per-app overrides / winmenu open |

---

## 2. Presentation / animations / eye candy

| Feature | Status | Notes |
|---|---|---|
| Fade / scale appear | ❌ | Opacity static; not animated fade |
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
| Xwayland | ❌ | Classic X11 apps fail |
| text-input / IME | ❌ | `zwp_text_input_v3` + `zwp_input_method_v2` — wlroots helpers exist; stonefly does not advertise. Needs seat relay + popup placement + IM client |
| `wp_fifo_v1` | ❌ | Presentation-time ✅; tearing control advertised (hint only); no fifo helper yet |
| `wp_tearing_control_v1` async flip | 🚧 | Advertised; hint log only |
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
| IPC inputs | ❌ | `outputs` ✅ |
| APK release buildtype | 🚧 | **TEMP:** clang `debugoptimized` + ASan/UBSan (`!strip`); release `abuild-meson --buildtype=release` kept commented until crash hunting ends |
| Exit CPU/heap profiles | ❌ | |

---

## 7. Still-open FORK / gap crumbs

| Pri | Item | Status |
|---|---|---|
| P1 | WM opacity | 🚧 global focused + inactive; no per-window API |
| P2 | Workspaces polish | ❌ rules / geometry persist / switch anim |
| P2 | `wp_fifo_v1` | ❌ |
| — | Xwayland | ❌ |
| — | Always-on-top / multi-mon send/focus | ❌ |
| — | Tiling engines (`columns`/`tile`) | 🚧 reserved |

---

## 8. Day-zero holes still open

| Habit | Status |
|---|---|
| Always-on-top / winmenu | ❌ |
| Multi-mon send/focus, pointer center | ❌ |
| Geometry persist | ❌ |
| Xwayland | ❌ |
| Tiling layouts | ❌ (`columns`/`tile` reserved) |
| Alt+Tab cycle policy polish | 🚧 noctalia switcher + `FOCUS_PREV_ALL` |

---

## 9. Suggested attack order

1. **Xwayland** (classic apps)
2. Config open items (§10) — live reload, validate CLI, inputs/outputs APIs
3. Optional WM: always-on-top, multi-mon send/focus, tiling engines, geometry persist / tag-based rules, `wp_fifo_v1`
4. Capture: full-output screencopy blanking; shell UI for privacy optional
5. Input: tablet; IME / text-input project
6. IPC: action allowlist / inputs
7. Packaging: flip APK back to release buildtype when ASan hunting ends

Shell/OSD/media stay **🌙 Noctalia**.

---

## 10. Config — open

| Open | Status | Notes |
|---|---|---|
| Live reload mid-session | ❌ | Loads once at init; `sf.exec` `reload true` waits on this |
| Config validate CLI | ❌ | Check without applying |
| `--write-config-lua` | ❌ | Offload compiled/plain Lua instead of `.fnl` |
| `inputs.fnl` / `outputs.fnl` APIs | 🚧 | Files are no-op stubs. Output `:layout` override planned (workspace→output→global; global `opt.layout` wired) |
| IPC `config` inputs/outputs sections | 🚧 | Emit `{}` until those APIs exist |
| Neovim API compat / fake `vim.*` | ❌ | Non-goal |
| Fennel as C dependency | ❌ | Non-goal (Fennel is Lua in the pack) |
| Remote plugin fetch | ❌ | Non-goal for v1 |
| Static-link LuaJIT (drop `luajit` runtime dep) | ❌ | Optional packaging choice later |

Shipped surface: [`FEATURES.md`](FEATURES.md) §10.

---

## 11. Testing — open

| Suite | Status | Scope | Gate |
|---|---|---|---|
| Unit (next) | 📋 | IPC argv/section parse, layout/geometry, config decode — no display server | Extend `make test` / `meson test` |
| Integration | 📋 later | Headless / virtual WL + client/IPC drivers | Separate Meson suite tag; skip by default until stable |
| Coverage floors / HTML | 📋 optional | gcovr/`llvm-cov` report; fail under a floor | Dev/CI only — not APK `check()` |

### Non-goals (for now)

- Third-party C unit frameworks as `checkdepends`
- Mocking frameworks / linker `--wrap` as the default story
- Running the full compositor under every `meson test`
- Collapsing unit + headless integration into one binary
- Coverage % gate inside APK `check()`

---

## Legend for future edits

When a row ships, update **FEATURES.md** and remove or shrink the matching
roadmap row (and §7/§8/§9 if it is a FORK or day-zero item). Prefer stonefly
source paths (`src/wm/…`) in notes; river issue numbers only when they still
explain historical context.
