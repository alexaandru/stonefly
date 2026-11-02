/* All day-zero knobs in one place — migrate to a real config later. */
#ifndef STONEFLY_HARDCODED_H
#define STONEFLY_HARDCODED_H

#include <stddef.h>
#include <xkbcommon/xkbcommon.h>
#include <wlr/types/wlr_keyboard.h>

/* Output / HiDPI — DRM seats only; nested/headless use 1.0 via
 * stonefly_output_scale_factor() (parent compositor already scaled). */
#define STONEFLY_OUTPUT_SCALE 1.5f

/* Compositor xcursor size when XCURSOR_SIZE is unset (peeble default). */
#define STONEFLY_CURSOR_SIZE_DEFAULT 24

/* Gaps between windows and the output edge (peeble gaps(15)). */
#define STONEFLY_GAP_SIZE 15

/* Cascade placement: offset between consecutive new windows (peeble). */
#define STONEFLY_CASCADE_STEP 25
#define STONEFLY_CASCADE_WRAP 10

/*
 * Presentation slide (peeble animation("appear") / window_effect show-hide).
 * Duration 0 disables. Closest = nearest usable edge (peeble dir "closest").
 */
#define STONEFLY_SLIDE_DURATION_MS 200
#define STONEFLY_SLIDE_DIR_CLOSEST 1

/* SceneFX eye candy (FORK.md P3). RGBA floats 0..1. */
#define STONEFLY_CLIENT_OPACITY 0.96f
#define STONEFLY_CORNER_RADIUS 12
#define STONEFLY_BORDER_THICKNESS 3
#define STONEFLY_SHADOW_BLUR_SIGMA 20.0f
/*
 * Neon blue (active) / dim charcoal (inactive) / amber (urgent + bell).
 * Inactive matches niri-ish #505050 — old silver (0.75) read as “still on”.
 * Colors are straight RGBA; decor premultiplies for SceneFX.
 */
/* Static storage — not compound literals. Pointers to ((float[4]){…})
 * die at the end of the enclosing block (ASan stack-use-after-scope in
 * stonefly_decor_set_focused → border_rgba_premul on first map/focus). */
static const float STONEFLY_BORDER_ACTIVE[4] = { 0.10f, 0.75f, 1.00f, 1.00f };
static const float STONEFLY_BORDER_INACTIVE[4] = { 0.22f, 0.22f, 0.24f, 1.00f };
static const float STONEFLY_BORDER_URGENT[4] = { 1.00f, 0.45f, 0.10f, 1.00f };
static const float STONEFLY_SHADOW_COLOR[4] = { 0.0f, 0.0f, 0.0f, 0.55f };
/*
 * Per-window SceneFX blur plate under the client. Off for day-zero: a failed
 * / empty optimized-blur sample paints an opaque black rect and kills window
 * opacity (niri FAQ: border-as-background shows through translucent clients).
 * Mango keeps blur; re-enable once wallpaper blur looks right.
 */
#define STONEFLY_WINDOW_BLUR 0
/* Visual system-bell flash duration (FORK.md #1389). */
#define STONEFLY_BELL_FLASH_MS 200

/*
 * Drag-snap (peeble snap_threshold / snap_preview_border_size).
 * Stick + release-to-snap within threshold of usable±gap; push-through
 * is 4× threshold (seat/op/move.go).
 */
#define STONEFLY_SNAP_THRESHOLD 26
#define STONEFLY_SNAP_PREVIEW_BORDER 1
#define STONEFLY_SNAP_PREVIEW_OPACITY 0.40f

/* Super tap: max hold for a tap; gap before single fires (peeble seat). */
#define STONEFLY_TAP_MAX_HOLD_MS 500
#define STONEFLY_DOUBLE_TAP_GAP_MS 300

/* Interesting modifiers for bind matching (ignore Caps etc.). */
#define STONEFLY_MOD_MASK \
	(WLR_MODIFIER_SHIFT | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO)

#define STONEFLY_SUPER WLR_MODIFIER_LOGO
#define STONEFLY_ALT   WLR_MODIFIER_ALT
#define STONEFLY_CTRL  WLR_MODIFIER_CTRL
#define STONEFLY_SHIFT WLR_MODIFIER_SHIFT

/* Spawn commands (peeble default.risor spawn{} subset). */
#define STONEFLY_CMD_FOOT       "foot"
#define STONEFLY_CMD_NAUTILUS   "nautilus"
#define STONEFLY_CMD_GHOSTTY    "ghostty +new-window"
#define STONEFLY_CMD_GHOSTTY_QT "ghostty +toggle-quick-terminal"
#define STONEFLY_CMD_EDITOR     "gtk-launch org.gnome.TextEditor"
#define STONEFLY_CMD_LAUNCHER   "noctalia msg panel-toggle launcher"

