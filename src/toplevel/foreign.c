#include <stdlib.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>

#include "log/log.h"
#include "output/output.h"
#include "server/server.h"
#include "toplevel/foreign.h"
#include "wm/layout_sm.h"
#include "wm/wm.h"
#include "wm/workspace.h"

#include "wlr-foreign-toplevel-management-unstable-v1-protocol.h"

struct sf_foreign_toplevel_handle;

struct sf_foreign_handle_output {
	struct wl_list link;
	struct wlr_output *output;
	struct wl_listener output_destroy;
	struct sf_foreign_toplevel_handle *handle;
};

struct sf_foreign_toplevel_handle {
	struct wl_list link;
	struct sf_server *server;
	struct sf_toplevel *toplevel;
	struct wl_resource *resource;
	struct wl_list outputs;
};

struct sf_foreign_manager_resource {
	struct wl_list link;
	struct sf_server *server;
	struct wl_resource *resource;
};

static struct sf_toplevel *foreign_toplevel_from_resource(struct wl_resource *resource) {
	struct sf_foreign_toplevel_handle *handle = wl_resource_get_user_data(resource);
	if (handle == NULL) {
		return NULL;
	}
	return handle->toplevel;
}

static struct sf_foreign_toplevel_handle *foreign_handle_for_client(struct sf_server *server,
								    struct sf_toplevel *toplevel,
								    struct wl_client *client) {
	struct sf_foreign_toplevel_handle *handle;
	wl_list_for_each(handle, &server->foreign.handles, link) {
		if (handle->toplevel == toplevel &&
		    wl_resource_get_client(handle->resource) == client) {
			return handle;
		}
	}
	return NULL;
}

static struct wl_resource *output_resource_for_client(struct wlr_output *output,
						      struct wl_client *client) {
	struct wl_resource *resource;
	wl_resource_for_each(resource, &output->resources) {
		if (wl_resource_get_client(resource) == client) {
			return resource;
		}
	}
	return NULL;
}

static void sf_foreign_encode_state(struct sf_toplevel *toplevel, struct wl_array *states) {
	wl_array_init(states);

	uint32_t *state;

	if (sf_layout_sm_is_max_edges(&toplevel->layout) ||
	    sf_layout_sm_is_max_gaps(&toplevel->layout)) {
		state = wl_array_add(states, sizeof(uint32_t));
		if (state != NULL) {
			*state = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED;
		}
	}
	if (toplevel->minimized) {
		state = wl_array_add(states, sizeof(uint32_t));
		if (state != NULL) {
			*state = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED;
		}
	}
	if (sf_focused_toplevel(toplevel->server) == toplevel) {
		state = wl_array_add(states, sizeof(uint32_t));
		if (state != NULL) {
			*state = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED;
		}
	}
	if (sf_layout_sm_is_fullscreen(&toplevel->layout)) {
		state = wl_array_add(states, sizeof(uint32_t));
		if (state != NULL) {
			*state = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN;
		}
	}
}

static void foreign_send_handle_state(struct wl_resource *resource, struct sf_toplevel *toplevel) {
	struct wl_array states;
	sf_foreign_encode_state(toplevel, &states);
	zwlr_foreign_toplevel_handle_v1_send_state(resource, &states);
	wl_array_release(&states);
}

static void foreign_send_parent(struct sf_foreign_toplevel_handle *handle) {
	if (wl_resource_get_version(handle->resource) < 3) {
		return;
	}
	struct sf_toplevel *toplevel = handle->toplevel;
	if (toplevel == NULL) {
		return;
	}

	struct wl_resource *parent_resource = NULL;
	struct wlr_xdg_toplevel *parent = toplevel->xdg_toplevel->parent;
	if (parent != NULL && parent->base->data != NULL) {
		struct wlr_scene_tree *tree = parent->base->data;
		if (tree != NULL && tree->node.data != NULL) {
			struct sf_toplevel *parent_tl = tree->node.data;
			if (parent_tl->kind == SF_NODE_TOPLEVEL) {
				struct sf_foreign_toplevel_handle *parent_handle =
					foreign_handle_for_client(
						toplevel->server, parent_tl,
						wl_resource_get_client(handle->resource));
				if (parent_handle != NULL) {
					parent_resource = parent_handle->resource;
				}
			}
		}
	}
	zwlr_foreign_toplevel_handle_v1_send_parent(handle->resource, parent_resource);
}

