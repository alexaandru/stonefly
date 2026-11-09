#include "seat/seat.h"
#include "seat/internal.h"

#include <stdlib.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/region.h>

struct stonefly_constraint {
	struct stonefly_server *server;
	struct wlr_pointer_constraint_v1 *constraint;
	struct wl_listener destroy;
};

static void warp_to_constraint_hint(struct stonefly_server *server,
				    struct wlr_pointer_constraint_v1 *constraint) {
	if (constraint == NULL) {
		return;
	}
	if (!(constraint->current.committed &
	      (uint32_t)WLR_POINTER_CONSTRAINT_V1_STATE_CURSOR_HINT)) {
		return;
	}
	double sx = constraint->current.cursor_hint.x;
	double sy = constraint->current.cursor_hint.y;
	/* Surface origin from last pointer focus coords on this surface. */
	double ox = server->cursor->x - server->seat->pointer_state.sx;
	double oy = server->cursor->y - server->seat->pointer_state.sy;
	wlr_cursor_warp(server->cursor, NULL, ox + sx, oy + sy);
	wlr_seat_pointer_warp(server->seat, sx, sy);
}

static void stonefly_constraint_deactivate(struct stonefly_server *server, bool warp_hint) {
	if (server->constraints.current == NULL) {
		return;
	}
	if (warp_hint) {
		warp_to_constraint_hint(server, server->constraints.current);
	}
	wlr_pointer_constraint_v1_send_deactivated(server->constraints.current);
	server->constraints.current = NULL;
}

static void stonefly_constraint_activate(struct stonefly_server *server,
					 struct wlr_pointer_constraint_v1 *constraint) {
	if (server->constraints.current == constraint) {
		return;
	}
	if (server->constraints.current != NULL) {
		stonefly_constraint_deactivate(server, constraint == NULL);
	}
	server->constraints.current = constraint;
	if (constraint == NULL) {
		return;
	}
	wlr_pointer_constraint_v1_send_activated(constraint);
}

static void constraint_handle_destroy(struct wl_listener *listener, void *data) {
	struct stonefly_constraint *c = wl_container_of(listener, c, destroy);
	struct stonefly_server *server = c->server;
	struct wlr_pointer_constraint_v1 *constraint = data;

	wl_list_remove(&c->destroy.link);
	if (server->constraints.current == constraint) {
		warp_to_constraint_hint(server, constraint);
		server->constraints.current = NULL;
	}
	free(c);
}

void server_new_pointer_constraint(struct wl_listener *listener, void *data) {
	struct stonefly_server *server = wl_container_of(listener, server, constraints.on_new);
	struct wlr_pointer_constraint_v1 *constraint = data;

	struct stonefly_constraint *c = calloc(1, sizeof(*c));
	if (c == NULL) {
		return;
	}
	c->server = server;
	c->constraint = constraint;
	c->destroy.notify = constraint_handle_destroy;
	wl_signal_add(&constraint->events.destroy, &c->destroy);

	if (server->seat->pointer_state.focused_surface == constraint->surface) {
		stonefly_constraint_activate(server, constraint);
	}
}

void apply_pointer_constraint(struct stonefly_server *server, double *dx, double *dy) {
	struct wlr_pointer_constraint_v1 *constraint = server->constraints.current;
	if (constraint == NULL) {
		return;
	}
	if (constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
		*dx = 0;
		*dy = 0;
		return;
	}
	if (constraint->type != WLR_POINTER_CONSTRAINT_V1_CONFINED) {
		return;
	}
	if (server->seat->pointer_state.focused_surface != constraint->surface) {
		return;
	}
	double sx = server->seat->pointer_state.sx;
	double sy = server->seat->pointer_state.sy;
	double cx, cy;
	if (!wlr_region_confine(&constraint->region, sx, sy, sx + *dx, sy + *dy, &cx, &cy)) {
		return;
	}
	*dx = cx - sx;
	*dy = cy - sy;
}

void seat_pointer_focus_change(struct wl_listener *listener, void *data) {
	struct stonefly_server *server = wl_container_of(listener, server, pointer_focus_change);
	/* This event is raised when the pointer focus is changed, including when the
	 * client is closed. We set the cursor image to its default if target surface
	 * is NULL */
	struct wlr_seat_pointer_focus_change_event *event = data;
	if (event->new_surface == NULL) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	}

	/* Activate constraint for the focused surface; deactivate on leave. */
	if (server->constraints.mgr == NULL) {
		return;
	}
	struct wlr_pointer_constraint_v1 *constraint = NULL;
	if (event->new_surface != NULL) {
		constraint = wlr_pointer_constraints_v1_constraint_for_surface(
			server->constraints.mgr, event->new_surface, server->seat);
	}
	if (server->constraints.current != constraint) {
		stonefly_constraint_activate(server, constraint);
	}
}
