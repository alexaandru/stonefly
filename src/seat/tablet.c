#include "seat/tablet.h"
#include "seat/internal.h"

#include <linux/input-event-codes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_tablet_pad.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <scenefx/types/wlr_scene.h>

#include "log/log.h"
#include "seat/seat.h"
#include "server/server.h"
#include "session/session.h"
#include "toplevel/toplevel.h"
#include "wm/overview.h"
#include "wm/wm.h"

struct sf_tablet {
	struct wl_list link;
	struct sf_server *server;
	struct wlr_input_device *device;
	struct wlr_tablet *wlr_tablet;
	struct wlr_tablet_v2_tablet *v2;
	struct wl_listener destroy;
};

struct sf_tablet_tool {
	struct wl_list link;
	struct sf_server *server;
	struct wlr_tablet_tool *tool;
	struct wlr_tablet_v2_tablet_tool *v2;
	double x, y;
	double tilt_x, tilt_y;
	bool tip_down;
	bool emulating;
	struct wl_listener destroy;
	struct wl_listener set_cursor;
};

struct sf_tablet_pad {
	struct wl_list link;
	struct sf_server *server;
	struct wlr_input_device *device;
	struct wlr_tablet_pad *wlr_pad;
	struct wlr_tablet_v2_tablet_pad *v2;
	struct sf_tablet *tablet;
	struct wlr_surface *current_surface;
	unsigned int last_mode;
	bool have_mode;
	struct wl_listener button;
	struct wl_listener ring;
	struct wl_listener strip;
	struct wl_listener destroy;
	struct wl_listener surface_destroy;
};

static void listener_clear(struct wl_listener *listener) {
	if (listener->link.next == NULL) {
		return;
	}
	wl_list_remove(&listener->link);
	listener->link.next = NULL;
	listener->link.prev = NULL;
}

static struct sf_tablet *tablet_from_wlr(struct sf_server *server, struct wlr_tablet *wlr) {
	struct sf_tablet *tablet;
	wl_list_for_each(tablet, &server->tablet.tablets, link) {
		if (tablet->wlr_tablet == wlr) {
			return tablet;
		}
	}
	return NULL;
}

static struct wlr_surface *tablet_surface_at(struct sf_server *server, double lx, double ly,
					     double *sx, double *sy) {
	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
	if (node != NULL && node->type == WLR_SCENE_NODE_RECT && node->parent != NULL &&
	    node->parent->node.data != NULL) {
		enum sf_node_kind pkind = *(enum sf_node_kind *)node->parent->node.data;
		if (pkind == SF_NODE_TOPLEVEL) {
			struct sf_toplevel *tl = node->parent->node.data;
			if (tl->dim_overlay != NULL && node == &tl->dim_overlay->node &&
			    tl->xdg_scene_tree != NULL) {
				int px = 0, py = 0;
				wlr_scene_node_coords(&tl->scene_tree->node, &px, &py);
				node = wlr_scene_node_at(&tl->xdg_scene_tree->node, lx - px,
							 ly - py, sx, sy);
			}
		}
	}
	if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}
	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
	if (scene_surface == NULL) {
		return NULL;
	}
	return scene_surface->surface;
}

struct tablet_coords_ctx {
	struct wlr_surface *target;
	double cursor_x, cursor_y;
	double sx, sy;
	bool found;
};

static void tablet_coords_iter(struct wlr_scene_buffer *buffer, int sx, int sy, void *data) {
	(void)sx;
	(void)sy;
	struct tablet_coords_ctx *ctx = data;
	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(buffer);
	if (scene_surface == NULL || scene_surface->surface != ctx->target) {
		return;
	}
	int lx = 0, ly = 0;
	if (!wlr_scene_node_coords(&buffer->node, &lx, &ly)) {
		return;
	}
	ctx->sx = ctx->cursor_x - (double)lx;
	ctx->sy = ctx->cursor_y - (double)ly;
	ctx->found = true;
}