static void foreign_handle_clear_outputs(struct sf_foreign_toplevel_handle *handle) {
	struct sf_foreign_handle_output *hop, *tmp;
	wl_list_for_each_safe(hop, tmp, &handle->outputs, link) {
		wl_list_remove(&hop->output_destroy.link);
		wl_list_remove(&hop->link);
		free(hop);
	}
}

static void foreign_handle_output_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_foreign_handle_output *hop = wl_container_of(listener, hop, output_destroy);
	struct sf_foreign_toplevel_handle *handle = hop->handle;
	struct wl_resource *output_resource =
		output_resource_for_client(hop->output, wl_resource_get_client(handle->resource));
	if (output_resource != NULL) {
		zwlr_foreign_toplevel_handle_v1_send_output_leave(handle->resource,
								  output_resource);
		zwlr_foreign_toplevel_handle_v1_send_done(handle->resource);
	}
	wl_list_remove(&hop->output_destroy.link);
	wl_list_remove(&hop->link);
	free(hop);
}

static bool toplevel_intersects_output(struct sf_toplevel *toplevel, struct wlr_output *output) {
	if (output == NULL || !output->enabled) {
		return false;
	}
	/*
	 * Fullscreen is reparented onto layers.fullscreen; mid-configure the
	 * client buffer can be 0×0 or still the pre-fs size. Taskbars that key
	 * off output_enter would then drop the window. Pin to the workspace
	 * (or center) head while fullscreen so the handle stays advertised.
	 */
	if (sf_layout_sm_is_fullscreen(&toplevel->layout)) {
		if (toplevel->workspace != NULL && toplevel->workspace->output != NULL) {
			return toplevel->workspace->output->wlr_output == output;
		}
		struct wlr_box fs;
		sf_toplevel_layout_geom(toplevel, &fs);
		double cx = fs.x + fs.width / 2.0;
		double cy = fs.y + fs.height / 2.0;
		struct wlr_output *at =
			wlr_output_layout_output_at(toplevel->server->output_layout, cx, cy);
		return at == output;
	}
	struct wlr_box geom, out_box, inter;
	sf_toplevel_layout_geom(toplevel, &geom);
	if (geom.width <= 0 || geom.height <= 0) {
		return false;
	}
	wlr_output_layout_get_box(toplevel->server->output_layout, output, &out_box);
	if (out_box.width <= 0 || out_box.height <= 0) {
		return false;
	}
	return wlr_box_intersection(&inter, &geom, &out_box);
}

static void foreign_handle_enter_output(struct sf_foreign_toplevel_handle *handle,
					struct wlr_output *output) {
	struct sf_foreign_handle_output *hop;
	wl_list_for_each(hop, &handle->outputs, link) {
		if (hop->output == output) {
			return;
		}
	}

	hop = calloc(1, sizeof(*hop));
	if (hop == NULL) {
		return;
	}
	hop->output = output;
	hop->handle = handle;
	hop->output_destroy.notify = foreign_handle_output_destroy;
	wl_signal_add(&output->events.destroy, &hop->output_destroy);
	wl_list_insert(&handle->outputs, &hop->link);

	struct wl_resource *output_resource =
		output_resource_for_client(output, wl_resource_get_client(handle->resource));
	if (output_resource != NULL) {
		zwlr_foreign_toplevel_handle_v1_send_output_enter(handle->resource,
								  output_resource);
	}
}

