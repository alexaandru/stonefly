#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <linux/input-event-codes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/box.h>
#include <wlr/util/edges.h>
#include <wlr/util/region.h>
#include <xkbcommon/xkbcommon.h>
#include <math.h>

#include "config/config.h"
#include "log/log.h"
#include "output/output.h"
#include "seat/seat.h"
#include "session/session.h"
#include "ssd/ssd.h"
#include "wm/drag_snap.h"
#include "wm/snap_preview.h"
#include "wm/wm.h"
#include "wm/workspace.h"
#include "xdg/xdg.h"

struct stonefly_pointer {
	struct wl_list link;
	struct stonefly_server *server;
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

struct stonefly_touch {
	struct wl_list link;
	struct stonefly_server *server;
	struct wl_listener destroy;
};

struct stonefly_constraint {
	struct stonefly_server *server;
	struct wlr_pointer_constraint_v1 *constraint;
	struct wl_listener destroy;
};

static void seat_update_capabilities(struct stonefly_server *server);
static void stonefly_constraint_activate(struct stonefly_server *server,
					 struct wlr_pointer_constraint_v1 *constraint);
static void stonefly_constraint_deactivate(struct stonefly_server *server, bool warp_hint);

static int64_t timespec_ms_since(const struct timespec *then) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (int64_t)(now.tv_sec - then->tv_sec) * 1000 +
	       (now.tv_nsec - then->tv_nsec) / 1000000;
}

static const struct stonefly_tapbind *tapbind_for_sym(struct stonefly_server *server,
						      xkb_keysym_t sym) {
	const struct stonefly_config *cfg = stonefly_cfg(server);
	for (size_t i = 0; i < cfg->n_tapbinds; i++) {
		const struct stonefly_tapbind *b = &cfg->tapbinds[i];
		if (sym == b->sym || (b->sym_alt != 0 && sym == b->sym_alt)) {
			return b;
		}
	}
	return NULL;
}

void stonefly_tap_disarm(struct stonefly_server *server) {
	server->tap.armed = false;
}

static void tap_clear_timer(struct stonefly_server *server) {
	if (server->tap.timer != NULL) {
		wl_event_source_remove(server->tap.timer);
		server->tap.timer = NULL;
	}
}

static int tap_double_timer(void *data) {
	struct stonefly_server *server = data;
	server->tap.timer = NULL;
	if (server->tap.taps != 1) {
		return 0;
	}
	enum stonefly_action single = (enum stonefly_action)server->tap.single;
	const char *cmd = server->tap.single_cmd;
	server->tap.taps = 0;
	if (single != STONEFLY_ACTION_NONE) {
		stonefly_run_action(server, single, cmd);
	}
	return 0;
}

static void tap_finish(struct stonefly_server *server, enum stonefly_action action,
		       const char *cmd) {
	tap_clear_timer(server);
	server->tap.taps = 0;
	if (action != STONEFLY_ACTION_NONE) {
		stonefly_run_action(server, action, cmd);
	}
}

static void tap_on_press(struct stonefly_server *server, const struct stonefly_tapbind *bind) {
	server->tap.armed = true;
	clock_gettime(CLOCK_MONOTONIC, &server->tap.since);
	server->tap.single = bind->single;
	server->tap.double_tap = bind->double_tap;
	/* Config lives for process lifetime; pointers into cfg are stable. */
	server->tap.single_cmd = bind->single_cmd;
	server->tap.double_cmd = bind->double_cmd;
}

