#ifndef STONEFLY_SEAT_H
#define STONEFLY_SEAT_H

#include "server/server.h"

struct wlr_keyboard;
struct wlr_surface;
struct stonefly_toplevel;

struct stonefly_keyboard {
	struct wl_list link;
	struct stonefly_server *server;
	struct wlr_keyboard *wlr_keyboard;
	bool is_virtual;
	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

void stonefly_seat_setup(struct stonefly_server *server);
void stonefly_seat_finish(struct stonefly_server *server);

struct stonefly_toplevel *stonefly_desktop_toplevel_at(struct stonefly_server *server, double lx,
						       double ly, struct wlr_surface **surface,
						       double *sx, double *sy);

void stonefly_reset_cursor_mode(struct stonefly_server *server);

/* Cancel an armed Super tap (combination chord or Super+click). */
void stonefly_tap_disarm(struct stonefly_server *server);

/* Xcursor name for resize edges (n-resize, se-resize, …). */
const char *stonefly_resize_xcursor_name(uint32_t edges);

/* Apply compositor face for the current MOVE/RESIZE grab (move → grabbing
 * with fallbacks; resize → edge name). Call after entering a grab and on
 * each grab motion so a prior client set_cursor surface cannot stick. */
void stonefly_seat_apply_grab_cursor(struct stonefly_server *server);

#endif
