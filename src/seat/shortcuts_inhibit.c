#include "seat/internal.h"

#include <stdlib.h>
#include <wlr/types/wlr_keyboard_shortcuts_inhibit_v1.h>
#include <wlr/types/wlr_seat.h>

#include "log/log.h"
#include "session/session.h"

struct sf_shortcuts_inhibitor {
	struct wl_list link;
	struct sf_server *server;
	struct wlr_keyboard_shortcuts_inhibitor_v1 *inhibitor;
	struct wl_listener destroy;
};

static void shortcuts_inhibitor_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_shortcuts_inhibitor *item = wl_container_of(listener, item, destroy);
	wl_list_remove(&item->destroy.link);
	wl_list_remove(&item->link);
	free(item);
	SF_LOGD("keyboard shortcuts inhibitor removed");
}

void server_new_shortcuts_inhibitor(struct wl_listener *listener, void *data) {
	struct sf_server *server = wl_container_of(listener, server, shortcuts_inhibit.on_new);
	struct wlr_keyboard_shortcuts_inhibitor_v1 *inhibitor = data;

	struct sf_shortcuts_inhibitor *item = calloc(1, sizeof(*item));
	if (item == NULL) {
		return;
	}
	item->server = server;
	item->inhibitor = inhibitor;
	item->destroy.notify = shortcuts_inhibitor_destroy;
	wl_signal_add(&inhibitor->events.destroy, &item->destroy);
	wl_list_insert(&server->shortcuts_inhibit.inhibitors, &item->link);

	wlr_keyboard_shortcuts_inhibitor_v1_activate(inhibitor);
	SF_LOGD("keyboard shortcuts inhibitor activated");
}

bool sf_shortcuts_inhibited(const struct sf_server *server) {
	if (server == NULL || server->seat == NULL || sf_session_locked(server)) {
		return false;
	}
	struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
	if (focused == NULL) {
		return false;
	}
	struct sf_shortcuts_inhibitor *item;
	wl_list_for_each(item, &server->shortcuts_inhibit.inhibitors, link) {
		const struct wlr_keyboard_shortcuts_inhibitor_v1 *inh = item->inhibitor;
		if (inh->active && inh->seat == server->seat && inh->surface == focused) {
			return true;
		}
	}

	return false;
}

void sf_shortcuts_inhibit_finish(struct sf_server *server) {
	struct sf_shortcuts_inhibitor *item, *tmp;
	wl_list_for_each_safe(item, tmp, &server->shortcuts_inhibit.inhibitors, link) {
		wl_list_remove(&item->destroy.link);
		wl_list_remove(&item->link);
		free(item);
	}
	if (server->shortcuts_inhibit.mgr != NULL) {
		wl_list_remove(&server->shortcuts_inhibit.on_new.link);
		server->shortcuts_inhibit.mgr = NULL;
	}
}
