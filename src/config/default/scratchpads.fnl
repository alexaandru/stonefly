;; Named scratchpads.
(local pads [
  {:name :term
   :cmd "ghostty --class=stonefly.scratch.term"
   :app_id "stonefly.scratch.term"}
])

(each [_ pad (ipairs pads)]
  (stonefly.scratchpad pad))
