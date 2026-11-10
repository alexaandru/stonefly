#include "seat/seat.h"
#include "seat/internal.h"

#include <errno.h>
#include <limits.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>
#include <wlr/util/edges.h>

#include "config/config.h"
#include "log/log.h"
#include "output/output.h"
#include "session/session.h"
#include "ssd/ssd.h"
#include "util/time.h"
#include "wm/drag_snap.h"
#include "wm/geom.h"
#include "wm/snap_preview.h"
#include "wm/wm.h"
#include "xdg/xdg.h"

static void process_cursor_motion(struct sf_server *server, uint32_t time);
static void seat_set_xcursor_fallback(struct sf_server *server, const char *const *names);
static struct sf_toplevel *border_resize_toplevel_at(struct sf_server *server, double lx, double ly,
						     uint32_t *edges_out);

/* Peeble setupCursorTheme: honor XCURSOR_* so desktop and clients match. */
static uint32_t cursor_size_from_env(uint32_t fallback) {
	const char *s = getenv("XCURSOR_SIZE");
	if (s == NULL || s[0] == '\0') {
		return fallback;
	}
	char *end = NULL;
	errno = 0;
	long v = strtol(s, &end, 10);
	if (errno != 0 || end == s || *end != '\0' || v <= 0 || v > (long)UINT32_MAX) {
		SF_LOGW("invalid XCURSOR_SIZE, using default", sf_str("value", s),
			sf_i64("size", fallback));
		return fallback;
	}
	return (uint32_t)v;
}

/*
 * Nested/headless: parent already applied HiDPI to XCURSOR_* (e.g. size 36 and
 * Hackneyed-36px under output scale 1.5). Using those again with nested scale
 * 1.0 makes the cursor huge once the parent scales our cursor surface. Mirror
 * sf_output_scale_factor: logical defaults only when session == NULL.
 */
uint32_t cursor_size_for_server(struct sf_server *server) {
	uint32_t fallback = (uint32_t)sf_cfg(server)->cursor_size_default;
	if (fallback < 1) {
		fallback = 24;
	}
	if (server->session == NULL) {
		return fallback;
	}
	return cursor_size_from_env(fallback);
}

/* Dup XCURSOR_THEME; when nested, strip a trailing "-NNpx" HiDPI variant. */
char *cursor_theme_dup(struct sf_server *server) {
	const char *env = getenv("XCURSOR_THEME");
	if (env == NULL || env[0] == '\0') {
		return NULL;
	}
	size_t n = strlen(env);
	size_t keep = n;
	if (server->session == NULL && n > 4 && env[n - 2] == 'p' && env[n - 1] == 'x') {
		size_t i = n - 3;
		while (i > 0 && env[i] >= '0' && env[i] <= '9') {
			i--;
		}
		if (i > 0 && env[i] == '-' && i + 1 < n - 2) {
			keep = i;
		}
	}
	char *out = malloc(keep + 1);
	if (out == NULL) {
		return NULL;
	}
	memcpy(out, env, keep);
	out[keep] = '\0';
	return out;
}

void destroy_touch(struct sf_touch *touch) {
	struct sf_server *server = touch->server;
	wl_list_remove(&touch->destroy.link);
	wl_list_remove(&touch->link);
	free(touch);
	seat_update_capabilities(server);
}

static void touch_handle_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_touch *touch = wl_container_of(listener, touch, destroy);
	destroy_touch(touch);
}

void server_new_touch(struct sf_server *server, struct wlr_input_device *device) {
	wlr_cursor_attach_input_device(server->cursor, device);

	struct sf_touch *touch = calloc(1, sizeof(*touch));
	if (touch == NULL) {
		return;
	}
	touch->server = server;
	touch->destroy.notify = touch_handle_destroy;
	wl_signal_add(&device->events.destroy, &touch->destroy);
	wl_list_insert(&server->touch, &touch->link);
}

/* True when the pointer sits in the WM border/corner resize hit band. */
static bool seat_pointer_in_border_resize(struct sf_server *server) {
	uint32_t edges = 0;
	return border_resize_toplevel_at(server, server->cursor->x, server->cursor->y, &edges) !=
		       NULL &&
	       edges != 0;
}

void seat_request_cursor(struct wl_listener *listener, void *data) {
	struct sf_server *server = wl_container_of(listener, server, request_cursor);
	/* This event is raised by the seat when a client provides a cursor image */
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client = server->seat->pointer_state.focused_client;
	/* WM interactive move/resize owns the face; ignore client images. */
	if (server->cursor_mode != SF_CURSOR_PASSTHROUGH) {
		return;
	}
	/* Border hit band shows a resize face while still forwarding enter/motion. */
	if (seat_pointer_in_border_resize(server)) {
		return;
	}
	/* This can be sent by any client, so we check to make sure this one is
	 * actually has pointer focus first. */
	if (focused_client == event->seat_client) {
		/* Once we've vetted the client, we can tell the cursor to use the
		 * provided surface as the cursor image. It will set the hardware cursor
		 * on the output that it's currently on and continue to do so as the
		 * cursor moves between outputs. */
		wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x,
				       event->hotspot_y);
	}
}

/* Clients that speak wp_cursor_shape_v1 ask for a named shape; we render it
 * from the compositor xcursor theme so theme and size match the desktop. */
