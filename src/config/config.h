/* Runtime config: C defaults + LuaJIT/Fennel overlays (FEATURES/ROADMAP §10). */
#ifndef SF_CONFIG_H
#define SF_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>
#include <wlr/types/wlr_keyboard.h>

#include "ssd/ssd_policy.h"
#include "util/color.h"
#include "util/path.h"
#include "wm/workspace.h"

struct sf_server;

/* Interesting modifiers for bind matching (ignore Caps etc.). */
#define SF_MOD_MASK                                                                                \
	((uint32_t)WLR_MODIFIER_SHIFT | (uint32_t)WLR_MODIFIER_CTRL | (uint32_t)WLR_MODIFIER_ALT | \
	 (uint32_t)WLR_MODIFIER_LOGO)

#define SF_SUPER ((uint32_t)WLR_MODIFIER_LOGO)
#define SF_ALT ((uint32_t)WLR_MODIFIER_ALT)
#define SF_CTRL ((uint32_t)WLR_MODIFIER_CTRL)
#define SF_SHIFT ((uint32_t)WLR_MODIFIER_SHIFT)

#define SF_SCRATCHPAD_MAX 8
#define SF_WINDOW_RULE_MAX 64
#define SF_EXEC_MAX 64
#define SF_INPUT_RULE_MAX 64

/* libinput accel profile for sf.input; UNSET = leave device default. */
enum sf_input_accel {
	SF_INPUT_ACCEL_UNSET = 0,
	SF_INPUT_ACCEL_NONE,
	SF_INPUT_ACCEL_FLAT,
	SF_INPUT_ACCEL_ADAPTIVE,
};

enum sf_action {
	SF_ACTION_NONE = 0,
	SF_ACTION_QUIT,
	SF_ACTION_CLOSE,
	SF_ACTION_FOCUS_NEXT,
	SF_ACTION_FOCUS_PREV,
	SF_ACTION_FOCUS_NEXT_ALL,
	SF_ACTION_FOCUS_PREV_ALL,
	/* Gap-max (usual maximize) / edge-max / restore either. */
	SF_ACTION_MAXIMIZE,
	SF_ACTION_MAXIMIZE_TO_EDGES,
	SF_ACTION_UNMAXIMIZE,
	SF_ACTION_TOGGLE_MAXIMIZE,
	SF_ACTION_TOGGLE_MAXIMIZE_TO_EDGES,
	SF_ACTION_MINIMIZE,
	SF_ACTION_UNMINIMIZE,
	SF_ACTION_SNAP_LEFT,
	SF_ACTION_SNAP_RIGHT,
	SF_ACTION_SNAP_TOP,
	SF_ACTION_SNAP_BOTTOM,
	SF_ACTION_UNSNAP,
	/* Run keybind.cmd via /bin/sh -c (spawn, noctalia msg, etc.). */
	SF_ACTION_EXEC,
	/* Named scratchpad; cmd holds pad name (default "term"). */
	SF_ACTION_SCRATCH_TOGGLE,
	SF_ACTION_SCRATCH_CLAIM,
	/* Dynamic workspaces (cmd holds 1-based index for switch/move). */
	SF_ACTION_WORKSPACE_NEXT,
	SF_ACTION_WORKSPACE_PREV,
	SF_ACTION_WORKSPACE_SWITCH,
	SF_ACTION_MOVE_TO_WORKSPACE_NEXT,
	SF_ACTION_MOVE_TO_WORKSPACE_PREV,
	SF_ACTION_MOVE_TO_WORKSPACE,
	/* Warp pointer to center of focused window (ignores cursor_follows_focus). */
	SF_ACTION_POINTER_CENTER,
	/* cmd = target: connector name, left|right|up|down, next|prev. */
	SF_ACTION_FOCUS_MONITOR,
	SF_ACTION_SEND_TO_MONITOR,
	/* cmd = left|right|up|down. Resize: right/down grow, left/up shrink. */
	SF_ACTION_NUDGE,
	SF_ACTION_RESIZE_EDGE,
	SF_ACTION_TOGGLE_ALWAYS_ON_TOP,
	/* cmd = true|false|1|0|on|off. */
	SF_ACTION_ALWAYS_ON_TOP,
	/* Fullscreen: set / unset / toggle (default F11 → toggle). */
	SF_ACTION_SET_FULLSCREEN,
	SF_ACTION_UNSET_FULLSCREEN,
	SF_ACTION_TOGGLE_FULLSCREEN,
	/* cmd = next|prev (default next). Cycles stacking/columns/master/dwindle/monocle. */
	SF_ACTION_ARRANGE_CYCLE,
	/* cmd = stacking|columns|master|tile|dwindle|monocle. */
	SF_ACTION_ARRANGE_SET,
	/* cmd = left|right|up|down — focus nearest tiled peer (or cycle in monocle). */
	SF_ACTION_FOCUS_DIR,
	/* Tiling: leave/rejoin the tile set. Stacking: no-op unless edged→float. */
	SF_ACTION_TOGGLE_FLOAT,
	SF_ACTION_FLOAT,
	SF_ACTION_UNFLOAT,
	/* Columns: cmd = left|right — stack focused into neighbor column. */
	SF_ACTION_CONSUME,
	/* cmd = next|cw|prev|ccw (default next). Rotate tile order; focus stays put.
	 * next/cw: last→first (columns: last becomes 1st, others shift right). */
	SF_ACTION_ARRANGE_ROTATE,