static bool surface_local_coords(struct sf_server *server, struct wlr_surface *surface, double *sx,
				 double *sy) {
	if (surface == NULL) {
		return false;
	}
	struct tablet_coords_ctx ctx = {
		.target = surface,
		.cursor_x = server->cursor->x,
		.cursor_y = server->cursor->y,
	};
	wlr_scene_node_for_each_buffer(&server->scene->tree.node, tablet_coords_iter, &ctx);
	if (!ctx.found) {
		return false;
	}
	*sx = ctx.sx;
	*sy = ctx.sy;
	return true;
}

static bool tablet_should_emulate(struct sf_server *server, struct sf_tablet *tablet,
				  struct sf_tablet_tool *tool, struct wlr_surface *surface) {
	if (tablet == NULL || tablet->v2 == NULL || tool->v2 == NULL) {
		return true;
	}
	if (sf_overview_active(server) || sf_session_locked(server) ||
	    server->cursor_mode != SF_CURSOR_PASSTHROUGH || server->seat->drag != NULL) {
		return true;
	}
	if (tool->emulating && tool->tip_down) {
		return true;
	}
	if (tool->tool->type == WLR_TABLET_TOOL_TYPE_MOUSE ||
	    tool->tool->type == WLR_TABLET_TOOL_TYPE_LENS) {
		return true;
	}
	if (surface == NULL) {
		return true;
	}
	return !wlr_surface_accepts_tablet_v2(surface, tablet->v2);
}

static void tool_set_emulating(struct sf_tablet_tool *tool, bool emulating) {
	if (tool->emulating == emulating) {
		return;
	}
	if (emulating) {
		if (tool->v2 != NULL && tool->v2->focused_surface != NULL) {
			wlr_tablet_v2_tablet_tool_notify_proximity_out(tool->v2);
		}
	} else {
		wlr_seat_pointer_notify_clear_focus(tool->server->seat);
	}
	tool->emulating = emulating;
}

static void pad_leave_surface(struct sf_tablet_pad *pad) {
	if (pad->current_surface == NULL) {
		return;
	}
	if (pad->v2 != NULL) {
		wlr_tablet_v2_tablet_pad_notify_leave(pad->v2, pad->current_surface);
	}
	listener_clear(&pad->surface_destroy);
	pad->current_surface = NULL;
}

static void on_pad_surface_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_tablet_pad *pad = wl_container_of(listener, pad, surface_destroy);
	pad->current_surface = NULL;
	listener_clear(&pad->surface_destroy);
}

static void pad_enter_surface(struct sf_server *server, struct wlr_surface *surface) {
	if (surface == NULL) {
		return;
	}
	struct sf_tablet_pad *pad;
	wl_list_for_each(pad, &server->tablet.pads, link) {
		if (pad->tablet == NULL || pad->v2 == NULL || pad->tablet->v2 == NULL) {
			continue;
		}
		if (pad->current_surface == surface) {
			continue;
		}
		pad_leave_surface(pad);
		pad->current_surface = surface;
		wlr_tablet_v2_tablet_pad_notify_enter(pad->v2, pad->tablet->v2, surface);
		pad->surface_destroy.notify = on_pad_surface_destroy;
		wl_signal_add(&surface->events.destroy, &pad->surface_destroy);
	}
}

static void pads_reattach(struct sf_server *server) {
	struct sf_tablet_pad *pad;
	wl_list_for_each(pad, &server->tablet.pads, link) {
		pad->tablet = NULL;
	}
	struct sf_tablet *tablet;
	wl_list_for_each(tablet, &server->tablet.tablets, link) {
		if (!wlr_input_device_is_libinput(tablet->device)) {
			continue;
		}
		struct libinput_device *tdev = wlr_libinput_get_device_handle(tablet->device);
		if (tdev == NULL) {
			continue;
		}
		struct libinput_device_group *tgroup = libinput_device_get_device_group(tdev);
		wl_list_for_each(pad, &server->tablet.pads, link) {
			if (!wlr_input_device_is_libinput(pad->device)) {
				continue;
			}
			struct libinput_device *pdev = wlr_libinput_get_device_handle(pad->device);
			if (pdev == NULL) {
				continue;
			}
			if (libinput_device_get_device_group(pdev) == tgroup) {
				pad->tablet = tablet;
			}
		}
	}
	/* Single tablet + pads: attach any ungrouped pads as a fallback. */
	if (!wl_list_empty(&server->tablet.tablets)) {
		struct sf_tablet *only = wl_container_of(server->tablet.tablets.next, only, link);
		bool sole = only->link.next == &server->tablet.tablets;
		if (sole) {
			wl_list_for_each(pad, &server->tablet.pads, link) {
				if (pad->tablet == NULL) {
					pad->tablet = only;
				}
			}
		}
	}
}