void seat_request_set_cursor_shape(struct wl_listener *listener, void *data) {
	struct sf_server *server = wl_container_of(listener, server, request_set_cursor_shape);
	struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;

	if (server->cursor_mode != SF_CURSOR_PASSTHROUGH) {
		return;
	}
	if (seat_pointer_in_border_resize(server)) {
		return;
	}
	if (event->seat_client != server->seat->pointer_state.focused_client) {
		return;
	}
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr,
			       wlr_cursor_shape_v1_name(event->shape));
}

struct sf_toplevel *sf_desktop_toplevel_at(struct sf_server *server, double lx, double ly,
					   struct wlr_surface **surface, double *sx, double *sy) {
	/* Topmost buffer at layout coords. Client surfaces or compositor SSD. */
	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
	if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}
	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);

	/* SSD titlebar: no wlr_surface; node.data is sf_ssd. */
	struct sf_toplevel *ssd_tl = NULL;
	if (sf_ssd_node_is_titlebar(node, &ssd_tl)) {
		*surface = NULL;
		return ssd_tl;
	}

	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
	if (!scene_surface) {
		return NULL;
	}

	*surface = scene_surface->surface;
	/* Walk up to a scene node with data. Layer surfaces also stash data;
	 * only return when the kind tag is an xdg toplevel. */
	struct wlr_scene_tree *tree = node->parent;
	while (tree != NULL && tree->node.data == NULL) {
		tree = tree->node.parent;
	}
	if (tree == NULL || tree->node.data == NULL) {
		return NULL;
	}
	enum sf_node_kind kind = *(enum sf_node_kind *)tree->node.data;
	if (kind != SF_NODE_TOPLEVEL) {
		return NULL;
	}
	return tree->node.data;
}

void sf_reset_cursor_mode(struct sf_server *server) {
	/* Reset the cursor mode to passthrough. */
	if (server->drag_snap.active) {
		sf_snap_preview_hide(server);
		sf_drag_snap_reset(&server->drag_snap);
	}
	server->cursor_mode = SF_CURSOR_PASSTHROUGH;
	server->grabbed_toplevel = NULL;
	/* Re-enter under the pointer so the client (or empty-desktop default) face
	 * replaces the WM grabbing/resize xcursor without waiting for motion. */
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	uint32_t msec = (uint32_t)sf_timespec_ms(&now);
	process_cursor_motion(server, msec);
}

void sf_pointer_warp_to(struct sf_server *server, double lx, double ly) {
	if (server == NULL || server->cursor_mode != SF_CURSOR_PASSTHROUGH) {
		return;
	}
	if (server->constraints.current != NULL &&
	    server->constraints.current->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
		return;
	}
	wlr_cursor_warp(server->cursor, NULL, lx, ly);
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	process_cursor_motion(server, (uint32_t)sf_timespec_ms(&now));
}

void sf_pointer_center(struct sf_server *server, struct sf_toplevel *toplevel) {
	if (server == NULL || toplevel == NULL || toplevel->scene_tree == NULL) {
		return;
	}
	struct wlr_box geom;
	sf_toplevel_layout_geom(toplevel, &geom);
	if (geom.width <= 0 || geom.height <= 0) {
		return;
	}
	sf_pointer_warp_to(server, geom.x + geom.width / 2.0, geom.y + geom.height / 2.0);
}

void sf_pointer_follow_focus(struct sf_server *server) {
	if (server == NULL || !sf_cfg(server)->cursor_follows_focus) {
		return;
	}
	sf_pointer_center(server, sf_focused_toplevel(server));
}

static void process_cursor_move(struct sf_server *server) {
	/* Keep WM face over any lingering client set_cursor surface. */
	sf_seat_apply_grab_cursor(server);
	/* Magnetic resistance + snap preview (peeble Move.Render). */
	sf_drag_snap_motion(server);
}

#define SF_RESIZE_CENTER_MIN 50

/*
 * Chrome outside the xdg geometry box so the outer *opaque* edge sits on the
 * gap. SSD: titleband + compositor border. CSD: geometry is already the
 * placement box (xdg-shell); surface−geo is mostly drop-shadow — using it as
 * hard inset left ~1″ empty gap on the heavy shadow sides (ghostty R/B).
 */
static void resize_frame_insets(struct sf_toplevel *toplevel, int *left, int *top, int *right,
				int *bottom) {
	*left = 0;
	*top = 0;
	*right = 0;
	*bottom = 0;
	if (toplevel == NULL || toplevel->xdg_toplevel == NULL) {
		return;
	}
	int border = sf_cfg(toplevel->server)->border_thickness;
	if (border < 0) {
		border = 0;
	}
	int ssd_h = sf_ssd_top_inset(toplevel);
	*left = border;
	*right = border;
	*bottom = border;
	*top = (ssd_h > 0 ? ssd_h : 0) + border;
}

/* Usable∩gaps box for the *geometry* edges so the visible frame stays inside. */
static bool resize_usable_content_area(struct sf_toplevel *toplevel, struct wlr_box *area) {
	if (toplevel == NULL || area == NULL || !sf_toplevel_output_box(toplevel, area)) {
		return false;
	}
	sf_box_inset_gap(area, sf_cfg(toplevel->server)->gap_size);
	int L, T, R, B;
	resize_frame_insets(toplevel, &L, &T, &R, &B);
	area->x += L;
	area->y += T;
	area->width -= L + R;
	area->height -= T + B;
	return area->width > 0 && area->height > 0;
}