	/* One past the last real action — table sizes / completeness checks. */
	SF_ACTION_COUNT,
};

/* One step in a bind/tap/rule action (or sf.chain). cmd for EXEC / scratch / ws. */
struct sf_action_step {
	enum sf_action action;
	char *cmd;
};

struct sf_keybind {
	uint32_t mods; /* compared against (modifiers & SF_MOD_MASK) */
	xkb_keysym_t sym;
	xkb_keysym_t sym_alt; /* optional second sym (e.g. ISO_Left_Tab); 0 = none */
	/* SF_ARRANGE_UNSET = any; SF_ARRANGE_TILING = any tiling engine; else exact. */
	enum sf_arrange arrange;
	struct sf_action_step *steps;
	size_t n_steps;
};

/* Lone-modifier tap: press+release without other keys. Double waits the gap. */
struct sf_tapbind {
	xkb_keysym_t sym;
	xkb_keysym_t sym_alt; /* e.g. Super_R; 0 = none */
	struct sf_action_step *single_steps;
	size_t n_single_steps;
	struct sf_action_step *double_steps; /* NULL / 0 = single-only */
	size_t n_double_steps;
};

struct sf_scratchpad_spec {
	char *name;
	char *cmd;
	char *app_id; /* match for spawn claim; empty = first map while pending */
	/* Hide when focus moves to another window on the same output (peeble). */
	bool auto_hide;
};

/* Session spawn from sf.exec(...). cmd is sh -c payload; reload=false is
 * once-per-compositor-start (Hyprland exec-once); reload=true re-runs when
 * live config reload lands. */
struct sf_exec {
	char *cmd;
	bool reload;
};

/* sf.input(match, opts). match is a type keyword (keyboard|pointer|mouse|
 * touchpad|touch) or a case-insensitive device-name substring. Bool/int
 * fields use -1 as unset so later rules can override. Applied on device add. */
struct sf_input_rule {
	char *match;
	int tap_to_click;   /* -1 unset, 0/1 */
	int natural_scroll; /* -1 unset, 0/1 */
	int left_handed;    /* -1 unset, 0/1 */
	int dwt;	    /* disable-while-typing; -1 unset, 0/1 */
	bool accel_speed_set;
	double accel_speed; /* [-1, 1] */
	enum sf_input_accel accel_profile;
	bool repeat_rate_set;
	int repeat_rate; /* keys/sec */
	bool repeat_delay_set;
	int repeat_delay; /* ms before first repeat */
};

/* Fennel window_rule(match, actions [, parent]). Parent merge baked in.
 * app_id / title are Lua patterns (string.find); empty/NULL = any. */
struct sf_window_rule {
	char id[40];
	char parent_id[40];
	char *app_id;
	char *title;
	bool center;
	bool center_set;
	bool float_win;
	bool float_set;
	bool borderless;
	bool borderless_set;
	bool always_on_top;
	bool always_on_top_set;
	bool ssd_set;
	enum sf_ssd_mode ssd_mode;
	bool width_set;
	bool height_set;
	int width;
	int height;
	/* Optional WM actions (:action string or sf.chain); run on the matched window. */
	struct sf_action_step *steps;
	size_t n_steps;
};

