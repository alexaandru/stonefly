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

struct sf_lock_surface {
	struct wl_list link;
	struct sf_server *server;
	struct wlr_session_lock_surface_v1 *lock_surface;
	struct wlr_scene_tree *scene;
	struct sf_output *output;
	struct wl_listener destroy;
	struct wl_listener map;
};

static void session_focus_lock_surface(struct sf_server *server, struct wlr_surface *surface) {
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	if (keyboard != NULL) {
		wlr_seat_keyboard_notify_enter(server->seat, surface, keyboard->keycodes,
					       keyboard->num_keycodes, &keyboard->modifiers);
	} else {
		wlr_seat_keyboard_notify_enter(server->seat, surface, NULL, 0, NULL);
	}
}

static void session_focus_any_lock(struct sf_server *server) {
	if (server->lock.current == NULL) {
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		return;
	}
	struct sf_lock_surface *ls;
	wl_list_for_each(ls, &server->lock.surfaces, link) {
		if (ls->lock_surface->surface->mapped) {
			session_focus_lock_surface(server, ls->lock_surface->surface);
			return;
		}
	}
	wlr_seat_keyboard_notify_clear_focus(server->seat);
}

static void lock_surface_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_lock_surface *ls = wl_container_of(listener, ls, destroy);
	struct sf_server *server = ls->server;
	bool had_focus = server->seat->keyboard_state.focused_surface == ls->lock_surface->surface;

	wl_list_remove(&ls->destroy.link);
	wl_list_remove(&ls->map.link);
	wl_list_remove(&ls->link);
	if (ls->scene != NULL) {
		wlr_scene_node_destroy(&ls->scene->node);
	}
	free(ls);

	if (server->lock.active && had_focus) {
		session_focus_any_lock(server);
	}
}

static void lock_surface_map(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_lock_surface *ls = wl_container_of(listener, ls, map);
	session_focus_lock_surface(ls->server, ls->lock_surface->surface);
}

static void session_new_lock_surface(struct wl_listener *listener, void *data) {
	struct sf_server *server = wl_container_of(listener, server, lock.on_new_surface);
	struct wlr_session_lock_surface_v1 *lock_surface = data;
	struct wlr_output *wlr_output = lock_surface->output;
	struct sf_output *output = wlr_output != NULL ? wlr_output->data : NULL;

	struct sf_lock_surface *ls = calloc(1, sizeof(*ls));
	if (ls == NULL) {
		return;
	}
	ls->server = server;
	ls->lock_surface = lock_surface;
	ls->output = output;

	struct wlr_scene_tree *parent = server->lock.tree;
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
	wlr_session_lock_surface_v1_configure(lock_surface, (uint32_t)ow, (uint32_t)oh);

	ls->destroy.notify = lock_surface_destroy;
	wl_signal_add(&lock_surface->events.destroy, &ls->destroy);
	ls->map.notify = lock_surface_map;
	wl_signal_add(&lock_surface->surface->events.map, &ls->map);

	wl_list_insert(&server->lock.surfaces, &ls->link);
}

static void session_destroy_lock(struct sf_server *server, bool unlocked) {
	if (server->lock.current == NULL) {
		return;
	}

	wl_list_remove(&server->lock.on_new_surface.link);
	wl_list_remove(&server->lock.on_unlock.link);
	wl_list_remove(&server->lock.on_destroy.link);

	struct sf_lock_surface *ls, *tmp;
	wl_list_for_each_safe(ls, tmp, &server->lock.surfaces, link) {
		wl_list_remove(&ls->destroy.link);
		wl_list_remove(&ls->map.link);
		wl_list_remove(&ls->link);
		if (ls->scene != NULL) {
			wlr_scene_node_destroy(&ls->scene->node);
		}
		free(ls);
	}
	wl_list_init(&server->lock.surfaces);

	if (server->lock.tree != NULL) {
		wlr_scene_node_destroy(&server->lock.tree->node);
		server->lock.tree = NULL;
	}
	server->lock.current = NULL;

	if (unlocked) {
		server->lock.active = false;
		if (server->lock.blank != NULL) {
			wlr_scene_node_set_enabled(&server->lock.blank->node, false);
		}
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		sf_focus_top(server);
		SF_LOGI("session unlocked");
	} else {
		/* Client died while locked: keep blank up, stay locked. */
		server->lock.active = true;
		if (server->lock.blank != NULL) {
			wlr_scene_node_set_enabled(&server->lock.blank->node, true);
		}
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		SF_LOGW("session lock client gone; blank retained");
	}
}

