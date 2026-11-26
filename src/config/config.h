/* Runtime config: C defaults + LuaJIT/Fennel overlays (FEATURES/ROADMAP §10). */
#ifndef SF_CONFIG_H
#define SF_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>
#include <wlr/types/wlr_keyboard.h>

#include "config/chord.h"
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
#define SF_OUTPUT_RULE_MAX 32
#define SF_GESTURE_MAX 32
#define SF_MOUSEBIND_MAX 16
#define SF_WHEELBIND_MAX 16
#define SF_TAPBIND_MAX 8
#define SF_TAP_COUNT_MAX 9 /* same range as sf.gesture fingers */

/* libinput accel profile for sf.input; UNSET = leave device default. */
enum sf_input_accel {
	SF_INPUT_ACCEL_UNSET = 0,
	SF_INPUT_ACCEL_NONE,
	SF_INPUT_ACCEL_FLAT,
	SF_INPUT_ACCEL_ADAPTIVE,
};

/* Chrome/protocol “maximize” (CSD / SSD button / foreign) — not Super+f.
 * Default gaps matches Super+m (column / gap-inset max). */
enum sf_client_maximize {
	SF_CLIENT_MAXIMIZE_GAPS = 0,
	SF_CLIENT_MAXIMIZE_EDGES,
};

/* Where unminimize lands the window. Default active = carry to current WS. */
enum sf_unminimize_workspace {
	SF_UNMINIMIZE_WS_ACTIVE = 0,
	SF_UNMINIMIZE_WS_HOME,
};

/* Super+Tab / focus_next|prev on a tiling workspace. Default spatial. */
enum sf_focus_cycle {
	SF_FOCUS_CYCLE_SPATIAL = 0, /* atan2 ring around usable centre */
	SF_FOCUS_CYCLE_MRU,	    /* seat MRU list (stacking default path) */
	SF_FOCUS_CYCLE_STRIP,	    /* columns→rows collect order */
};

/* Columns strip pan focus retarget. Default center. */
enum sf_pan_focus {
	SF_PAN_FOCUS_CENTER = 0,    /* centre-most visible cell (default) */
	SF_PAN_FOCUS_KEEP,	    /* never retarget during pan */
	SF_PAN_FOCUS_WARP,	    /* centre + warp pointer on focus change */
	SF_PAN_FOCUS_UNDER_POINTER, /* prefer cell under cursor among visible */
};

/* Where a new tiled window joins the strip. Default after focused column. */
enum sf_tile_insert {
	SF_TILE_INSERT_AFTER_FOCUS = 0,
	SF_TILE_INSERT_END,
	SF_TILE_INSERT_BEGIN,
};

/* Per-op animation style for sf.opt.anim.<op>.style. */
enum sf_anim_style {
	SF_ANIM_STYLE_DEFAULT = 0, /* auto: popin for tiled, slide for float/scratch */
	SF_ANIM_STYLE_POPIN,	   /* scale + fade at resting box */
	SF_ANIM_STYLE_SLIDE,	   /* slide from/to closest usable edge */
	SF_ANIM_STYLE_FADE,	   /* fade only (scale = 1.0) */
	SF_ANIM_STYLE_NONE,	   /* instant */
};

/* One animation op in sf.opt.anim (enabled, duration_ms, style, scale). */
struct sf_anim_op {
	bool enabled;
	int duration_ms; /* < 0 inherits legacy slide_duration_ms / morph_duration_ms */
	enum sf_anim_style style;
	float scale; /* 0.05..1.0 for popin/popout */
};

/* sf.gesture match direction (Hyprland-shaped). */
enum sf_gesture_dir {
	SF_GESTURE_DIR_SWIPE = 0, /* any swipe axis */
	SF_GESTURE_DIR_HORIZONTAL,
	SF_GESTURE_DIR_VERTICAL,
	SF_GESTURE_DIR_LEFT,
	SF_GESTURE_DIR_RIGHT,
	SF_GESTURE_DIR_UP,
	SF_GESTURE_DIR_DOWN,
	SF_GESTURE_DIR_PINCH,	  /* any pinch */
	SF_GESTURE_DIR_PINCH_IN,  /* scale < 1 */
	SF_GESTURE_DIR_PINCH_OUT, /* scale > 1 */
};