static void process_cursor_resize(struct sf_server *server) {
	/*
	 * Resizing the grabbed toplevel can be a little bit complicated, because we
	 * could be resizing from any corner or edge. This not only resizes the
	 * toplevel on one or two axes, but can also move the toplevel if you resize
	 * from the top or left edges (or top-left corner).
	 *
	 * Note that some shortcuts are taken here. In a more fleshed-out
	 * compositor, you'd wait for the client to prepare a buffer at the new
	 * size, then commit any movement that was prepared.
	 */
	struct sf_toplevel *toplevel = server->grabbed_toplevel;
	double border_x = server->cursor->x - server->grab_x;
	double border_y = server->cursor->y - server->grab_y;
	int new_left = server->grab_geobox.x;
	int new_right = server->grab_geobox.x + server->grab_geobox.width;
	int new_top = server->grab_geobox.y;
	int new_bottom = server->grab_geobox.y + server->grab_geobox.height;

	if (server->resize_edges & (uint32_t)WLR_EDGE_TOP) {
		new_top = (int)border_y;
		if (new_top >= new_bottom) {
			new_top = new_bottom - 1;
		}
	} else if (server->resize_edges & (uint32_t)WLR_EDGE_BOTTOM) {
		new_bottom = (int)border_y;
		if (new_bottom <= new_top) {
			new_bottom = new_top + 1;
		}
	}
	if (server->resize_edges & (uint32_t)WLR_EDGE_LEFT) {
		new_left = (int)border_x;
		if (new_left >= new_right) {
			new_left = new_right - 1;
		}
	} else if (server->resize_edges & (uint32_t)WLR_EDGE_RIGHT) {
		new_right = (int)border_x;
		if (new_right <= new_left) {
			new_right = new_left + 1;
		}
	}

	/* Keep the dragged edge(s) inside usable∩gaps; anchored edges stay put. */
	struct wlr_box area;
	if (resize_usable_content_area(toplevel, &area)) {
		int area_right = area.x + area.width;
		int area_bottom = area.y + area.height;
		if (server->resize_edges & (uint32_t)WLR_EDGE_TOP) {
			if (new_top < area.y) {
				new_top = area.y;
			}
			if (new_top >= new_bottom) {
				new_top = new_bottom - 1;
			}
		} else if (server->resize_edges & (uint32_t)WLR_EDGE_BOTTOM) {
			if (new_bottom > area_bottom) {
				new_bottom = area_bottom;
			}
			if (new_bottom <= new_top) {
				new_bottom = new_top + 1;
			}
		}
		if (server->resize_edges & (uint32_t)WLR_EDGE_LEFT) {
			if (new_left < area.x) {
				new_left = area.x;
			}
			if (new_left >= new_right) {
				new_left = new_right - 1;
			}
		} else if (server->resize_edges & (uint32_t)WLR_EDGE_RIGHT) {
			if (new_right > area_right) {
				new_right = area_right;
			}
			if (new_right <= new_left) {
				new_right = new_left + 1;
			}
		}
	}

	/* scene_tree origin is geometry TL — do not subtract geo.x/y (CSD). */
	wlr_scene_node_set_position(&toplevel->scene_tree->node, new_left, new_top);
	toplevel->resting_x = new_left;
	toplevel->resting_y = new_top;

	int new_width = new_right - new_left;
	int new_height = new_bottom - new_top;
	wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, new_width, new_height);
	sf_seat_apply_grab_cursor(server);
}

/*
 * Place the scene so the *committed* content box stays centered on the locked
 * point. Positioning for the pending configure size before the client acks
 * made the buffer translate (felt like move, not resize).
 */
void sf_seat_resize_center_sync(struct sf_server *server) {
	if (server == NULL || server->cursor_mode != SF_CURSOR_RESIZE_CENTER) {
		return;
	}
	struct sf_toplevel *toplevel = server->grabbed_toplevel;
	if (toplevel == NULL || toplevel->scene_tree == NULL || toplevel->xdg_toplevel == NULL) {
		return;
	}

	struct wlr_box *geo = &toplevel->xdg_toplevel->base->geometry;
	int cur_w = geo->width;
	int cur_h = geo->height;
	if (cur_w <= 0 || cur_h <= 0) {
		return;
	}

	/* scene_tree origin is geometry TL — same as layout_geom / decor. */
	int x = (int)lround(server->resize_center_x - (double)cur_w / 2.0);
	int y = (int)lround(server->resize_center_y - (double)cur_h / 2.0);
	wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y);
	toplevel->resting_x = x;
	toplevel->resting_y = y;
}

