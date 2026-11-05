#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_ext_image_copy_capture_v1.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "config/config.h"
#include "log/log.h"
#include "server/server.h"
#include "toplevel/ext_capture.h"
#include "xdg/xdg.h"

/* Match app_id against comma-separated substrings in capture_block_app_ids. */
static bool capture_app_id_blocked(const struct stonefly_server *server, const char *app_id) {
	const char *list = stonefly_cfg(server)->capture_block_app_ids;
	if (list == NULL || list[0] == '\0' || app_id == NULL || app_id[0] == '\0') {
		return false;
	}

	const char *p = list;
	while (*p != '\0') {
		while (*p == ',' || *p == ' ' || *p == '\t') {
			p++;
		}
		if (*p == '\0') {
			break;
		}
		const char *start = p;
		while (*p != '\0' && *p != ',') {
			p++;
		}
		size_t n = (size_t)(p - start);
		while (n > 0 && (start[n - 1] == ' ' || start[n - 1] == '\t')) {
			n--;
		}
		if (n == 0) {
			continue;
		}
		for (const char *hay = app_id; *hay != '\0'; hay++) {
			if (strncmp(hay, start, n) == 0) {
				return true;
			}
		}
	}
	return false;
}

void stonefly_ext_capture_apply_block_rules(struct stonefly_toplevel *toplevel) {
	if (toplevel == NULL) {
		return;
	}
	const char *app_id = toplevel->xdg_toplevel->app_id;
	if (capture_app_id_blocked(toplevel->server, app_id)) {
		toplevel->capture_blocked = true;
	}
}

static void ext_capture_update_state(struct stonefly_toplevel *toplevel) {
	if (toplevel->ext_handle == NULL) {
		return;
	}
	struct wlr_ext_foreign_toplevel_handle_v1_state state = {
		.title = toplevel->xdg_toplevel->title,
		.app_id = toplevel->xdg_toplevel->app_id,
	};
	wlr_ext_foreign_toplevel_handle_v1_update_state(toplevel->ext_handle, &state);
}

static void ext_capture_destroy_handle(struct stonefly_toplevel *toplevel) {
	if (toplevel->ext_handle == NULL) {
		return;
	}
	wlr_ext_foreign_toplevel_handle_v1_destroy(toplevel->ext_handle);
	toplevel->ext_handle = NULL;
}

static void ext_capture_destroy_scene(struct stonefly_toplevel *toplevel) {
	if (toplevel->image_capture_scene == NULL) {
		return;
	}
	wlr_scene_node_destroy(&toplevel->image_capture_scene->tree.node);
	toplevel->image_capture_scene = NULL;
	toplevel->image_capture_tree = NULL;
	/* Source is owned by wlroots once accepted; drop our pointer. */
	toplevel->image_capture_source = NULL;
}

static void handle_capture_request(struct wl_listener *listener, void *data) {
	struct stonefly_server *server =
		wl_container_of(listener, server, ext_toplevel_capture_request);
	struct wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request *request = data;
	struct stonefly_toplevel *toplevel = request->toplevel_handle->data;
	if (toplevel == NULL || toplevel->image_capture_scene == NULL) {
		return;
	}
	if (toplevel->capture_blocked) {
		/* Leave rejected: client gets no source. */
		SF_LOGI("ext capture blocked", sf_i64("id", (int64_t)toplevel->id));
		return;
	}

	if (toplevel->image_capture_source == NULL) {
		toplevel->image_capture_source =
			wlr_ext_image_capture_source_v1_create_with_scene_node(
				&toplevel->image_capture_scene->tree.node,
				wl_display_get_event_loop(server->wl_display), server->allocator,
				server->renderer);
		if (toplevel->image_capture_source == NULL) {
			SF_LOGW("ext capture source create failed",
				sf_i64("id", (int64_t)toplevel->id));
			return;
		}
	}

	if (!wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request_accept(
		    request, toplevel->image_capture_source)) {
		SF_LOGW("ext capture request_accept failed", sf_i64("id", (int64_t)toplevel->id));
	}
}

