#include "seat/seat.h"
#include "seat/internal.h"

#include <math.h>
#include <stdlib.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_seat.h>

#include "config/config.h"
#include "output/output.h"
#include "session/session.h"
#include "wm/workspace.h"

void destroy_pointer(struct stonefly_pointer *pointer) {
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
	struct stonefly_pointer *pointer = wl_container_of(listener, pointer, destroy);
	destroy_pointer(pointer);
}

static void pointer_swipe_begin(struct wl_listener *listener, void *data) {
	struct stonefly_pointer *pointer = wl_container_of(listener, pointer, swipe_begin);
	struct stonefly_server *server = pointer->server;
	struct wlr_pointer_swipe_begin_event *event = data;

	server->swipe.active = true;
	server->swipe.consumed = false;
	server->swipe.fingers = event->fingers;
	server->swipe.dx = 0;
	server->swipe.dy = 0;

	if (server->pointer_gestures != NULL) {
		wlr_pointer_gestures_v1_send_swipe_begin(server->pointer_gestures, server->seat,
							 event->time_msec, event->fingers);
	}
}

static void pointer_swipe_update(struct wl_listener *listener, void *data) {
	struct stonefly_pointer *pointer = wl_container_of(listener, pointer, swipe_update);
	struct stonefly_server *server = pointer->server;
	struct wlr_pointer_swipe_update_event *event = data;

	if (!server->swipe.active) {
		return;
	}
	server->swipe.dx += event->dx;
	server->swipe.dy += event->dy;

	bool workspace_swipe = server->swipe.fingers == 3 &&
			       server->cursor_mode == STONEFLY_CURSOR_PASSTHROUGH &&
			       !stonefly_session_locked(server);
	double adx = fabs(server->swipe.dx);
	double ady = fabs(server->swipe.dy);
	int threshold = stonefly_cfg(server)->swipe_workspace_threshold;
	if (threshold < 1) {
		threshold = 80;
	}

	if (workspace_swipe && !server->swipe.consumed && ady > adx && ady >= (double)threshold) {
		struct stonefly_output *output = stonefly_output_at_cursor(server);
		if (output != NULL) {
			/* Classic: finger down → next. Natural (default): content
			 * tracks the finger — finger up → next, finger down → prev. */
			int offset = server->swipe.dy < 0 ? -1 : 1;
			if (stonefly_cfg(server)->swipe_workspace_natural) {
				offset = -offset;
			}
			stonefly_workspace_switch_offset(output, offset);
		}
		server->swipe.consumed = true;
		server->swipe.dx = 0;
		server->swipe.dy = 0;
	}

	if (server->pointer_gestures != NULL && !server->swipe.consumed) {
		wlr_pointer_gestures_v1_send_swipe_update(server->pointer_gestures, server->seat,
							  event->time_msec, event->dx, event->dy);
	}
}

static void pointer_swipe_end(struct wl_listener *listener, void *data) {
	struct stonefly_pointer *pointer = wl_container_of(listener, pointer, swipe_end);
	struct stonefly_server *server = pointer->server;
	struct wlr_pointer_swipe_end_event *event = data;

	if (server->pointer_gestures != NULL && !server->swipe.consumed) {
		wlr_pointer_gestures_v1_send_swipe_end(server->pointer_gestures, server->seat,
						       event->time_msec, event->cancelled);
	}
	server->swipe.active = false;
	server->swipe.consumed = false;
	server->swipe.fingers = 0;
	server->swipe.dx = 0;
	server->swipe.dy = 0;
}

static void pointer_pinch_begin(struct wl_listener *listener, void *data) {
	struct stonefly_pointer *pointer = wl_container_of(listener, pointer, pinch_begin);
	struct stonefly_server *server = pointer->server;
	struct wlr_pointer_pinch_begin_event *event = data;
	if (server->pointer_gestures != NULL) {
		wlr_pointer_gestures_v1_send_pinch_begin(server->pointer_gestures, server->seat,
							 event->time_msec, event->fingers);
	}
}

static void pointer_pinch_update(struct wl_listener *listener, void *data) {
	struct stonefly_pointer *pointer = wl_container_of(listener, pointer, pinch_update);
	struct stonefly_server *server = pointer->server;
	struct wlr_pointer_pinch_update_event *event = data;
	if (server->pointer_gestures != NULL) {
		wlr_pointer_gestures_v1_send_pinch_update(server->pointer_gestures, server->seat,
							  event->time_msec, event->dx, event->dy,
							  event->scale, event->rotation);
	}
}

static void pointer_pinch_end(struct wl_listener *listener, void *data) {
	struct stonefly_pointer *pointer = wl_container_of(listener, pointer, pinch_end);
	struct stonefly_server *server = pointer->server;
	struct wlr_pointer_pinch_end_event *event = data;
	if (server->pointer_gestures != NULL) {
		wlr_pointer_gestures_v1_send_pinch_end(server->pointer_gestures, server->seat,
						       event->time_msec, event->cancelled);
	}
}

static void pointer_hold_begin(struct wl_listener *listener, void *data) {
	struct stonefly_pointer *pointer = wl_container_of(listener, pointer, hold_begin);
	struct stonefly_server *server = pointer->server;
	struct wlr_pointer_hold_begin_event *event = data;
	if (server->pointer_gestures != NULL) {
		wlr_pointer_gestures_v1_send_hold_begin(server->pointer_gestures, server->seat,
							event->time_msec, event->fingers);
	}
}

static void pointer_hold_end(struct wl_listener *listener, void *data) {
	struct stonefly_pointer *pointer = wl_container_of(listener, pointer, hold_end);
	struct stonefly_server *server = pointer->server;
	struct wlr_pointer_hold_end_event *event = data;
	if (server->pointer_gestures != NULL) {
		wlr_pointer_gestures_v1_send_hold_end(server->pointer_gestures, server->seat,
						      event->time_msec, event->cancelled);
	}
}

void server_new_pointer(struct stonefly_server *server, struct wlr_input_device *device) {
	/* Pointer motion/button go through wlr_cursor; gestures are listened on
	 * the raw wlr_pointer so we can consume 3-finger vertical swipes. */
	wlr_cursor_attach_input_device(server->cursor, device);

	struct wlr_pointer *wlr_pointer = wlr_pointer_from_input_device(device);
	if (wlr_pointer == NULL) {
		return;
	}
	struct stonefly_pointer *pointer = calloc(1, sizeof(*pointer));
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
