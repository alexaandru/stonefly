#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <systemd/sd-bus.h>

#include "dbus/dbus.h"
#include "log/log.h"
#include "server/server.h"

extern char **environ;

#define STONEFLY_NOTIFY_BUS_NAME "org.freedesktop.Notifications"
#define STONEFLY_NOTIFY_WAIT_MS 20000

struct stonefly_dbus_notify {
	struct stonefly_server *server;
	sd_bus *bus;
	sd_bus_slot *match_slot;
	struct wl_event_source *bus_source;
	struct wl_event_source *timer_source;
	char *body;
	bool finished; /* Notify attempted; destroy after sd_bus_process returns */
};

static void notify_pending_destroy(struct stonefly_dbus_notify *n) {
	if (n == NULL) {
		return;
	}
	if (n->server != NULL && n->server->config_notify == n) {
		n->server->config_notify = NULL;
	}
	if (n->timer_source != NULL) {
		wl_event_source_remove(n->timer_source);
		n->timer_source = NULL;
	}
	if (n->bus_source != NULL) {
		wl_event_source_remove(n->bus_source);
		n->bus_source = NULL;
	}
	n->match_slot = sd_bus_slot_unref(n->match_slot);
	if (n->bus != NULL) {
		sd_bus_flush_close_unref(n->bus);
		n->bus = NULL;
	}
	free(n->body);
	free(n);
}

static int notify_send(sd_bus *bus, const char *body) {
	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message *m = NULL;
	sd_bus_message *reply = NULL;
	int r;

	r = sd_bus_message_new_method_call(bus, &m, STONEFLY_NOTIFY_BUS_NAME,
					   "/org/freedesktop/Notifications",
					   STONEFLY_NOTIFY_BUS_NAME, "Notify");
	if (r < 0) {
		return r;
	}
	/* Do not D-Bus-activate a random .service; we waited for a real owner. */
	sd_bus_message_set_auto_start(m, 0);

	r = sd_bus_message_append(m, "susssasa{sv}i", "stonefly", 0u, "dialog-error",
				  "stonefly: config load failed", body != NULL ? body : "", 0, 1,
				  "urgency", "y", (uint8_t)2, 0);
	if (r < 0) {
		sd_bus_message_unref(m);
		return r;
	}

	r = sd_bus_call(bus, m, 0, &error, &reply);
	sd_bus_message_unref(m);
	if (r < 0) {
		SF_LOGW("config warn Notify failed",
			sf_str("err", error.message != NULL ? error.message : strerror(-r)));
		sd_bus_error_free(&error);
		return r;
	}
	sd_bus_message_unref(reply);
	sd_bus_error_free(&error);
	SF_LOGI("config warn notification sent");
	return 0;
}

/*
 * destroy_now=false when called from a bus match inside sd_bus_process —
 * tear down only after the process loop returns (see on_bus_fd).
 */
static void notify_complete(struct stonefly_dbus_notify *n, bool destroy_now) {
	if (n == NULL || n->finished) {
		return;
	}
	n->finished = true;
	if (n->timer_source != NULL) {
		wl_event_source_remove(n->timer_source);
		n->timer_source = NULL;
	}
	if (n->bus != NULL) {
		(void)notify_send(n->bus, n->body);
	}
	if (destroy_now) {
		notify_pending_destroy(n);
	}
}

static int notifications_name_owned(sd_bus *bus, bool *owned) {
	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message *reply = NULL;
	int has = 0;
	int r = sd_bus_call_method(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
				   "org.freedesktop.DBus", "NameHasOwner", &error, &reply, "s",
				   STONEFLY_NOTIFY_BUS_NAME);
	if (r < 0) {
		SF_LOGW("NameHasOwner failed",
			sf_str("err", error.message != NULL ? error.message : strerror(-r)));
		sd_bus_error_free(&error);
		return r;
	}
	r = sd_bus_message_read(reply, "b", &has);
	sd_bus_message_unref(reply);
	sd_bus_error_free(&error);
	if (r < 0) {
		return r;
	}
	*owned = has != 0;
	return 0;
}

static int on_name_owner_changed(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
	(void)ret_error;
	struct stonefly_dbus_notify *n = userdata;
	const char *name = NULL;
	const char *old_owner = NULL;
	const char *new_owner = NULL;

	if (sd_bus_message_read(m, "sss", &name, &old_owner, &new_owner) < 0) {
		return 0;
	}
	if (name == NULL || strcmp(name, STONEFLY_NOTIFY_BUS_NAME) != 0) {
		return 0;
	}
	if (new_owner == NULL || new_owner[0] == '\0') {
		return 0;
	}
	SF_LOGI("notifications bus name claimed", sf_str("owner", new_owner));
	notify_complete(n, false);
	return 0;
}

static int on_bus_fd(int fd, uint32_t mask, void *data) {
	(void)fd;
	struct stonefly_dbus_notify *n = data;
	if (n == NULL || n->bus == NULL) {
		return 0;
	}
	if (mask & ((uint32_t)WL_EVENT_ERROR | (uint32_t)WL_EVENT_HANGUP)) {
		SF_LOGW("config warn notify: session bus hung up");
		notify_pending_destroy(n);
		return 0;
	}
	int r;
	do {
		r = sd_bus_process(n->bus, NULL);
	} while (r > 0);
	if (n->finished) {
		notify_pending_destroy(n);
		return 0;
	}
	if (r < 0) {
		SF_LOGW("config warn notify: sd_bus_process failed", sf_str("err", strerror(-r)));
		notify_pending_destroy(n);
	}
	return 0;
}