static void on_tool_destroy(struct wl_listener *listener, void *data);
static void on_tool_set_cursor(struct wl_listener *listener, void *data);

static struct sf_tablet_tool *tool_ensure(struct sf_server *server,
					  struct wlr_tablet_tool *wlr_tool) {
	struct sf_tablet_tool *tool;
	wl_list_for_each(tool, &server->tablet.tools, link) {
		if (tool->tool == wlr_tool) {
			return tool;
		}
	}
	tool = calloc(1, sizeof(*tool));
	if (tool == NULL) {
		return NULL;
	}
	tool->server = server;
	tool->tool = wlr_tool;
	tool->v2 = wlr_tablet_tool_create(server->tablet.mgr, server->seat, wlr_tool);
	if (tool->v2 == NULL) {
		free(tool);
		return NULL;
	}
	tool->destroy.notify = on_tool_destroy;
	wl_signal_add(&wlr_tool->events.destroy, &tool->destroy);
	tool->set_cursor.notify = on_tool_set_cursor;
	wl_signal_add(&tool->v2->events.set_cursor, &tool->set_cursor);
	wl_list_insert(&server->tablet.tools, &tool->link);
	SF_LOGI("tablet tool", sf_str("pressure", wlr_tool->pressure ? "yes" : "no"),
		sf_str("tilt", wlr_tool->tilt ? "yes" : "no"),
		sf_str("distance", wlr_tool->distance ? "yes" : "no"),
		sf_str("rotation", wlr_tool->rotation ? "yes" : "no"),
		sf_str("slider", wlr_tool->slider ? "yes" : "no"),
		sf_str("wheel", wlr_tool->wheel ? "yes" : "no"));
	return tool;
}

static void on_tool_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_tablet_tool *tool = wl_container_of(listener, tool, destroy);
	wl_list_remove(&tool->destroy.link);
	wl_list_remove(&tool->set_cursor.link);
	wl_list_remove(&tool->link);
	free(tool);
}

static void on_tool_set_cursor(struct wl_listener *listener, void *data) {
	struct sf_tablet_tool *tool = wl_container_of(listener, tool, set_cursor);
	struct wlr_tablet_v2_event_cursor *event = data;
	struct sf_server *server = tool->server;
	if (server->cursor_mode != SF_CURSOR_PASSTHROUGH || tool->emulating) {
		return;
	}
	struct wlr_seat_client *focused = server->seat->pointer_state.focused_client;
	if (tool->v2->focused_surface != NULL) {
		struct wl_client *client =
			wl_resource_get_client(tool->v2->focused_surface->resource);
		if (event->seat_client->client != client) {
			return;
		}
	} else if (focused != NULL && event->seat_client != focused) {
		return;
	}
	wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
}

static void process_tablet_motion(struct sf_server *server, uint32_t time_msec,
				  struct sf_tablet_tool *tool, struct sf_tablet *tablet) {
	double sx = 0, sy = 0;
	struct wlr_surface *surface =
		tablet_surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy);

	if (tablet_should_emulate(server, tablet, tool, surface)) {
		tool_set_emulating(tool, true);
		seat_process_cursor_motion(server, time_msec);
		wlr_seat_pointer_notify_frame(server->seat);
		return;
	}

	if (tool->v2->focused_surface != NULL &&
	    (tool->tip_down || wlr_tablet_tool_v2_has_implicit_grab(tool->v2))) {
		double gsx = sx, gsy = sy;
		if (surface != tool->v2->focused_surface &&
		    !surface_local_coords(server, tool->v2->focused_surface, &gsx, &gsy)) {
			gsx = 0;
			gsy = 0;
		} else if (surface == tool->v2->focused_surface) {
			gsx = sx;
			gsy = sy;
		}
		wlr_tablet_v2_tablet_tool_notify_motion(tool->v2, gsx, gsy);
		return;
	}

	tool_set_emulating(tool, false);
	wlr_tablet_v2_tablet_tool_notify_proximity_in(tool->v2, tablet->v2, surface);
	wlr_tablet_v2_tablet_tool_notify_motion(tool->v2, sx, sy);
	pad_enter_surface(server, surface);
	/* Keep pointer focus off the control under the pen tip. */
	wlr_seat_pointer_notify_motion(server->seat, time_msec, -1, -1);
}

