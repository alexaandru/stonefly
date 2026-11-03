;; Day-zero knobs — one table, applied through stonefly.opt's __newindex.
(local opt {
  ;; layout — global default; resolve is workspace → output → global.
  ;; stacking is implemented; columns/tile reserved for later engines.
  :layout "stacking"
  :scale 1.5
  :cursor_size 24
  :gap 15
  :cascade_step 25
  :cascade_wrap 10
  ;; animation
  :slide_duration_ms 200
  :slide_dir_closest true
  :morph_duration_ms 200
  ;; chrome / SceneFX
  ;; Focused vs unfocused opacity (hypr/mango/niri: dim is opt-in).
  ;; Set inactive_opacity lower (e.g. 0.85) to dim unfocused windows.
  :client_opacity 0.96
  :inactive_opacity 0.96
  :corner_radius 12
  :border_thickness 3
  :shadow_blur_sigma 20.0
  :border_active [0.10 0.75 1.00 1.00]
  :border_inactive [0.22 0.22 0.24 1.00]
  :border_urgent [1.00 0.45 0.10 1.00]
  :shadow_color [0.0 0.0 0.0 0.55]
  ;; Per-window blur (needs a background/bottom wallpaper; else looks black).
  :window_blur true
  ;; false = blur real backdrop (other windows); true = wallpaper-only (cheap).
  :blur_xray false
  :blur_num_passes 2
  :blur_radius 5
  :blur_noise 0.02
  :blur_brightness 0.9
  :blur_contrast 0.9
  :blur_saturation 1.2
  :bell_flash_ms 200
  ;; snap / taps
  :snap_threshold 26
  :snap_preview_border 1
  :snap_preview_opacity 0.40
  :tap_max_hold_ms 500
  :double_tap_gap_ms 300
  ;; workspaces — 3-finger vertical swipe
  :swipe_workspace_threshold 80
  ;; true = natural (finger up → next / content moves with finger);
  ;; false = classic (finger down → next).
  :swipe_workspace_natural true
  ;; Capture privacy (#1481): comma-separated app_id substrings, or a table.
  ;; Matching windows get capture_blocked on map / app_id change.
  ;; :capture_block_app_ids "keepass,bitwarden"
  ;; :capture_block_app_ids ["keepass" "bitwarden"]
  :capture_block_app_ids ""
  ;; spawn commands (SPAWN_* actions)
  :cmd_foot "foot"
  :cmd_nautilus "nautilus"
  :cmd_ghostty "ghostty +new-window"
  :cmd_ghostty_qt "ghostty +toggle-quick-terminal"
  :cmd_editor "gtk-launch org.gnome.TextEditor"
  :cmd_launcher "noctalia msg panel-toggle launcher"
})

(each [k v (pairs opt)]
  (tset stonefly.opt k v))
