;; Per-monitor arrange via sf.output(name, opts).
;; name: case-insensitive exact connector (wlr_output->name), e.g. "eDP-1".
;; opts:
;;   :arrange "stacking" | "columns" | "inherit"
;;     Retired names master/tile/dwindle/monocle alias to columns.
;;   :tearing true|false — allow async page-flips for eligible fullscreen
;;     clients that request tearing (default false; safety gate).
;; Later rules override earlier for the same connector.
;; Resolve: workspace override → output → global (sf.opt.arrange).
;; Applied when an output appears and again on live config reload.
;;
;; Examples (uncomment and edit to your connectors):
;;   (sf.output "eDP-1" {:arrange "stacking"})
;;   (sf.output "DP-1"  {:arrange "columns" :tearing true})
