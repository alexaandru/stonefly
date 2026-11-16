#include "seat/seat.h"
#include "seat/internal.h"
#include "seat/input_config.h"

#include <math.h>
#include <stdlib.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_seat.h>

#include "config/config.h"
#include "config/gesture.h"
#include "output/output.h"
#include "session/session.h"
#include "wm/arrange.h"
#include "wm/wm.h"
#include "wm/workspace.h"

static uint32_t seat_mods(struct sf_server *server) {
	struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
	if (kb == NULL) {
		return 0;
	}
	return wlr_keyboard_get_modifiers(kb) & (uint32_t)SF_MOD_MASK;
}

enum {
	SF_SWIPE_AXIS_NONE = 0,
	SF_SWIPE_AXIS_H = 1,
	SF_SWIPE_AXIS_V = 2,
};

void destroy_pointer(struct sf_pointer *pointer) {
	wl_list_remove(&pointer->swipe_begin.link);
	wl_list_remove(&pointer->swipe_update.link);
	wl_list_remove(&pointer->swipe_end.link);
	wl_list_remove(&pointer->pinch_begin.link);
	wl_list_remove(&pointer->pinch_update.link);
	wl_list_remove(&pointer->pinch_end.link);
	wl_list_remove(&pointer->hold_begin.link);
	wl_list_remove(&pointer->hold_end.link);
	wl_list_remove(&pointer->destroy.link);
	wl_list_remove(&pointer->link);
	free(pointer);
}

static void pointer_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_pointer *pointer = wl_container_of(listener, pointer, destroy);
	destroy_pointer(pointer);
}

/* |scale - 1| past this fires a pinch action once (libinput scale units). */
enum {
	SF_PINCH_SCALE_THRESHOLD_X100 = 15
}; /* 0.15 */

static void swipe_reset(struct sf_server *server) {
	server->swipe.active = false;
	server->swipe.consumed = false;
	server->swipe.axis = SF_SWIPE_AXIS_NONE;
	server->swipe.fingers = 0;
	server->swipe.dx = 0;
	server->swipe.dy = 0;
	server->swipe.match_idx = -1;
	server->swipe.stepped = false;
}

static void pinch_reset(struct sf_server *server) {
	server->pinch.active = false;
	server->pinch.consumed = false;
	server->pinch.fingers = 0;
	server->pinch.scale = 1.0;
	server->pinch.match_idx = -1;
	server->pinch.stepped = false;
}

static const struct sf_gesture *swipe_matched(struct sf_server *server) {
	if (server->swipe.match_idx < 0) {
		return NULL;
	}
	size_t idx = (size_t)server->swipe.match_idx;
	if (idx >= sf_cfg(server)->n_gestures) {
		return NULL;
	}
	return &sf_cfg(server)->gestures[idx];
}

static const struct sf_gesture *pinch_matched(struct sf_server *server) {
	if (server->pinch.match_idx < 0) {
		return NULL;
	}
	size_t idx = (size_t)server->pinch.match_idx;
	if (idx >= sf_cfg(server)->n_gestures) {
		return NULL;
	}
	return &sf_cfg(server)->gestures[idx];
}

static void pointer_swipe_begin(struct wl_listener *listener, void *data) {
	struct sf_pointer *pointer = wl_container_of(listener, pointer, swipe_begin);
	struct sf_server *server = pointer->server;
	struct wlr_pointer_swipe_begin_event *event = data;

	server->swipe.active = true;
	server->swipe.consumed = false;
	server->swipe.axis = SF_SWIPE_AXIS_NONE;
	server->swipe.fingers = event->fingers;
	server->swipe.dx = 0;
	server->swipe.dy = 0;
	server->swipe.match_idx = -1;
	server->swipe.stepped = false;

	if (server->pointer_gestures != NULL) {
		wlr_pointer_gestures_v1_send_swipe_begin(server->pointer_gestures, server->seat,
							 event->time_msec, event->fingers);
	}
}

