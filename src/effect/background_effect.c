#include <pixman.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/addon.h>

#include "decor/decor.h"
#include "effect/background_effect.h"
#include "log/log.h"
#include "server/server.h"
#include "toplevel/toplevel.h"

#include "ext-background-effect-v1-protocol.h"

struct sf_background_effect {
	struct wl_resource *resource;
	struct sf_server *server;
	struct wlr_surface *surface;
	struct wlr_addon addon;
	struct wl_listener surface_commit;

	pixman_region32_t pending;
	bool pending_set;
	pixman_region32_t current;
};

static void effect_destroy(struct sf_background_effect *effect);

/* Resolve xdg toplevels before map (Foot often binds blur early). */
static struct sf_toplevel *toplevel_for_surface(struct sf_server *server,
						struct wlr_surface *surface) {
	(void)server;
	if (surface == NULL) {
		return NULL;
	}
	struct wlr_surface *root = wlr_surface_get_root_surface(surface);
	struct wlr_xdg_surface *xdg = wlr_xdg_surface_try_from_wlr_surface(root);
	if (xdg == NULL || xdg->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL || xdg->data == NULL) {
		return NULL;
	}
	struct wlr_scene_tree *tree = xdg->data;
	if (tree->node.data == NULL) {
		return NULL;
	}
	struct sf_toplevel *t = tree->node.data;
	if (t->kind != SF_NODE_TOPLEVEL) {
		return NULL;
	}
	return t;
}

static void effect_apply_client_blur(struct sf_background_effect *effect) {
	bool want = pixman_region32_not_empty(&effect->current);
	struct sf_toplevel *toplevel = toplevel_for_surface(effect->server, effect->surface);
	if (toplevel == NULL) {
		return;
	}
	if (toplevel->client_blur == want) {
		return;
	}
	toplevel->client_blur = want;
	sf_decor_update_geom(toplevel);
	sf_decor_refresh_blur(toplevel);
	SF_LOGI("client blur", sf_i64("id", (int64_t)toplevel->id), sf_bool("on", want));
}

static void effect_clear_client_blur(struct sf_background_effect *effect) {
	struct sf_toplevel *toplevel = toplevel_for_surface(effect->server, effect->surface);
	if (toplevel == NULL || !toplevel->client_blur) {
		return;
	}
	toplevel->client_blur = false;
	sf_decor_update_geom(toplevel);
	sf_decor_refresh_blur(toplevel);
}

static void effect_finish_body(struct sf_background_effect *effect) {
	if (effect->resource != NULL) {
		wl_resource_set_user_data(effect->resource, NULL);
		effect->resource = NULL;
	}
	pixman_region32_fini(&effect->pending);
	pixman_region32_fini(&effect->current);
	free(effect);
}

/*
 * Detach from the surface and free. Safe from the commit listener and from
 * the addon destroy callback. wlr_addon_finish must run whenever the addon
 * was initialized — wlroots set_finish asserts an empty list after destroy.
 */
static void effect_destroy(struct sf_background_effect *effect) {
	if (effect->surface != NULL) {
		effect_clear_client_blur(effect);
		wl_list_remove(&effect->surface_commit.link);
		wl_list_init(&effect->surface_commit.link);
		wlr_addon_finish(&effect->addon);
		effect->surface = NULL;
	}
	effect_finish_body(effect);
}

/* Surface addon-set teardown: must call wlr_addon_finish (wlroots contract). */
static void effect_addon_destroy(struct wlr_addon *addon) {
	struct sf_background_effect *effect = wl_container_of(addon, effect, addon);
	effect_destroy(effect);
}

static const struct wlr_addon_interface effect_addon_impl = {
	.name = "ext_background_effect_surface_v1",
	.destroy = effect_addon_destroy,
};

static void effect_surface_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_background_effect *effect = wl_container_of(listener, effect, surface_commit);
	if (!effect->pending_set) {
		return;
	}
	pixman_region32_copy(&effect->current, &effect->pending);
	effect->pending_set = false;
	effect_apply_client_blur(effect);

	/* Resource already gone after destroy request; free after apply. */
	if (effect->resource == NULL) {
		effect_destroy(effect);
	}
}

