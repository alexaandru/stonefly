;; Named scratchpads. auto_hide (default true): hide when focus moves to
;; another window on the same output (click / Super+Tab); other-head or
;; nil focus leaves the pad shown (peeble per-monitor fake-layer).
;;
;; Spawn claim uses an xdg-activation token (real app_id / taskbar icons).
;; Optional :app_id is only a soft fallback filter for clients that ignore
;; XDG_ACTIVATION_TOKEN — do not use a fake class just to claim.
(local pads [
  {:name :term
   :cmd "ghostty"
   :auto_hide true}
])

(each [_ pad (ipairs pads)]
  (sf.scratchpad pad))