static void on_tablet_tool_axis(struct wl_listener *listener, void *data) {
	struct sf_server *server = wl_container_of(listener, server, tablet.tool_axis);
	struct wlr_tablet_tool_axis_event *event = data;
	sf_session_notify_activity(server);

	struct sf_tablet *tablet = tablet_from_wlr(server, event->tablet);
	struct sf_tablet_tool *tool = tool_ensure(server, event->tool);
	if (tool == NULL) {
		return;
	}

	uint32_t axes = event->updated_axes;
	if ((axes & (uint32_t)WLR_TABLET_TOOL_AXIS_X) != 0) {
		tool->x = event->x;
	}
	if ((axes & (uint32_t)WLR_TABLET_TOOL_AXIS_Y) != 0) {
		tool->y = event->y;
	}

	if (event->tool->type == WLR_TABLET_TOOL_TYPE_MOUSE ||
	    event->tool->type == WLR_TABLET_TOOL_TYPE_LENS) {
		wlr_cursor_move(server->cursor, &event->tablet->base, event->dx, event->dy);
	} else if ((axes & ((uint32_t)WLR_TABLET_TOOL_AXIS_X | (uint32_t)WLR_TABLET_TOOL_AXIS_Y)) !=
		   0) {
		wlr_cursor_warp_absolute(server->cursor, &event->tablet->base, tool->x, tool->y);
	}

	process_tablet_motion(server, event->time_msec, tool, tablet);
	if (tool->emulating) {
		if ((axes & (uint32_t)WLR_TABLET_TOOL_AXIS_WHEEL) != 0) {
			wlr_seat_pointer_notify_axis(
				server->seat, event->time_msec, WL_POINTER_AXIS_VERTICAL_SCROLL,
				event->wheel_delta, event->wheel_delta >= 0 ? 1 : -1,
				WL_POINTER_AXIS_SOURCE_WHEEL,
				WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
			wlr_seat_pointer_notify_frame(server->seat);
		}
		return;
	}

	if ((axes & (uint32_t)WLR_TABLET_TOOL_AXIS_PRESSURE) != 0) {
		wlr_tablet_v2_tablet_tool_notify_pressure(tool->v2, event->pressure);
	}
	if ((axes & (uint32_t)WLR_TABLET_TOOL_AXIS_TILT_X) != 0) {
		tool->tilt_x = event->tilt_x;
	}
	if ((axes & (uint32_t)WLR_TABLET_TOOL_AXIS_TILT_Y) != 0) {
		tool->tilt_y = event->tilt_y;
	}
	if ((axes & ((uint32_t)WLR_TABLET_TOOL_AXIS_TILT_X |
		     (uint32_t)WLR_TABLET_TOOL_AXIS_TILT_Y)) != 0) {
		wlr_tablet_v2_tablet_tool_notify_tilt(tool->v2, tool->tilt_x, tool->tilt_y);
	}
	if ((axes & (uint32_t)WLR_TABLET_TOOL_AXIS_DISTANCE) != 0) {
		wlr_tablet_v2_tablet_tool_notify_distance(tool->v2, event->distance);
	}
	if ((axes & (uint32_t)WLR_TABLET_TOOL_AXIS_ROTATION) != 0) {
		wlr_tablet_v2_tablet_tool_notify_rotation(tool->v2, event->rotation);
	}
	if ((axes & (uint32_t)WLR_TABLET_TOOL_AXIS_SLIDER) != 0) {
		wlr_tablet_v2_tablet_tool_notify_slider(tool->v2, event->slider);
	}
	if ((axes & (uint32_t)WLR_TABLET_TOOL_AXIS_WHEEL) != 0) {
		wlr_tablet_v2_tablet_tool_notify_wheel(tool->v2, event->wheel_delta, 0);
	}
}

static void on_tablet_tool_proximity(struct wl_listener *listener, void *data) {
	struct sf_server *server = wl_container_of(listener, server, tablet.tool_proximity);
	struct wlr_tablet_tool_proximity_event *event = data;
	sf_session_notify_activity(server);

	if (event->state == WLR_TABLET_TOOL_PROXIMITY_IN) {
		struct sf_tablet *tablet = tablet_from_wlr(server, event->tablet);
		struct sf_tablet_tool *tool = tool_ensure(server, event->tool);
		if (tool == NULL) {
			return;
		}
		tool->x = event->x;
		tool->y = event->y;
		wlr_cursor_warp_absolute(server->cursor, &event->tablet->base, event->x, event->y);
		process_tablet_motion(server, event->time_msec, tool, tablet);
		return;
	}

	struct sf_tablet_tool *tool = NULL;
	struct sf_tablet_tool *iter;
	wl_list_for_each(iter, &server->tablet.tools, link) {
		if (iter->tool == event->tool) {
			tool = iter;
			break;
		}
	}
	if (tool == NULL) {
		return;
	}
	if (tool->emulating && tool->tip_down) {
		wlr_seat_pointer_notify_button(server->seat, event->time_msec, BTN_LEFT,
					       WL_POINTER_BUTTON_STATE_RELEASED);
		wlr_seat_pointer_notify_frame(server->seat);
	}
	if (!tool->emulating && tool->v2 != NULL && tool->v2->focused_surface != NULL) {
		wlr_tablet_v2_tablet_tool_notify_proximity_out(tool->v2);
	}
	tool->tip_down = false;
}

static void on_tablet_tool_tip(struct wl_listener *listener, void *data) {
	struct sf_server *server = wl_container_of(listener, server, tablet.tool_tip);
	struct wlr_tablet_tool_tip_event *event = data;
	sf_session_notify_activity(server);

	struct sf_tablet_tool *tool = tool_ensure(server, event->tool);
	if (tool == NULL) {
		return;
	}
	bool down = event->state == WLR_TABLET_TOOL_TIP_DOWN;
	if (tool->emulating) {
		tool->tip_down = down;
		wlr_seat_pointer_notify_button(server->seat, event->time_msec, BTN_LEFT,
					       down ? WL_POINTER_BUTTON_STATE_PRESSED
						    : WL_POINTER_BUTTON_STATE_RELEASED);
		wlr_seat_pointer_notify_frame(server->seat);
		return;
	}
	if (down) {
		tool->tip_down = true;
		double sx = 0, sy = 0;
		struct wlr_surface *surface = NULL;
		struct sf_toplevel *toplevel = sf_desktop_toplevel_at(
			server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
		if (toplevel != NULL) {
			sf_focus_toplevel(toplevel);
		}
		wlr_tablet_v2_tablet_tool_notify_down(tool->v2);
		wlr_tablet_tool_v2_start_implicit_grab(tool->v2);
	} else {
		tool->tip_down = false;
		wlr_tablet_v2_tablet_tool_notify_up(tool->v2);
	}
}

static uint32_t map_stylus_button(uint32_t button) {
	switch (button) {
	case BTN_STYLUS:
		return BTN_RIGHT;
	case BTN_STYLUS2:
		return BTN_MIDDLE;
	case BTN_STYLUS3:
		return BTN_SIDE;
	default:
		return button;
	}
}

static void on_tablet_tool_button(struct wl_listener *listener, void *data) {
	struct sf_server *server = wl_container_of(listener, server, tablet.tool_button);
	struct wlr_tablet_tool_button_event *event = data;
	sf_session_notify_activity(server);

	struct sf_tablet_tool *tool = tool_ensure(server, event->tool);
	if (tool == NULL) {
		return;
	}
	bool pressed = event->state == WLR_BUTTON_PRESSED;
	if (!tool->emulating) {
		wlr_tablet_v2_tablet_tool_notify_button(
			tool->v2, event->button,
			pressed ? ZWP_TABLET_PAD_V2_BUTTON_STATE_PRESSED
				: ZWP_TABLET_PAD_V2_BUTTON_STATE_RELEASED);
		return;
	}
	uint32_t mapped = map_stylus_button(event->button);
	wlr_seat_pointer_notify_button(server->seat, event->time_msec, mapped,
				       pressed ? WL_POINTER_BUTTON_STATE_PRESSED
					       : WL_POINTER_BUTTON_STATE_RELEASED);
	wlr_seat_pointer_notify_frame(server->seat);
}

static void on_tablet_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_tablet *tablet = wl_container_of(listener, tablet, destroy);
	struct sf_server *server = tablet->server;
	wl_list_remove(&tablet->destroy.link);
	wl_list_remove(&tablet->link);
	free(tablet);
	pads_reattach(server);
}

void server_new_tablet(struct sf_server *server, struct wlr_input_device *device) {
	struct sf_tablet *tablet = calloc(1, sizeof(*tablet));
	if (tablet == NULL) {
		return;
	}
	tablet->server = server;
	tablet->device = device;
	tablet->wlr_tablet = wlr_tablet_from_input_device(device);
	tablet->wlr_tablet->data = tablet;
	tablet->v2 = wlr_tablet_create(server->tablet.mgr, server->seat, device);
	wlr_cursor_attach_input_device(server->cursor, device);
	tablet->destroy.notify = on_tablet_destroy;
	wl_signal_add(&device->events.destroy, &tablet->destroy);
	wl_list_insert(&server->tablet.tablets, &tablet->link);
	pads_reattach(server);
	SF_LOGI("tablet", sf_str("name", device->name != NULL ? device->name : ""),
		sf_i64("width_mm", (int64_t)tablet->wlr_tablet->width_mm),
		sf_i64("height_mm", (int64_t)tablet->wlr_tablet->height_mm));
}

static void pad_maybe_mode(struct sf_tablet_pad *pad, unsigned int group, unsigned int mode,
			   uint32_t time_msec) {
	if (pad->v2 == NULL) {
		return;
	}
	if (pad->have_mode && pad->last_mode == mode) {
		return;
	}
	pad->have_mode = true;
	pad->last_mode = mode;
	wlr_tablet_v2_tablet_pad_notify_mode(pad->v2, group, mode, time_msec);
}

static void on_pad_button(struct wl_listener *listener, void *data) {
	struct sf_tablet_pad *pad = wl_container_of(listener, pad, button);
	struct wlr_tablet_pad_button_event *event = data;
	sf_session_notify_activity(pad->server);
	if (pad->v2 == NULL || pad->current_surface == NULL) {
		return;
	}
	pad_maybe_mode(pad, event->group, event->mode, event->time_msec);
	wlr_tablet_v2_tablet_pad_notify_button(pad->v2, event->button, event->time_msec,
					       event->state == WLR_BUTTON_PRESSED
						       ? ZWP_TABLET_PAD_V2_BUTTON_STATE_PRESSED
						       : ZWP_TABLET_PAD_V2_BUTTON_STATE_RELEASED);
}

static void on_pad_ring(struct wl_listener *listener, void *data) {
	struct sf_tablet_pad *pad = wl_container_of(listener, pad, ring);
	struct wlr_tablet_pad_ring_event *event = data;
	sf_session_notify_activity(pad->server);
	if (pad->v2 == NULL || pad->current_surface == NULL) {
		return;
	}
	pad_maybe_mode(pad, 0, event->mode, event->time_msec);
	wlr_tablet_v2_tablet_pad_notify_ring(pad->v2, event->ring, event->position,
					     event->source == WLR_TABLET_PAD_RING_SOURCE_FINGER,
					     event->time_msec);
}

static void on_pad_strip(struct wl_listener *listener, void *data) {
	struct sf_tablet_pad *pad = wl_container_of(listener, pad, strip);
	struct wlr_tablet_pad_strip_event *event = data;
	sf_session_notify_activity(pad->server);
	if (pad->v2 == NULL || pad->current_surface == NULL) {
		return;
	}
	pad_maybe_mode(pad, 0, event->mode, event->time_msec);
	wlr_tablet_v2_tablet_pad_notify_strip(pad->v2, event->strip, event->position,
					      event->source == WLR_TABLET_PAD_STRIP_SOURCE_FINGER,
					      event->time_msec);
}

static void on_pad_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_tablet_pad *pad = wl_container_of(listener, pad, destroy);
	pad_leave_surface(pad);
	wl_list_remove(&pad->button.link);
	wl_list_remove(&pad->ring.link);
	wl_list_remove(&pad->strip.link);
	wl_list_remove(&pad->destroy.link);
	wl_list_remove(&pad->link);
	free(pad);
}

