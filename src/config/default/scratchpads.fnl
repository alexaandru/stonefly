;; Named scratchpads. auto_hide (default true): hide when focus moves to
;; another window on the same output (click / Super+Tab); other-head or
;; nil focus leaves the pad shown (peeble per-monitor fake-layer).
(local pads [
  {:name :term
   :cmd "ghostty --class=stonefly.scratch.term"
   :app_id "stonefly.scratch.term"
   :auto_hide true}
])

(each [_ pad (ipairs pads)]
  (sf.scratchpad pad))
