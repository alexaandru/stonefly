;; Input-device policy via sf.input(match, opts).
;; match: "keyboard" | "pointer" | "mouse" | "touchpad" | "touch"
;;   or a case-insensitive substring of the libinput device name.
;; Later rules override earlier ones for the same setting.
;; Bools: :tap_to_click :natural_scroll :left_handed :dwt
;; Accel: :accel_speed (-1..1) :accel_profile ("none"|"flat"|"adaptive")
;; Keyboard: :repeat_rate (keys/sec) :repeat_delay (ms)
;; Applied when a device appears (live reload is TODO NEXT #7).

(sf.input "touchpad" {:tap_to_click true
                      :natural_scroll true
                      :dwt true})
