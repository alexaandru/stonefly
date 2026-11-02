#ifndef STONEFLY_XDG_H
#define STONEFLY_XDG_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>

#include "server/server.h"

enum stonefly_snap {
	STONEFLY_SNAP_NONE = 0,
	STONEFLY_SNAP_LEFT,
	STONEFLY_SNAP_RIGHT,
	STONEFLY_SNAP_TOP,
	STONEFLY_SNAP_BOTTOM,
};

struct wlr_scene_blur;
struct wlr_scene_rect;
struct wlr_scene_shadow;

struct stonefly_toplevel {
	enum stonefly_node_kind kind; /* STONEFLY_NODE_TOPLEVEL */
	struct wl_list link;
	struct wl_list minimize_link;
	struct stonefly_server *server;
	struct wlr_xdg_toplevel *xdg_toplevel;
	/* Wrapper: blur + shadow + border + xdg_scene_tree. */
	struct wlr_scene_tree *scene_tree;
	struct wlr_scene_tree *xdg_scene_tree;
	struct wlr_scene_blur *blur;
	struct wlr_scene_shadow *shadow;
	/* Full rect + clipped_region hole (mango). Blur stays off so the underlay
	 * is not an opaque black plate; hole lets client opacity show the desktop. */
	struct wlr_scene_rect *border;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener set_title;
	struct wl_listener set_app_id;
	struct wl_listener set_parent;

	/* Stable for the life of this toplevel; used by IPC `windows`. */
	uint64_t id;

	struct wlr_box saved_geom;
	bool has_saved_geom;
	bool maximized_edges;
	bool maximized_gaps;
	bool fullscreen;
	enum stonefly_snap snapped;
	bool minimized;
	/* Demand-attention until focused (xdg-activation). */
	bool urgent;
	/* Transient system-bell flash overrides border color. */
	bool bell_flash;
	struct wl_event_source *bell_timer;

	/* Named scratchpad id, or empty when not on a pad. */
	char scratchpad[32];

	/* Logical resting position while a slide runs (scene node is visual). */
	int resting_x;
	int resting_y;
	bool anim_hide;
	struct {
		bool active;
		struct timespec start;
		int64_t dur_ns;
		int from_x, from_y;
		int to_x, to_y;
		void (*on_done)(struct stonefly_toplevel *);
	} anim;
};

struct stonefly_popup {
	struct wlr_xdg_popup *xdg_popup;
	struct wl_listener commit;
	struct wl_listener destroy;
};

void stonefly_xdg_setup(struct stonefly_server *server);

/* Used by seat for interactive move/resize. */
void stonefly_begin_interactive(struct stonefly_toplevel *toplevel,
		enum stonefly_cursor_mode mode, uint32_t edges);

#endif
