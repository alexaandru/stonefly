;; Session spawns (Hyprland exec-once / sway exec).
;;   (sf.exec "cmd with pipes | ok")
;;   (sf.exec "flatpak" "run" "com.slack.Slack")
;;   (sf.exec "helper" {:reload true})  ; also on future config reload
;; :reload defaults to false (once per compositor start).

(sf.exec "flatpak" "run" "com.slack.Slack")