/* Max content size that still fits usable∩gaps around a fixed center. */
static void resize_center_max_size(struct sf_toplevel *toplevel, double cx, double cy, int *max_w,
				   int *max_h) {
	*max_w = INT_MAX;
	*max_h = INT_MAX;
	struct wlr_box area;
	if (!resize_usable_content_area(toplevel, &area)) {
		return;
	}
	double left = cx - (double)area.x;
	double right = (double)area.x + (double)area.width - cx;
	double up = cy - (double)area.y;
	double down = (double)area.y + (double)area.height - cy;
	if (left < 0.0) {
		left = 0.0;
	}
	if (right < 0.0) {
		right = 0.0;
	}
	if (up < 0.0) {
		up = 0.0;
	}
	if (down < 0.0) {
		down = 0.0;
	}
	*max_w = (int)(2.0 * (left < right ? left : right));
	*max_h = (int)(2.0 * (up < down ? up : down));
}

/* Super+Shift+RMB: propose Δw = 2·Δx / Δh = 2·Δy; keep committed buffer centered. */
static void process_cursor_resize_center(struct sf_server *server) {
	struct sf_toplevel *toplevel = server->grabbed_toplevel;
	if (toplevel == NULL || toplevel->scene_tree == NULL || toplevel->xdg_toplevel == NULL) {
		return;
	}

	int dx = (int)lround(server->cursor->x - server->grab_x);
	int dy = (int)lround(server->cursor->y - server->grab_y);
	int new_width = server->grab_geobox.width + 2 * dx;
	int new_height = server->grab_geobox.height + 2 * dy;
	if (new_width < SF_RESIZE_CENTER_MIN) {
		new_width = SF_RESIZE_CENTER_MIN;
	}
	if (new_height < SF_RESIZE_CENTER_MIN) {
		new_height = SF_RESIZE_CENTER_MIN;
	}

	int max_w, max_h;
	resize_center_max_size(toplevel, server->resize_center_x, server->resize_center_y, &max_w,
			       &max_h);
	if (max_w < SF_RESIZE_CENTER_MIN) {
		max_w = SF_RESIZE_CENTER_MIN;
	}
	if (max_h < SF_RESIZE_CENTER_MIN) {
		max_h = SF_RESIZE_CENTER_MIN;
	}
	if (new_width > max_w) {
		new_width = max_w;
	}
	if (new_height > max_h) {
		new_height = max_h;
	}

	wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, new_width, new_height);
	sf_seat_resize_center_sync(server);
	sf_seat_apply_grab_cursor(server);
}

struct seat_surface_coords_ctx {
	struct wlr_surface *target;
	double cursor_x;
	double cursor_y;
	double sx;
	double sy;
	bool found;
};

static void seat_surface_coords_iter(struct wlr_scene_buffer *buffer, int sx, int sy,
				     void *user_data) {
	(void)sx;
	(void)sy;
	struct seat_surface_coords_ctx *ctx = user_data;
	if (ctx->found) {
		return;
	}
	struct wlr_scene_surface *ss = wlr_scene_surface_try_from_buffer(buffer);
	if (ss == NULL || ss->surface != ctx->target) {
		return;
	}
	int lx = 0;
	int ly = 0;
	if (!wlr_scene_node_coords(&buffer->node, &lx, &ly)) {
		return;
	}
	ctx->sx = ctx->cursor_x - (double)lx;
	ctx->sy = ctx->cursor_y - (double)ly;
	ctx->found = true;
}

/* Layout cursor → surface-local for an arbitrary scene-backed surface. */
static bool seat_pointer_surface_coords(struct sf_server *server, struct wlr_surface *surface,
					double *sx, double *sy) {
	if (surface == NULL) {
		return false;
	}
	struct seat_surface_coords_ctx ctx = {
		.target = surface,
		.cursor_x = server->cursor->x,
		.cursor_y = server->cursor->y,
	};
	wlr_scene_node_for_each_buffer(&server->scene->tree.node, seat_surface_coords_iter, &ctx);
	if (!ctx.found) {
		return false;
	}
	*sx = ctx.sx;
	*sy = ctx.sy;
	return true;
}