/* Built-in continuous/step verbs, or a normal action fire. */
enum sf_gesture_kind {
	SF_GESTURE_KIND_WORKSPACE = 0, /* threshold step → workspace next/prev */
	SF_GESTURE_KIND_COLUMNS_PAN,   /* continuous strip pan */
	SF_GESTURE_KIND_OVERVIEW,      /* continuous 4-finger open/close progress */
	SF_GESTURE_KIND_OVERVIEW_PAN,  /* continuous filmstrip pan while overview open */
	SF_GESTURE_KIND_ACTION,	       /* fire steps once past threshold */
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
	/* Run keybind.cmd via sf_spawn_cmd (spawn, noctalia msg, etc.). */
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
	/* cmd = next|prev (default next). Cycles stacking ↔ columns. */
	SF_ACTION_ARRANGE_CYCLE,
	/* cmd = stacking|columns (master/tile/dwindle/monocle alias to columns). */
	SF_ACTION_ARRANGE_SET,
	/* cmd = left|right|up|down — focus nearest tiled peer. */
	SF_ACTION_FOCUS_DIR,
	/* Tiling: leave/rejoin the tile set. Stacking: no-op unless edged→float. */
	SF_ACTION_TOGGLE_FLOAT,
	SF_ACTION_FLOAT,
	SF_ACTION_UNFLOAT,
	/* Columns: cmd = left|right — stack focused into neighbor column. */
	SF_ACTION_CONSUME,
	/* Columns: cmd = left|right — pull neighbor's first row into focused column. */
	SF_ACTION_CONSUME_FROM,
	/* cmd = next|cw|prev|ccw (default next). Rotate tile order; focus stays put.
	 * next/cw: last→first (columns: last becomes 1st, others shift right). */
	SF_ACTION_ARRANGE_ROTATE,
	/* Re-run Lua/Fennel init into a staging config; keep previous on failure. */
	SF_ACTION_RELOAD,
	/* Pointer grabs (sf.bind mouse chords). Arm interactive under cursor. */
	SF_ACTION_MOVE_WINDOW,
	SF_ACTION_RESIZE_WINDOW,
	SF_ACTION_RESIZE_CENTER,
	/* Workspace overview filmstrip (Umbriel Mod+O / 4-finger). */
	SF_ACTION_OVERVIEW_TOGGLE,
	SF_ACTION_OVERVIEW_OPEN,
	SF_ACTION_OVERVIEW_CLOSE,
	/* Focus highlighted window + close (Enter / Space while overview). */
	SF_ACTION_OVERVIEW_CONFIRM,
	/* cmd = up|down|left|right — pan the overview filmstrip. */
	SF_ACTION_OVERVIEW_SCROLL,
	/* In-process keybind cheatsheet / splash (Umbriel-shaped). */
	SF_ACTION_CHEATSHEET_TOGGLE,
	SF_ACTION_CHEATSHEET_OPEN,
	SF_ACTION_CHEATSHEET_CLOSE,

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

/* sf.bind "Super+LMB" — pointer button + mods (Hyprland bindm-shaped). */
struct sf_mousebind {
	uint32_t mods;
	uint32_t button; /* linux BTN_* */
	enum sf_arrange arrange;
	struct sf_action_step *steps;
	size_t n_steps;
};

/* sf.bind "Super+WheelUp" — requires mods ≠ 0 (never steal bare client scroll). */
struct sf_wheelbind {
	uint32_t mods;
	enum sf_wheel_dir dir;
	enum sf_arrange arrange;
	struct sf_action_step *steps;
	size_t n_steps;
};

/* One N-tap level (count 1 = single, 2 = double, …). */
struct sf_tap_level {
	unsigned count; /* 1..SF_TAP_COUNT_MAX */
	struct sf_action_step *steps;
	size_t n_steps;
};

/* Lone-modifier tap: press+release without other keys. Gap waits for more taps. */
struct sf_tapbind {
	xkb_keysym_t sym;
	xkb_keysym_t sym_alt; /* e.g. Super_R; 0 = none */
	struct sf_tap_level *levels;
	size_t n_levels;
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

/* sf.output(name, opts). name is a case-insensitive exact match on
 * wlr_output->name (e.g. "eDP-1"). Later rules override earlier for the same
 * connector. Applied on output connect and live config reload.
 * arrange: stacking|columns|inherit (aliases master/tile/dwindle/monocle →
 * columns). UNSET/inherit leaves the output layer empty so global wins. */
struct sf_output_rule {
	char *match;
	enum sf_arrange arrange;
	/* Allow async page-flips for eligible fullscreen clients (default false). */
	bool tearing;
	bool tearing_set;
};

/* sf.gesture({fingers, axis|direction, mods?}, action). action is :workspace,
 * :columns_pan, :overview (continuous open/close), or a normal bind action /
 * chain. */
struct sf_gesture {
	int fingers; /* typically 3–4; 2 is often scroll/pinch, not swipe */
	enum sf_gesture_dir dir;
	uint32_t mods; /* exact SF_MOD_MASK match (0 = no modifiers) */
	enum sf_gesture_kind kind;
	struct sf_action_step *steps; /* KIND_ACTION only */
	size_t n_steps;
};

/* Fennel window_rule(match, actions [, parent]). Parent merge baked in.
 * app_id / title / tag are Lua patterns (string.find); empty/NULL = any.
 * tag matches xdg-toplevel-tag-v1 (match.tag or alias match.xdg_tag). */
struct sf_window_rule {
	char id[40];
	char parent_id[40];
	char *app_id;
	char *title;
	char *tag;
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
	bool opacity_set;
	float opacity;
	bool inactive_opacity_set;
	float inactive_opacity;
	bool no_dim_set;
	bool no_dim;
	/* Optional: force (true) or veto (false) tearing; unset follows client hint. */
	bool tearing_set;
	bool tearing;
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
	/* Per-op animation table (sf.opt.anim). duration_ms < 0 inherits
	 * slide_duration_ms (or morph_duration_ms for morph). */
	struct {
		bool enabled;
		struct sf_anim_op appear;
		struct sf_anim_op close;
		struct sf_anim_op minimize;
		struct sf_anim_op scratch;
		struct sf_anim_op morph;
		struct sf_anim_op workspace;
	} anim;
	float client_opacity;	/* focused / active chrome */
	float inactive_opacity; /* unfocused; default = client_opacity (no dim) */
	/* Translucent shade over unfocused windows (0 = off; e.g. 0.15 spotlight). */
	float dim_inactive;
	struct sf_rgba dim_inactive_color;
	/* Hang modal dialogs on their parent (xdg_wm_dialog_v1). Default true. */
	bool attach_modal_dialogs;
	/* Shade strength over a parent while a modal dialog is attached (0..1). */
	float dialog_dim;
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
	/* Suppress repeated Mod+Wheel actions (Umbriel cooldown_ms). Default 150. */
	int wheel_bind_cooldown_ms;

