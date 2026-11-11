;; Day-zero keybinds + Super tap (peeble-style "Super+w" chords).
(fn noctalia [msg]
  (.. "noctalia msg " msg))

(fn bind [chord opts]
  (sf.bind chord opts))

(local binds [
  ;; session / window
  ["Super+Esc" {:action :quit}]
  ["Super+Shift+Q" {:action :quit}]
  ["Super+q" {:action :close}]
  ;; focus
  ["Super+Tab" {:action :focus_next}]
  ["Super+Shift+Tab" {:action :focus_prev}]
  ["Alt+Tab" {:action :exec :cmd (noctalia "window-switcher")}]
  ["Alt+Shift+Tab" {:action :focus_prev_all}]
  ;; maximize / fullscreen / minimize
  ["Super+f" {:action :toggle_maximize_to_edges}]
  ["Super+m" {:action :toggle_maximize}]
  ["F11" {:action :toggle_fullscreen}]
  ["Ctrl+Esc" {:action :minimize}]
  ["Ctrl+Shift+Esc" {:action :unminimize}]
  ;; snap only while stacking; tiling uses focus_dir instead
  ["Super+Left" {:action :snap_left :arrange "stacking"}]
  ["Super+Right" {:action :snap_right :arrange "stacking"}]
  ["Super+Up" {:action :snap_top :arrange "stacking"}]
  ["Super+Down" {:action :snap_bottom :arrange "stacking"}]
  ["Super+Left" {:action :focus_dir :cmd "left" :arrange "tiling"}]
  ["Super+Right" {:action :focus_dir :cmd "right" :arrange "tiling"}]
  ["Super+Up" {:action :focus_dir :cmd "up" :arrange "tiling"}]
  ["Super+Down" {:action :focus_dir :cmd "down" :arrange "tiling"}]
  ["Super+u" {:action :unsnap :arrange "stacking"}]
  ;; tiling: leave/rejoin the tile set (Super+LMB drag swaps tiles instead)
  ["Super+v" {:action :toggle_float :arrange "tiling"}]
  ["Super+Shift+v" {:action :float :arrange "tiling"}]
  ["Super+Ctrl+v" {:action :unfloat :arrange "tiling"}]
  ;; columns: consume into neighbor, or expel if stacked with no neighbor
  ["Super+comma" {:action :consume :cmd "left" :arrange "columns"}]
  ["Super+period" {:action :consume :cmd "right" :arrange "columns"}]
  ;; tiling: rotate tile order (cw = last→first; columns push others right)
  ["Super+c" {:action :arrange_rotate :cmd "cw" :arrange "tiling"}]
  ["Super+Shift+c" {:action :arrange_rotate :cmd "ccw" :arrange "tiling"}]
  ["Super+Shift+p" {:action :toggle_always_on_top}]
  ;; arrange engines: stacking → columns → master → dwindle → monocle
  ["Super+bracketright" {:action :arrange_cycle :cmd "next"}]
  ["Super+bracketleft" {:action :arrange_cycle :cmd "prev"}]
  ;; multi-monitor (cmd = left|right|up|down|next|prev|connector)
  ["Super+Ctrl+Left" {:action :focus_monitor :cmd "left"}]
  ["Super+Ctrl+Right" {:action :focus_monitor :cmd "right"}]
  ["Super+Ctrl+Up" {:action :focus_monitor :cmd "up"}]
  ["Super+Ctrl+Down" {:action :focus_monitor :cmd "down"}]
  ["Super+Shift+Left" {:action :send_to_monitor :cmd "left"}]
  ["Super+Shift+Right" {:action :send_to_monitor :cmd "right"}]
  ["Super+Shift+Up" {:action :send_to_monitor :cmd "up"}]
  ["Super+Shift+Down" {:action :send_to_monitor :cmd "down"}]
  ;; keyboard nudge (sf.opt.nudge_step px; no-op when max/snap/fullscreen)
  ["Alt+Left" {:action :nudge :cmd "left"}]
  ["Alt+Right" {:action :nudge :cmd "right"}]
  ["Alt+Up" {:action :nudge :cmd "up"}]
  ["Alt+Down" {:action :nudge :cmd "down"}]
  ;; resize: right/down grow, left/up shrink (top-left anchored)
  ["Alt+Shift+Left" {:action :resize_edge :cmd "left"}]
  ["Alt+Shift+Right" {:action :resize_edge :cmd "right"}]
  ["Alt+Shift+Up" {:action :resize_edge :cmd "up"}]
  ["Alt+Shift+Down" {:action :resize_edge :cmd "down"}]
  ;; workspaces (per-output strip; move follows focus)
  ["Super+Prior" {:action :workspace_prev}]
  ["Super+Next" {:action :workspace_next}]
  ["Super+1" {:action :workspace_switch :cmd "1"}]
  ["Super+2" {:action :workspace_switch :cmd "2"}]
  ["Super+3" {:action :workspace_switch :cmd "3"}]
  ["Super+4" {:action :workspace_switch :cmd "4"}]
  ["Super+5" {:action :workspace_switch :cmd "5"}]
  ["Super+6" {:action :workspace_switch :cmd "6"}]
  ["Super+7" {:action :workspace_switch :cmd "7"}]
  ["Super+8" {:action :workspace_switch :cmd "8"}]
  ["Super+9" {:action :workspace_switch :cmd "9"}]
  ["Super+Shift+Prior" {:action :move_to_workspace_prev}]
  ["Super+Shift+Next" {:action :move_to_workspace_next}]
  ["Super+Shift+1" {:action :move_to_workspace :cmd "1"}]
  ["Super+Shift+2" {:action :move_to_workspace :cmd "2"}]
  ["Super+Shift+3" {:action :move_to_workspace :cmd "3"}]
  ["Super+Shift+4" {:action :move_to_workspace :cmd "4"}]
  ["Super+Shift+5" {:action :move_to_workspace :cmd "5"}]
  ["Super+Shift+6" {:action :move_to_workspace :cmd "6"}]
  ["Super+Shift+7" {:action :move_to_workspace :cmd "7"}]
  ["Super+Shift+8" {:action :move_to_workspace :cmd "8"}]
  ["Super+Shift+9" {:action :move_to_workspace :cmd "9"}]
  ;; spawn / scratch (exec cmd or named pad)
  ["Super+space" {:action :exec :cmd "foot"}]
  ["Super+z" {:action :exec :cmd "nautilus"}]
  ["Super+w" {:action :exec :cmd "ghostty +new-window"}]
  ["Alt+Return" {:action :exec :cmd "ghostty +toggle-quick-terminal"}]
  ["Super+e" {:action :exec :cmd "gtk-launch org.gnome.TextEditor"}]
  ["Super+t" {:action :scratch_claim :cmd "term"}]
  ;; screenshots → noctalia
  ["Print" {:action :exec :cmd (noctalia "screenshot-fullscreen")}]
  ["Shift+Print" {:action :exec :cmd (noctalia "screenshot-region")}]
  ["Ctrl+Shift+Print" {:action :exec :cmd (noctalia "screenshot-window")}]
  ;; media / brightness / mic (XF86*)
  ["XF86AudioRaiseVolume" {:action :exec :cmd (noctalia "volume-up")}]
  ["XF86AudioLowerVolume" {:action :exec :cmd (noctalia "volume-down")}]
  ["XF86AudioMute" {:action :exec :cmd (noctalia "volume-mute")}]
  ["XF86AudioMicMute" {:action :exec :cmd (noctalia "mic-mute")}]
  ["XF86Launch5" {:action :exec :cmd (noctalia "mic-volume-up")}]
  ["Shift+XF86Launch5" {:action :exec :cmd (noctalia "mic-volume-down")}]
  ["XF86MonBrightnessUp" {:action :exec :cmd (noctalia "brightness-up")}]
  ["XF86MonBrightnessDown" {:action :exec :cmd (noctalia "brightness-down")}]
  ["XF86KbdBrightnessUp" {:action :exec :cmd (noctalia "keyboard-backlight-up")}]
  ["XF86KbdBrightnessDown" {:action :exec :cmd (noctalia "keyboard-backlight-down")}]
  ["XF86AudioPlay" {:action :exec :cmd (noctalia "media toggle")
                    :key_alt :XF86AudioPause}]
  ["XF86AudioNext" {:action :exec :cmd (noctalia "media next")}]
  ["XF86AudioPrev" {:action :exec :cmd (noctalia "media previous")}]
])

(each [_ b (ipairs binds)]
  (bind (. b 1) (. b 2)))

;; Super tap → launcher; double-tap → scratchpad term.
(sf.tap "Super" {:single :exec
                       :single_cmd "noctalia msg panel-toggle launcher"
                       :double :scratch_toggle
                       :double_cmd "term"})