void server_new_tablet_pad(struct sf_server *server, struct wlr_input_device *device) {
	struct sf_tablet_pad *pad = calloc(1, sizeof(*pad));
	if (pad == NULL) {
		return;
	}
	pad->server = server;
	pad->device = device;
	pad->wlr_pad = wlr_tablet_pad_from_input_device(device);
	pad->wlr_pad->data = pad;
	pad->v2 = wlr_tablet_pad_create(server->tablet.mgr, server->seat, device);
	pad->button.notify = on_pad_button;
	wl_signal_add(&pad->wlr_pad->events.button, &pad->button);
	pad->ring.notify = on_pad_ring;
	wl_signal_add(&pad->wlr_pad->events.ring, &pad->ring);
	pad->strip.notify = on_pad_strip;
	wl_signal_add(&pad->wlr_pad->events.strip, &pad->strip);
	pad->destroy.notify = on_pad_destroy;
	wl_signal_add(&device->events.destroy, &pad->destroy);
	pad->surface_destroy.link.next = NULL;
	wl_list_insert(&server->tablet.pads, &pad->link);
	pads_reattach(server);
	SF_LOGI("tablet pad", sf_str("name", device->name != NULL ? device->name : ""),
		sf_i64("buttons", (int64_t)pad->wlr_pad->button_count),
		sf_i64("rings", (int64_t)pad->wlr_pad->ring_count),
		sf_i64("strips", (int64_t)pad->wlr_pad->strip_count));
}