	/* Global arrange default (workspace → output → global). */
	enum sf_arrange arrange;
	/* Niri columns: column width as fraction of usable (default 0.5). */
	float column_ratio;
	/* Columns focus scroll: never|always|on_overflow (default on_overflow). */
	enum sf_center_focused center_focused;
	/* 3-finger vertical swipe distance (logical px) to change workspace. */
	int swipe_workspace_threshold;
	/* Natural: finger up moves content up → next; finger down → prev.
	 * Classic (false): finger points at the destination (down → next). */
	bool swipe_workspace_natural;
	/* Overview filmstrip scale when fully open (0.1–0.75; Umbriel default 0.5). */
	float overview_zoom;
	/* Overview wheel / discrete scroll sensitivity (default 1.0). */
	float overview_scroll_factor;

	/* Warp pointer into the newly focused window on keyboard focus cycle
	 * and workspace switch (click-to-focus does not warp). */
	bool cursor_follows_focus;

	/* Pixel step for nudge / resize_edge actions (default 25). */
	int nudge_step;

	/* SSD titlebars: never|auto|prefer_ssd|always. */
	enum sf_ssd_mode ssd_mode;
	int ssd_height;	   /* logical px; default 28 */
	int ssd_font_size; /* fcft pt; default 17 */

	/* CSD / SSD max button / foreign set_maximized → gaps|edges (default gaps). */
	enum sf_client_maximize client_maximize;
	/* Unminimize: active = carry to current WS; home = stay on hide WS. */
	enum sf_unminimize_workspace unminimize_workspace;

