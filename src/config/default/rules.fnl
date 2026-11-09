;; Window rules — match.app_id / match.title are Lua patterns (string.find).
;; Pattern docs: https://www.lua.org/manual/5.1/manual.html#5.4.1
;; Exact whole string: "^Google Chrome$". Escape magic (.-%[]()*+?^$) with %
;; (e.g. "google%-chrome"). Actions: center, float, width, height,
;; ssd ("never"|"auto"|"prefer_ssd"|"always"), borderless. Parent tag composes
;; match+actions.
;;
;; No size-based "looks like dialog" guessing — only explicit rules.

(local chrome (stonefly.window_rule {:app_id "google%-chrome"} {}))
(stonefly.window_rule {:title "^Google Chrome$"}
                      {:center true :borderless true :ssd "never"}
                      chrome)
(stonefly.window_rule {:app_id "gcr%-prompter"}
                      {:center true :borderless true})