static void pointer_swipe_update(struct wl_listener *listener, void *data) {
	struct sf_pointer *pointer = wl_container_of(listener, pointer, swipe_update);
	struct sf_server *server = pointer->server;
	struct wlr_pointer_swipe_update_event *event = data;

	if (!server->swipe.active) {
		return;
	}
	server->swipe.dx += event->dx;
	server->swipe.dy += event->dy;

	bool eligible = server->cursor_mode == SF_CURSOR_PASSTHROUGH && !sf_session_locked(server);
	double adx = fabs(server->swipe.dx);
	double ady = fabs(server->swipe.dy);
	int threshold = sf_cfg(server)->swipe_workspace_threshold;
	if (threshold < 1) {
		threshold = 80;
	}

	if (eligible && server->swipe.axis == SF_SWIPE_AXIS_NONE) {
		if (adx >= (double)threshold || ady >= (double)threshold) {
			server->swipe.axis = adx > ady ? SF_SWIPE_AXIS_H : SF_SWIPE_AXIS_V;
			enum sf_gesture_dir locked = server->swipe.axis == SF_SWIPE_AXIS_H
							     ? SF_GESTURE_DIR_HORIZONTAL
							     : SF_GESTURE_DIR_VERTICAL;
			double primary = server->swipe.axis == SF_SWIPE_AXIS_H ? server->swipe.dx
									       : server->swipe.dy;
			const struct sf_gesture *g =
				sf_gesture_match(sf_cfg(server), server->swipe.fingers,
						 seat_mods(server), locked, primary);
			if (g != NULL) {
				server->swipe.match_idx = (int)(g - sf_cfg(server)->gestures);
			}
		}
	}

	const struct sf_gesture *g = swipe_matched(server);
	if (eligible && g != NULL && server->swipe.axis == SF_SWIPE_AXIS_H &&
	    g->kind == SF_GESTURE_KIND_COLUMNS_PAN) {
		struct sf_output *output = sf_output_at_cursor(server);
		struct sf_workspace *ws = output != NULL ? output->active : NULL;
		if (ws != NULL && sf_workspace_arrange(ws) == SF_ARRANGE_COLUMNS) {
			double step = event->dx;
			if (!sf_cfg(server)->swipe_workspace_natural) {
				step = -step;
			}
			sf_arrange_columns_pan(ws, step);
			server->swipe.consumed = true;
		} else {
			/* No columns strip — drop the match so clients see the rest. */
			server->swipe.match_idx = -1;
			server->swipe.axis = SF_SWIPE_AXIS_NONE;
		}
	}

	if (eligible && g != NULL && server->swipe.axis == SF_SWIPE_AXIS_V &&
	    g->kind == SF_GESTURE_KIND_COLUMNS_PAN) {
		struct sf_output *output = sf_output_at_cursor(server);
		struct sf_workspace *ws = output != NULL ? output->active : NULL;
		if (ws != NULL && sf_workspace_arrange(ws) == SF_ARRANGE_COLUMNS) {
			double step = event->dy;
			if (!sf_cfg(server)->swipe_workspace_natural) {
				step = -step;
			}
			sf_arrange_columns_pan(ws, step);
			server->swipe.consumed = true;
		} else {
			server->swipe.match_idx = -1;
			server->swipe.axis = SF_SWIPE_AXIS_NONE;
		}
	}

	if (eligible && g != NULL && g->kind == SF_GESTURE_KIND_WORKSPACE &&
	    !server->swipe.stepped) {
		double primary =
			server->swipe.axis == SF_SWIPE_AXIS_H ? server->swipe.dx : server->swipe.dy;
		if (fabs(primary) >= (double)threshold) {
			struct sf_output *output = sf_output_at_cursor(server);
			if (output != NULL) {
				int offset = primary < 0.0 ? -1 : 1;
				if (sf_cfg(server)->swipe_workspace_natural) {
					offset = -offset;
				}
				sf_workspace_switch_offset(output, offset);
			}
			server->swipe.stepped = true;
			server->swipe.consumed = true;
			server->swipe.dx = 0;
			server->swipe.dy = 0;
		}
	}

	if (eligible && g != NULL && g->kind == SF_GESTURE_KIND_ACTION && !server->swipe.stepped) {
		double primary =
			server->swipe.axis == SF_SWIPE_AXIS_H ? server->swipe.dx : server->swipe.dy;
		if (fabs(primary) >= (double)threshold) {
			sf_run_actions(server, NULL, g->steps, g->n_steps);
			server->swipe.stepped = true;
			server->swipe.consumed = true;
			server->swipe.dx = 0;
			server->swipe.dy = 0;
		}
	}

	if (server->pointer_gestures != NULL && !server->swipe.consumed) {
		wlr_pointer_gestures_v1_send_swipe_update(server->pointer_gestures, server->seat,
							  event->time_msec, event->dx, event->dy);
	}
}

static void pointer_swipe_end(struct wl_listener *listener, void *data) {
	struct sf_pointer *pointer = wl_container_of(listener, pointer, swipe_end);
	struct sf_server *server = pointer->server;
	struct wlr_pointer_swipe_end_event *event = data;

	if (server->pointer_gestures != NULL && !server->swipe.consumed) {
		wlr_pointer_gestures_v1_send_swipe_end(server->pointer_gestures, server->seat,
						       event->time_msec, event->cancelled);
	}
	swipe_reset(server);
}

static void pointer_pinch_begin(struct wl_listener *listener, void *data) {
	struct sf_pointer *pointer = wl_container_of(listener, pointer, pinch_begin);
	struct sf_server *server = pointer->server;
	struct wlr_pointer_pinch_begin_event *event = data;

	server->pinch.active = true;
	server->pinch.consumed = false;
	server->pinch.fingers = event->fingers;
	server->pinch.scale = 1.0;
	server->pinch.match_idx = -1;
	server->pinch.stepped = false;

	if (server->pointer_gestures != NULL) {
		wlr_pointer_gestures_v1_send_pinch_begin(server->pointer_gestures, server->seat,
							 event->time_msec, event->fingers);
	}
}