void stonefly_ext_capture_setup(struct stonefly_server *server) {
	/* Clients copy frames from sources created below. */
	wlr_ext_image_copy_capture_manager_v1_create(server->wl_display, 1);

	server->ext_foreign_toplevel_list =
		wlr_ext_foreign_toplevel_list_v1_create(server->wl_display, 1);
	if (server->ext_foreign_toplevel_list == NULL) {
		SF_LOGW("ext foreign toplevel list", sf_str("err", "create failed"));
		return;
	}

	server->ext_toplevel_capture_mgr =
		wlr_ext_foreign_toplevel_image_capture_source_manager_v1_create(server->wl_display,
										1);
	if (server->ext_toplevel_capture_mgr == NULL) {
		SF_LOGW("ext toplevel image capture", sf_str("err", "create failed"));
		return;
	}

	server->ext_toplevel_capture_request.notify = handle_capture_request;
	wl_signal_add(&server->ext_toplevel_capture_mgr->events.new_request,
		      &server->ext_toplevel_capture_request);

	SF_LOGI("ext foreign toplevel capture", sf_str("list", "v1"), sf_str("image_capture", "v1"),
		sf_str("image_copy", "v1"));
}

void stonefly_ext_capture_finish(struct stonefly_server *server) {
	if (server->ext_toplevel_capture_mgr != NULL) {
		wl_list_remove(&server->ext_toplevel_capture_request.link);
		server->ext_toplevel_capture_mgr = NULL;
	}
	server->ext_foreign_toplevel_list = NULL;
}

void stonefly_ext_capture_on_map(struct stonefly_toplevel *toplevel) {
	struct stonefly_server *server = toplevel->server;
	stonefly_ext_capture_apply_block_rules(toplevel);
	if (server->ext_foreign_toplevel_list == NULL) {
		return;
	}

	if (toplevel->image_capture_scene == NULL) {
		toplevel->image_capture_scene = wlr_scene_create();
		if (toplevel->image_capture_scene == NULL) {
			SF_LOGW("ext capture scene create failed",
				sf_i64("id", (int64_t)toplevel->id));
			return;
		}
		toplevel->image_capture_tree = wlr_scene_xdg_surface_create(
			&toplevel->image_capture_scene->tree, toplevel->xdg_toplevel->base);
		if (toplevel->image_capture_tree == NULL) {
			SF_LOGW("ext capture xdg tree create failed",
				sf_i64("id", (int64_t)toplevel->id));
			ext_capture_destroy_scene(toplevel);
			return;
		}
	}

	if (toplevel->ext_handle == NULL) {
		struct wlr_ext_foreign_toplevel_handle_v1_state state = {
			.title = toplevel->xdg_toplevel->title,
			.app_id = toplevel->xdg_toplevel->app_id,
		};
		toplevel->ext_handle = wlr_ext_foreign_toplevel_handle_v1_create(
			server->ext_foreign_toplevel_list, &state);
		if (toplevel->ext_handle == NULL) {
			SF_LOGW("ext foreign handle create failed",
				sf_i64("id", (int64_t)toplevel->id));
			return;
		}
		toplevel->ext_handle->data = toplevel;
	} else {
		ext_capture_update_state(toplevel);
	}
}

void stonefly_ext_capture_on_unmap(struct stonefly_toplevel *toplevel) {
	/* Match zwlr: drop the public handle while unmapped. Capture scene stays
	 * so a remap can re-announce without rebuilding the xdg tree. */
	ext_capture_destroy_handle(toplevel);
}

void stonefly_ext_capture_on_title_change(struct stonefly_toplevel *toplevel) {
	ext_capture_update_state(toplevel);
}

void stonefly_ext_capture_on_app_id_change(struct stonefly_toplevel *toplevel) {
	stonefly_ext_capture_apply_block_rules(toplevel);
	ext_capture_update_state(toplevel);
}

void stonefly_ext_capture_on_close(struct stonefly_toplevel *toplevel) {
	ext_capture_destroy_handle(toplevel);
	ext_capture_destroy_scene(toplevel);
}
