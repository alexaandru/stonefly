#ifndef SF_SEAT_H
#define SF_SEAT_H

#include "server/server.h"

struct wlr_keyboard;
struct wlr_surface;
struct sf_toplevel;

struct sf_keyboard {
	struct wl_list link;
	struct sf_server *server;
	struct wlr_keyboard *wlr_keyboard;
	bool is_virtual;
	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

void sf_seat_setup(struct sf_server *server);
void sf_seat_finish(struct sf_server *server);

struct sf_toplevel *sf_desktop_toplevel_at(struct sf_server *server, double lx, double ly,
					   struct wlr_surface **surface, double *sx, double *sy);

void sf_reset_cursor_mode(struct sf_server *server);

/* Cancel an armed Super tap (combination chord or Super+click). */
void sf_tap_disarm(struct sf_server *server);

/* Xcursor name for resize edges (n-resize, se-resize, …). */
const char *sf_resize_xcursor_name(uint32_t edges);

/* Apply compositor face for the current MOVE/RESIZE grab (move → grabbing
 * with fallbacks; resize → edge name). Call after entering a grab and on
 * each grab motion so a prior client set_cursor surface cannot stick. */
void sf_seat_apply_grab_cursor(struct sf_server *server);

/* Warp the pointer to layout coords and refresh enter/motion.
 * No-op during MOVE/RESIZE grabs or a locked pointer constraint. */
void sf_pointer_warp_to(struct sf_server *server, double lx, double ly);

/* Warp the pointer to the center of toplevel and refresh enter/motion.
 * No-op for nil/zero-sized windows, MOVE/RESIZE grabs, or a locked constraint. */
void sf_pointer_center(struct sf_server *server, struct sf_toplevel *toplevel);

/* If sf.opt.cursor_follows_focus, center on the keyboard-focused toplevel. */
void sf_pointer_follow_focus(struct sf_server *server);

#endif