static void process_cursor_motion(struct sf_server *server, uint32_t time) {
	/* If the mode is non-passthrough, delegate to those functions. */
	if (server->cursor_mode == SF_CURSOR_MOVE) {
		process_cursor_move(server);
		return;
	}
	if (server->cursor_mode == SF_CURSOR_RESIZE) {
		process_cursor_resize(server);
		return;
	}
	if (server->cursor_mode == SF_CURSOR_RESIZE_CENTER) {
		process_cursor_resize_center(server);
		return;
	}

	/* Otherwise, find the toplevel under the pointer and send the event along.
	 * When locked, lock surfaces / blank sit above the rest of the scene. */
	double sx, sy;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *surface = NULL;
	struct sf_toplevel *toplevel = sf_desktop_toplevel_at(
		server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	bool buttons_down = seat->pointer_state.button_count > 0;
	struct wlr_surface *grabbed = buttons_down ? seat->pointer_state.focused_surface : NULL;

	/*
	 * Implicit grab: while any button is held, keep sending motion to the
	 * press surface. Clearing focus here drops the matching release (Chrome
	 * side-panel resize keeps going; terminal selection/primary goes weird).
	 */
	if (grabbed != NULL) {
		double gsx;
		double gsy;
		if (surface == grabbed) {
			gsx = sx;
			gsy = sy;
		} else if (!seat_pointer_surface_coords(server, grabbed, &gsx, &gsy)) {
			gsx = seat->pointer_state.sx;
			gsy = seat->pointer_state.sy;
		}
		wlr_seat_pointer_notify_motion(seat, time, gsx, gsy);
		return;
	}

	/*
	 * Border/corner hit band: WM owns the resize cursor. Keep client pointer
	 * focus when a surface is still under the cursor (inner band) so leave
	 * does not cancel selection; ignore client set_cursor while in-band.
	 */
	if (!sf_session_locked(server)) {
		uint32_t bedges = 0;
		struct sf_toplevel *border_tl = border_resize_toplevel_at(
			server, server->cursor->x, server->cursor->y, &bedges);
		if (border_tl != NULL && bedges != 0) {
			(void)toplevel;
			wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr,
					       sf_resize_xcursor_name(bedges));
			if (surface != NULL) {
				wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
				wlr_seat_pointer_notify_motion(seat, time, sx, sy);
			} else {
				wlr_seat_pointer_clear_focus(seat);
			}
			return;
		}
	}

	/* SSD titlebar: no client surface — show move affordance before grab. */
	if (surface == NULL && toplevel != NULL && toplevel->ssd != NULL) {
		double hx, hy;
		struct wlr_scene_node *node = wlr_scene_node_at(
			&server->scene->tree.node, server->cursor->x, server->cursor->y, &hx, &hy);
		struct sf_toplevel *ssd_tl = NULL;
		if (node != NULL && sf_ssd_node_is_titlebar(node, &ssd_tl) && ssd_tl == toplevel) {
			static const char *const move_names[] = {"grabbing", "closedhand", "move",
								 "fleur", NULL};
			seat_set_xcursor_fallback(server, move_names);
			wlr_seat_pointer_clear_focus(seat);
			return;
		}
	}

	if (surface == NULL) {
		/* Empty desktop (or non-surface node): show the theme default.
		 * Layer-shell surfaces still set surface and may set_cursor. */
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	}
	if (surface) {
		/*
		 * Send pointer enter and motion events.
		 *
		 * The enter event gives the surface "pointer focus", which is distinct
		 * from keyboard focus. You get pointer focus by moving the pointer over
		 * a window.
		 *
		 * Note that wlroots will avoid sending duplicate enter/motion events if
		 * the surface has already has pointer focus or if the client is already
		 * aware of the coordinates passed.
		 */
		wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(seat, time, sx, sy);
	} else {
		/* Clear pointer focus so future button events and such are not sent to
		 * the last client to have the cursor over it. */
		wlr_seat_pointer_clear_focus(seat);
	}
}

void server_cursor_motion(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits a _relative_
	 * pointer motion event (i.e. a delta) */
	struct sf_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	sf_session_notify_activity(server);

	/* Relative-pointer gets unclamped deltas even when the cursor is locked. */
	if (server->relative_pointer_mgr != NULL) {
		wlr_relative_pointer_manager_v1_send_relative_motion(
			server->relative_pointer_mgr, server->seat,
			(uint64_t)event->time_msec * 1000, event->delta_x, event->delta_y,
			event->unaccel_dx, event->unaccel_dy);
	}

	double dx = event->delta_x;
	double dy = event->delta_y;
	apply_pointer_constraint(server, &dx, &dy);
	if (dx != 0 || dy != 0) {
		wlr_cursor_move(server->cursor, &event->pointer->base, dx, dy);
		process_cursor_motion(server, event->time_msec);
	} else if (server->constraints.current != NULL &&
		   server->constraints.current->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
		/* Locked: cursor stays put; clients still got relative motion. */
	} else {
		process_cursor_motion(server, event->time_msec);
	}
}

void server_cursor_motion_absolute(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits an _absolute_
	 * motion event, from 0..1 on each axis. This happens, for example, when
	 * wlroots is running under a Wayland window rather than KMS+DRM, and you
	 * move the mouse over the window. You could enter the window from any edge,
	 * so we have to warp the mouse there. There is also some hardware which
	 * emits these events. */
	struct sf_server *server = wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	sf_session_notify_activity(server);

	double lx, ly;
	wlr_cursor_absolute_to_layout_coords(server->cursor, &event->pointer->base, event->x,
					     event->y, &lx, &ly);
	double dx = lx - server->cursor->x;
	double dy = ly - server->cursor->y;

	if (server->constraints.current != NULL &&
	    server->constraints.current->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
		/* Absolute motion ignored while locked (relative path still works). */
		return;
	}
	apply_pointer_constraint(server, &dx, &dy);
	wlr_cursor_move(server->cursor, &event->pointer->base, dx, dy);
	process_cursor_motion(server, event->time_msec);
}

static uint32_t seat_keyboard_mods(struct sf_server *server) {
	struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
	if (kb == NULL) {
		return 0;
	}
	return wlr_keyboard_get_modifiers(kb);
}

/* Resize from the edges nearest the pointer (sway/i3-style). */
static uint32_t resize_edges_at(struct sf_toplevel *toplevel, double lx, double ly) {
	struct wlr_box geom;
	sf_toplevel_layout_geom(toplevel, &geom);
	uint32_t edges = 0;
	if (lx < geom.x + geom.width / 2.0) {
		edges |= (uint32_t)WLR_EDGE_LEFT;
	} else {
		edges |= (uint32_t)WLR_EDGE_RIGHT;
	}
	if (ly < geom.y + geom.height / 2.0) {
		edges |= (uint32_t)WLR_EDGE_TOP;
	} else {
		edges |= (uint32_t)WLR_EDGE_BOTTOM;
	}
	return edges;
}