static int on_notify_timeout(void *data) {
	struct stonefly_dbus_notify *n = data;
	SF_LOGW("config warn notify: no org.freedesktop.Notifications within 20s; giving up");
	notify_pending_destroy(n);
	return 0;
}

void stonefly_dbus_update_activation_environment(void) {
	sd_bus *bus = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message *m = NULL;
	int r;

	r = sd_bus_open_user(&bus);
	if (r < 0) {
		SF_LOGW("dbus activation: open session bus failed", sf_str("err", strerror(-r)));
		return;
	}

	r = sd_bus_message_new_method_call(bus, &m, "org.freedesktop.DBus", "/org/freedesktop/DBus",
					   "org.freedesktop.DBus", "UpdateActivationEnvironment");
	if (r < 0) {
		SF_LOGW("dbus activation: message failed", sf_str("err", strerror(-r)));
		goto out;
	}

	r = sd_bus_message_open_container(m, 'a', "{ss}");
	if (r < 0) {
		goto out_msg;
	}
	for (char **ep = environ; ep != NULL && *ep != NULL; ep++) {
		char *eq = strchr(*ep, '=');
		if (eq == NULL || eq == *ep) {
			continue;
		}
		size_t klen = (size_t)(eq - *ep);
		char key[256];
		if (klen == 0 || klen >= sizeof key) {
			continue;
		}
		memcpy(key, *ep, klen);
		key[klen] = '\0';
		r = sd_bus_message_append(m, "{ss}", key, eq + 1);
		if (r < 0) {
			SF_LOGW("dbus activation: append env failed", sf_str("err", strerror(-r)));
			goto out_msg;
		}
	}
	r = sd_bus_message_close_container(m);
	if (r < 0) {
		goto out_msg;
	}

	r = sd_bus_call(bus, m, 0, &error, NULL);
	if (r < 0) {
		SF_LOGW("UpdateActivationEnvironment failed",
			sf_str("err", error.message != NULL ? error.message : strerror(-r)));
		sd_bus_error_free(&error);
	} else {
		SF_LOGI("dbus activation environment updated");
	}

out_msg:
	sd_bus_message_unref(m);
out:
	sd_bus_flush_close_unref(bus);
}

void stonefly_dbus_notify_config_warn(struct stonefly_server *server) {
	if (server == NULL || server->config_warn == NULL || server->config_warn[0] == '\0') {
		return;
	}
	if (server->config_notify != NULL) {
		return;
	}

	struct stonefly_dbus_notify *n = calloc(1, sizeof(*n));
	if (n == NULL) {
		SF_LOGW("config warn notify: out of memory");
		return;
	}
	n->server = server;
	n->body = strdup(server->config_warn);
	if (n->body == NULL) {
		free(n);
		SF_LOGW("config warn notify: out of memory");
		return;
	}

	int r = sd_bus_open_user(&n->bus);
	if (r < 0) {
		SF_LOGW("config warn notify: open session bus failed", sf_str("err", strerror(-r)));
		notify_pending_destroy(n);
		return;
	}

	/*
	 * Subscribe before NameHasOwner so we cannot miss a claim that races
	 * between the check and the match install (Noctalia via -s).
	 */
	r = sd_bus_add_match(n->bus, &n->match_slot,
			     "type='signal',"
			     "sender='org.freedesktop.DBus',"
			     "path='/org/freedesktop/DBus',"
			     "interface='org.freedesktop.DBus',"
			     "member='NameOwnerChanged',"
			     "arg0='" STONEFLY_NOTIFY_BUS_NAME "'",
			     on_name_owner_changed, n);
	if (r < 0) {
		SF_LOGW("config warn notify: NameOwnerChanged match failed",
			sf_str("err", strerror(-r)));
		notify_pending_destroy(n);
		return;
	}

	struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
	int bus_fd = sd_bus_get_fd(n->bus);
	if (bus_fd < 0) {
		SF_LOGW("config warn notify: sd_bus_get_fd failed",
			sf_str("err", strerror(-bus_fd)));
		notify_pending_destroy(n);
		return;
	}
	n->bus_source = wl_event_loop_add_fd(
		loop, bus_fd,
		(uint32_t)WL_EVENT_READABLE | (uint32_t)WL_EVENT_HANGUP | (uint32_t)WL_EVENT_ERROR,
		on_bus_fd, n);
	if (n->bus_source == NULL) {
		SF_LOGW("config warn notify: bus fd watch failed");
		notify_pending_destroy(n);
		return;
	}

	n->timer_source = wl_event_loop_add_timer(loop, on_notify_timeout, n);
	if (n->timer_source == NULL) {
		SF_LOGW("config warn notify: timer failed");
		notify_pending_destroy(n);
		return;
	}
	wl_event_source_timer_update(n->timer_source, STONEFLY_NOTIFY_WAIT_MS);

	server->config_notify = n;

	bool owned = false;
	r = notifications_name_owned(n->bus, &owned);
	if (r < 0) {
		/* Match + timer still armed; a later claim can still win. */
		return;
	}
	if (owned) {
		SF_LOGI("notifications bus name already owned");
		notify_complete(n, true);
	}
}

void stonefly_dbus_finish(struct stonefly_server *server) {
	if (server == NULL) {
		return;
	}
	if (server->config_notify != NULL) {
		notify_pending_destroy(server->config_notify);
	}
}