static void effect_surface_destroy_req(struct wl_client *client, struct wl_resource *resource) {
	(void)client;
	struct sf_background_effect *effect = wl_resource_get_user_data(resource);
	if (effect == NULL) {
		return;
	}
	if (effect->surface == NULL) {
		wl_resource_post_error(resource,
				       EXT_BACKGROUND_EFFECT_SURFACE_V1_ERROR_SURFACE_DESTROYED,
				       "surface already destroyed");
		return;
	}
	pixman_region32_clear(&effect->pending);
	effect->pending_set = true;
	wl_resource_destroy(resource);
}

static void effect_set_blur_region(struct wl_client *client, struct wl_resource *resource,
				   struct wl_resource *region_resource) {
	(void)client;
	struct sf_background_effect *effect = wl_resource_get_user_data(resource);
	if (effect == NULL) {
		return;
	}
	if (effect->surface == NULL) {
		wl_resource_post_error(resource,
				       EXT_BACKGROUND_EFFECT_SURFACE_V1_ERROR_SURFACE_DESTROYED,
				       "surface already destroyed");
		return;
	}

	pixman_region32_clear(&effect->pending);
	if (region_resource != NULL) {
		const pixman_region32_t *region = wlr_region_from_resource(region_resource);
		if (region != NULL) {
			pixman_region32_copy(&effect->pending, region);
		}
	}
	effect->pending_set = true;
}

static const struct ext_background_effect_surface_v1_interface effect_surface_impl = {
	.destroy = effect_surface_destroy_req,
	.set_blur_region = effect_set_blur_region,
};

static void effect_resource_destroy(struct wl_resource *resource) {
	struct sf_background_effect *effect = wl_resource_get_user_data(resource);
	if (effect == NULL) {
		return;
	}
	effect->resource = NULL;
	if (effect->surface == NULL) {
		effect_finish_body(effect);
		return;
	}
	/* Protocol: clear effect on next commit. */
	if (!effect->pending_set) {
		pixman_region32_clear(&effect->pending);
		effect->pending_set = true;
	}
}

static void manager_destroy(struct wl_client *client, struct wl_resource *resource) {
	(void)client;
	wl_resource_destroy(resource);
}

static void manager_get_background_effect(struct wl_client *client, struct wl_resource *resource,
					  uint32_t id, struct wl_resource *surface_resource) {
	struct sf_server *server = wl_resource_get_user_data(resource);
	struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);

	if (wlr_addon_find(&surface->addons, surface, &effect_addon_impl) != NULL) {
		wl_resource_post_error(
			resource, EXT_BACKGROUND_EFFECT_MANAGER_V1_ERROR_BACKGROUND_EFFECT_EXISTS,
			"surface already has a background effect object");
		return;
	}

	struct sf_background_effect *effect = calloc(1, sizeof(*effect));
	if (effect == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	effect->server = server;
	effect->surface = surface;
	pixman_region32_init(&effect->pending);
	pixman_region32_init(&effect->current);

	int version = wl_resource_get_version(resource);
	effect->resource = wl_resource_create(client, &ext_background_effect_surface_v1_interface,
					      version, id);
	if (effect->resource == NULL) {
		pixman_region32_fini(&effect->pending);
		pixman_region32_fini(&effect->current);
		free(effect);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(effect->resource, &effect_surface_impl, effect,
				       effect_resource_destroy);

	wlr_addon_init(&effect->addon, &surface->addons, surface, &effect_addon_impl);
	effect->surface_commit.notify = effect_surface_commit;
	wl_signal_add(&surface->events.commit, &effect->surface_commit);
}

static const struct ext_background_effect_manager_v1_interface manager_impl = {
	.destroy = manager_destroy,
	.get_background_effect = manager_get_background_effect,
};

static void manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
	struct sf_server *server = data;
	struct wl_resource *resource = wl_resource_create(
		client, &ext_background_effect_manager_v1_interface, (int)version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, server, NULL);
	ext_background_effect_manager_v1_send_capabilities(
		resource, EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR);
}

void sf_background_effect_setup(struct sf_server *server) {
	server->background_effect_global =
		wl_global_create(server->wl_display, &ext_background_effect_manager_v1_interface, 1,
				 server, manager_bind);
	if (server->background_effect_global == NULL) {
		SF_LOGW("background effect", sf_str("err", "failed to create global"));
		return;
	}
	SF_LOGI("ext-background-effect-v1", sf_str("capability", "blur"));
}

void sf_background_effect_finish(struct sf_server *server) {
	if (server->background_effect_global != NULL) {
		wl_global_destroy(server->background_effect_global);
		server->background_effect_global = NULL;
	}
}
