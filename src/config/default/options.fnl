;; Day-zero knobs — one table, applied through stonefly.opt's __newindex.
(local opt {
  ;; layout
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
  :client_opacity 0.96
  :corner_radius 12
  :border_thickness 3
  :shadow_blur_sigma 20.0
  :border_active [0.10 0.75 1.00 1.00]
  :border_inactive [0.22 0.22 0.24 1.00]
  :border_urgent [1.00 0.45 0.10 1.00]
  :shadow_color [0.0 0.0 0.0 0.55]
  :window_blur false
  :bell_flash_ms 200
  ;; snap / taps
  :snap_threshold 26
  :snap_preview_border 1
  :snap_preview_opacity 0.40
  :tap_max_hold_ms 500
  :double_tap_gap_ms 300
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
