;; Day-zero keybinds + Super tap.
(fn noctalia [msg]
  (.. "noctalia msg " msg))

(local super [:super])
(local super-shift [:super :shift])
(local alt [:alt])
(local alt-shift [:alt :shift])
(local ctrl [:ctrl])
(local ctrl-shift [:ctrl :shift])
(local shift [:shift])
(local none [])

(local binds [
  ;; session / window
  {:mods super :key :Escape :action :quit}
  {:mods super-shift :key :Q :action :quit}
  {:mods super :key :q :action :close}
  ;; focus
  {:mods super :key :Tab :action :focus_next}
  {:mods super-shift :key :Tab :key_alt :ISO_Left_Tab :action :focus_prev}
  {:mods alt :key :Tab :action :exec :cmd (noctalia "window-switcher")}
  {:mods alt-shift :key :Tab :key_alt :ISO_Left_Tab :action :focus_prev_all}
  ;; maximize / minimize
  {:mods super :key :f :action :toggle_max_edges}
  {:mods super :key :m :action :toggle_max_gaps}
  {:mods ctrl :key :Escape :action :minimize}
  {:mods ctrl-shift :key :Escape :action :unminimize}
  ;; snap
  {:mods super :key :Left :action :snap_left}
  {:mods super :key :Right :action :snap_right}
  {:mods super :key :Up :action :snap_top}
  {:mods super :key :Down :action :snap_bottom}
  {:mods super :key :u :action :unsnap}
  ;; spawn / scratch
  {:mods super :key :space :action :spawn_foot}
  {:mods super :key :z :action :spawn_nautilus}
  {:mods super :key :w :action :spawn_ghostty}
  {:mods alt :key :Return :action :spawn_ghostty_qt}
  {:mods super :key :e :action :spawn_editor}
  {:mods super :key :t :action :scratch_claim_term}
  ;; screenshots → noctalia
  {:mods none :key :Print :action :exec :cmd (noctalia "screenshot-fullscreen")}
  {:mods shift :key :Print :action :exec :cmd (noctalia "screenshot-region")}
  {:mods ctrl-shift :key :Print :action :exec :cmd (noctalia "screenshot-window")}
  ;; media / brightness / mic (XF86*)
  {:mods none :key :XF86AudioRaiseVolume :action :exec :cmd (noctalia "volume-up")}
  {:mods none :key :XF86AudioLowerVolume :action :exec :cmd (noctalia "volume-down")}
  {:mods none :key :XF86AudioMute :action :exec :cmd (noctalia "volume-mute")}
  {:mods none :key :XF86AudioMicMute :action :exec :cmd (noctalia "mic-mute")}
  {:mods none :key :XF86Launch5 :action :exec :cmd (noctalia "mic-volume-up")}
  {:mods shift :key :XF86Launch5 :action :exec :cmd (noctalia "mic-volume-down")}
  {:mods none :key :XF86MonBrightnessUp :action :exec :cmd (noctalia "brightness-up")}
  {:mods none :key :XF86MonBrightnessDown :action :exec :cmd (noctalia "brightness-down")}
  {:mods none :key :XF86KbdBrightnessUp :action :exec :cmd (noctalia "keyboard-backlight-up")}
  {:mods none :key :XF86KbdBrightnessDown :action :exec :cmd (noctalia "keyboard-backlight-down")}
  {:mods none :key :XF86AudioPlay :key_alt :XF86AudioPause
   :action :exec :cmd (noctalia "media toggle")}
  {:mods none :key :XF86AudioNext :action :exec :cmd (noctalia "media next")}
  {:mods none :key :XF86AudioPrev :action :exec :cmd (noctalia "media previous")}
])

(each [_ b (ipairs binds)]
  (stonefly.bind b))

;; Super tap → launcher; double-tap → scratchpad term.
(stonefly.tap {:key :Super_L :key_alt :Super_R
               :single :spawn_launcher
               :double :scratch_toggle_term})
