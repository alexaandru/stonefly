;; Window rules — match.app_id / match.title are Lua patterns (string.find).
;; Pattern docs: https://www.lua.org/manual/5.1/manual.html#5.4.1
;; Exact whole string: "^Google Chrome$". Escape magic (.-%[]()*+?^$) with %
;; (e.g. "google%-chrome").
;;
;; Declarative: center, float, width, height, borderless, always_on_top,
;;   ssd ("never"|"auto"|"prefer_ssd"|"always").
;; WM: :action string | {:action :cmd} | (sf.chain …) — same names as binds
;;   (maximize, maximize_to_edges, unmaximize, …). Prefer absolute maximize
;;   on rules; toggles flip again on SoftPlace reapply.
;; Parent tag composes match+actions.
;;
;; No size-based "looks like dialog" guessing — only explicit rules.

(let [rule sf.window_rule
      chrome (rule {:app_id "google%-chrome"} {})]
  (rule {:title "^Google Chrome$"} {:center true :borderless true :ssd "never"}
        chrome)
  (rule {:app_id "dosbox"} {:center true :borderless true :ssd "never"})
  (rule {:app_id "gcr%-prompter"} {:center true :borderless true})
  (rule {:app_id "com.slack.Slack"}
        {:borderless true :action :maximize_to_edges}))