static void session_lock_unlock(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_server *server = wl_container_of(listener, server, lock.on_unlock);
	session_destroy_lock(server, true);
}

static void session_lock_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_server *server = wl_container_of(listener, server, lock.on_destroy);
	/* destroy without unlock event → remain locked with blank. */
	session_destroy_lock(server, false);
}

static void session_new_lock(struct wl_listener *listener, void *data) {
	struct sf_server *server = wl_container_of(listener, server, lock.on_new);
	struct wlr_session_lock_v1 *lock = data;

	if (server->lock.current != NULL) {
		SF_LOGW("session lock rejected; already locked");
		wlr_session_lock_v1_destroy(lock);
		return;
	}

	server->lock.current = lock;
	server->lock.active = true;
	wl_list_init(&server->lock.surfaces);

	if (server->lock.blank != NULL) {
		/* Cover the whole layout while the locker maps surfaces. */
		struct wlr_box layout = {0};
		wlr_output_layout_get_box(server->output_layout, NULL, &layout);
		if (layout.width > 0 && layout.height > 0) {
			wlr_scene_rect_set_size(server->lock.blank, layout.width, layout.height);
			wlr_scene_node_set_position(&server->lock.blank->node, layout.x, layout.y);
		}
		wlr_scene_node_set_enabled(&server->lock.blank->node, true);
	}

	server->lock.tree = wlr_scene_tree_create(server->layers.lock);

	/* Drop interactive grab / normal client focus. */
	server->exclusive_layer = NULL;
	sf_reset_cursor_mode(server);
	wlr_seat_keyboard_notify_clear_focus(server->seat);
	wlr_seat_pointer_clear_focus(server->seat);

	server->lock.on_new_surface.notify = session_new_lock_surface;
	wl_signal_add(&lock->events.new_surface, &server->lock.on_new_surface);
	server->lock.on_unlock.notify = session_lock_unlock;
	wl_signal_add(&lock->events.unlock, &server->lock.on_unlock);
	server->lock.on_destroy.notify = session_lock_destroy;
	wl_signal_add(&lock->events.destroy, &server->lock.on_destroy);

	wlr_session_lock_v1_send_locked(lock);
	SF_LOGI("session locked");
}

struct idle_inhibitor_listener {
	struct wl_listener destroy;
	struct sf_server *server;
	struct wlr_surface *surface;
};

static void idle_inhibitor_destroy_cb(struct wl_listener *listener, void *data) {
	(void)data;
	struct idle_inhibitor_listener *wrap = wl_container_of(listener, wrap, destroy);
	/* Inhibitor is still on the manager list during destroy; exclude it. */
	struct wlr_surface *root =
		wrap->surface != NULL ? wlr_surface_get_root_surface(wrap->surface) : NULL;
	struct sf_server *server = wrap->server;
	wl_list_remove(&wrap->destroy.link);
	free(wrap);
	sf_session_check_inhibitors(server, root);
}

static void session_new_idle_inhibitor(struct wl_listener *listener, void *data) {
	struct sf_server *server = wl_container_of(listener, server, idle.on_inhibitor_new);
	struct wlr_idle_inhibitor_v1 *inhibitor = data;

	struct idle_inhibitor_listener *wrap = calloc(1, sizeof(*wrap));
	if (wrap == NULL) {
		sf_session_check_inhibitors(server, NULL);
		return;
	}
	wrap->server = server;
	wrap->surface = inhibitor->surface;
	wrap->destroy.notify = idle_inhibitor_destroy_cb;
	wl_signal_add(&inhibitor->events.destroy, &wrap->destroy);

	sf_session_check_inhibitors(server, NULL);
}