static void tap_on_release(struct stonefly_server *server) {
	bool armed = server->tap.armed;
	server->tap.armed = false;
	if (!armed) {
		return;
	}
	const struct stonefly_config *cfg = stonefly_cfg(server);
	if (timespec_ms_since(&server->tap.since) > cfg->tap_max_hold_ms) {
		return;
	}

	if (server->tap.double_tap == STONEFLY_ACTION_NONE) {
		tap_finish(server, (enum stonefly_action)server->tap.single,
			   server->tap.single_cmd);
		return;
	}

	server->tap.taps++;
	if (server->tap.taps >= 2) {
		tap_finish(server, (enum stonefly_action)server->tap.double_tap,
			   server->tap.double_cmd);
		return;
	}

	/* First tap: wait for a second before firing single. */
	tap_clear_timer(server);
	struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
	server->tap.timer = wl_event_loop_add_timer(loop, tap_double_timer, server);
	wl_event_source_timer_update(server->tap.timer, cfg->double_tap_gap_ms);
}

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

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data) {
	(void)data;
	/* This event is raised when a modifier key, such as shift or alt, is
	 * pressed. We simply communicate this to the client. */
	struct stonefly_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
	/*
	 * A seat can only have one keyboard, but this is a limitation of the
	 * Wayland protocol - not wlroots. We assign all connected keyboards to the
	 * same seat. You can swap out the underlying wlr_keyboard like this and
	 * wlr_seat handles this transparently.
	 */
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
	/* Send modifiers to the client. */
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
					   &keyboard->wlr_keyboard->modifiers);
}

static bool handle_vt_switch(struct stonefly_server *server, xkb_keysym_t sym) {
	/* Ctrl+Alt+Fn is mapped by xkb to XF86Switch_VT_*; the compositor must
	 * call wlr_session_change_vt or the switch is unreliable / can yank DRM
	 * mid-frame (seen as mass unmap + death). */
	if (sym < XKB_KEY_XF86Switch_VT_1 || sym > XKB_KEY_XF86Switch_VT_12) {
		return false;
	}
	if (server->session == NULL) {
		return true; /* consume; nested backend has no VTs */
	}
	unsigned vt = (unsigned)(sym - XKB_KEY_XF86Switch_VT_1) + 1;
	SF_LOGI("session change_vt", sf_i64("vt", (int64_t)vt));
	wlr_session_change_vt(server->session, vt);
	return true;
}

static bool handle_keybinding(struct stonefly_server *server, uint32_t modifiers,
			      xkb_keysym_t sym) {
	if (handle_vt_switch(server, sym)) {
		return true;
	}
	/* Session lock owns input; only VT switch escapes. */
	if (stonefly_session_locked(server)) {
		return false;
	}
	uint32_t mods = modifiers & (uint32_t)STONEFLY_MOD_MASK;
	const struct stonefly_config *cfg = stonefly_cfg(server);
	for (size_t i = 0; i < cfg->n_keybinds; i++) {
		const struct stonefly_keybind *bind = &cfg->keybinds[i];
		if (bind->mods != mods) {
			continue;
		}
		if (sym != bind->sym && (bind->sym_alt == 0 || sym != bind->sym_alt)) {
			continue;
		}
		stonefly_tap_disarm(server);
		stonefly_run_action(server, bind->action, bind->cmd);
		return true;
	}
	return false;
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
	/* This event is raised when a key is pressed or released. */
	struct stonefly_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct stonefly_server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = data;
	struct wlr_seat *seat = server->seat;

	stonefly_session_notify_activity(server);

	/* Translate libinput keycode -> xkbcommon */
	uint32_t keycode = event->keycode + 8;
	/* Get a list of keysyms based on the keymap for this keyboard */
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);

	bool handled = false;
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
	bool locked = stonefly_session_locked(server);

	/* Virtual keyboards skip compositor binds (untrusted injectors). */
	if (!keyboard->is_virtual) {
		if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
			for (int i = 0; i < nsyms; i++) {
				if (!locked) {
					const struct stonefly_tapbind *tap =
						tapbind_for_sym(server, syms[i]);
					if (tap != NULL) {
						tap_on_press(server, tap);
						handled = true;
						break;
					}
					/* Other key while tap armed → chord, not a tap. */
					if (server->tap.armed) {
						stonefly_tap_disarm(server);
					}
				}
				if (handle_keybinding(server, modifiers, syms[i])) {
					handled = true;
					break;
				}
			}
		} else if (event->state == WL_KEYBOARD_KEY_STATE_RELEASED && !locked) {
			for (int i = 0; i < nsyms; i++) {
				if (tapbind_for_sym(server, syms[i]) != NULL) {
					tap_on_release(server);
					handled = true;
					break;
				}
			}
		}
	}

	if (!handled) {
		/* Otherwise, we pass it along to the client (lock surface when locked). */
		wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
		wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
	}
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	/* This event is raised by the keyboard base wlr_input_device to signal
	 * the destruction of the wlr_keyboard. It will no longer receive events
	 * and should be destroyed.
	 */
	struct stonefly_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
	struct stonefly_server *server = keyboard->server;
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
	seat_update_capabilities(server);
}

