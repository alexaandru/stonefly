#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <scenefx/types/wlr_scene.h>

#include "log/log.h"
#include "output/output.h"
#include "seat/seat.h"
#include "server/server.h"
#include "session/session.h"
#include "wm/wm.h"

struct stonefly_lock_surface {
	struct wl_list link;
	struct stonefly_server *server;
	struct wlr_session_lock_surface_v1 *lock_surface;
	struct wlr_scene_tree *scene;
	struct stonefly_output *output;
	struct wl_listener destroy;
	struct wl_listener map;
};

static void session_focus_lock_surface(struct stonefly_server *server,
		struct wlr_surface *surface) {
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	if (keyboard != NULL) {
		wlr_seat_keyboard_notify_enter(server->seat, surface,
			keyboard->keycodes, keyboard->num_keycodes,
			&keyboard->modifiers);
	} else {
		wlr_seat_keyboard_notify_enter(server->seat, surface,
			NULL, 0, NULL);
	}
}

static void session_focus_any_lock(struct stonefly_server *server) {
	if (server->session_lock == NULL) {
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		return;
	}
	struct stonefly_lock_surface *ls;
	wl_list_for_each(ls, &server->lock_surfaces, link) {
		if (ls->lock_surface->surface->mapped) {
			session_focus_lock_surface(server, ls->lock_surface->surface);
			return;
		}
	}
	wlr_seat_keyboard_notify_clear_focus(server->seat);
}

static void lock_surface_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct stonefly_lock_surface *ls =
		wl_container_of(listener, ls, destroy);
	struct stonefly_server *server = ls->server;
	bool had_focus = server->seat->keyboard_state.focused_surface ==
		ls->lock_surface->surface;

	wl_list_remove(&ls->destroy.link);
	wl_list_remove(&ls->map.link);
	wl_list_remove(&ls->link);
	if (ls->scene != NULL) {
		wlr_scene_node_destroy(&ls->scene->node);
	}
	free(ls);

	if (server->locked && had_focus) {
		session_focus_any_lock(server);
	}
}

static void lock_surface_map(struct wl_listener *listener, void *data) {
	(void)data;
	struct stonefly_lock_surface *ls =
		wl_container_of(listener, ls, map);
	session_focus_lock_surface(ls->server, ls->lock_surface->surface);
}

static void session_new_lock_surface(struct wl_listener *listener, void *data) {
	struct stonefly_server *server =
		wl_container_of(listener, server, session_lock_new_surface);
	struct wlr_session_lock_surface_v1 *lock_surface = data;
	struct wlr_output *wlr_output = lock_surface->output;
	struct stonefly_output *output =
		wlr_output != NULL ? wlr_output->data : NULL;

	struct stonefly_lock_surface *ls = calloc(1, sizeof(*ls));
	if (ls == NULL) {
		return;
	}
	ls->server = server;
	ls->lock_surface = lock_surface;
	ls->output = output;

	struct wlr_scene_tree *parent = server->session_lock_tree;
	if (parent == NULL) {
		parent = server->layers.lock;
	}
	ls->scene = wlr_scene_subsurface_tree_create(parent, lock_surface->surface);
	lock_surface->surface->data = ls->scene;

	int ox = 0, oy = 0, ow = 0, oh = 0;
	if (output != NULL) {
		struct wlr_box box;
		wlr_output_layout_get_box(server->output_layout, wlr_output, &box);
		ox = box.x;
		oy = box.y;
		ow = box.width;
		oh = box.height;
	}
	if (ow <= 0 || oh <= 0) {
		ow = wlr_output != NULL ? wlr_output->width : 0;
		oh = wlr_output != NULL ? wlr_output->height : 0;
	}
	wlr_scene_node_set_position(&ls->scene->node, ox, oy);
	wlr_session_lock_surface_v1_configure(lock_surface, (uint32_t)ow,
		(uint32_t)oh);

	ls->destroy.notify = lock_surface_destroy;
	wl_signal_add(&lock_surface->events.destroy, &ls->destroy);
	ls->map.notify = lock_surface_map;
	wl_signal_add(&lock_surface->surface->events.map, &ls->map);

	wl_list_insert(&server->lock_surfaces, &ls->link);
}