static void output_power_set_mode(struct wl_listener *listener, void *data) {
	struct sf_server *server = wl_container_of(listener, server, output_power.on_set_mode);
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
			sf_str("output", wlr_output->name != NULL ? wlr_output->name : "?"),
			sf_bool("enable", enable));
	} else {
		SF_LOGI("output power",
			sf_str("output", wlr_output->name != NULL ? wlr_output->name : "?"),
			sf_bool("enable", enable));
	}
	wlr_output_state_finish(&state);
}

void sf_session_check_inhibitors(struct sf_server *server, struct wlr_surface *exclude) {
	if (server->idle.notifier == NULL || server->idle.inhibit_mgr == NULL) {
		return;
	}
	bool inhibited = false;
	struct wlr_idle_inhibitor_v1 *inhibitor;
	wl_list_for_each(inhibitor, &server->idle.inhibit_mgr->inhibitors, link) {
		struct wlr_surface *root = wlr_surface_get_root_surface(inhibitor->surface);
		if (exclude != NULL && root == exclude) {
			continue;
		}
		/* Any remaining inhibitor counts (caffeine / video). Visibility
		 * checks are nice-to-have; Noctalia caffeine is explicit. */
		inhibited = true;
		break;
	}
	wlr_idle_notifier_v1_set_inhibited(server->idle.notifier, inhibited);
}

void sf_session_notify_activity(struct sf_server *server) {
	if (server->idle.notifier == NULL || server->seat == NULL) {
		return;
	}
	wlr_idle_notifier_v1_notify_activity(server->idle.notifier, server->seat);
}

bool sf_session_locked(const struct sf_server *server) {
	return server != NULL && server->lock.active;
}

void sf_session_setup(struct sf_server *server) {
	wl_list_init(&server->lock.surfaces);
	server->lock.active = false;
	server->lock.current = NULL;
	server->lock.tree = NULL;

	/* Above overlay: lock blank + lock surfaces. */
	server->layers.lock = wlr_scene_tree_create(&server->scene->tree);
	float blank[4] = {0.05f, 0.05f, 0.07f, 1.0f};
	server->lock.blank = wlr_scene_rect_create(server->layers.lock, 0, 0, blank);
	wlr_scene_node_set_enabled(&server->lock.blank->node, false);

	server->lock.mgr = wlr_session_lock_manager_v1_create(server->wl_display);
	server->lock.on_new.notify = session_new_lock;
	wl_signal_add(&server->lock.mgr->events.new_lock, &server->lock.on_new);

	server->idle.notifier = wlr_idle_notifier_v1_create(server->wl_display);
	server->idle.inhibit_mgr = wlr_idle_inhibit_v1_create(server->wl_display);
	server->idle.on_inhibitor_new.notify = session_new_idle_inhibitor;
	wl_signal_add(&server->idle.inhibit_mgr->events.new_inhibitor,
		      &server->idle.on_inhibitor_new);

	server->output_power.mgr = wlr_output_power_manager_v1_create(server->wl_display);
	server->output_power.on_set_mode.notify = output_power_set_mode;
	wl_signal_add(&server->output_power.mgr->events.set_mode,
		      &server->output_power.on_set_mode);

	SF_LOGI("session lock / idle / output-power", sf_str("session_lock", "v1"),
		sf_str("idle_notify", "v1"), sf_str("idle_inhibit", "v1"),
		sf_str("output_power", "v1"));
}

void sf_session_finish(struct sf_server *server) {
	if (server->lock.current != NULL) {
		session_destroy_lock(server, true);
	}
	if (server->lock.mgr != NULL) {
		wl_list_remove(&server->lock.on_new.link);
		server->lock.mgr = NULL;
	}
	if (server->idle.inhibit_mgr != NULL) {
		wl_list_remove(&server->idle.on_inhibitor_new.link);
		server->idle.inhibit_mgr = NULL;
	}
	if (server->output_power.mgr != NULL) {
		wl_list_remove(&server->output_power.on_set_mode.link);
		server->output_power.mgr = NULL;
	}
	server->idle.notifier = NULL;
	server->lock.blank = NULL;
	server->layers.lock = NULL;
}
