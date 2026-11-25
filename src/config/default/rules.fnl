;; Window rules — match.app_id / match.title are Lua patterns (string.find).
;; Pattern docs: https://www.lua.org/manual/5.1/manual.html#5.4.1
;; Exact whole string: "^Google Chrome$". Escape magic (.-%[]()*+?^$) with %
;; (e.g. "google%-chrome"). Lua patterns have no "|"; use one rule per alt.
;; Longest pattern wins (one match); put float+opacity on the same rule.
;;
;; Declarative: center, float, width, height, borderless, always_on_top,
;;   ssd ("never"|"auto"|"prefer_ssd"|"always"), opacity, inactive_opacity,
;;   no_dim, tearing.
;;   :float true → sticky arrange_floating (out of columns; pink border).
;; WM: :action string | {:action :cmd} | (sf.chain …) — same names as binds
;;   (float, maximize, maximize_to_edges, …). Prefer absolute maximize
;;   on rules; toggles flip again on SoftPlace reapply.
;; Parent tag composes match+actions.
;;
;; Defaults seeded from x/umbriel/config.toml [[window_rule]], with niri-only
;; extras (zoom annotate, Firefox profile, NewsFlash). No size-based dialog
;; guessing — only explicit rules.

(let [rule sf.window_rule
      chrome (rule {:app_id "google%-chrome"} {})
      chromium (rule {:app_id "^chromium$"} {})
      float-chrome {:center true :borderless false :ssd :never :float true}
      ;; Sticky-float app_ids (Umbriel float list + peers).
      float-apps ["^dev%.noctalia%.Noctalia%.Settings$"
                  "^org%.quickshell$"
                  "^anki$"
                  "^udiskie$"
                  "^io%.github%.dvlv%.boxbuddyrs$"
                  "^org%.gnome%.eog$"
                  "^zenity$"
                  "^xdg%-desktop%-portal%-gtk$"
                  "^org%.gnome%.Mahjongg$"
                  "^org%.gnome%.World%.PikaBackup$"
                  "^org%.gnome%.Nautilus$"
                  "^org%.gnome%.Loupe$"
                  "^org%.gnome%.TwentyFortyEight$"]]
  ;; Chrome / Chromium profile picker (Umbriel + existing stonefly chrome).
  (rule {:title "^Google Chrome$"} float-chrome chrome)
  (rule {:title "^Chromium$"} float-chrome chromium)
  ;; Noctalia shell windows (Umbriel sizes).
  (rule {:app_id "^dev%.noctalia%.Noctalia$"}
        {:float true :width 1020 :height 900})
  (rule {:app_id "^dev%.noctalia%.UmbrielSharePicker$"}
        {:float true :width 800 :height 600})
  (rule {:app_id :dev.noctalia.luciole} {:ssd :never})
  ;; Picture-in-Picture / toast-like surfaces.
  (rule {:title "^Picture%-in%-Picture$"} {:float true})
  (rule {:title "^Picture in picture$"} {:float true})
  (rule {:title "^notificationtoasts_.*_desktop"}
        {:always_on_top true})
  ;; Generic float list.
  (each [_ id (ipairs float-apps)]
    (rule {:app_id id} {:float true}))
  ;; Float + opacity together (longest-match picks one rule).
  (rule {:app_id "^neovide$"} {:float true :opacity 0.92})
  (rule {:app_id "^org%.squidowl%.halloy$"} {:opacity 0.95})
  (rule {:app_id "^io%.gitlab%.news_flash%.NewsFlash$"} {:opacity 0.95})
  ;; Niri-only extras Umbriel lacks.
  (rule {:app_id "^zoom$" :title "^annotate_toolbar$"}
        {:float true :width 1 :height 1})
  (rule {:app_id "^firefox$" :title "^Firefox %- Choose a profile$"}
        {:float true})
  (rule {:app_id "^Facebook %- Google Chrome$"} {:float true})
  ;; Existing stonefly keepers (not in Umbriel defaults).
  (rule {:app_id "dosbox"} {:center true
                            :borderless false
                            :ssd :never
                            :float true})
  (rule {:app_id "gcr%-prompter"} {:center true :borderless false :float true})
  (rule {:app_id :com.slack.Slack}
        {:borderless true :action :maximize_to_edges :float true :ssd :never}))
