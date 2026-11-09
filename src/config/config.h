/* Runtime config: C defaults + LuaJIT/Fennel overlays (MATRIX §10). */
#ifndef STONEFLY_CONFIG_H
#define STONEFLY_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>
#include <wlr/types/wlr_keyboard.h>

struct stonefly_server;

/* Interesting modifiers for bind matching (ignore Caps etc.). */
#define STONEFLY_MOD_MASK                                                                          \
	((uint32_t)WLR_MODIFIER_SHIFT | (uint32_t)WLR_MODIFIER_CTRL | (uint32_t)WLR_MODIFIER_ALT | \
	 (uint32_t)WLR_MODIFIER_LOGO)

#define STONEFLY_SUPER ((uint32_t)WLR_MODIFIER_LOGO)
#define STONEFLY_ALT ((uint32_t)WLR_MODIFIER_ALT)
#define STONEFLY_CTRL ((uint32_t)WLR_MODIFIER_CTRL)
#define STONEFLY_SHIFT ((uint32_t)WLR_MODIFIER_SHIFT)

#define STONEFLY_SCRATCHPAD_MAX 8
#define STONEFLY_WINDOW_RULE_MAX 64

enum stonefly_action {
	STONEFLY_ACTION_NONE = 0,
	STONEFLY_ACTION_QUIT,
	STONEFLY_ACTION_CLOSE,
	STONEFLY_ACTION_FOCUS_NEXT,
	STONEFLY_ACTION_FOCUS_PREV,
	STONEFLY_ACTION_FOCUS_NEXT_ALL,
	STONEFLY_ACTION_FOCUS_PREV_ALL,
	STONEFLY_ACTION_TOGGLE_MAX_EDGES,
	STONEFLY_ACTION_TOGGLE_MAX_GAPS,
	STONEFLY_ACTION_MINIMIZE,
	STONEFLY_ACTION_UNMINIMIZE,
	STONEFLY_ACTION_SNAP_LEFT,
	STONEFLY_ACTION_SNAP_RIGHT,
	STONEFLY_ACTION_SNAP_TOP,
	STONEFLY_ACTION_SNAP_BOTTOM,
	STONEFLY_ACTION_UNSNAP,
	STONEFLY_ACTION_SPAWN_FOOT,
	STONEFLY_ACTION_SPAWN_NAUTILUS,
	STONEFLY_ACTION_SPAWN_GHOSTTY,
	STONEFLY_ACTION_SPAWN_GHOSTTY_QT,
	STONEFLY_ACTION_SPAWN_EDITOR,
	STONEFLY_ACTION_SPAWN_LAUNCHER,
	STONEFLY_ACTION_SCRATCH_TOGGLE_TERM,
	STONEFLY_ACTION_SCRATCH_CLAIM_TERM,
	/* Run keybind.cmd via /bin/sh -c (noctalia msg, etc.). */
	STONEFLY_ACTION_EXEC,
	/* Named scratchpad; cmd holds pad name (default "term"). */
	STONEFLY_ACTION_SCRATCH_TOGGLE,
	STONEFLY_ACTION_SCRATCH_CLAIM,
	/* Dynamic workspaces (cmd holds 1-based index for switch/move). */
	STONEFLY_ACTION_WORKSPACE_NEXT,
	STONEFLY_ACTION_WORKSPACE_PREV,
	STONEFLY_ACTION_WORKSPACE_SWITCH,
	STONEFLY_ACTION_MOVE_TO_WORKSPACE_NEXT,
	STONEFLY_ACTION_MOVE_TO_WORKSPACE_PREV,
	STONEFLY_ACTION_MOVE_TO_WORKSPACE,
};

struct stonefly_keybind {
	uint32_t mods; /* compared against (modifiers & STONEFLY_MOD_MASK) */
	xkb_keysym_t sym;
	xkb_keysym_t sym_alt; /* optional second sym (e.g. ISO_Left_Tab); 0 = none */
	enum stonefly_action action;
	char *cmd; /* EXEC / scratch name; owned when on heap config */
};

