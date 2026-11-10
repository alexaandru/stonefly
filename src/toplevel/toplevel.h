#ifndef SF_TOPLEVEL_H
#define SF_TOPLEVEL_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/util/box.h>

#include "server/server.h"
#include "ssd/ssd_policy.h"
#include "wm/layout_sm.h"
#include "wm/place_sm.h"

struct wlr_buffer;
struct wlr_scene_blur;
struct wlr_scene_rect;
struct wlr_scene_shadow;
struct wlr_scene;
struct wlr_ext_foreign_toplevel_handle_v1;
struct wlr_ext_image_capture_source_v1;
struct wlr_xdg_toplevel;
struct wlr_xdg_toplevel_decoration_v1;
struct wlr_xdg_toplevel_icon_v1;
struct sf_workspace;
struct sf_ssd;

struct sf_toplevel {
	enum sf_node_kind kind; /* SF_NODE_TOPLEVEL */
	struct wl_list link;
	struct wl_list minimize_link;
	struct sf_server *server;
	struct wlr_xdg_toplevel *xdg_toplevel;
	/* Wrapper: blur + shadow + border + xdg_scene_tree. */
	struct wlr_scene_tree *scene_tree;
	struct wlr_scene_tree *xdg_scene_tree;
	struct wlr_scene_blur *blur;
	struct wlr_scene_shadow *shadow;
	/* Full rect + clipped_region hole (mango). Blur stays off so the underlay
	 * is not an opaque black plate; hole lets client opacity show the desktop. */
	struct wlr_scene_rect *border;

	/* Off-screen duplicate tree for ext-image-capture (Noctalia switcher). */
	struct wlr_ext_foreign_toplevel_handle_v1 *ext_handle;
	struct wlr_scene *image_capture_scene;
	struct wlr_scene_tree *image_capture_tree;
	struct wlr_ext_image_capture_source_v1 *image_capture_source;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_minimize;
	struct wl_listener request_fullscreen;
	struct wl_listener set_title;
	struct wl_listener set_app_id;
	struct wl_listener set_parent;

	/* Stable for the life of this toplevel; used by IPC `windows`. */
	uint64_t id;

	/* Exclusive float/max/snap/fullscreen + PreMax geom (wm/layout_sm). */
	struct sf_layout_sm layout;
	/* Map / SoftPlace / appear-hold (wm/place_sm). */
	struct sf_place_sm place;
	/* Workspace arrange engine owns this window's box. */
	bool arrange_managed;
	/* After break-out: suppress GEOM regain until box leaves hold. */
	bool geom_regain_hold;
	struct wlr_box geom_regain_box;
	bool minimized;
	/* xdg_dialog_v1.set_modal (requires parent). */
	bool modal;
	/* window_rule borderless: no WM border/shadow. */
	bool borderless;
	/* Sticky: stay above normal windows in the same scene tree. */
	bool always_on_top;
	/* window_rule ssd override; when rule_ssd_set, use rule_ssd_mode. */
	bool rule_ssd_set;
	enum sf_ssd_mode rule_ssd_mode;
	/* Demand-attention until focused (xdg-activation). */
	bool urgent;
	/* Block ext-image-capture source accept (IPC / config #1481). */
	bool capture_blocked;
	/* Transient system-bell flash overrides border color. */
	bool bell_flash;
	struct wl_event_source *bell_timer;

	/* Named scratchpad id, or empty when not on a pad. */
	char scratchpad[32];

	/* Per-output workspace membership; NULL for scratchpads. */
	struct sf_workspace *workspace;

	/* xdg-toplevel-tag-v1 (client-set; heap, may be NULL). */
	char *tag;
	char *description;

	/* xdg-toplevel-icon-v1 (ref'd icon + cached name for IPC). */
	struct wlr_xdg_toplevel_icon_v1 *icon;
	char *icon_name;

	/* ext-background-effect-v1: non-empty blur region requested. */
	bool client_blur;

	/* xdg-decoration: client preference + compositor SSD titlebar. */
	bool want_ssd;
	/* Last mode we passed to set_mode (avoid configure storms). */
	int ssd_set_mode;
	struct wlr_xdg_toplevel_decoration_v1 *decoration;
	struct sf_ssd *ssd;

	/* Last good client buffer for Ctrl+W / CSD close-ghost (NULL-buffer unmap). */
	struct wlr_buffer *close_snap;
	int close_snap_w;
	int close_snap_h;

	/* Logical resting position while a slide/morph runs (scene node is visual). */
	int resting_x;
	int resting_y;
	bool anim_hide;
	struct {
		bool active;
		bool morph; /* false = pos-only slide; true = live peeble-style morph */
		struct timespec start;
		int64_t dur_ns;
		int from_x, from_y;
		int to_x, to_y;
		int from_w, from_h;
		int to_w, to_h;
		/* Last size proposed this morph (skip redundant set_size). */
		int prop_w, prop_h;
		void (*on_done)(struct sf_toplevel *);
	} anim;
};

#endif