	/* Super+Tab tiling walk: spatial|mru|strip (default spatial). */
	enum sf_focus_cycle focus_cycle;
	/* Columns pan focus: keep|center|warp|under_pointer (default center). */
	enum sf_pan_focus pan_focus;
	/* New tiled column insert: after_focus|end|begin (default after_focus). */
	enum sf_tile_insert tile_insert;
	/* Cell-drag mid vs fringe seam (0..0.5; default 0.15). */
	float column_drop_seam;
	/* Tiled Super+LMB/RMB → arrange-swap / width_frac (default true). */
	bool tiled_grab_remap;
	/* Bare LMB on border hitband → resize (default true). */
	bool border_resize;

	/* Border/corner resize hit slop (logical px). Inner = into the window from
	 * the content/SSD edge; outer = past the drawn border. Both 0 disables. */
	int resize_border_in;  /* default 3 */
	int resize_border_out; /* default 1 */

	/* Comma-separated app_id substrings that auto-set capture_blocked. */
	char *capture_block_app_ids;

	/* Watch the config directory and reload on save (inotify). Default true. */
	bool config_watch;

	/* Show keybind cheatsheet overlay on startup. Default true. */
	bool show_cheatsheet;
	/* Cheatsheet fcft face: pt (default 18 = former 12×1.5) + family
	 * (fontconfig name; default "sans"; empty/NULL → sans). */
	int cheatsheet_font_size;
	char *cheatsheet_font;

	struct sf_keybind *keybinds;
	size_t n_keybinds;
	struct sf_mousebind *mousebinds;
	size_t n_mousebinds;
	struct sf_wheelbind *wheelbinds;
	size_t n_wheelbinds;
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
	struct sf_output_rule *output_rules;
	size_t n_output_rules;
	struct sf_gesture *gestures;
	size_t n_gestures;

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

/*
 * Live reload: re-run user init (or embedded) into a staging config. On Lua
 * failure keep the previous config (no embedded fallback). Rebinds scratchpads
 * by name, reapplies rules/input/decor, runs sf.exec {:reload true}.
 */
bool sf_config_reload(struct sf_server *server);

/*
 * Check config without a running session (stonefly validate). Prints the
 * resolved path and exact Lua error to stderr on failure. Returns 0 if ok.
 */
int sf_config_validate(void);

/* Fork sf.exec entries. is_reload false → all; true → only {:reload true}.
 * Call after WAYLAND_DISPLAY is set (same window as -s). */
void sf_config_run_execs(struct sf_server *server, bool is_reload);

const struct sf_config *sf_cfg(const struct sf_server *server);

enum sf_client_maximize sf_client_maximize_parse(const char *s);
const char *sf_client_maximize_name(enum sf_client_maximize mode);
enum sf_unminimize_workspace sf_unminimize_workspace_parse(const char *s);
const char *sf_unminimize_workspace_name(enum sf_unminimize_workspace mode);
enum sf_focus_cycle sf_focus_cycle_parse(const char *s);
const char *sf_focus_cycle_name(enum sf_focus_cycle mode);
enum sf_pan_focus sf_pan_focus_parse(const char *s);
const char *sf_pan_focus_name(enum sf_pan_focus mode);
enum sf_tile_insert sf_tile_insert_parse(const char *s);
const char *sf_tile_insert_name(enum sf_tile_insert mode);
/* center_focused parse/name live in arrange_math.h (layout math domain). */
enum sf_anim_style sf_anim_style_parse(const char *s);
const char *sf_anim_style_name(enum sf_anim_style style);
int sf_anim_op_duration_ms(const struct sf_config *cfg, const struct sf_anim_op *op,
			   int fallback_ms);

#endif /* SF_CONFIG_H */