static void foreign_handle_leave_output(struct sf_foreign_toplevel_handle *handle,
					struct sf_foreign_handle_output *hop) {
	struct wl_resource *output_resource =
		output_resource_for_client(hop->output, wl_resource_get_client(handle->resource));
	if (output_resource != NULL) {
		zwlr_foreign_toplevel_handle_v1_send_output_leave(handle->resource,
								  output_resource);
	}
	wl_list_remove(&hop->output_destroy.link);
	wl_list_remove(&hop->link);
	free(hop);
}

static void foreign_handle_sync_outputs(struct sf_foreign_toplevel_handle *handle) {
	struct sf_toplevel *toplevel = handle->toplevel;
	if (toplevel == NULL) {
		return;
	}

	struct sf_foreign_handle_output *hop, *tmp;
	wl_list_for_each_safe(hop, tmp, &handle->outputs, link) {
		if (!toplevel_intersects_output(toplevel, hop->output)) {
			foreign_handle_leave_output(handle, hop);
		}
	}

	struct sf_output *output;
	wl_list_for_each(output, &toplevel->server->outputs, link) {
		if (toplevel_intersects_output(toplevel, output->wlr_output)) {
			foreign_handle_enter_output(handle, output->wlr_output);
		}
	}
}

static void foreign_send_handle_props(struct sf_foreign_toplevel_handle *handle) {
	struct sf_toplevel *toplevel = handle->toplevel;
	if (toplevel == NULL) {
		return;
	}

	if (toplevel->xdg_toplevel->title != NULL) {
		zwlr_foreign_toplevel_handle_v1_send_title(handle->resource,
							   toplevel->xdg_toplevel->title);
	}
	if (toplevel->xdg_toplevel->app_id != NULL) {
		zwlr_foreign_toplevel_handle_v1_send_app_id(handle->resource,
							    toplevel->xdg_toplevel->app_id);
	}
	foreign_handle_sync_outputs(handle);
	foreign_send_handle_state(handle->resource, toplevel);
	foreign_send_parent(handle);
	zwlr_foreign_toplevel_handle_v1_send_done(handle->resource);
}

/*
 * After `closed`, the client must destroy the handle object. Destroying the
 * resource here races that destroy request and kills the shell with
 * "error in client communication" (seen every gcr-prompter unmap).
 */
static void foreign_close_handles_for(struct sf_toplevel *toplevel) {
	struct sf_server *server = toplevel->server;
	struct sf_foreign_toplevel_handle *handle, *tmp;
	wl_list_for_each_safe(handle, tmp, &server->foreign.handles, link) {
		if (handle->toplevel != toplevel) {
			continue;
		}
		handle->toplevel = NULL;
		foreign_handle_clear_outputs(handle);
		zwlr_foreign_toplevel_handle_v1_send_closed(handle->resource);
		wl_list_remove(&handle->link);
		wl_list_init(&handle->link);
	}
}

static void foreign_handle_destroy_cb(struct wl_resource *resource) {
	struct sf_foreign_toplevel_handle *handle = wl_resource_get_user_data(resource);
	if (handle == NULL) {
		return;
	}
	foreign_handle_clear_outputs(handle);
	if (!wl_list_empty(&handle->link)) {
		wl_list_remove(&handle->link);
	}
	free(handle);
}

static void foreign_handle_set_maximized(struct wl_client *client, struct wl_resource *resource) {
	(void)client;
	struct sf_toplevel *toplevel = foreign_toplevel_from_resource(resource);
	if (toplevel == NULL) {
		return;
	}
	sf_toplevel_set_max_edges(toplevel, true);
}

static void foreign_handle_unset_maximized(struct wl_client *client, struct wl_resource *resource) {
	(void)client;
	struct sf_toplevel *toplevel = foreign_toplevel_from_resource(resource);
	if (toplevel == NULL) {
		return;
	}
	sf_toplevel_set_max_edges(toplevel, false);
}

static void foreign_handle_set_minimized(struct wl_client *client, struct wl_resource *resource) {
	(void)client;
	struct sf_toplevel *toplevel = foreign_toplevel_from_resource(resource);
	if (toplevel == NULL) {
		return;
	}
	SF_LOGI("foreign set_minimized", sf_i64("id", (int64_t)toplevel->id));
	sf_toplevel_minimize(toplevel);
}

