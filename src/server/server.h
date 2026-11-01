#ifndef STONEFLY_SERVER_H
#define STONEFLY_SERVER_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/util/box.h>

struct wlr_allocator;
struct wlr_backend;
struct wlr_cursor;
struct wlr_cursor_shape_manager_v1;
struct wlr_layer_shell_v1;
struct wlr_output_layout;
struct wlr_renderer;
struct wlr_scene;
struct wlr_scene_optimized_blur;
struct wlr_scene_output_layout;
struct wlr_scene_tree;
struct wlr_seat;
struct wlr_xcursor_manager;
struct wlr_xdg_activation_v1;
struct wlr_xdg_shell;
struct wlr_xdg_system_bell_v1;

struct stonefly_toplevel;
struct stonefly_output;
struct stonefly_keyboard;
struct stonefly_ipc;
struct stonefly_layer_surface;
struct stonefly_snap_preview;

#include "wm/drag_snap.h"

enum stonefly_cursor_mode {
	STONEFLY_CURSOR_PASSTHROUGH,
	STONEFLY_CURSOR_MOVE,
	STONEFLY_CURSOR_RESIZE,
};

/* Discriminator for scene node->data (toplevel vs layer). First field. */
enum stonefly_node_kind {
	STONEFLY_NODE_TOPLEVEL = 1,
	STONEFLY_NODE_LAYER = 2,
};

struct stonefly_server {
	struct wl_display *wl_display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;

	/* Z-order: background < bottom < blur < toplevel < top < overlay. */
	struct {
		struct wlr_scene_tree *background;
		struct wlr_scene_tree *bottom;
		struct wlr_scene_tree *toplevel;
		struct wlr_scene_tree *top;
		struct wlr_scene_tree *overlay;
	} layers;
	struct wlr_scene_optimized_blur *blur_layer;

	/* Client-close ghosts sliding out after surface destroy. */
	struct wl_list ghosts;

	struct wlr_layer_shell_v1 *layer_shell;
	struct wl_listener new_layer_surface;
	struct stonefly_layer_surface *exclusive_layer;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;
	struct wl_list toplevels;

	/* FORK.md #1281 / #1389 — demand-attention + system bell. */
	struct wlr_xdg_activation_v1 *xdg_activation;
	struct wl_listener request_activate;
	struct wlr_xdg_system_bell_v1 *system_bell;
	struct wl_listener system_bell_ring;

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wlr_cursor_shape_manager_v1 *cursor_shape_mgr;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;

	struct wlr_seat *seat;
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener request_set_cursor_shape;
	struct wl_listener pointer_focus_change;
	struct wl_listener request_set_selection;
	struct wl_list keyboards;
	enum stonefly_cursor_mode cursor_mode;
	struct stonefly_toplevel *grabbed_toplevel;
	double grab_x, grab_y;
	struct wlr_box grab_geobox;
	uint32_t resize_edges;
	struct stonefly_drag_snap drag_snap;
	struct stonefly_snap_preview *snap_preview;

	struct wlr_output_layout *output_layout;
	struct wl_list outputs;
	struct wl_listener new_output;

	struct wl_list minimized;

	uint64_t next_toplevel_id;

	struct stonefly_ipc *ipc;

	/* Lone-modifier tap (Super): peeble bind_tap semantics.
	 * single/double_tap hold stonefly_action values from hardcoded.h. */
	struct {
		bool armed;
		struct timespec since;
		int taps;
		struct wl_event_source *timer;
		int single;
		int double_tap;
	} tap;
};

bool stonefly_server_init(struct stonefly_server *server);
int stonefly_server_run(struct stonefly_server *server, const char *startup_cmd);
void stonefly_server_finish(struct stonefly_server *server);

#endif
