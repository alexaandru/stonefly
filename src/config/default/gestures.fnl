;; Touchpad gesture binds (sf.gesture). First call replaces C defaults.
;; Swipe: fingers 3–4 are reliable; 2 is usually scroll/pinch, not swipe.
;; Pinch: typically 2 fingers; directions pinch / pinchin / pinchout.
;; :mods is an exact modifier mask (same as keybinds) — Super+pinch leaves
;; plain pinch for browser/map zoom.
;; Built-ins (swipe): :workspace (threshold step) :columns_pan (continuous)
;; :overview (4-finger open/close) :overview_pan (filmstrip while open).
;; Pinch fires normal bind actions once |scale-1| ≥ 0.15.
(sf.gesture {:fingers 3 :axis :vertical} :workspace)
(sf.gesture {:fingers 3 :axis :horizontal} :columns_pan)
(sf.gesture {:fingers 4 :axis :vertical} :overview)
;; While overview is open these outrank workspace/columns_pan for the same fingers.
(sf.gesture {:fingers 3 :axis :vertical} :overview_pan)
(sf.gesture {:fingers 3 :axis :horizontal} :overview_pan)

;; Super+pinch out → edge-max; Super+pinch in → restore.
(sf.gesture {:fingers 2 :axis :pinchout :mods :Super} :maximize_to_edges)
(sf.gesture {:fingers 2 :axis :pinchin :mods :Super} :unmaximize)