static void foreign_handle_unset_minimized(struct wl_client *client, struct wl_resource *resource) {
	(void)client;
	struct sf_toplevel *toplevel = foreign_toplevel_from_resource(resource);
	if (toplevel == NULL) {
		return;
	}
	SF_LOGI("foreign unset_minimized", sf_i64("id", (int64_t)toplevel->id));
	sf_toplevel_unminimize(toplevel);
}

static void foreign_handle_activate(struct wl_client *client, struct wl_resource *resource,
				    struct wl_resource *seat_resource) {
	(void)client;
	(void)seat_resource;
	struct sf_toplevel *toplevel = foreign_toplevel_from_resource(resource);
	if (toplevel == NULL) {
		return;
	}
	SF_LOGI("foreign activate", sf_i64("id", (int64_t)toplevel->id));
	sf_workspace_reveal_toplevel(toplevel);
	sf_focus_toplevel(toplevel);
}

static void foreign_handle_close(struct wl_client *client, struct wl_resource *resource) {
	(void)client;
	struct sf_toplevel *toplevel = foreign_toplevel_from_resource(resource);
	if (toplevel == NULL) {
		return;
	}
	SF_LOGI("foreign close req", sf_i64("id", (int64_t)toplevel->id));
	wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);
}

static void foreign_handle_set_rectangle(struct wl_client *client, struct wl_resource *resource,
					 struct wl_resource *surface_resource, int32_t x, int32_t y,
					 int32_t width, int32_t height) {
	(void)client;
	(void)surface_resource;
	(void)x;
	(void)y;
	if (width < 0 || height < 0) {
		wl_resource_post_error(resource,
				       ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_ERROR_INVALID_RECTANGLE,
				       "invalid rectangle");
		return;
	}
}

static void foreign_handle_destroy_req(struct wl_client *client, struct wl_resource *resource) {
	(void)client;
	wl_resource_destroy(resource);
}

static void foreign_handle_set_fullscreen(struct wl_client *client, struct wl_resource *resource,
					  struct wl_resource *output_resource) {
	(void)client;
	struct sf_toplevel *toplevel = foreign_toplevel_from_resource(resource);
	if (toplevel == NULL) {
		return;
	}
	struct wlr_output *hint = NULL;
	if (output_resource != NULL) {
		hint = wlr_output_from_resource(output_resource);
	}
	sf_toplevel_set_fullscreen(toplevel, true, hint);
}

static void foreign_handle_unset_fullscreen(struct wl_client *client,
					    struct wl_resource *resource) {
	(void)client;
	struct sf_toplevel *toplevel = foreign_toplevel_from_resource(resource);
	if (toplevel == NULL) {
		return;
	}
	sf_toplevel_set_fullscreen(toplevel, false, NULL);
}

static const struct zwlr_foreign_toplevel_handle_v1_interface foreign_handle_impl = {
	.set_maximized = foreign_handle_set_maximized,
	.unset_maximized = foreign_handle_unset_maximized,
	.set_minimized = foreign_handle_set_minimized,
	.unset_minimized = foreign_handle_unset_minimized,
	.activate = foreign_handle_activate,
	.close = foreign_handle_close,
	.set_rectangle = foreign_handle_set_rectangle,
	.destroy = foreign_handle_destroy_req,
	.set_fullscreen = foreign_handle_set_fullscreen,
	.unset_fullscreen = foreign_handle_unset_fullscreen,
};

static struct sf_foreign_toplevel_handle *foreign_create_handle(struct sf_server *server,
								struct wl_client *client,
								uint32_t version,
								struct sf_toplevel *toplevel) {
	struct sf_foreign_toplevel_handle *handle = calloc(1, sizeof(*handle));
	if (handle == NULL) {
		return NULL;
	}

	handle->server = server;
	handle->toplevel = toplevel;
	wl_list_init(&handle->outputs);
	handle->resource = wl_resource_create(client, &zwlr_foreign_toplevel_handle_v1_interface,
					      (int)version, 0);
	if (handle->resource == NULL) {
		free(handle);
		return NULL;
	}

	wl_resource_set_implementation(handle->resource, &foreign_handle_impl, handle,
				       foreign_handle_destroy_cb);
	wl_list_insert(&server->foreign.handles, &handle->link);
	return handle;
}

