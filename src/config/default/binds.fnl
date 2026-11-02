;; Day-zero keybinds + Super tap (was hardcoded.h tables).
(local super ["super"])
(local super-shift ["super" "shift"])
(local alt ["alt"])
(local alt-shift ["alt" "shift"])
(local ctrl ["ctrl"])
(local ctrl-shift ["ctrl" "shift"])
(local shift ["shift"])

(stonefly.bind {:mods super :key "Escape" :action "quit"})
(stonefly.bind {:mods super-shift :key "Q" :action "quit"})
(stonefly.bind {:mods super :key "q" :action "close"})

(stonefly.bind {:mods super :key "Tab" :action "focus_next"})
(stonefly.bind {:mods super-shift :key "Tab" :key_alt "ISO_Left_Tab"
                :action "focus_prev"})

(stonefly.bind {:mods alt :key "Tab" :action "exec"
                :cmd "noctalia msg window-switcher"})
(stonefly.bind {:mods alt-shift :key "Tab" :key_alt "ISO_Left_Tab"
                :action "focus_prev_all"})

(stonefly.bind {:mods super :key "f" :action "toggle_max_edges"})
(stonefly.bind {:mods super :key "m" :action "toggle_max_gaps"})

(stonefly.bind {:mods ctrl :key "Escape" :action "minimize"})
(stonefly.bind {:mods ctrl-shift :key "Escape" :action "unminimize"})

(stonefly.bind {:mods super :key "Left" :action "snap_left"})
(stonefly.bind {:mods super :key "Right" :action "snap_right"})
(stonefly.bind {:mods super :key "Up" :action "snap_top"})
(stonefly.bind {:mods super :key "Down" :action "snap_bottom"})
(stonefly.bind {:mods super :key "u" :action "unsnap"})

(stonefly.bind {:mods super :key "space" :action "spawn_foot"})
(stonefly.bind {:mods super :key "z" :action "spawn_nautilus"})
(stonefly.bind {:mods super :key "w" :action "spawn_ghostty"})
(stonefly.bind {:mods alt :key "Return" :action "spawn_ghostty_qt"})
(stonefly.bind {:mods super :key "e" :action "spawn_editor"})
(stonefly.bind {:mods super :key "t" :action "scratch_claim_term"})

(stonefly.bind {:mods [] :key "Print" :action "exec"
                :cmd "noctalia msg screenshot-fullscreen"})
(stonefly.bind {:mods shift :key "Print" :action "exec"
                :cmd "noctalia msg screenshot-region"})
(stonefly.bind {:mods ctrl-shift :key "Print" :action "exec"
                :cmd "noctalia msg screenshot-window"})

(stonefly.bind {:mods [] :key "XF86AudioRaiseVolume" :action "exec"
                :cmd "noctalia msg volume-up"})
(stonefly.bind {:mods [] :key "XF86AudioLowerVolume" :action "exec"
                :cmd "noctalia msg volume-down"})
(stonefly.bind {:mods [] :key "XF86AudioMute" :action "exec"
                :cmd "noctalia msg volume-mute"})
(stonefly.bind {:mods [] :key "XF86AudioMicMute" :action "exec"
                :cmd "noctalia msg mic-mute"})
(stonefly.bind {:mods [] :key "XF86Launch5" :action "exec"
                :cmd "noctalia msg mic-volume-up"})
(stonefly.bind {:mods shift :key "XF86Launch5" :action "exec"
                :cmd "noctalia msg mic-volume-down"})
(stonefly.bind {:mods [] :key "XF86MonBrightnessUp" :action "exec"
                :cmd "noctalia msg brightness-up"})
(stonefly.bind {:mods [] :key "XF86MonBrightnessDown" :action "exec"
                :cmd "noctalia msg brightness-down"})
(stonefly.bind {:mods [] :key "XF86KbdBrightnessUp" :action "exec"
                :cmd "noctalia msg keyboard-backlight-up"})
(stonefly.bind {:mods [] :key "XF86KbdBrightnessDown" :action "exec"
                :cmd "noctalia msg keyboard-backlight-down"})
(stonefly.bind {:mods [] :key "XF86AudioPlay" :key_alt "XF86AudioPause"
                :action "exec" :cmd "noctalia msg media toggle"})
(stonefly.bind {:mods [] :key "XF86AudioNext" :action "exec"
                :cmd "noctalia msg media next"})
(stonefly.bind {:mods [] :key "XF86AudioPrev" :action "exec"
                :cmd "noctalia msg media previous"})

;; Super tap → launcher; double-tap → scratchpad term.
(stonefly.tap {:key "Super_L" :key_alt "Super_R"
               :single "spawn_launcher"
               :double "scratch_toggle_term"})
