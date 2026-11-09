#include "seat/seat.h"
#include "seat/internal.h"

#include <stdlib.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <xkbcommon/xkbcommon.h>

#include "config/config.h"
#include "log/log.h"
#include "session/session.h"
#include "util/time.h"
#include "wm/wm.h"

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

void tap_clear_timer(struct stonefly_server *server) {
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
	if (stonefly_timespec_ms_since(&server->tap.since) > cfg->tap_max_hold_ms) {
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

void server_new_keyboard(struct stonefly_server *server, struct wlr_input_device *device,
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

void server_new_virtual_keyboard(struct wl_listener *listener, void *data) {
	struct stonefly_server *server = wl_container_of(listener, server, new_virtual_keyboard);
	struct wlr_virtual_keyboard_v1 *virtual_keyboard = data;
	/* wlroots 0.20: keyboard is a wlr_keyboard; device is keyboard.base. */
	server_new_keyboard(server, &virtual_keyboard->keyboard.base, true);
	seat_update_capabilities(server);
}