static void session_destroy_lock(struct stonefly_server *server, bool unlocked) {
	if (server->session_lock == NULL) {
		return;
	}

	wl_list_remove(&server->session_lock_new_surface.link);
	wl_list_remove(&server->session_lock_unlock.link);
	wl_list_remove(&server->session_lock_destroy.link);

	struct stonefly_lock_surface *ls, *tmp;
	wl_list_for_each_safe(ls, tmp, &server->lock_surfaces, link) {
		wl_list_remove(&ls->destroy.link);
		wl_list_remove(&ls->map.link);
		wl_list_remove(&ls->link);
		if (ls->scene != NULL) {
			wlr_scene_node_destroy(&ls->scene->node);
		}
		free(ls);
	}
	wl_list_init(&server->lock_surfaces);

	if (server->session_lock_tree != NULL) {
		wlr_scene_node_destroy(&server->session_lock_tree->node);
		server->session_lock_tree = NULL;
	}
	server->session_lock = NULL;

	if (unlocked) {
		server->locked = false;
		if (server->lock_blank != NULL) {
			wlr_scene_node_set_enabled(&server->lock_blank->node, false);
		}
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		stonefly_focus_top(server);
		SF_LOGI("session unlocked");
	} else {
		/* Client died while locked: keep blank up, stay locked. */
		server->locked = true;
		if (server->lock_blank != NULL) {
			wlr_scene_node_set_enabled(&server->lock_blank->node, true);
		}
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		SF_LOGW("session lock client gone; blank retained");
	}
}

static void session_lock_unlock(struct wl_listener *listener, void *data) {
	(void)data;
	struct stonefly_server *server =
		wl_container_of(listener, server, session_lock_unlock);
	session_destroy_lock(server, true);
}

static void session_lock_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct stonefly_server *server =
		wl_container_of(listener, server, session_lock_destroy);
	/* destroy without unlock event → remain locked with blank. */
	session_destroy_lock(server, false);
}

static void session_new_lock(struct wl_listener *listener, void *data) {
	struct stonefly_server *server =
		wl_container_of(listener, server, session_lock_new);
	struct wlr_session_lock_v1 *lock = data;

	if (server->session_lock != NULL) {
		SF_LOGW("session lock rejected; already locked");
		wlr_session_lock_v1_destroy(lock);
		return;
	}

	server->session_lock = lock;
	server->locked = true;
	wl_list_init(&server->lock_surfaces);

	if (server->lock_blank != NULL) {
		/* Cover the whole layout while the locker maps surfaces. */
		struct wlr_box layout = {0};
		wlr_output_layout_get_box(server->output_layout, NULL, &layout);
		if (layout.width > 0 && layout.height > 0) {
			wlr_scene_rect_set_size(server->lock_blank,
				layout.width, layout.height);
			wlr_scene_node_set_position(&server->lock_blank->node,
				layout.x, layout.y);
		}
		wlr_scene_node_set_enabled(&server->lock_blank->node, true);
	}

	server->session_lock_tree =
		wlr_scene_tree_create(server->layers.lock);

	/* Drop interactive grab / normal client focus. */
	server->exclusive_layer = NULL;
	stonefly_reset_cursor_mode(server);
	wlr_seat_keyboard_notify_clear_focus(server->seat);
	wlr_seat_pointer_clear_focus(server->seat);

	server->session_lock_new_surface.notify = session_new_lock_surface;
	wl_signal_add(&lock->events.new_surface, &server->session_lock_new_surface);
	server->session_lock_unlock.notify = session_lock_unlock;
	wl_signal_add(&lock->events.unlock, &server->session_lock_unlock);
	server->session_lock_destroy.notify = session_lock_destroy;
	wl_signal_add(&lock->events.destroy, &server->session_lock_destroy);

	wlr_session_lock_v1_send_locked(lock);
	SF_LOGI("session locked");
}

struct idle_inhibitor_listener {
	struct wl_listener destroy;
	struct stonefly_server *server;
	struct wlr_surface *surface;
};

static void idle_inhibitor_destroy_cb(struct wl_listener *listener, void *data) {
	(void)data;
	struct idle_inhibitor_listener *wrap =
		wl_container_of(listener, wrap, destroy);
	/* Inhibitor is still on the manager list during destroy; exclude it. */
	struct wlr_surface *root = wrap->surface != NULL
		? wlr_surface_get_root_surface(wrap->surface) : NULL;
	struct stonefly_server *server = wrap->server;
	wl_list_remove(&wrap->destroy.link);
	free(wrap);
	stonefly_session_check_inhibitors(server, root);
}

static void session_new_idle_inhibitor(struct wl_listener *listener, void *data) {
	struct stonefly_server *server =
		wl_container_of(listener, server, idle_inhibitor_new);
	struct wlr_idle_inhibitor_v1 *inhibitor = data;

	struct idle_inhibitor_listener *wrap = calloc(1, sizeof(*wrap));
	if (wrap == NULL) {
		stonefly_session_check_inhibitors(server, NULL);
		return;
	}
	wrap->server = server;
	wrap->surface = inhibitor->surface;
	wrap->destroy.notify = idle_inhibitor_destroy_cb;
	wl_signal_add(&inhibitor->events.destroy, &wrap->destroy);

	stonefly_session_check_inhibitors(server, NULL);
}

