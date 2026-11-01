#ifndef STONEFLY_SERVER_H
#define STONEFLY_SERVER_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/util/box.h>

struct wlr_allocator;
struct wlr_backend;
struct wlr_cursor;
struct wlr_output_layout;
struct wlr_renderer;
struct wlr_scene;
struct wlr_scene_output_layout;
struct wlr_seat;
struct wlr_xcursor_manager;
struct wlr_xdg_shell;

struct stonefly_toplevel;
struct stonefly_output;
struct stonefly_keyboard;
struct stonefly_ipc;

enum stonefly_cursor_mode {
	STONEFLY_CURSOR_PASSTHROUGH,
	STONEFLY_CURSOR_MOVE,
	STONEFLY_CURSOR_RESIZE,
};

struct stonefly_server {
	struct wl_display *wl_display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;
	struct wl_list toplevels;

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;

	struct wlr_seat *seat;
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener pointer_focus_change;
	struct wl_listener request_set_selection;
	struct wl_list keyboards;
	enum stonefly_cursor_mode cursor_mode;
	struct stonefly_toplevel *grabbed_toplevel;
	double grab_x, grab_y;
	struct wlr_box grab_geobox;
	uint32_t resize_edges;

	struct wlr_output_layout *output_layout;
	struct wl_list outputs;
	struct wl_listener new_output;

	struct wl_list minimized;

	uint64_t next_toplevel_id;

	struct stonefly_ipc *ipc;
};

bool stonefly_server_init(struct stonefly_server *server);
int stonefly_server_run(struct stonefly_server *server, const char *startup_cmd);
void stonefly_server_finish(struct stonefly_server *server);

#endif
