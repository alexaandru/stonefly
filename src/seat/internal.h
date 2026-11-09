#ifndef SF_SEAT_INTERNAL_H
#define SF_SEAT_INTERNAL_H

#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_pointer.h>

#include "server/server.h"

struct sf_pointer {
	struct wl_list link;
	struct sf_server *server;
	struct wlr_pointer *wlr_pointer;
	struct wl_listener swipe_begin;
	struct wl_listener swipe_update;
	struct wl_listener swipe_end;
	struct wl_listener pinch_begin;
	struct wl_listener pinch_update;
	struct wl_listener pinch_end;
	struct wl_listener hold_begin;
	struct wl_listener hold_end;
	struct wl_listener destroy;
};

struct sf_touch {
	struct wl_list link;
	struct sf_server *server;
	struct wl_listener destroy;
};

/* seat.c */
void seat_update_capabilities(struct sf_server *server);

/* keyboard.c */
void tap_clear_timer(struct sf_server *server);
void server_new_keyboard(struct sf_server *server, struct wlr_input_device *device,
			 bool is_virtual);
void server_new_virtual_keyboard(struct wl_listener *listener, void *data);

/* gestures.c */
void destroy_pointer(struct sf_pointer *pointer);
void server_new_pointer(struct sf_server *server, struct wlr_input_device *device);

/* pointer.c */
void destroy_touch(struct sf_touch *touch);
void server_new_touch(struct sf_server *server, struct wlr_input_device *device);
uint32_t cursor_size_for_server(struct sf_server *server);
char *cursor_theme_dup(struct sf_server *server);
void seat_request_cursor(struct wl_listener *listener, void *data);
void seat_request_set_cursor_shape(struct wl_listener *listener, void *data);
void server_cursor_motion(struct wl_listener *listener, void *data);
void server_cursor_motion_absolute(struct wl_listener *listener, void *data);
void server_cursor_button(struct wl_listener *listener, void *data);
void server_cursor_axis(struct wl_listener *listener, void *data);
void server_cursor_frame(struct wl_listener *listener, void *data);
void server_cursor_touch_down(struct wl_listener *listener, void *data);
void server_cursor_touch_up(struct wl_listener *listener, void *data);
void server_cursor_touch_motion(struct wl_listener *listener, void *data);
void server_cursor_touch_cancel(struct wl_listener *listener, void *data);
void server_cursor_touch_frame(struct wl_listener *listener, void *data);

/* constraints.c */
void apply_pointer_constraint(struct sf_server *server, double *dx, double *dy);
void server_new_pointer_constraint(struct wl_listener *listener, void *data);
void seat_pointer_focus_change(struct wl_listener *listener, void *data);

#endif