static void output_power_set_mode(struct wl_listener *listener, void *data) {
	struct stonefly_server *server =
		wl_container_of(listener, server, output_power_set_mode);
	struct wlr_output_power_v1_set_mode_event *event = data;
	struct wlr_output *wlr_output = event->output;
	(void)server;

	bool enable = event->mode == ZWLR_OUTPUT_POWER_V1_MODE_ON;
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, enable);
	if (enable && wlr_output->current_mode == NULL) {
		struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
		if (mode != NULL) {
			wlr_output_state_set_mode(&state, mode);
		}
	}
	if (!wlr_output_commit_state(wlr_output, &state)) {
		SF_LOGW("output power commit failed",
			sf_str("output", wlr_output->name != NULL
				? wlr_output->name : "?"),
			sf_bool("enable", enable));
	} else {
		SF_LOGI("output power",
			sf_str("output", wlr_output->name != NULL
				? wlr_output->name : "?"),
			sf_bool("enable", enable));
	}
	wlr_output_state_finish(&state);
}

void stonefly_session_check_inhibitors(struct stonefly_server *server,
		struct wlr_surface *exclude) {
	if (server->idle_notifier == NULL || server->idle_inhibit_mgr == NULL) {
		return;
	}
	bool inhibited = false;
	struct wlr_idle_inhibitor_v1 *inhibitor;
	wl_list_for_each(inhibitor, &server->idle_inhibit_mgr->inhibitors, link) {
		struct wlr_surface *root =
			wlr_surface_get_root_surface(inhibitor->surface);
		if (exclude != NULL && root == exclude) {
			continue;
		}
		/* Any remaining inhibitor counts (caffeine / video). Visibility
		 * checks are nice-to-have; Noctalia caffeine is explicit. */
		inhibited = true;
		break;
	}
	wlr_idle_notifier_v1_set_inhibited(server->idle_notifier, inhibited);
}

void stonefly_session_notify_activity(struct stonefly_server *server) {
	if (server->idle_notifier == NULL || server->seat == NULL) {
		return;
	}
	wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
}

bool stonefly_session_locked(const struct stonefly_server *server) {
	return server != NULL && server->locked;
}

void stonefly_session_setup(struct stonefly_server *server) {
	wl_list_init(&server->lock_surfaces);
	server->locked = false;
	server->session_lock = NULL;
	server->session_lock_tree = NULL;

	/* Above overlay: lock blank + lock surfaces. */
	server->layers.lock = wlr_scene_tree_create(&server->scene->tree);
	float blank[4] = { 0.05f, 0.05f, 0.07f, 1.0f };
	server->lock_blank = wlr_scene_rect_create(server->layers.lock, 0, 0, blank);
	wlr_scene_node_set_enabled(&server->lock_blank->node, false);

	server->session_lock_mgr =
		wlr_session_lock_manager_v1_create(server->wl_display);
	server->session_lock_new.notify = session_new_lock;
	wl_signal_add(&server->session_lock_mgr->events.new_lock,
		&server->session_lock_new);

	server->idle_notifier =
		wlr_idle_notifier_v1_create(server->wl_display);
	server->idle_inhibit_mgr =
		wlr_idle_inhibit_v1_create(server->wl_display);
	server->idle_inhibitor_new.notify = session_new_idle_inhibitor;
	wl_signal_add(&server->idle_inhibit_mgr->events.new_inhibitor,
		&server->idle_inhibitor_new);

	server->output_power_mgr =
		wlr_output_power_manager_v1_create(server->wl_display);
	server->output_power_set_mode.notify = output_power_set_mode;
	wl_signal_add(&server->output_power_mgr->events.set_mode,
		&server->output_power_set_mode);

	SF_LOGI("session lock / idle / output-power",
		sf_str("session_lock", "v1"),
		sf_str("idle_notify", "v1"),
		sf_str("idle_inhibit", "v1"),
		sf_str("output_power", "v1"));
}

void stonefly_session_finish(struct stonefly_server *server) {
	if (server->session_lock != NULL) {
		session_destroy_lock(server, true);
	}
	if (server->session_lock_mgr != NULL) {
		wl_list_remove(&server->session_lock_new.link);
		server->session_lock_mgr = NULL;
	}
	if (server->idle_inhibit_mgr != NULL) {
		wl_list_remove(&server->idle_inhibitor_new.link);
		server->idle_inhibit_mgr = NULL;
	}
	if (server->output_power_mgr != NULL) {
		wl_list_remove(&server->output_power_set_mode.link);
		server->output_power_mgr = NULL;
	}
	server->idle_notifier = NULL;
	server->lock_blank = NULL;
	server->layers.lock = NULL;
}
