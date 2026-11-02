;; Day-zero keybinds + Super tap (peeble-style "Super+w" chords).
(fn noctalia [msg]
  (.. "noctalia msg " msg))

(fn bind [chord opts]
  (stonefly.bind chord opts))

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
  ;; maximize / minimize
  ["Super+f" {:action :toggle_max_edges}]
  ["Super+m" {:action :toggle_max_gaps}]
  ["Ctrl+Esc" {:action :minimize}]
  ["Ctrl+Shift+Esc" {:action :unminimize}]
  ;; snap
  ["Super+Left" {:action :snap_left}]
  ["Super+Right" {:action :snap_right}]
  ["Super+Up" {:action :snap_top}]
  ["Super+Down" {:action :snap_bottom}]
  ["Super+u" {:action :unsnap}]
  ;; spawn / scratch
  ["Super+space" {:action :spawn_foot}]
  ["Super+z" {:action :spawn_nautilus}]
  ["Super+w" {:action :spawn_ghostty}]
  ["Alt+Return" {:action :spawn_ghostty_qt}]
  ["Super+e" {:action :spawn_editor}]
  ["Super+t" {:action :scratch_claim_term}]
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
(stonefly.tap "Super" {:single :spawn_launcher
                       :double :scratch_toggle_term})