/* Same mapping as wlroots wlr_xcursor_get_resize_name; uint32_t bitflags. */
const char *sf_resize_xcursor_name(uint32_t edges) {
	if (edges & (uint32_t)WLR_EDGE_TOP) {
		if (edges & (uint32_t)WLR_EDGE_RIGHT) {
			return "ne-resize";
		}
		if (edges & (uint32_t)WLR_EDGE_LEFT) {
			return "nw-resize";
		}
		return "n-resize";
	}
	if (edges & (uint32_t)WLR_EDGE_BOTTOM) {
		if (edges & (uint32_t)WLR_EDGE_RIGHT) {
			return "se-resize";
		}
		if (edges & (uint32_t)WLR_EDGE_LEFT) {
			return "sw-resize";
		}
		return "s-resize";
	}
	if (edges & (uint32_t)WLR_EDGE_RIGHT) {
		return "e-resize";
	}
	if (edges & (uint32_t)WLR_EDGE_LEFT) {
		return "w-resize";
	}
	return "se-resize";
}

static bool xcursor_theme_has(struct sf_server *server, const char *name) {
	if (server->cursor_mgr == NULL || name == NULL) {
		return false;
	}
	float scale = sf_output_scale_factor(server);
	return wlr_xcursor_manager_get_xcursor(server->cursor_mgr, name, scale) != NULL;
}

static void seat_set_xcursor_fallback(struct sf_server *server, const char *const *names) {
	for (size_t i = 0; names[i] != NULL; i++) {
		if (xcursor_theme_has(server, names[i])) {
			wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, names[i]);
			return;
		}
	}
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
}

void sf_seat_apply_grab_cursor(struct sf_server *server) {
	if (server == NULL || server->cursor == NULL || server->cursor_mgr == NULL) {
		return;
	}
	if (server->cursor_mode == SF_CURSOR_MOVE) {
		static const char *const move_names[] = {
			"grabbing", "closedhand", "move", "fleur", "all-scroll", "size_all", NULL};
		seat_set_xcursor_fallback(server, move_names);
		return;
	}
	if (server->cursor_mode == SF_CURSOR_RESIZE) {
		const char *name = sf_resize_xcursor_name(server->resize_edges);
		if (xcursor_theme_has(server, name)) {
			wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, name);
		} else {
			wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
		}
		return;
	}
	if (server->cursor_mode == SF_CURSOR_RESIZE_CENTER) {
		static const char *const center_names[] = {"size_all", "all-scroll", "fleur",
							   "se-resize", NULL};
		seat_set_xcursor_fallback(server, center_names);
	}
}

/* Content + SSD titlebar in layout coords (drawn border sits outside this). */
static void toplevel_frame_box(struct sf_toplevel *toplevel, struct wlr_box *frame) {
	sf_toplevel_layout_geom(toplevel, frame);
	int ssd_h = sf_ssd_top_inset(toplevel);
	frame->y -= ssd_h;
	frame->height += ssd_h;
}

static int toplevel_chrome_border(struct sf_toplevel *toplevel) {
	if (toplevel->fullscreen || toplevel->maximized_edges || toplevel->borderless) {
		return 0;
	}
	return sf_cfg(toplevel->server)->border_thickness;
}

/*
 * Edges under the pointer for border/corner resize, or 0 when not in the hit
 * band. Band = from (content/SSD edge − in) out through (drawn border + out).
 */
static uint32_t border_resize_edges_at(struct sf_toplevel *toplevel, double lx, double ly) {
	const struct sf_config *cfg = sf_cfg(toplevel->server);
	int inner = cfg->resize_border_in;
	int outer = cfg->resize_border_out;
	if (inner <= 0 && outer <= 0) {
		return 0;
	}
	if (toplevel->fullscreen || toplevel->minimized) {
		return 0;
	}
	if (toplevel->scene_tree == NULL || !toplevel->scene_tree->node.enabled) {
		return 0;
	}

	struct wlr_box frame;
	toplevel_frame_box(toplevel, &frame);
	if (frame.width <= 0 || frame.height <= 0) {
		return 0;
	}

	int border = toplevel_chrome_border(toplevel);
	double left = (double)frame.x - (double)border - (double)outer;
	double right = (double)frame.x + (double)frame.width + (double)border + (double)outer;
	double top = (double)frame.y - (double)border - (double)outer;
	double bottom = (double)frame.y + (double)frame.height + (double)border + (double)outer;
	if (lx < left || lx >= right || ly < top || ly >= bottom) {
		return 0;
	}

	/* Deep inside the content/SSD: not a resize grab. */
	double ileft = (double)frame.x + (double)inner;
	double iright = (double)frame.x + (double)frame.width - (double)inner;
	double itop = (double)frame.y + (double)inner;
	double ibottom = (double)frame.y + (double)frame.height - (double)inner;
	if (inner > 0 && lx >= ileft && lx < iright && ly >= itop && ly < ibottom) {
		return 0;
	}
	/* Degenerate tiny window: treat whole outer box as all-edges. */
	if (inner * 2 >= frame.width) {
		ileft = (double)frame.x + (double)frame.width / 2.0;
		iright = ileft;
	}
	if (inner * 2 >= frame.height) {
		itop = (double)frame.y + (double)frame.height / 2.0;
		ibottom = itop;
	}

	uint32_t edges = 0;
	if (lx < ileft) {
		edges |= (uint32_t)WLR_EDGE_LEFT;
	}
	if (lx >= iright) {
		edges |= (uint32_t)WLR_EDGE_RIGHT;
	}
	if (ly < itop) {
		edges |= (uint32_t)WLR_EDGE_TOP;
	}
	if (ly >= ibottom) {
		edges |= (uint32_t)WLR_EDGE_BOTTOM;
	}
	return edges;
}