static void pointer_pinch_update(struct wl_listener *listener, void *data) {
	struct sf_pointer *pointer = wl_container_of(listener, pointer, pinch_update);
	struct sf_server *server = pointer->server;
	struct wlr_pointer_pinch_update_event *event = data;

	if (!server->pinch.active) {
		return;
	}
	server->pinch.scale = event->scale;

	bool eligible = server->cursor_mode == SF_CURSOR_PASSTHROUGH && !sf_session_locked(server);
	double delta = fabs(event->scale - 1.0);
	double threshold = (double)SF_PINCH_SCALE_THRESHOLD_X100 / 100.0;

	if (eligible && server->pinch.match_idx < 0 && delta >= threshold) {
		const struct sf_gesture *g = sf_gesture_match_pinch(
			sf_cfg(server), server->pinch.fingers, seat_mods(server), event->scale);
		if (g != NULL) {
			server->pinch.match_idx = (int)(g - sf_cfg(server)->gestures);
		}
	}

	const struct sf_gesture *g = pinch_matched(server);
	if (eligible && g != NULL && !server->pinch.stepped && delta >= threshold) {
		if (g->kind == SF_GESTURE_KIND_ACTION) {
			sf_run_actions(server, NULL, g->steps, g->n_steps);
			server->pinch.stepped = true;
			server->pinch.consumed = true;
		} else {
			/* workspace / columns_pan make no sense on pinch — ignore. */
			server->pinch.match_idx = -1;
		}
	}

	if (server->pointer_gestures != NULL && !server->pinch.consumed) {
		wlr_pointer_gestures_v1_send_pinch_update(server->pointer_gestures, server->seat,
							  event->time_msec, event->dx, event->dy,
							  event->scale, event->rotation);
	}
}

static void pointer_pinch_end(struct wl_listener *listener, void *data) {
	struct sf_pointer *pointer = wl_container_of(listener, pointer, pinch_end);
	struct sf_server *server = pointer->server;
	struct wlr_pointer_pinch_end_event *event = data;

	if (server->pointer_gestures != NULL && !server->pinch.consumed) {
		wlr_pointer_gestures_v1_send_pinch_end(server->pointer_gestures, server->seat,
						       event->time_msec, event->cancelled);
	}
	pinch_reset(server);
}

static void pointer_hold_begin(struct wl_listener *listener, void *data) {
	struct sf_pointer *pointer = wl_container_of(listener, pointer, hold_begin);
	struct sf_server *server = pointer->server;
	struct wlr_pointer_hold_begin_event *event = data;
	if (server->pointer_gestures != NULL) {
		wlr_pointer_gestures_v1_send_hold_begin(server->pointer_gestures, server->seat,
							event->time_msec, event->fingers);
	}
}

static void pointer_hold_end(struct wl_listener *listener, void *data) {
	struct sf_pointer *pointer = wl_container_of(listener, pointer, hold_end);
	struct sf_server *server = pointer->server;
	struct wlr_pointer_hold_end_event *event = data;
	if (server->pointer_gestures != NULL) {
		wlr_pointer_gestures_v1_send_hold_end(server->pointer_gestures, server->seat,
						      event->time_msec, event->cancelled);
	}
}

void server_new_pointer(struct sf_server *server, struct wlr_input_device *device) {
	/* Pointer motion/button go through wlr_cursor; gestures are listened on
	 * the raw wlr_pointer so we can consume configured swipes. */
	wlr_cursor_attach_input_device(server->cursor, device);
	sf_input_apply_device(server, device);

	struct wlr_pointer *wlr_pointer = wlr_pointer_from_input_device(device);
	if (wlr_pointer == NULL) {
		return;
	}
	struct sf_pointer *pointer = calloc(1, sizeof(*pointer));
	if (pointer == NULL) {
		return;
	}
	pointer->server = server;
	pointer->wlr_pointer = wlr_pointer;
	pointer->swipe_begin.notify = pointer_swipe_begin;
	wl_signal_add(&wlr_pointer->events.swipe_begin, &pointer->swipe_begin);
	pointer->swipe_update.notify = pointer_swipe_update;
	wl_signal_add(&wlr_pointer->events.swipe_update, &pointer->swipe_update);
	pointer->swipe_end.notify = pointer_swipe_end;
	wl_signal_add(&wlr_pointer->events.swipe_end, &pointer->swipe_end);
	pointer->pinch_begin.notify = pointer_pinch_begin;
	wl_signal_add(&wlr_pointer->events.pinch_begin, &pointer->pinch_begin);
	pointer->pinch_update.notify = pointer_pinch_update;
	wl_signal_add(&wlr_pointer->events.pinch_update, &pointer->pinch_update);
	pointer->pinch_end.notify = pointer_pinch_end;
	wl_signal_add(&wlr_pointer->events.pinch_end, &pointer->pinch_end);
	pointer->hold_begin.notify = pointer_hold_begin;
	wl_signal_add(&wlr_pointer->events.hold_begin, &pointer->hold_begin);
	pointer->hold_end.notify = pointer_hold_end;
	wl_signal_add(&wlr_pointer->events.hold_end, &pointer->hold_end);
	pointer->destroy.notify = pointer_destroy;
	wl_signal_add(&device->events.destroy, &pointer->destroy);
	wl_list_insert(&server->pointers, &pointer->link);
}
