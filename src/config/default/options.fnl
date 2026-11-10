;; Day-zero knobs — one table, applied through sf.opt's __newindex.
(local opt {;; arrange — global default; resolve is workspace → output → global.
            ;; stacking | columns (niri strip) | master (tile) | dwindle | monocle
            :arrange "stacking"
            ;; master engine: left|right|top|bottom|center (merges mango tile variants)
            :master_pos "left"
            :master_factor 0.55
            :master_count 1
            ;; dwindle first/rest split (Super+RMB while dwindle)
            :dwindle_factor 0.5
            ;; niri columns: fraction of usable width per column
            :column_ratio 0.5
            :scale 1.5
            :cursor_size 24
            :gap 15
            :cascade_step 25
            :cascade_wrap 10
            ;; animation (slide_duration_ms also drives workspace strip switches)
            :slide_duration_ms 200
            :slide_dir_closest true
            :morph_duration_ms 200
            ;; chrome / SceneFX
            ;; Focused vs unfocused opacity (hypr/mango/niri: dim is opt-in).
            ;; Set inactive_opacity lower (e.g. 0.85) to dim unfocused windows.
            :client_opacity 0.96
            :inactive_opacity 0.96
            :corner_radius 8
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
            ;; Warp pointer to the newly focused window on Super+Tab / workspace
            ;; switch (and move-follow). Click-to-focus does not warp.
            :cursor_follows_focus true
            ;; Pixel step for Alt+arrows nudge / Alt+Shift+arrows resize_edge.
            :nudge_step 25
            ;; SSD titlebars — never | auto | prefer_ssd | always
            ;; auto: SSD when client prefers server-side or has no preference.
            :ssd_mode "auto"
            :ssd_height 28
            :ssd_font_size 17
            ;; Border/corner mouse resize hit slop (logical px).
            ;; in = into the window from the content/SSD edge; out = past the drawn border.
            ;; Hover shows edge cursors; LMB drag resizes (Super+RMB still works). Both 0 = off.
            :resize_border_in 3
            :resize_border_out 1
            ;; Capture privacy (#1481): comma-separated app_id substrings, or a table.
            ;; Matching windows get capture_blocked on map / app_id change.
            ;; :capture_block_app_ids "keepass,bitwarden"
            ;; :capture_block_app_ids ["keepass" "bitwarden"]
            :capture_block_app_ids ""})

(each [k v (pairs opt)]
  (tset sf.opt k v))