/* True when a layer-shell surface sits under the pointer (above windows). */
static bool seat_layer_blocks_border_resize(struct sf_server *server, double lx, double ly) {
	double sx, sy;
	struct wlr_scene_node *node =
		wlr_scene_node_at(&server->scene->tree.node, lx, ly, &sx, &sy);
	while (node != NULL) {
		if (node->data != NULL) {
			enum sf_node_kind kind = *(enum sf_node_kind *)node->data;
			if (kind == SF_NODE_LAYER) {
				return true;
			}
			if (kind == SF_NODE_TOPLEVEL || kind == SF_NODE_SSD) {
				return false;
			}
		}
		node = node->parent != NULL ? &node->parent->node : NULL;
	}
	return false;
}

/*
 * Frontmost mapped toplevel whose outer resize box contains (lx, ly). Sets
 * *edges_out to border_resize_edges_at (may be 0 when deep inside that window).
 */
static struct sf_toplevel *border_resize_toplevel_at(struct sf_server *server, double lx, double ly,
						     uint32_t *edges_out) {
	*edges_out = 0;
	const struct sf_config *cfg = sf_cfg(server);
	if (cfg->resize_border_in <= 0 && cfg->resize_border_out <= 0) {
		return NULL;
	}
	if (seat_layer_blocks_border_resize(server, lx, ly)) {
		return NULL;
	}

	struct sf_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel->fullscreen || toplevel->minimized) {
			continue;
		}
		if (toplevel->scene_tree == NULL || !toplevel->scene_tree->node.enabled) {
			continue;
		}
		struct wlr_box frame;
		toplevel_frame_box(toplevel, &frame);
		int border = toplevel_chrome_border(toplevel);
		int outer = cfg->resize_border_out;
		double left = (double)frame.x - (double)border - (double)outer;
		double right =
			(double)frame.x + (double)frame.width + (double)border + (double)outer;
		double top = (double)frame.y - (double)border - (double)outer;
		double bottom =
			(double)frame.y + (double)frame.height + (double)border + (double)outer;
		if (lx < left || lx >= right || ly < top || ly >= bottom) {
			continue;
		}
		/* Topmost window under the pointer — stop even if deep inside. */
		*edges_out = border_resize_edges_at(toplevel, lx, ly);
		return toplevel;
	}
	return NULL;
}

