# Wayland protocol inventory

Living list of globals stonefly advertises, how complete they are, and how
that compares to **Umbriel**, **niri**, and **mango**. Update this file in
the same turn as protocol work (see [`AGENTS.md`](AGENTS.md)).

Shipped product notes stay in [`FEATURES.md`](FEATURES.md) §5 / §9. Open
hollow ads and missing protocols stay in [`ROADMAP.md`](ROADMAP.md).

Legend: **yes** advertised and used · **partial** advertised or incomplete ·
**no** not advertised. Core `wl_*` (compositor, seat, shm, linux-dmabuf,
data-device) are assumed on all four.

| Protocol | What it is | stonefly | niri | mango | umbriel |
|---|---|---|---|---|---|
| **xdg-shell** | App windows, popups, configure/fullscreen | yes **v7** (bounds, caps, suspended, constrained) | yes (v6–7) | yes | yes (v6) |
| **zxdg-decoration** | Client vs server-side titlebars | yes (SSD in `ssd_mode`) | yes | yes | yes |
| **org_kde_kwin_server_decoration** | Old GTK SSD hint | no | yes | no | yes |
| **xdg_wm_dialog_v1** | Modal dialog + parent | **partial** (`modal` stored, no focus trap) | no* | no* | yes |
| **xdg-toplevel-tag-v1** | Stable window id for restore/rules | **yes** (store + IPC; no persist/rules yet) | no* | no* | yes |
| **xdg-toplevel-icon-v1** | Client-set window icon | yes | no* | no* | no* |
| **xdg-toplevel-drag-v1** | Tab/window drag between clients | no | no* | no | yes |
| **xdg-foreign-v2** | Embed another client’s surface | no | yes | no | yes |
| **xdg-activation-v1** | Focus/urgency tokens | yes (never steal; amber urgent) | yes | yes | yes |
| **xdg-system-bell-v1** | Visual/audio bell | yes (flash) | no* | no* | no* |
| **zwlr-layer-shell-v1** | Bar, wallpaper, OSD, launcher | yes **v5** + exclusive_edge | yes | yes | yes **v4** |
| **ext-workspace-v1** | Shell workspace list/activate | yes (per-output groups) | yes | yes | yes |
| **zwlr-foreign-toplevel-v1** | Taskbar window list + activate | yes **v3**; pin to workspace output | yes | yes | yes |
| **ext-foreign-toplevel-list-v1** | Stable ids for capture/shell | yes | yes | yes | yes |
| **ext-session-lock-v1** | Screen lock surfaces | yes | yes | yes | yes |
| **ext-idle-notify-v1** | Idle timeout for lockers | yes | yes | yes | yes |
| **zwp-idle-inhibit-v1** | Video/fullscreen keeps session awake | yes | yes | yes | yes |
| **zwlr-output-power-v1** | DPMS / output off | yes | yes* | yes | no* |
| **zwlr-gamma-control-v1** | Night-light LUT | yes | yes | yes* | yes |
| **wp-color-manager-v1** | HDR / ICC / transfer | no | no | **hdr.c** custom | yes (if renderer supports) |
| **zwlr-output-management-v1** | kanshi / wlr-randr layout | yes | yes | yes | yes |
| **zxdg-output-v1** | Logical size/name under fractional scale | yes | yes | yes | yes |
| **wp-fractional-scale-v1** | Non-integer scale | yes | yes | yes* | yes |
| **wp-viewporter** | Crop/scale buffer | yes | yes | yes* | yes |
| **wp-presentation** | Present timestamps | yes (v2) | yes | ? | yes (v2) |
| **wp-tearing-control-v1** | Async page-flip hint | **partial** (advertise, no async flip) | no* | yes | yes |
| **wp-fifo-v1** | Commit after a refresh | no | no | no | no* |
| **wp-content-type-v1** | Game/video/photo hint | no | no | no | yes |
| **wp-alpha-modifier-v1** | Client surface opacity | no | no | no | yes |
| **wp-linux-drm-syncobj-v1** | Explicit GPU sync | no | smithay DRM path | ? | yes |
| **ext-data-control-v1** | Clipboard managers | yes | yes | ? | yes |
| **zwlr-data-control-v1** | Legacy clipboard mgr | yes | yes | ? | yes |
| **wp-primary-selection** | Middle-click paste | yes | yes | yes | yes |
| **zwlr-screencopy-v1** | grim / old portal capture | yes | yes | ? | yes |
| **ext-image-copy-capture-v1** | New output capture | yes | yes | yes | yes |
| **ext-*-image-capture-source-v1** | Per-output / per-toplevel capture | yes (toplevel + privacy block) | yes | yes | yes |
| **ext-background-effect-v1** | Client blur regions (Noctalia) | yes (layers per-rect; toplevels full plate) | yes | no* | yes |
| **wp-cursor-shape-v1** | Named cursors | yes v2 + WM grab faces | yes | yes | yes |
| **zwp-pointer-gestures-v1** | Touchpad swipe/pinch | yes (`sf.gesture`) | yes | yes | yes |
| **zwp-relative-pointer-v1** | FPS look | yes | yes | yes | yes |
| **zwp-pointer-constraints-v1** | Lock/confine pointer | yes | yes | yes | yes |
| **zwp-virtual-keyboard-v1** | OSK / wtype | yes | yes | yes | yes |
| **zwlr-virtual-pointer-v1** | Synthetic mouse | no | yes | yes | yes |
| **zwp-tablet-v2** | Stylus | **no** | yes | yes | yes |
| **zwp-text-input-v3** + **zwp-input-method-v2** | IME | **no** | yes | yes | yes |
| **zwp-keyboard-shortcuts-inhibit-v1** | Games grab Super | no | yes | yes | yes |
| **wp-security-context-v1** | Flatpak restricted globals | **no** | yes | no | yes |
| **wp-drm-lease-v1** | VR / non-desktop DRM | no | ? | yes | yes |
| **Xwayland** | X11 apps | **no** | xwayland-satellite | in-process | on-demand in-process |
| **xdg-desktop-portal** | screencast UX | generic wlr/gnome | gnome portal | xdp-wlr | xdg-desktop-portal-umbriel |

\* = not confirmed in the sources used for the last pass; treat as “not advertised”
until a live dump or tree read says otherwise.

`./protocol/` only vendors XML this tree must wayland-scanner itself. Absence
from that directory does not mean unimplemented (`xdg-shell` via
`wlr_xdg_shell_create`).

## xdg-shell versions (stonefly v7)

| Ver | Events / states | Stonefly use |
|---|---|---|
| 1–3 | Toplevel/popup, tiled edges, ping | Core path |
| 4 | `configure_bounds` | Usable box (`sf_xdg_sync_toplevel`) |
| 5 | `wm_capabilities` | maximize / fullscreen / minimize (no window-menu until a menu exists) |
| 6 | `suspended` | Minimize / scratch hide, hidden workspace (not mid-slide), session lock |
| 7 | `constrained_*` edges | Tile/max/FS: all edges; snap: flush edges; float: geom∩usable |

Send capabilities and available bounds on initial configure, including remap. Later syncs compare
against the last scheduled values so an unchanged client commit does not request another configure.

Do not suspend columns that are only scrolled off-strip: they stay in the
scene and foreign-toplevel still pins them to the output.

## Highest-leverage gaps

1. IME (`text-input-v3` + `input-method-v2`)
2. Xwayland
3. Tablet v2
4. `wp_security_context_v1`
5. `wp_color_manager_v1` / real tearing page-flips
6. Dialog focus trap; `ext-background-effect` toplevel region mask