/* Noctalia IPC (noctalia msg --help). */
#define STONEFLY_CMD_SHOT_FULL   "noctalia msg screenshot-fullscreen"
#define STONEFLY_CMD_SHOT_REGION "noctalia msg screenshot-region"
#define STONEFLY_CMD_SHOT_WINDOW "noctalia msg screenshot-window"
#define STONEFLY_CMD_VOL_UP      "noctalia msg volume-up"
#define STONEFLY_CMD_VOL_DOWN    "noctalia msg volume-down"
#define STONEFLY_CMD_VOL_MUTE    "noctalia msg volume-mute"
#define STONEFLY_CMD_MIC_MUTE    "noctalia msg mic-mute"
#define STONEFLY_CMD_MIC_UP      "noctalia msg mic-volume-up"
#define STONEFLY_CMD_MIC_DOWN    "noctalia msg mic-volume-down"
#define STONEFLY_CMD_BRIGHT_UP   "noctalia msg brightness-up"
#define STONEFLY_CMD_BRIGHT_DOWN "noctalia msg brightness-down"
#define STONEFLY_CMD_KBD_UP      "noctalia msg keyboard-backlight-up"
#define STONEFLY_CMD_KBD_DOWN    "noctalia msg keyboard-backlight-down"
#define STONEFLY_CMD_MEDIA_TOGGLE "noctalia msg media toggle"
#define STONEFLY_CMD_MEDIA_NEXT   "noctalia msg media next"
#define STONEFLY_CMD_MEDIA_PREV   "noctalia msg media previous"

/* Named scratchpad "term" — distinct class so Super+w windows are not claimed. */
#define STONEFLY_CMD_SCRATCH_TERM \
	"ghostty --class=stonefly.scratch.term"
#define STONEFLY_SCRATCH_TERM_APP_ID "stonefly.scratch.term"

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
};

struct stonefly_keybind {
	uint32_t mods; /* compared against (modifiers & STONEFLY_MOD_MASK) */
	xkb_keysym_t sym;
	xkb_keysym_t sym_alt; /* optional second sym (e.g. ISO_Left_Tab); 0 = none */
	enum stonefly_action action;
	const char *cmd; /* STONEFLY_ACTION_EXEC only; NULL otherwise */
};

/* Lone-modifier tap: press+release without other keys. Double waits the gap. */
struct stonefly_tapbind {
	xkb_keysym_t sym;
	xkb_keysym_t sym_alt; /* e.g. Super_R; 0 = none */
	enum stonefly_action single;
	enum stonefly_action double_tap; /* NONE = single-only */
};

struct stonefly_scratchpad_spec {
	const char *name;
	const char *cmd;
	const char *app_id; /* match for spawn claim; empty = first map while pending */
};