void server_cursor_button(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits a button
	 * event. */
	struct sf_server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;

	sf_session_notify_activity(server);

	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
		bool grabbing = server->cursor_mode != SF_CURSOR_PASSTHROUGH;
		if (server->cursor_mode == SF_CURSOR_MOVE) {
			sf_drag_snap_finish(server);
		}
		sf_reset_cursor_mode(server);
		/* Titlebar-initiated move: drop pending button state. */
		if (grabbing) {
			sf_ssd_pointer_reset();
		}
		if (!grabbing && !sf_session_locked(server)) {
			double sx, sy;
			struct wlr_surface *surface = NULL;
			struct sf_toplevel *toplevel = sf_desktop_toplevel_at(
				server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
			if (toplevel != NULL && surface == NULL && toplevel->ssd != NULL &&
			    sf_ssd_pointer_button(server, toplevel, sx, sy, event)) {
				return;
			}
			wlr_seat_pointer_notify_button(server->seat, event->time_msec,
						       event->button, event->state);
		} else if (!grabbing) {
			wlr_seat_pointer_notify_button(server->seat, event->time_msec,
						       event->button, event->state);
		}
		return;
	}

	/* Super+LMB move, Super+RMB edge resize, Super+Shift+RMB center-lock. */
	if (!sf_session_locked(server)) {
		uint32_t mods = seat_keyboard_mods(server) & (uint32_t)SF_MOD_MASK;
		if (event->button == BTN_RIGHT && mods == (SF_SUPER | SF_SHIFT)) {
			sf_tap_disarm(server);
			double sx, sy;
			struct wlr_surface *surface = NULL;
			struct sf_toplevel *toplevel = sf_desktop_toplevel_at(
				server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
			if (toplevel != NULL) {
				sf_focus_toplevel(toplevel);
				sf_begin_interactive(toplevel, SF_CURSOR_RESIZE_CENTER, 0);
				return;
			}
		}
		if (mods == SF_SUPER && (event->button == BTN_LEFT || event->button == BTN_RIGHT)) {
			sf_tap_disarm(server);
			double sx, sy;
			struct wlr_surface *surface = NULL;
			struct sf_toplevel *toplevel = sf_desktop_toplevel_at(
				server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
			if (toplevel != NULL) {
				sf_focus_toplevel(toplevel);
				if (event->button == BTN_LEFT) {
					sf_begin_interactive(toplevel, SF_CURSOR_MOVE, 0);
				} else {
					sf_begin_interactive(toplevel, SF_CURSOR_RESIZE,
							     resize_edges_at(toplevel,
									     server->cursor->x,
									     server->cursor->y));
				}
				return;
			}
		}
	}

	/* LMB on border/corner hit band → resize (no Super required). */
	if (!sf_session_locked(server) && event->button == BTN_LEFT) {
		uint32_t bedges = 0;
		struct sf_toplevel *border_tl = border_resize_toplevel_at(
			server, server->cursor->x, server->cursor->y, &bedges);
		if (border_tl != NULL && bedges != 0) {
			sf_tap_disarm(server);
			sf_focus_toplevel(border_tl);
			sf_begin_interactive(border_tl, SF_CURSOR_RESIZE, bedges);
			return;
		}
	}

	if (!sf_session_locked(server)) {
		double sx, sy;
		struct wlr_surface *surface = NULL;
		struct sf_toplevel *toplevel = sf_desktop_toplevel_at(
			server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
		/* SSD titlebar: no client surface; consume button locally. */
		if (toplevel != NULL && surface == NULL && toplevel->ssd != NULL &&
		    sf_ssd_pointer_button(server, toplevel, sx, sy, event)) {
			return;
		}
		wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button,
					       event->state);
		sf_focus_toplevel(toplevel);
		return;
	}

	wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
}

void server_cursor_axis(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits an axis event,
	 * for example when you move the scroll wheel. */
	struct sf_server *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;
	sf_session_notify_activity(server);
	/* Notify the client with pointer focus of the axis event. */
	wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation,
				     event->delta, event->delta_discrete, event->source,
				     event->relative_direction);
}

void server_cursor_frame(struct wl_listener *listener, void *data) {
	(void)data;
	/* This event is forwarded by the cursor when a pointer emits an frame
	 * event. Frame events are sent after regular pointer events to group
	 * multiple events together. For instance, two axis events may happen at the
	 * same time, in which case a frame event won't be sent in between. */
	struct sf_server *server = wl_container_of(listener, server, cursor_frame);
	/* Notify the client with pointer focus of the frame event. */
	wlr_seat_pointer_notify_frame(server->seat);
}

void server_cursor_touch_down(struct wl_listener *listener, void *data) {
	struct sf_server *server = wl_container_of(listener, server, cursor_touch_down);
	struct wlr_touch_down_event *event = data;
	sf_session_notify_activity(server);

	double lx, ly;
	wlr_cursor_absolute_to_layout_coords(server->cursor, &event->touch->base, event->x,
					     event->y, &lx, &ly);

	double sx, sy;
	struct wlr_surface *surface = NULL;
	struct sf_toplevel *toplevel = sf_desktop_toplevel_at(server, lx, ly, &surface, &sx, &sy);
	if (surface == NULL) {
		return;
	}
	wlr_seat_touch_notify_down(server->seat, surface, event->time_msec, event->touch_id, sx,
				   sy);
	if (toplevel != NULL && !sf_session_locked(server)) {
		sf_focus_toplevel(toplevel);
	}
}

void server_cursor_touch_up(struct wl_listener *listener, void *data) {
	struct sf_server *server = wl_container_of(listener, server, cursor_touch_up);
	struct wlr_touch_up_event *event = data;
	sf_session_notify_activity(server);
	if (wlr_seat_touch_get_point(server->seat, event->touch_id) == NULL) {
		return;
	}
	wlr_seat_touch_notify_up(server->seat, event->time_msec, event->touch_id);
}

void server_cursor_touch_motion(struct wl_listener *listener, void *data) {
	struct sf_server *server = wl_container_of(listener, server, cursor_touch_motion);
	struct wlr_touch_motion_event *event = data;
	sf_session_notify_activity(server);
	if (wlr_seat_touch_get_point(server->seat, event->touch_id) == NULL) {
		return;
	}

	double lx, ly;
	wlr_cursor_absolute_to_layout_coords(server->cursor, &event->touch->base, event->x,
					     event->y, &lx, &ly);

	double sx, sy;
	struct wlr_surface *surface = NULL;
	sf_desktop_toplevel_at(server, lx, ly, &surface, &sx, &sy);
	if (surface != NULL) {
		wlr_seat_touch_point_focus(server->seat, surface, event->time_msec, event->touch_id,
					   sx, sy);
		wlr_seat_touch_notify_motion(server->seat, event->time_msec, event->touch_id, sx,
					     sy);
	} else {
		wlr_seat_touch_point_clear_focus(server->seat, event->time_msec, event->touch_id);
	}
}

void server_cursor_touch_cancel(struct wl_listener *listener, void *data) {
	struct sf_server *server = wl_container_of(listener, server, cursor_touch_cancel);
	struct wlr_touch_cancel_event *event = data;
	struct wlr_touch_point *point = wlr_seat_touch_get_point(server->seat, event->touch_id);
	if (point == NULL) {
		return;
	}
	wlr_seat_touch_notify_cancel(server->seat, point->client);
}

void server_cursor_touch_frame(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_server *server = wl_container_of(listener, server, cursor_touch_frame);
	wlr_seat_touch_notify_frame(server->seat);
}