static void server_new_keyboard(struct stonefly_server *server, struct wlr_input_device *device,
				bool is_virtual) {
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

	struct stonefly_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	if (keyboard == NULL) {
		return;
	}
	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;
	keyboard->is_virtual = is_virtual;

	/* Physical keyboards get a default XKB map; virtual ones wait for the
	 * client keymap from zwp_virtual_keyboard_v1. */
	if (!is_virtual) {
		struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
		struct xkb_keymap *keymap =
			xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);

		wlr_keyboard_set_keymap(wlr_keyboard, keymap);
		xkb_keymap_unref(keymap);
		xkb_context_unref(context);
	}
	wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

	keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
	keyboard->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);

	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);

	wl_list_insert(&server->keyboards, &keyboard->link);
}

static void server_new_virtual_keyboard(struct wl_listener *listener, void *data) {
	struct stonefly_server *server = wl_container_of(listener, server, new_virtual_keyboard);
	struct wlr_virtual_keyboard_v1 *virtual_keyboard = data;
	/* wlroots 0.20: keyboard is a wlr_keyboard; device is keyboard.base. */
	server_new_keyboard(server, &virtual_keyboard->keyboard.base, true);
	seat_update_capabilities(server);
}

static void destroy_pointer(struct stonefly_pointer *pointer) {
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

static void server_new_pointer(struct stonefly_server *server, struct wlr_input_device *device) {
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

static void destroy_touch(struct stonefly_touch *touch) {
	struct stonefly_server *server = touch->server;
	wl_list_remove(&touch->destroy.link);
	wl_list_remove(&touch->link);
	free(touch);
	seat_update_capabilities(server);
}

static void touch_handle_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct stonefly_touch *touch = wl_container_of(listener, touch, destroy);
	destroy_touch(touch);
}

static void server_new_touch(struct stonefly_server *server, struct wlr_input_device *device) {
	wlr_cursor_attach_input_device(server->cursor, device);

	struct stonefly_touch *touch = calloc(1, sizeof(*touch));
	if (touch == NULL) {
		return;
	}
	touch->server = server;
	touch->destroy.notify = touch_handle_destroy;
	wl_signal_add(&device->events.destroy, &touch->destroy);
	wl_list_insert(&server->touch, &touch->link);
}

static void seat_update_capabilities(struct stonefly_server *server) {
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
	struct stonefly_server *server = wl_container_of(listener, server, new_input);
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

static void seat_request_cursor(struct wl_listener *listener, void *data) {
	struct stonefly_server *server = wl_container_of(listener, server, request_cursor);
	/* This event is raised by the seat when a client provides a cursor image */
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client = server->seat->pointer_state.focused_client;
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
static void seat_request_set_cursor_shape(struct wl_listener *listener, void *data) {
	struct stonefly_server *server =
		wl_container_of(listener, server, request_set_cursor_shape);
	struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;

	if (server->cursor_mode != STONEFLY_CURSOR_PASSTHROUGH) {
		return;
	}
	if (event->seat_client != server->seat->pointer_state.focused_client) {
		return;
	}
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr,
			       wlr_cursor_shape_v1_name(event->shape));
}

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
	if (server->current_constraint == NULL) {
		return;
	}
	if (warp_hint) {
		warp_to_constraint_hint(server, server->current_constraint);
	}
	wlr_pointer_constraint_v1_send_deactivated(server->current_constraint);
	server->current_constraint = NULL;
}

static void stonefly_constraint_activate(struct stonefly_server *server,
					 struct wlr_pointer_constraint_v1 *constraint) {
	if (server->current_constraint == constraint) {
		return;
	}
	if (server->current_constraint != NULL) {
		stonefly_constraint_deactivate(server, constraint == NULL);
	}
	server->current_constraint = constraint;
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
	if (server->current_constraint == constraint) {
		warp_to_constraint_hint(server, constraint);
		server->current_constraint = NULL;
	}
	free(c);
}

static void server_new_pointer_constraint(struct wl_listener *listener, void *data) {
	struct stonefly_server *server = wl_container_of(listener, server, new_pointer_constraint);
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

static void apply_pointer_constraint(struct stonefly_server *server, double *dx, double *dy) {
	struct wlr_pointer_constraint_v1 *constraint = server->current_constraint;
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

static void seat_pointer_focus_change(struct wl_listener *listener, void *data) {
	struct stonefly_server *server = wl_container_of(listener, server, pointer_focus_change);
	/* This event is raised when the pointer focus is changed, including when the
	 * client is closed. We set the cursor image to its default if target surface
	 * is NULL */
	struct wlr_seat_pointer_focus_change_event *event = data;
	if (event->new_surface == NULL) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	}

	/* Activate constraint for the focused surface; deactivate on leave. */
	if (server->pointer_constraints == NULL) {
		return;
	}
	struct wlr_pointer_constraint_v1 *constraint = NULL;
	if (event->new_surface != NULL) {
		constraint = wlr_pointer_constraints_v1_constraint_for_surface(
			server->pointer_constraints, event->new_surface, server->seat);
	}
	if (server->current_constraint != constraint) {
		stonefly_constraint_activate(server, constraint);
	}
}

static void seat_request_set_selection(struct wl_listener *listener, void *data) {
	/* This event is raised by the seat when a client wants to set the selection,
	 * usually when the user copies something. wlroots allows compositors to
	 * ignore such requests if they so choose, but in stonefly we always honor
	 */
	struct stonefly_server *server = wl_container_of(listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static void seat_request_set_primary_selection(struct wl_listener *listener, void *data) {
	struct stonefly_server *server =
		wl_container_of(listener, server, request_set_primary_selection);
	struct wlr_seat_request_set_primary_selection_event *event = data;
	wlr_seat_set_primary_selection(server->seat, event->source, event->serial);
}

struct stonefly_toplevel *stonefly_desktop_toplevel_at(struct stonefly_server *server, double lx,
						       double ly, struct wlr_surface **surface,
						       double *sx, double *sy) {
	/* Topmost buffer at layout coords. Client surfaces or compositor SSD. */
	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
	if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}
	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);

	/* SSD titlebar: no wlr_surface; node.data is stonefly_ssd. */
	struct stonefly_toplevel *ssd_tl = NULL;
	if (stonefly_ssd_node_is_titlebar(node, &ssd_tl)) {
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
	enum stonefly_node_kind kind = *(enum stonefly_node_kind *)tree->node.data;
	if (kind != STONEFLY_NODE_TOPLEVEL) {
		return NULL;
	}
	return tree->node.data;
}

void stonefly_reset_cursor_mode(struct stonefly_server *server) {
	/* Reset the cursor mode to passthrough. */
	if (server->drag_snap.active) {
		stonefly_snap_preview_hide(server);
		stonefly_drag_snap_reset(&server->drag_snap);
	}
	server->cursor_mode = STONEFLY_CURSOR_PASSTHROUGH;
	server->grabbed_toplevel = NULL;
}

static void process_cursor_move(struct stonefly_server *server) {
	/* Magnetic resistance + snap preview (peeble Move.Render). */
	stonefly_drag_snap_motion(server);
}

static void process_cursor_resize(struct stonefly_server *server) {
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
	struct stonefly_toplevel *toplevel = server->grabbed_toplevel;
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

	struct wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;
	int x = new_left - geo_box->x;
	int y = new_top - geo_box->y;
	wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y);
	toplevel->resting_x = x;
	toplevel->resting_y = y;

	int new_width = new_right - new_left;
	int new_height = new_bottom - new_top;
	wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, new_width, new_height);
}

static void process_cursor_motion(struct stonefly_server *server, uint32_t time) {
	/* If the mode is non-passthrough, delegate to those functions. */
	if (server->cursor_mode == STONEFLY_CURSOR_MOVE) {
		process_cursor_move(server);
		return;
	} else if (server->cursor_mode == STONEFLY_CURSOR_RESIZE) {
		process_cursor_resize(server);
		return;
	}

	/* Otherwise, find the toplevel under the pointer and send the event along.
	 * When locked, lock surfaces / blank sit above the rest of the scene. */
	double sx, sy;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *surface = NULL;
	struct stonefly_toplevel *toplevel = stonefly_desktop_toplevel_at(
		server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	(void)toplevel;
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

static void server_cursor_motion(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits a _relative_
	 * pointer motion event (i.e. a delta) */
	struct stonefly_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	stonefly_session_notify_activity(server);

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
	} else if (server->current_constraint != NULL &&
		   server->current_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
		/* Locked: cursor stays put; clients still got relative motion. */
	} else {
		process_cursor_motion(server, event->time_msec);
	}
}

static void server_cursor_motion_absolute(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits an _absolute_
	 * motion event, from 0..1 on each axis. This happens, for example, when
	 * wlroots is running under a Wayland window rather than KMS+DRM, and you
	 * move the mouse over the window. You could enter the window from any edge,
	 * so we have to warp the mouse there. There is also some hardware which
	 * emits these events. */
	struct stonefly_server *server = wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	stonefly_session_notify_activity(server);

	double lx, ly;
	wlr_cursor_absolute_to_layout_coords(server->cursor, &event->pointer->base, event->x,
					     event->y, &lx, &ly);
	double dx = lx - server->cursor->x;
	double dy = ly - server->cursor->y;

	if (server->current_constraint != NULL &&
	    server->current_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
		/* Absolute motion ignored while locked (relative path still works). */
		return;
	}
	apply_pointer_constraint(server, &dx, &dy);
	wlr_cursor_move(server->cursor, &event->pointer->base, dx, dy);
	process_cursor_motion(server, event->time_msec);
}

static uint32_t seat_keyboard_mods(struct stonefly_server *server) {
	struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
	if (kb == NULL) {
		return 0;
	}
	return wlr_keyboard_get_modifiers(kb);
}

/* Resize from the edges nearest the pointer (sway/i3-style). */
static uint32_t resize_edges_at(struct stonefly_toplevel *toplevel, double lx, double ly) {
	struct wlr_box geom;
	stonefly_toplevel_layout_geom(toplevel, &geom);
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

static void server_cursor_button(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits a button
	 * event. */
	struct stonefly_server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;

	stonefly_session_notify_activity(server);

	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
		bool grabbing = server->cursor_mode != STONEFLY_CURSOR_PASSTHROUGH;
		if (server->cursor_mode == STONEFLY_CURSOR_MOVE) {
			stonefly_drag_snap_finish(server);
		}
		stonefly_reset_cursor_mode(server);
		/* Titlebar-initiated move: drop pending button state. */
		if (grabbing) {
			stonefly_ssd_pointer_reset();
		}
		if (!grabbing && !stonefly_session_locked(server)) {
			double sx, sy;
			struct wlr_surface *surface = NULL;
			struct stonefly_toplevel *toplevel = stonefly_desktop_toplevel_at(
				server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
			if (toplevel != NULL && surface == NULL && toplevel->ssd != NULL &&
			    stonefly_ssd_pointer_button(server, toplevel, sx, sy, event)) {
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

	/* Super+LMB move, Super+RMB resize — do not forward the press. */
	if (!stonefly_session_locked(server)) {
		uint32_t mods = seat_keyboard_mods(server) & (uint32_t)STONEFLY_MOD_MASK;
		if (mods == STONEFLY_SUPER &&
		    (event->button == BTN_LEFT || event->button == BTN_RIGHT)) {
			stonefly_tap_disarm(server);
			double sx, sy;
			struct wlr_surface *surface = NULL;
			struct stonefly_toplevel *toplevel = stonefly_desktop_toplevel_at(
				server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
			if (toplevel != NULL) {
				stonefly_focus_toplevel(toplevel);
				if (event->button == BTN_LEFT) {
					stonefly_begin_interactive(toplevel, STONEFLY_CURSOR_MOVE,
								   0);
				} else {
					stonefly_begin_interactive(
						toplevel, STONEFLY_CURSOR_RESIZE,
						resize_edges_at(toplevel, server->cursor->x,
								server->cursor->y));
				}
				return;
			}
		}
	}

	if (!stonefly_session_locked(server)) {
		double sx, sy;
		struct wlr_surface *surface = NULL;
		struct stonefly_toplevel *toplevel = stonefly_desktop_toplevel_at(
			server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
		/* SSD titlebar: no client surface; consume button locally. */
		if (toplevel != NULL && surface == NULL && toplevel->ssd != NULL &&
		    stonefly_ssd_pointer_button(server, toplevel, sx, sy, event)) {
			return;
		}
		wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button,
					       event->state);
		stonefly_focus_toplevel(toplevel);
		return;
	}

	wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
}

static void server_cursor_axis(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits an axis event,
	 * for example when you move the scroll wheel. */
	struct stonefly_server *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;
	stonefly_session_notify_activity(server);
	/* Notify the client with pointer focus of the axis event. */
	wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation,
				     event->delta, event->delta_discrete, event->source,
				     event->relative_direction);
}

static void server_cursor_frame(struct wl_listener *listener, void *data) {
	(void)data;
	/* This event is forwarded by the cursor when a pointer emits an frame
	 * event. Frame events are sent after regular pointer events to group
	 * multiple events together. For instance, two axis events may happen at the
	 * same time, in which case a frame event won't be sent in between. */
	struct stonefly_server *server = wl_container_of(listener, server, cursor_frame);
	/* Notify the client with pointer focus of the frame event. */
	wlr_seat_pointer_notify_frame(server->seat);
}

static void server_cursor_touch_down(struct wl_listener *listener, void *data) {
	struct stonefly_server *server = wl_container_of(listener, server, cursor_touch_down);
	struct wlr_touch_down_event *event = data;
	stonefly_session_notify_activity(server);

	double lx, ly;
	wlr_cursor_absolute_to_layout_coords(server->cursor, &event->touch->base, event->x,
					     event->y, &lx, &ly);

	double sx, sy;
	struct wlr_surface *surface = NULL;
	struct stonefly_toplevel *toplevel =
		stonefly_desktop_toplevel_at(server, lx, ly, &surface, &sx, &sy);
	if (surface == NULL) {
		return;
	}
	wlr_seat_touch_notify_down(server->seat, surface, event->time_msec, event->touch_id, sx,
				   sy);
	if (toplevel != NULL && !stonefly_session_locked(server)) {
		stonefly_focus_toplevel(toplevel);
	}
}

static void server_cursor_touch_up(struct wl_listener *listener, void *data) {
	struct stonefly_server *server = wl_container_of(listener, server, cursor_touch_up);
	struct wlr_touch_up_event *event = data;
	stonefly_session_notify_activity(server);
	if (wlr_seat_touch_get_point(server->seat, event->touch_id) == NULL) {
		return;
	}
	wlr_seat_touch_notify_up(server->seat, event->time_msec, event->touch_id);
}

static void server_cursor_touch_motion(struct wl_listener *listener, void *data) {
	struct stonefly_server *server = wl_container_of(listener, server, cursor_touch_motion);
	struct wlr_touch_motion_event *event = data;
	stonefly_session_notify_activity(server);
	if (wlr_seat_touch_get_point(server->seat, event->touch_id) == NULL) {
		return;
	}

	double lx, ly;
	wlr_cursor_absolute_to_layout_coords(server->cursor, &event->touch->base, event->x,
					     event->y, &lx, &ly);

	double sx, sy;
	struct wlr_surface *surface = NULL;
	stonefly_desktop_toplevel_at(server, lx, ly, &surface, &sx, &sy);
	if (surface != NULL) {
		wlr_seat_touch_point_focus(server->seat, surface, event->time_msec, event->touch_id,
					   sx, sy);
		wlr_seat_touch_notify_motion(server->seat, event->time_msec, event->touch_id, sx,
					     sy);
	} else {
		wlr_seat_touch_point_clear_focus(server->seat, event->time_msec, event->touch_id);
	}
}

static void server_cursor_touch_cancel(struct wl_listener *listener, void *data) {
	struct stonefly_server *server = wl_container_of(listener, server, cursor_touch_cancel);
	struct wlr_touch_cancel_event *event = data;
	struct wlr_touch_point *point = wlr_seat_touch_get_point(server->seat, event->touch_id);
	if (point == NULL) {
		return;
	}
	wlr_seat_touch_notify_cancel(server->seat, point->client);
}

static void server_cursor_touch_frame(struct wl_listener *listener, void *data) {
	(void)data;
	struct stonefly_server *server = wl_container_of(listener, server, cursor_touch_frame);
	wlr_seat_touch_notify_frame(server->seat);
}

void stonefly_seat_setup(struct stonefly_server *server) {
	server->cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server->cursor, server->output_layout);

	/* setenv may invalidate getenv pointers; copy before mutating env. */
	char *theme = NULL;
	const char *env_theme = getenv("XCURSOR_THEME");
	if (env_theme != NULL && env_theme[0] != '\0') {
		theme = strdup(env_theme);
	}
	uint32_t size = cursor_size_from_env((uint32_t)stonefly_cfg(server)->cursor_size_default);
	if (theme != NULL) {
		/* Keep clients spawned after seat setup on the same size. */
		char sizebuf[16];
		snprintf(sizebuf, sizeof(sizebuf), "%u", size);
		setenv("XCURSOR_SIZE", sizebuf, 1);
	}
	server->cursor_mgr = wlr_xcursor_manager_create(theme, size);
	wlr_xcursor_manager_load(server->cursor_mgr, stonefly_output_scale_factor(server));
	SF_LOGI("cursor theme", sf_str("theme", theme != NULL ? theme : ""), sf_i64("size", size));
	free(theme);

	server->cursor_mode = STONEFLY_CURSOR_PASSTHROUGH;
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
	server->pointer_constraints = wlr_pointer_constraints_v1_create(server->wl_display);
	server->current_constraint = NULL;
	server->new_pointer_constraint.notify = server_new_pointer_constraint;
	wl_signal_add(&server->pointer_constraints->events.new_constraint,
		      &server->new_pointer_constraint);

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
	server->tap.single = STONEFLY_ACTION_NONE;
	server->tap.double_tap = STONEFLY_ACTION_NONE;
}

void stonefly_seat_finish(struct stonefly_server *server) {
	tap_clear_timer(server);
	server->tap.armed = false;
	server->tap.taps = 0;
	struct stonefly_pointer *pointer, *tmp;
	wl_list_for_each_safe(pointer, tmp, &server->pointers, link) {
		destroy_pointer(pointer);
	}
	struct stonefly_touch *touch, *ttmp;
	wl_list_for_each_safe(touch, ttmp, &server->touch, link) {
		destroy_touch(touch);
	}
	if (server->current_constraint != NULL) {
		server->current_constraint = NULL;
	}
	if (server->virtual_keyboard_mgr != NULL) {
		wl_list_remove(&server->new_virtual_keyboard.link);
		server->virtual_keyboard_mgr = NULL;
	}
	if (server->pointer_constraints != NULL) {
		wl_list_remove(&server->new_pointer_constraint.link);
		server->pointer_constraints = NULL;
	}
	server->pointer_gestures = NULL;
	server->relative_pointer_mgr = NULL;
}
