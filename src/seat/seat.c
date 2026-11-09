#include "seat/seat.h"
#include "seat/internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <wlr/backend.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>

#include "config/config.h"
#include "log/log.h"
#include "output/output.h"
#include "wm/wm.h"

void seat_update_capabilities(struct sf_server *server) {
	/* Always advertise pointer so the cursor theme works without a mouse. */
	uint32_t caps = (uint32_t)WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= (uint32_t)WL_SEAT_CAPABILITY_KEYBOARD;
	}
	if (!wl_list_empty(&server->touch)) {
		caps |= (uint32_t)WL_SEAT_CAPABILITY_TOUCH;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

static void server_new_input(struct wl_listener *listener, void *data) {
	/* This event is raised by the backend when a new input device becomes
	 * available. */
	struct sf_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		server_new_keyboard(server, device, false);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		server_new_pointer(server, device);
		break;
	case WLR_INPUT_DEVICE_TOUCH:
		server_new_touch(server, device);
		break;
	default:
		break;
	}
	seat_update_capabilities(server);
}

static void seat_request_set_selection(struct wl_listener *listener, void *data) {
	/* This event is raised by the seat when a client wants to set the selection,
	 * usually when the user copies something. wlroots allows compositors to
	 * ignore such requests if they so choose, but in sf we always honor
	 */
	struct sf_server *server = wl_container_of(listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static void seat_request_set_primary_selection(struct wl_listener *listener, void *data) {
	struct sf_server *server = wl_container_of(listener, server, request_set_primary_selection);
	struct wlr_seat_request_set_primary_selection_event *event = data;
	wlr_seat_set_primary_selection(server->seat, event->source, event->serial);
}

void sf_seat_setup(struct sf_server *server) {
	server->cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server->cursor, server->output_layout);

	/* setenv may invalidate getenv pointers; copy before mutating env. */
	char *theme = cursor_theme_dup(server);
	uint32_t size = cursor_size_for_server(server);
	/* Keep clients spawned after seat setup on the same logical size. */
	char sizebuf[16];
	snprintf(sizebuf, sizeof(sizebuf), "%u", size);
	setenv("XCURSOR_SIZE", sizebuf, 1);
	if (theme != NULL) {
		setenv("XCURSOR_THEME", theme, 1);
	}
	server->cursor_mgr = wlr_xcursor_manager_create(theme, size);
	wlr_xcursor_manager_load(server->cursor_mgr, sf_output_scale_factor(server));
	SF_LOGI("cursor theme", sf_str("theme", theme != NULL ? theme : ""), sf_i64("size", size),
		sf_bool("nested", server->session == NULL));
	free(theme);

	server->cursor_mode = SF_CURSOR_PASSTHROUGH;
	server->cursor_motion.notify = server_cursor_motion;
	wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);
	server->cursor_motion_absolute.notify = server_cursor_motion_absolute;
	wl_signal_add(&server->cursor->events.motion_absolute, &server->cursor_motion_absolute);
	server->cursor_button.notify = server_cursor_button;
	wl_signal_add(&server->cursor->events.button, &server->cursor_button);
	server->cursor_axis.notify = server_cursor_axis;
	wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);
	server->cursor_frame.notify = server_cursor_frame;
	wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);
	server->cursor_touch_down.notify = server_cursor_touch_down;
	wl_signal_add(&server->cursor->events.touch_down, &server->cursor_touch_down);
	server->cursor_touch_up.notify = server_cursor_touch_up;
	wl_signal_add(&server->cursor->events.touch_up, &server->cursor_touch_up);
	server->cursor_touch_motion.notify = server_cursor_touch_motion;
	wl_signal_add(&server->cursor->events.touch_motion, &server->cursor_touch_motion);
	server->cursor_touch_cancel.notify = server_cursor_touch_cancel;
	wl_signal_add(&server->cursor->events.touch_cancel, &server->cursor_touch_cancel);
	server->cursor_touch_frame.notify = server_cursor_touch_frame;
	wl_signal_add(&server->cursor->events.touch_frame, &server->cursor_touch_frame);

	wl_list_init(&server->keyboards);
	wl_list_init(&server->pointers);
	wl_list_init(&server->touch);
	server->pointer_gestures = wlr_pointer_gestures_v1_create(server->wl_display);
	server->relative_pointer_mgr = wlr_relative_pointer_manager_v1_create(server->wl_display);
	server->constraints.mgr = wlr_pointer_constraints_v1_create(server->wl_display);
	server->constraints.current = NULL;
	server->constraints.on_new.notify = server_new_pointer_constraint;
	wl_signal_add(&server->constraints.mgr->events.new_constraint, &server->constraints.on_new);

	server->virtual_keyboard_mgr = wlr_virtual_keyboard_manager_v1_create(server->wl_display);
	server->new_virtual_keyboard.notify = server_new_virtual_keyboard;
	wl_signal_add(&server->virtual_keyboard_mgr->events.new_virtual_keyboard,
		      &server->new_virtual_keyboard);

	server->new_input.notify = server_new_input;
	wl_signal_add(&server->backend->events.new_input, &server->new_input);
	server->seat = wlr_seat_create(server->wl_display, "seat0");
	server->request_cursor.notify = seat_request_cursor;
	wl_signal_add(&server->seat->events.request_set_cursor, &server->request_cursor);
	/* v2 matches River/peeble; wlroots 0.20 caps at CURSOR_SHAPE_MANAGER_V1_VERSION. */
	server->cursor_shape_mgr = wlr_cursor_shape_manager_v1_create(server->wl_display, 2);
	server->request_set_cursor_shape.notify = seat_request_set_cursor_shape;
	wl_signal_add(&server->cursor_shape_mgr->events.request_set_shape,
		      &server->request_set_cursor_shape);
	server->pointer_focus_change.notify = seat_pointer_focus_change;
	wl_signal_add(&server->seat->pointer_state.events.focus_change,
		      &server->pointer_focus_change);
	server->request_set_selection.notify = seat_request_set_selection;
	wl_signal_add(&server->seat->events.request_set_selection, &server->request_set_selection);
	server->request_set_primary_selection.notify = seat_request_set_primary_selection;
	wl_signal_add(&server->seat->events.request_set_primary_selection,
		      &server->request_set_primary_selection);

	server->tap.armed = false;
	server->tap.taps = 0;
	server->tap.timer = NULL;
	server->tap.single_steps = NULL;
	server->tap.n_single_steps = 0;
	server->tap.double_steps = NULL;
	server->tap.n_double_steps = 0;
}

void sf_seat_finish(struct sf_server *server) {
	tap_clear_timer(server);
	server->tap.armed = false;
	server->tap.taps = 0;
	struct sf_pointer *pointer, *tmp;
	wl_list_for_each_safe(pointer, tmp, &server->pointers, link) {
		destroy_pointer(pointer);
	}
	struct sf_touch *touch, *ttmp;
	wl_list_for_each_safe(touch, ttmp, &server->touch, link) {
		destroy_touch(touch);
	}
	if (server->constraints.current != NULL) {
		server->constraints.current = NULL;
	}
	if (server->virtual_keyboard_mgr != NULL) {
		wl_list_remove(&server->new_virtual_keyboard.link);
		server->virtual_keyboard_mgr = NULL;
	}
	if (server->constraints.mgr != NULL) {
		wl_list_remove(&server->constraints.on_new.link);
		server->constraints.mgr = NULL;
	}
	server->pointer_gestures = NULL;
	server->relative_pointer_mgr = NULL;
}
