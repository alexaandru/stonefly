-- Stonefly compositor API for fennel-ls.
-- Install: copy to $XDG_DATA_HOME/fennel-ls/docsets/sf.lua
--   (usually ~/.local/share/fennel-ls/docsets/sf.lua)
-- Enable in the project flsproject.fnl:
--   {:lua-version :lua5.1 :libraries {:sf true}}
-- Source of truth: src/config/config_lua.c (push_sf_table + opt_* tables).

return {
  sf = {
    binding = "sf",
    metadata = {
      ["fls/itemKind"] = "Module",
      ["fnl/docstring"] = "Stonefly compositor config API (LuaJIT / Fennel). Global injected by the compositor — not a require().",
    },
    fields = {
    ["opt"] = {
      binding = "sf.opt",
      metadata = {
        ["fls/itemKind"] = "Module",
        ["fnl/docstring"] = "Day-zero options. Assign with (tset sf.opt :key value). Unknown keys ignored. Colors are {r g b a} float tables.",
      },
      fields = {
      ["layout"] = {
        binding = "sf.opt.layout",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "Global layout: \"stacking\" (implemented); \"columns\"/\"tile\" reserved. Resolve workspace→output→global.",
        },
      },
      ["scale"] = {
        binding = "sf.opt.scale",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "DRM output scale (float). Nested/headless forces 1.0.",
        },
      },
      ["cursor_size"] = {
        binding = "sf.opt.cursor_size",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "Default cursor size in logical px (int).",
        },
      },
      ["gap"] = {
        binding = "sf.opt.gap",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "Gap around max-gaps / snap content box (int px).",
        },
      },
      ["cascade_step"] = {
        binding = "sf.opt.cascade_step",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "Cascade offset step (int px).",
        },
      },
      ["cascade_wrap"] = {
        binding = "sf.opt.cascade_wrap",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "Cascade wrap count (int).",
        },
      },
      ["slide_duration_ms"] = {
        binding = "sf.opt.slide_duration_ms",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "Map/unmap/scratch slide duration (int ms). 0 disables.",
        },
      },
      ["slide_dir_closest"] = {
        binding = "sf.opt.slide_dir_closest",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "bool — prefer closest usable edge for slides.",
        },
      },
      ["morph_duration_ms"] = {
        binding = "sf.opt.morph_duration_ms",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "Snap/max morph duration (int ms). 0 disables.",
        },
      },
      ["client_opacity"] = {
        binding = "sf.opt.client_opacity",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "Focused client opacity (float 0–1).",
        },
      },
      ["inactive_opacity"] = {
        binding = "sf.opt.inactive_opacity",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "Unfocused client opacity (float 0–1).",
        },
      },
      ["corner_radius"] = {
        binding = "sf.opt.corner_radius",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "SceneFX corner radius (int px).",
        },
      },
      ["border_thickness"] = {
        binding = "sf.opt.border_thickness",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "Compositor border thickness (int px).",
        },
      },
      ["shadow_blur_sigma"] = {
        binding = "sf.opt.shadow_blur_sigma",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "Drop-shadow blur sigma (float).",
        },
      },
      ["border_active"] = {
        binding = "sf.opt.border_active",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "RGBA table {r g b a} for focused border.",
        },
      },
      ["border_inactive"] = {
        binding = "sf.opt.border_inactive",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "RGBA table {r g b a} for idle border.",
        },
      },
      ["border_urgent"] = {
        binding = "sf.opt.border_urgent",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "RGBA table {r g b a} for urgent/bell border.",
        },
      },
      ["shadow_color"] = {
        binding = "sf.opt.shadow_color",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "RGBA table {r g b a} for drop shadow.",
        },
      },
      ["window_blur"] = {
        binding = "sf.opt.window_blur",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "bool — per-window SceneFX blur.",
        },
      },
      ["blur_xray"] = {
        binding = "sf.opt.blur_xray",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "bool — true = wallpaper-only blur (cheap); false = real backdrop.",
        },
      },
      ["blur_num_passes"] = {
        binding = "sf.opt.blur_num_passes",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "Blur passes (int).",
        },
      },
      ["blur_radius"] = {
        binding = "sf.opt.blur_radius",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "Blur radius (int).",
        },
      },
      ["blur_noise"] = {
        binding = "sf.opt.blur_noise",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "Blur noise (float).",
        },
      },
      ["blur_brightness"] = {
        binding = "sf.opt.blur_brightness",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "Blur brightness (float).",
        },
      },
      ["blur_contrast"] = {
        binding = "sf.opt.blur_contrast",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "Blur contrast (float).",
        },
      },
      ["blur_saturation"] = {
        binding = "sf.opt.blur_saturation",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "Blur saturation (float).",
        },
      },
      ["bell_flash_ms"] = {
        binding = "sf.opt.bell_flash_ms",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "xdg-system-bell amber flash duration (int ms).",
        },
      },
      ["snap_threshold"] = {
        binding = "sf.opt.snap_threshold",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "Magnetic drag-snap threshold (int px).",
        },
      },
      ["snap_preview_border"] = {
        binding = "sf.opt.snap_preview_border",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "Snap preview border (int px).",
        },
      },
      ["snap_preview_opacity"] = {
        binding = "sf.opt.snap_preview_opacity",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "Snap preview opacity (float).",
        },
      },
      ["tap_max_hold_ms"] = {
        binding = "sf.opt.tap_max_hold_ms",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "Max hold for a tap (int ms).",
        },
      },
      ["double_tap_gap_ms"] = {
        binding = "sf.opt.double_tap_gap_ms",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "Double-tap gap (int ms).",
        },
      },
      ["swipe_workspace_threshold"] = {
        binding = "sf.opt.swipe_workspace_threshold",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "3-finger vertical swipe threshold (int).",
        },
      },
      ["swipe_workspace_natural"] = {
        binding = "sf.opt.swipe_workspace_natural",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "bool — natural swipe (finger up → next).",
        },
      },
      ["cursor_follows_focus"] = {
        binding = "sf.opt.cursor_follows_focus",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "bool — warp pointer into newly focused window on keyboard focus cycle / workspace switch (default true). Click-to-focus does not warp.",
        },
      },
      ["nudge_step"] = {
        binding = "sf.opt.nudge_step",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "Pixel step for nudge / resize_edge actions (int, default 25).",
        },
      },
      ["ssd_mode"] = {
        binding = "sf.opt.ssd_mode",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "SSD policy: \"never\" | \"auto\" | \"prefer_ssd\" | \"always\".",
        },
      },
      ["ssd_height"] = {
        binding = "sf.opt.ssd_height",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "SSD titlebar height (int px, min 16).",
        },
      },
      ["ssd_font_size"] = {
        binding = "sf.opt.ssd_font_size",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "SSD title font size (int, 8–72).",
        },
      },
      ["resize_border_in"] = {
        binding = "sf.opt.resize_border_in",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "Resize hit slop into the window (int px). 0 = off.",
        },
      },
      ["resize_border_out"] = {
        binding = "sf.opt.resize_border_out",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "Resize hit slop past the border (int px). 0 = off.",
        },
      },
      ["capture_block_app_ids"] = {
        binding = "sf.opt.capture_block_app_ids",
        metadata = {
          ["fls/itemKind"] = "Variable",
          ["fnl/docstring"] = "Comma string or table of app_id substrings that get capture_blocked.",
        },
      },
      },
    },
    ["chain"] = {
      binding = "sf.chain",
      metadata = {
        ["fls/itemKind"] = "Function",
        ["fnl/arglist"] = {"…steps"},
        ["fnl/docstring"] = "Compose action steps for :action / :single / :double.\n  (sf.chain \"maximize\")\n  (sf.chain {:action \"exec\" :cmd \"…\"} \"maximize_to_edges\")\nEach arg is an action name string or {:action :cmd} table. Returns a value passable as :action.",
      },
    },
    ["bind"] = {
      binding = "sf.bind",
      metadata = {
        ["fls/itemKind"] = "Function",
        ["fnl/arglist"] = {"chord-or-opts", "?opts"},
        ["fnl/docstring"] = "Register a keybind.\n  (sf.bind \"Super+w\" {:action \"exec\" :cmd \"…\"})\n  (sf.bind {:key \"Super+Shift+Tab\" :action \"focus_prev\"})\nOptional :key_alt. Shift+Tab auto-adds ISO_Left_Tab.\nAction names: none quit close focus_next focus_prev focus_next_all focus_prev_all maximize maximize_to_edges unmaximize toggle_maximize toggle_maximize_to_edges minimize unminimize snap_left snap_right snap_top snap_bottom unsnap exec (needs :cmd) scratch_toggle scratch_claim (cmd = pad name) workspace_next workspace_prev workspace_switch (cmd = N) move_to_workspace_next move_to_workspace_prev move_to_workspace (cmd = N) pointer_center focus_monitor send_to_monitor (cmd = left|right|up|down|next|prev|connector) nudge resize_edge (cmd = left|right|up|down) toggle_always_on_top always_on_top (cmd = true|false).\n:action may be a string, {:action :cmd} table, or (sf.chain …).",
      },
    },
    ["tap"] = {
      binding = "sf.tap",
      metadata = {
        ["fls/itemKind"] = "Function",
        ["fnl/arglist"] = {"mod-or-opts", "?opts"},
        ["fnl/docstring"] = "Register a modifier tap (L+R expanded).\n  (sf.tap \"Super\" {:single \"exec\" :single_cmd \"…\" :double \"scratch_toggle\" :double_cmd \"term\"})\n  (sf.tap {:key \"Super\" :single (sf.chain …)})\nAction names: none quit close focus_next focus_prev focus_next_all focus_prev_all maximize maximize_to_edges unmaximize toggle_maximize toggle_maximize_to_edges minimize unminimize snap_left snap_right snap_top snap_bottom unsnap exec (needs :cmd) scratch_toggle scratch_claim (cmd = pad name) workspace_next workspace_prev workspace_switch (cmd = N) move_to_workspace_next move_to_workspace_prev move_to_workspace (cmd = N) pointer_center focus_monitor send_to_monitor (cmd = left|right|up|down|next|prev|connector) nudge resize_edge (cmd = left|right|up|down) toggle_always_on_top always_on_top (cmd = true|false).\n:action may be a string, {:action :cmd} table, or (sf.chain …).",
      },
    },
    ["scratchpad"] = {
      binding = "sf.scratchpad",
      metadata = {
        ["fls/itemKind"] = "Function",
        ["fnl/arglist"] = {"spec"},
        ["fnl/docstring"] = "Declare a named scratchpad. Keys: :name :cmd :app_id? :auto_hide? (default true).\nExample: (sf.scratchpad {:name \"term\" :cmd \"ghostty --class=stonefly.scratch.term\" :app_id \"stonefly.scratch.term\"})",
      },
    },
    ["window_rule"] = {
      binding = "sf.window_rule",
      metadata = {
        ["fls/itemKind"] = "Function",
        ["fnl/arglist"] = {"match", "actions", "?parent"},
        ["fnl/docstring"] = "Add a window rule; returns a tag string for parent composition.\n  (sf.window_rule match actions [?parent-tag])\nMatch: :app_id / :title as Lua patterns (string.find). Exact: \"^…$\"; escape magic with %.\nDeclarative: :center :float :borderless :always_on_top (bool), :width :height (int),\n  :ssd (\"never\"|\"auto\"|\"prefer_ssd\"|\"always\").\nWM: :action string | step table | (sf.chain …) — same vocabulary as binds (prefer maximize / maximize_to_edges over toggles; toggles flip on every reapply).\nLongest pattern wins; child overwrites parent keys.",
      },
    },
    ["exec"] = {
      binding = "sf.exec",
      metadata = {
        ["fls/itemKind"] = "Function",
        ["fnl/arglist"] = {"…cmd", "?opts"},
        ["fnl/docstring"] = "Session spawn after WAYLAND_DISPLAY is up (Hyprland exec-once / sway exec).\n  (sf.exec \"flatpak run com.slack.Slack\")\n  (sf.exec \"flatpak\" \"run\" \"com.slack.Slack\")\n  (sf.exec \"helper\" {:reload true})\nOne string → sh -c as-is (pipes ok). Several strings → shell-quoted argv join.\nTrailing opts table: :reload bool (default false = once per compositor start; true also on future config reload). Distinct from bind action :exec.",
      },
    },
    ["input"] = {
      binding = "sf.input",
      metadata = {
        ["fls/itemKind"] = "Function",
        ["fnl/arglist"] = {"match", "opts"},
        ["fnl/docstring"] = "Libinput / key-repeat rule for devices matching match.\n  (sf.input \"touchpad\" {:tap_to_click true :natural_scroll true :dwt true})\n  (sf.input \"keyboard\" {:repeat_rate 40 :repeat_delay 250})\nmatch: \"keyboard\"|\"pointer\"|\"mouse\"|\"touchpad\"|\"touch\" or a case-insensitive device-name substring.\nopts bools: :tap_to_click :natural_scroll :left_handed :dwt\nopts accel: :accel_speed (-1..1) :accel_profile (\"none\"|\"flat\"|\"adaptive\")\nopts keyboard: :repeat_rate (keys/sec) :repeat_delay (ms)\nLater rules override earlier ones for the same key. Applied on device add (reload is separate).",
      },
    },
    ["stdpath"] = {
      binding = "sf.stdpath",
      metadata = {
        ["fls/itemKind"] = "Function",
        ["fnl/arglist"] = {"which"},
        ["fnl/docstring"] = "Resolve XDG path. which is \"config\"|\"data\"|\"cache\" (STONEFLY_* env, else XDG_*, else ~/.config|share|cache/stonefly).",
      },
    },
    ["runtime_file"] = {
      binding = "sf.runtime_file",
      metadata = {
        ["fls/itemKind"] = "Function",
        ["fnl/arglist"] = {"rel"},
        ["fnl/docstring"] = "Find rel under the runtime path (rtp); return absolute path or nil.",
      },
    },
    ["packadd"] = {
      binding = "sf.packadd",
      metadata = {
        ["fls/itemKind"] = "Function",
        ["fnl/arglist"] = {"name"},
        ["fnl/docstring"] = "Load pack from data/pack/dist/opt/<name> or data/pack/opt/<name> (also system datadir). Adds to rtp and runs plugin/*.lua. Builtin: fennel-shim.",
      },
    },
    ["_fs_mtime"] = {
      binding = "sf._fs_mtime",
      metadata = {
        ["fls/itemKind"] = "Function",
        ["fnl/arglist"] = {"path"},
        ["fnl/docstring"] = "Internal: unix mtime of path, or nil if missing. Used by fennel-shim compile cache.",
      },
    },
    },
  },
}