static void foreign_announce_toplevel(struct sf_foreign_manager_resource *mgr,
				      struct sf_toplevel *toplevel) {
	uint32_t version = wl_resource_get_version(mgr->resource);
	struct sf_foreign_toplevel_handle *handle = foreign_create_handle(
		mgr->server, wl_resource_get_client(mgr->resource), version, toplevel);
	if (handle == NULL) {
		return;
	}
	zwlr_foreign_toplevel_manager_v1_send_toplevel(mgr->resource, handle->resource);
	foreign_send_handle_props(handle);
}

static void foreign_manager_stop(struct wl_client *client, struct wl_resource *resource) {
	(void)client;
	zwlr_foreign_toplevel_manager_v1_send_finished(resource);
	wl_resource_destroy(resource);
}

static const struct zwlr_foreign_toplevel_manager_v1_interface foreign_manager_impl = {
	.stop = foreign_manager_stop,
};

static void foreign_manager_destroy_cb(struct wl_resource *resource) {
	struct sf_foreign_manager_resource *mgr = wl_resource_get_user_data(resource);
	if (mgr == NULL) {
		return;
	}

	struct wl_client *client = wl_resource_get_client(resource);
	struct sf_server *server = mgr->server;
	struct sf_foreign_toplevel_handle *handle, *tmp;
	wl_list_for_each_safe(handle, tmp, &server->foreign.handles, link) {
		if (wl_resource_get_client(handle->resource) != client) {
			continue;
		}
		handle->toplevel = NULL;
		foreign_handle_clear_outputs(handle);
		wl_resource_destroy(handle->resource);
	}

	wl_list_remove(&mgr->link);
	free(mgr);
}

static void foreign_manager_bind(struct wl_client *client, void *data, uint32_t version,
				 uint32_t id) {
	struct sf_server *server = data;

	struct wl_resource *resource = wl_resource_create(
		client, &zwlr_foreign_toplevel_manager_v1_interface, (int)version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	struct sf_foreign_manager_resource *mgr = calloc(1, sizeof(*mgr));
	if (mgr == NULL) {
		wl_client_post_no_memory(client);
		wl_resource_destroy(resource);
		return;
	}
	mgr->server = server;
	mgr->resource = resource;
	wl_list_insert(&server->foreign.managers, &mgr->link);

	wl_resource_set_implementation(resource, &foreign_manager_impl, mgr,
				       foreign_manager_destroy_cb);
	SF_LOGI("foreign manager bind", sf_i64("version", (int64_t)version));

	struct sf_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		foreign_announce_toplevel(mgr, toplevel);
	}
}

void sf_foreign_toplevel_setup(struct sf_server *server) {
	wl_list_init(&server->foreign.handles);
	wl_list_init(&server->foreign.managers);

	server->foreign.wlr_global =
		wl_global_create(server->wl_display, &zwlr_foreign_toplevel_manager_v1_interface, 3,
				 server, foreign_manager_bind);

	if (server->foreign.wlr_global == NULL) {
		SF_LOGW("foreign toplevel", sf_str("err", "failed to create global"));
		return;
	}

	SF_LOGI("foreign toplevel management v1", sf_str("version", "3"));
}

void sf_foreign_toplevel_finish(struct sf_server *server) {
	if (server->foreign.wlr_global != NULL) {
		wl_global_destroy(server->foreign.wlr_global);
		server->foreign.wlr_global = NULL;
	}

	struct sf_foreign_toplevel_handle *handle, *handle_tmp;
	wl_list_for_each_safe(handle, handle_tmp, &server->foreign.handles, link) {
		handle->toplevel = NULL;
		foreign_handle_clear_outputs(handle);
		wl_resource_destroy(handle->resource);
	}

	struct sf_foreign_manager_resource *mgr, *mgr_tmp;
	wl_list_for_each_safe(mgr, mgr_tmp, &server->foreign.managers, link) {
		wl_resource_set_user_data(mgr->resource, NULL);
		wl_list_remove(&mgr->link);
		free(mgr);
	}
}