struct sf_config {
	float output_scale;
	int cursor_size_default;
	int gap_size;
	int cascade_step;
	int cascade_wrap;
	int slide_duration_ms;
	bool slide_dir_closest;
	int morph_duration_ms;
	float client_opacity;	/* focused / active chrome */
	float inactive_opacity; /* unfocused; default = client_opacity (no dim) */
	int corner_radius;
	int border_thickness;
	float shadow_blur_sigma;
	struct sf_rgba border_active;
	struct sf_rgba border_inactive;
	struct sf_rgba border_urgent;
	/* Explicit floats (arrange_floating): pink / dimmed pink. */
	struct sf_rgba border_float_active;
	struct sf_rgba border_float_inactive;
	struct sf_rgba shadow_color;
	bool window_blur; /* per-window SceneFX blur behind chrome */
	/* true → blur wallpaper/optimized underlay only (mango xray);
	 * false → blur whatever is actually behind the window (other clients). */
	bool blur_xray;
	/* Global SceneFX blur_data (optimized wallpaper pass). */
	int blur_num_passes;
	int blur_radius;
	float blur_noise;
	float blur_brightness;
	float blur_contrast;
	float blur_saturation;
	int bell_flash_ms;
	int snap_threshold;
	int snap_preview_border;
	float snap_preview_opacity;
	int tap_max_hold_ms;
	int double_tap_gap_ms;

	/* Global arrange default (workspace → output → global). */
	enum sf_arrange arrange;
	/* Master engine: side + factor + count (Lua-configurable). */
	enum sf_master_pos master_pos;
	float master_factor;  /* 0.05..0.95; default 0.55 */
	float dwindle_factor; /* 0.05..0.95; default 0.5 — first/rest split */
	int master_count;     /* ≥1; default 1 */
	/* Niri columns: column width as fraction of usable (default 0.5). */
	float column_ratio;
	/* 3-finger vertical swipe distance (logical px) to change workspace. */
	int swipe_workspace_threshold;
	/* Natural: finger up moves content up → next; finger down → prev.
	 * Classic (false): finger points at the destination (down → next). */
	bool swipe_workspace_natural;

	/* Warp pointer into the newly focused window on keyboard focus cycle
	 * and workspace switch (click-to-focus does not warp). */
	bool cursor_follows_focus;

	/* Pixel step for nudge / resize_edge actions (default 25). */
	int nudge_step;

	/* SSD titlebars: never|auto|prefer_ssd|always. */
	enum sf_ssd_mode ssd_mode;
	int ssd_height;	   /* logical px; default 28 */
	int ssd_font_size; /* fcft pt; default 17 */

	/* Border/corner resize hit slop (logical px). Inner = into the window from
	 * the content/SSD edge; outer = past the drawn border. Both 0 disables. */
	int resize_border_in;  /* default 3 */
	int resize_border_out; /* default 1 */

	/* Comma-separated app_id substrings that auto-set capture_blocked. */
	char *capture_block_app_ids;

	struct sf_keybind *keybinds;
	size_t n_keybinds;
	struct sf_tapbind *tapbinds;
	size_t n_tapbinds;
	struct sf_scratchpad_spec *scratchpads;
	size_t n_scratchpads;
	struct sf_window_rule *window_rules;
	size_t n_window_rules;
	struct sf_exec *execs;
	size_t n_execs;
	struct sf_input_rule *input_rules;
	size_t n_input_rules;

	/* LuaJIT state; NULL when SF_NOCONFIG or before load. */
	void *lua; /* lua_State * — opaque here to avoid forcing luajit.h on all TUs */
};

void sf_config_set_defaults(struct sf_config *cfg);
void sf_config_finish(struct sf_config *cfg);

/* XDG paths — honor env; never hardcode ~/.config. Return 0 on success. */
int sf_config_dir(char *buf, size_t n);
int sf_data_dir(char *buf, size_t n);
int sf_cache_dir(char *buf, size_t n);
int sf_config_init_path(char *buf, size_t n);

/* Recursive mkdir (mkdir -p); 0 = ok. */
/* Write embedded default tree; refuse if init.lua exists. 0 = ok. */
int sf_config_write_default(void);

/* Load user init.lua or embedded Fennel-first default into server->config. */
bool sf_config_load(struct sf_server *server);

/* Fork sf.exec entries. is_reload false → all; true → only {:reload true}.
 * Call after WAYLAND_DISPLAY is set (same window as -s). */
void sf_config_run_execs(struct sf_server *server, bool is_reload);

const struct sf_config *sf_cfg(const struct sf_server *server);

#endif /* SF_CONFIG_H */