void sf_tablet_setup(struct sf_server *server) {
	wl_list_init(&server->tablet.tablets);
	wl_list_init(&server->tablet.tools);
	wl_list_init(&server->tablet.pads);
	server->tablet.mgr = wlr_tablet_v2_create(server->wl_display);
	if (server->tablet.mgr == NULL) {
		SF_LOGW("tablet-v2 manager unavailable");
		return;
	}
	server->tablet.tool_axis.notify = on_tablet_tool_axis;
	wl_signal_add(&server->cursor->events.tablet_tool_axis, &server->tablet.tool_axis);
	server->tablet.tool_proximity.notify = on_tablet_tool_proximity;
	wl_signal_add(&server->cursor->events.tablet_tool_proximity,
		      &server->tablet.tool_proximity);
	server->tablet.tool_tip.notify = on_tablet_tool_tip;
	wl_signal_add(&server->cursor->events.tablet_tool_tip, &server->tablet.tool_tip);
	server->tablet.tool_button.notify = on_tablet_tool_button;
	wl_signal_add(&server->cursor->events.tablet_tool_button, &server->tablet.tool_button);
	SF_LOGI("tablet-v2");
}

void sf_tablet_finish(struct sf_server *server) {
	listener_clear(&server->tablet.tool_axis);
	listener_clear(&server->tablet.tool_proximity);
	listener_clear(&server->tablet.tool_tip);
	listener_clear(&server->tablet.tool_button);

	struct sf_tablet_tool *tool, *ttmp;
	wl_list_for_each_safe(tool, ttmp, &server->tablet.tools, link) {
		wl_list_remove(&tool->destroy.link);
		wl_list_remove(&tool->set_cursor.link);
		wl_list_remove(&tool->link);
		free(tool);
	}
	struct sf_tablet_pad *pad, *ptmp;
	wl_list_for_each_safe(pad, ptmp, &server->tablet.pads, link) {
		pad_leave_surface(pad);
		wl_list_remove(&pad->button.link);
		wl_list_remove(&pad->ring.link);
		wl_list_remove(&pad->strip.link);
		wl_list_remove(&pad->destroy.link);
		wl_list_remove(&pad->link);
		free(pad);
	}
	struct sf_tablet *tablet, *tabtmp;
	wl_list_for_each_safe(tablet, tabtmp, &server->tablet.tablets, link) {
		wl_list_remove(&tablet->destroy.link);
		wl_list_remove(&tablet->link);
		free(tablet);
	}
	server->tablet.mgr = NULL;
}