/* Lone-modifier tap: press+release without other keys. Double waits the gap. */
struct stonefly_tapbind {
	xkb_keysym_t sym;
	xkb_keysym_t sym_alt; /* e.g. Super_R; 0 = none */
	enum stonefly_action single;
	enum stonefly_action double_tap; /* NONE = single-only */
	char *single_cmd;
	char *double_cmd;
};

struct stonefly_scratchpad_spec {
	char *name;
	char *cmd;
	char *app_id; /* match for spawn claim; empty = first map while pending */
	/* Hide when focus moves to another window on the same output (peeble). */
	bool auto_hide;
};

/* Fennel window_rule(match, actions [, parent]). Parent merge baked in.
 * app_id / title are Lua patterns (string.find); empty/NULL = any. */
struct stonefly_window_rule {
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
	bool ssd_set;
	int ssd_mode; /* enum stonefly_ssd_mode when ssd_set */
	bool width_set;
	bool height_set;
	int width;
	int height;
};

struct stonefly_config {
	float output_scale;
	int cursor_size_default;
	int gap_size;
	int cascade_step;
	int cascade_wrap;
	int slide_duration_ms;
	int slide_dir_closest;
	int morph_duration_ms;
	float client_opacity;	/* focused / active chrome */
	float inactive_opacity; /* unfocused; default = client_opacity (no dim) */
	int corner_radius;
	int border_thickness;
	float shadow_blur_sigma;
	float border_active[4];
	float border_inactive[4];
	float border_urgent[4];
	float shadow_color[4];
	int window_blur; /* per-window SceneFX blur behind chrome */
	/* true → blur wallpaper/optimized underlay only (mango xray);
	 * false → blur whatever is actually behind the window (other clients). */
	int blur_xray;
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

	/* Global layout default (workspace → output → global). */
	int layout; /* enum stonefly_layout; STACKING today */
	/* 3-finger vertical swipe distance (logical px) to change workspace. */
	int swipe_workspace_threshold;
	/* Natural: finger up moves content up → next; finger down → prev.
	 * Classic (false): finger points at the destination (down → next). */
	int swipe_workspace_natural;

	/* SSD titlebars: never|auto|prefer_ssd|always (enum stonefly_ssd_mode). */
	int ssd_mode;
	int ssd_height;	   /* logical px; default 28 */
	int ssd_font_size; /* fcft pt; default 17 */

	/* Border/corner resize hit slop (logical px). Inner = into the window from
	 * the content/SSD edge; outer = past the drawn border. Both 0 disables. */
	int resize_border_in;  /* default 3 */
	int resize_border_out; /* default 1 */

	char *cmd_foot;
	char *cmd_nautilus;
	char *cmd_ghostty;
	char *cmd_ghostty_qt;
	char *cmd_editor;
	char *cmd_launcher;

	/* Comma-separated app_id substrings that auto-set capture_blocked. */
	char *capture_block_app_ids;

	struct stonefly_keybind *keybinds;
	size_t n_keybinds;
	struct stonefly_tapbind *tapbinds;
	size_t n_tapbinds;
	struct stonefly_scratchpad_spec *scratchpads;
	size_t n_scratchpads;
	struct stonefly_window_rule *window_rules;
	size_t n_window_rules;

	/* LuaJIT state; NULL when STONEFLY_NOCONFIG or before load. */
	void *lua; /* lua_State * — opaque here to avoid forcing luajit.h on all TUs */
};

void stonefly_config_set_defaults(struct stonefly_config *cfg);
void stonefly_config_finish(struct stonefly_config *cfg);

/* XDG paths — honor env; never hardcode ~/.config. Return 0 on success. */
int stonefly_config_dir(char *buf, size_t n);
int stonefly_data_dir(char *buf, size_t n);
int stonefly_cache_dir(char *buf, size_t n);
int stonefly_config_init_path(char *buf, size_t n);

/* Recursive mkdir (mkdir -p); 0 = ok. */
int stonefly_mkdir_p(const char *path);

/* Write embedded default tree; refuse if init.lua exists. 0 = ok. */
int stonefly_config_write_default(void);

/* Load user init.lua or embedded Fennel-first default into server->config. */
bool stonefly_config_load(struct stonefly_server *server);

const struct stonefly_config *stonefly_cfg(const struct stonefly_server *server);

#endif /* STONEFLY_CONFIG_H */