void sf_foreign_toplevel_on_map(struct sf_toplevel *toplevel) {
	struct sf_server *server = toplevel->server;
	if (wl_list_empty(&server->foreign.managers)) {
		return;
	}

	SF_LOGI("foreign on_map", sf_i64("id", (int64_t)toplevel->id));

	struct sf_foreign_manager_resource *mgr;
	wl_list_for_each(mgr, &server->foreign.managers, link) {
		foreign_announce_toplevel(mgr, toplevel);
	}

	/* Parent may map after a transient child; refresh those parent links. */
	struct sf_toplevel *child;
	wl_list_for_each(child, &server->toplevels, link) {
		if (child->xdg_toplevel->parent == toplevel->xdg_toplevel) {
			sf_foreign_toplevel_on_parent_change(child);
		}
	}
}

void sf_foreign_toplevel_on_unmap(struct sf_toplevel *toplevel) {
	SF_LOGI("foreign on_unmap", sf_i64("id", (int64_t)toplevel->id));
	foreign_close_handles_for(toplevel);
}

void sf_foreign_toplevel_on_title_change(struct sf_toplevel *toplevel) {
	struct sf_foreign_toplevel_handle *handle;
	wl_list_for_each(handle, &toplevel->server->foreign.handles, link) {
		if (handle->toplevel != toplevel) {
			continue;
		}
		const char *title = toplevel->xdg_toplevel->title;
		zwlr_foreign_toplevel_handle_v1_send_title(handle->resource,
							   title != NULL ? title : "");
		zwlr_foreign_toplevel_handle_v1_send_done(handle->resource);
	}
}

void sf_foreign_toplevel_on_app_id_change(struct sf_toplevel *toplevel) {
	struct sf_foreign_toplevel_handle *handle;
	wl_list_for_each(handle, &toplevel->server->foreign.handles, link) {
		if (handle->toplevel != toplevel) {
			continue;
		}
		const char *app_id = toplevel->xdg_toplevel->app_id;
		zwlr_foreign_toplevel_handle_v1_send_app_id(handle->resource,
							    app_id != NULL ? app_id : "");
		zwlr_foreign_toplevel_handle_v1_send_done(handle->resource);
	}
}

void sf_foreign_toplevel_on_parent_change(struct sf_toplevel *toplevel) {
	struct sf_foreign_toplevel_handle *handle;
	wl_list_for_each(handle, &toplevel->server->foreign.handles, link) {
		if (handle->toplevel != toplevel) {
			continue;
		}
		foreign_send_parent(handle);
		zwlr_foreign_toplevel_handle_v1_send_done(handle->resource);
	}
}

void sf_foreign_toplevel_on_state_change(struct sf_toplevel *toplevel) {
	struct sf_foreign_toplevel_handle *handle;
	wl_list_for_each(handle, &toplevel->server->foreign.handles, link) {
		if (handle->toplevel != toplevel) {
			continue;
		}
		foreign_send_handle_state(handle->resource, toplevel);
		zwlr_foreign_toplevel_handle_v1_send_done(handle->resource);
	}
}

void sf_foreign_toplevel_on_outputs_change(struct sf_toplevel *toplevel) {
	struct sf_foreign_toplevel_handle *handle;
	wl_list_for_each(handle, &toplevel->server->foreign.handles, link) {
		if (handle->toplevel != toplevel) {
			continue;
		}
		foreign_handle_sync_outputs(handle);
		zwlr_foreign_toplevel_handle_v1_send_done(handle->resource);
	}
}

void sf_foreign_toplevel_on_close(struct sf_toplevel *toplevel) {
	foreign_close_handles_for(toplevel);
}
