#ifndef STONEFLY_XDG_H
#define STONEFLY_XDG_H

#include <stdbool.h>
#include <stdint.h>
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

struct stonefly_toplevel {
	struct wl_list link;
	struct wl_list minimize_link;
	struct stonefly_server *server;
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_scene_tree *scene_tree;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;

	/* Stable for the life of this toplevel; used by IPC `windows`. */
	uint64_t id;

	struct wlr_box saved_geom;
	bool has_saved_geom;
	bool maximized_edges;
	enum stonefly_snap snapped;
	bool minimized;
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
