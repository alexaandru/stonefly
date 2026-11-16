;; Touchpad gesture binds (sf.gesture). First call replaces C defaults.
;; Swipe: fingers 3–4 are reliable; 2 is usually scroll/pinch, not swipe.
;; Pinch: typically 2 fingers; directions pinch / pinchin / pinchout.
;; :mods is an exact modifier mask (same as keybinds) — Super+pinch leaves
;; plain pinch for browser/map zoom.
;; Built-ins (swipe): :workspace (threshold step) :columns_pan (continuous).
;; Pinch fires normal bind actions once |scale-1| ≥ 0.15.
(sf.gesture {:fingers 3 :axis :vertical} :workspace)
(sf.gesture {:fingers 3 :axis :horizontal} :columns_pan)

;; Super+pinch out → edge-max; Super+pinch in → restore.
(sf.gesture {:fingers 2 :axis :pinchout :mods :Super} :maximize_to_edges)
(sf.gesture {:fingers 2 :axis :pinchin :mods :Super} :unmaximize)