/* Peeble window/spawn subset that stonefly supports today. */
static const struct stonefly_keybind stonefly_keybinds[] = {
	{ STONEFLY_SUPER, XKB_KEY_Escape, 0, STONEFLY_ACTION_QUIT, NULL },
	{ STONEFLY_SUPER | STONEFLY_SHIFT, XKB_KEY_Q, 0, STONEFLY_ACTION_QUIT, NULL },
	{ STONEFLY_SUPER, XKB_KEY_q, 0, STONEFLY_ACTION_CLOSE, NULL },

	{ STONEFLY_SUPER, XKB_KEY_Tab, 0, STONEFLY_ACTION_FOCUS_NEXT, NULL },
	{ STONEFLY_SUPER | STONEFLY_SHIFT, XKB_KEY_Tab, XKB_KEY_ISO_Left_Tab,
		STONEFLY_ACTION_FOCUS_PREV, NULL },

	{ STONEFLY_ALT, XKB_KEY_Tab, 0, STONEFLY_ACTION_FOCUS_NEXT_ALL, NULL },
	{ STONEFLY_ALT | STONEFLY_SHIFT, XKB_KEY_Tab, XKB_KEY_ISO_Left_Tab,
		STONEFLY_ACTION_FOCUS_PREV_ALL, NULL },

	{ STONEFLY_SUPER, XKB_KEY_f, 0, STONEFLY_ACTION_TOGGLE_MAX_EDGES, NULL },
	{ STONEFLY_SUPER, XKB_KEY_m, 0, STONEFLY_ACTION_TOGGLE_MAX_GAPS, NULL },

	{ STONEFLY_CTRL, XKB_KEY_Escape, 0, STONEFLY_ACTION_MINIMIZE, NULL },
	{ STONEFLY_CTRL | STONEFLY_SHIFT, XKB_KEY_Escape, 0, STONEFLY_ACTION_UNMINIMIZE, NULL },

	{ STONEFLY_SUPER, XKB_KEY_Left, 0, STONEFLY_ACTION_SNAP_LEFT, NULL },
	{ STONEFLY_SUPER, XKB_KEY_Right, 0, STONEFLY_ACTION_SNAP_RIGHT, NULL },
	{ STONEFLY_SUPER, XKB_KEY_Up, 0, STONEFLY_ACTION_SNAP_TOP, NULL },
	{ STONEFLY_SUPER, XKB_KEY_Down, 0, STONEFLY_ACTION_SNAP_BOTTOM, NULL },
	{ STONEFLY_SUPER, XKB_KEY_u, 0, STONEFLY_ACTION_UNSNAP, NULL },

	{ STONEFLY_SUPER, XKB_KEY_space, 0, STONEFLY_ACTION_SPAWN_FOOT, NULL },
	{ STONEFLY_SUPER, XKB_KEY_z, 0, STONEFLY_ACTION_SPAWN_NAUTILUS, NULL },
	{ STONEFLY_SUPER, XKB_KEY_w, 0, STONEFLY_ACTION_SPAWN_GHOSTTY, NULL },
	{ STONEFLY_ALT, XKB_KEY_Return, 0, STONEFLY_ACTION_SPAWN_GHOSTTY_QT, NULL },
	{ STONEFLY_SUPER, XKB_KEY_e, 0, STONEFLY_ACTION_SPAWN_EDITOR, NULL },

	/* Promote focused window onto the quick-terminal scratchpad (peeble Super+t). */
	{ STONEFLY_SUPER, XKB_KEY_t, 0, STONEFLY_ACTION_SCRATCH_CLAIM_TERM, NULL },

	/* Screenshots → noctalia (peeble Print / Shift+Print / Ctrl+Shift+Print). */
	{ 0, XKB_KEY_Print, 0, STONEFLY_ACTION_EXEC, STONEFLY_CMD_SHOT_FULL },
	{ STONEFLY_SHIFT, XKB_KEY_Print, 0, STONEFLY_ACTION_EXEC, STONEFLY_CMD_SHOT_REGION },
	{ STONEFLY_CTRL | STONEFLY_SHIFT, XKB_KEY_Print, 0, STONEFLY_ACTION_EXEC,
		STONEFLY_CMD_SHOT_WINDOW },

	/* Media / brightness / mic (peeble XF86* binds → noctalia msg). */
	{ 0, XKB_KEY_XF86AudioRaiseVolume, 0, STONEFLY_ACTION_EXEC, STONEFLY_CMD_VOL_UP },
	{ 0, XKB_KEY_XF86AudioLowerVolume, 0, STONEFLY_ACTION_EXEC, STONEFLY_CMD_VOL_DOWN },
	{ 0, XKB_KEY_XF86AudioMute, 0, STONEFLY_ACTION_EXEC, STONEFLY_CMD_VOL_MUTE },
	{ 0, XKB_KEY_XF86AudioMicMute, 0, STONEFLY_ACTION_EXEC, STONEFLY_CMD_MIC_MUTE },
	{ 0, XKB_KEY_XF86Launch5, 0, STONEFLY_ACTION_EXEC, STONEFLY_CMD_MIC_UP },
	{ STONEFLY_SHIFT, XKB_KEY_XF86Launch5, 0, STONEFLY_ACTION_EXEC, STONEFLY_CMD_MIC_DOWN },
	{ 0, XKB_KEY_XF86MonBrightnessUp, 0, STONEFLY_ACTION_EXEC, STONEFLY_CMD_BRIGHT_UP },
	{ 0, XKB_KEY_XF86MonBrightnessDown, 0, STONEFLY_ACTION_EXEC, STONEFLY_CMD_BRIGHT_DOWN },
	{ 0, XKB_KEY_XF86KbdBrightnessUp, 0, STONEFLY_ACTION_EXEC, STONEFLY_CMD_KBD_UP },
	{ 0, XKB_KEY_XF86KbdBrightnessDown, 0, STONEFLY_ACTION_EXEC, STONEFLY_CMD_KBD_DOWN },
	{ 0, XKB_KEY_XF86AudioPlay, XKB_KEY_XF86AudioPause, STONEFLY_ACTION_EXEC,
		STONEFLY_CMD_MEDIA_TOGGLE },
	{ 0, XKB_KEY_XF86AudioNext, 0, STONEFLY_ACTION_EXEC, STONEFLY_CMD_MEDIA_NEXT },
	{ 0, XKB_KEY_XF86AudioPrev, 0, STONEFLY_ACTION_EXEC, STONEFLY_CMD_MEDIA_PREV },
};

/*
 * Super tap → noctalia launcher; Super double-tap → named scratchpad "term".
 * Super still works in Super+key chords and Super+click move/resize.
 */
static const struct stonefly_tapbind stonefly_tapbinds[] = {
	{ XKB_KEY_Super_L, XKB_KEY_Super_R,
		STONEFLY_ACTION_SPAWN_LAUNCHER,
		STONEFLY_ACTION_SCRATCH_TOGGLE_TERM },
};

static const struct stonefly_scratchpad_spec stonefly_scratchpads[] = {
	{ "term", STONEFLY_CMD_SCRATCH_TERM, STONEFLY_SCRATCH_TERM_APP_ID },
};

static inline size_t stonefly_scratchpad_count(void) {
	return sizeof(stonefly_scratchpads) / sizeof(stonefly_scratchpads[0]);
}

#endif /* STONEFLY_HARDCODED_H */
