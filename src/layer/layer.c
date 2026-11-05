#include <stdlib.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>

#include "decor/decor.h"
#include "layer/layer.h"
#include "log/log.h"
#include "output/output.h"
#include "session/session.h"
#include "wm/wm.h"
#include "xdg/xdg.h"

/* Optimized blur caches wallpaper/bottom; refresh when those layers change. */
static void layer_maybe_dirty_blur(struct stonefly_layer_surface *layer) {
	if (layer == NULL || layer->layer_surface == NULL) {
		return;
	}
	enum zwlr_layer_shell_v1_layer z = layer->layer_surface->current.layer;
	if (z == ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND || z == ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM) {
		stonefly_decor_mark_blur_dirty(layer->server);
	}
}

#define STONEFLY_LAYER_SHELL_VERSION 5

struct wlr_scene_tree *stonefly_layer_tree(struct stonefly_server *server,
					   enum zwlr_layer_shell_v1_layer layer) {
	switch (layer) {
	case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
		return server->layers.background;
	case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
		return server->layers.bottom;
	case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
		return server->layers.top;
	case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
		return server->layers.overlay;
	}
	return server->layers.top;
}

bool stonefly_output_usable_box(struct stonefly_output *output, struct wlr_box *box) {
	if (output == NULL || box == NULL) {
		return false;
	}
	*box = output->usable;
	return box->width > 0 && box->height > 0;
}

bool stonefly_layout_usable_at(struct stonefly_server *server, double lx, double ly,
			       struct wlr_box *box) {
	struct wlr_output *wlr_output = wlr_output_layout_output_at(server->output_layout, lx, ly);
	if (wlr_output == NULL) {
		return false;
	}
	struct stonefly_output *output = wlr_output->data;
	if (output == NULL) {
		wlr_output_layout_get_box(server->output_layout, wlr_output, box);
		return box->width > 0 && box->height > 0;
	}
	return stonefly_output_usable_box(output, box);
}

static void layer_focus_interactive(struct stonefly_output *output) {
	struct stonefly_server *server = output->server;
	if (stonefly_session_locked(server)) {
		return;
	}
	static const enum zwlr_layer_shell_v1_layer above[] = {
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP,
	};

	for (size_t i = 0; i < sizeof(above) / sizeof(above[0]); i++) {
		struct stonefly_layer_surface *layer;
		wl_list_for_each_reverse(layer, &output->layers[above[i]], link) {
			struct wlr_layer_surface_v1 *ls = layer->layer_surface;
			if (!ls->surface->mapped) {
				continue;
			}
			/* Day-zero: exclusive only; on_demand stays with the client click. */
			if (ls->current.keyboard_interactive !=
			    ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE) {
				continue;
			}

			struct wlr_surface *prev = server->seat->keyboard_state.focused_surface;
			if (prev != NULL && prev != ls->surface) {
				struct wlr_xdg_toplevel *prev_xdg =
					wlr_xdg_toplevel_try_from_wlr_surface(prev);
				if (prev_xdg != NULL) {
					wlr_xdg_toplevel_set_activated(prev_xdg, false);
					struct wlr_surface *root =
						wlr_surface_get_root_surface(prev);
					struct stonefly_toplevel *t;
					wl_list_for_each(t, &server->toplevels, link) {
						if (t->xdg_toplevel->base->surface == root) {
							stonefly_decor_set_focused(t, false);
							break;
						}
					}
				}
			}
			server->exclusive_layer = layer;
			struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
			if (kb != NULL) {
				wlr_seat_keyboard_notify_enter(server->seat, ls->surface,
							       kb->keycodes, kb->num_keycodes,
							       &kb->modifiers);
			} else {
				wlr_seat_keyboard_notify_enter(server->seat, ls->surface, NULL, 0,
							       NULL);
			}
			return;
		}
	}

	if (server->exclusive_layer != NULL && server->exclusive_layer->output == output) {
		server->exclusive_layer = NULL;
		stonefly_focus_top(server);
	}
}

static void layer_arrange_list(struct stonefly_output *output, struct wl_list *list,
			       struct wlr_box *usable, bool exclusive) {
	struct wlr_box full = output->full;
	struct stonefly_layer_surface *layer;
	wl_list_for_each(layer, list, link) {
		struct wlr_layer_surface_v1 *ls = layer->layer_surface;
		/* wlroots 0.20 asserts configure only after the first commit. */
		if (!ls->initialized) {
			continue;
		}
		struct wlr_layer_surface_v1_state *state = &ls->current;
		/* exclusive_zone -1 means "size of the surface" (labwc !!zone). */
		bool wants_exclusive = state->exclusive_zone != 0;
		if (exclusive != wants_exclusive) {
			continue;
		}
		wlr_scene_layer_surface_v1_configure(layer->scene_layer, &full, usable);
		wlr_scene_node_set_position(&layer->popups->node, layer->scene->node.x,
					    layer->scene->node.y);
	}
}

void stonefly_layer_arrange_output(struct stonefly_output *output) {
	if (output == NULL || output->wlr_output == NULL) {
		return;
	}
	if (!output->wlr_output->enabled) {
		return;
	}

	struct stonefly_server *server = output->server;
	struct wlr_box full = {0};
	wlr_output_layout_get_box(server->output_layout, output->wlr_output, &full);
	if (full.width <= 0 || full.height <= 0) {
		return;
	}
	output->full = full;

	struct wlr_box usable = full;
	struct wlr_box prev_usable = output->usable;

	for (int i = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY; i >= 0; i--) {
		layer_arrange_list(output, &output->layers[i], &usable, true);
	}
	output->usable = usable;

	for (int i = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY; i >= 0; i--) {
		layer_arrange_list(output, &output->layers[i], &usable, false);
	}

	if (prev_usable.x != output->usable.x || prev_usable.y != output->usable.y ||
	    prev_usable.width != output->usable.width ||
	    prev_usable.height != output->usable.height) {
		SF_LOGI("usable area", sf_str("output", output->wlr_output->name),
			sf_i64("x", output->usable.x), sf_i64("y", output->usable.y),
			sf_i64("w", output->usable.width), sf_i64("h", output->usable.height));
		/* Peeble UsableAreaChanged: bar exclusive often arrives after map. */
		stonefly_wm_relayout_output(output);
	}

	layer_focus_interactive(output);
}

static void layer_surface_map(struct wl_listener *listener, void *data) {
	(void)data;
	struct stonefly_layer_surface *layer = wl_container_of(listener, layer, map);
	if (layer->output != NULL) {
		stonefly_layer_arrange_output(layer->output);
	}
	layer_maybe_dirty_blur(layer);
}

static void layer_surface_unmap(struct wl_listener *listener, void *data) {
	(void)data;
	struct stonefly_layer_surface *layer = wl_container_of(listener, layer, unmap);
	struct stonefly_server *server = layer->server;

	if (server->exclusive_layer == layer) {
		server->exclusive_layer = NULL;
	}
	if (layer->output != NULL) {
		stonefly_layer_arrange_output(layer->output);
	}
	if (server->seat->keyboard_state.focused_surface == layer->layer_surface->surface) {
		stonefly_focus_top(server);
	}
	layer_maybe_dirty_blur(layer);
}

static void layer_surface_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct stonefly_layer_surface *layer = wl_container_of(listener, layer, commit);
	struct wlr_layer_surface_v1 *ls = layer->layer_surface;
	struct wlr_output *wlr_output = ls->output;

	if (wlr_output == NULL) {
		return;
	}
	struct stonefly_output *output = wlr_output->data;
	if (output == NULL) {
		return;
	}
	layer->output = output;

	struct wlr_scene_tree *tree = stonefly_layer_tree(layer->server, ls->current.layer);
	if (layer->scene->node.parent != tree) {
		wlr_scene_node_reparent(&layer->scene->node, tree);
		wlr_scene_node_reparent(&layer->popups->node, tree);
		wl_list_remove(&layer->link);
		wl_list_insert(&output->layers[ls->current.layer], &layer->link);
	}
	/* Keep popups of bottom/background layers above shell windows. */
	if (ls->current.layer < ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
		wlr_scene_node_reparent(&layer->popups->node, layer->server->layers.top);
	}

	if (ls->current.committed != 0 || ls->initial_commit) {
		stonefly_layer_arrange_output(output);
	}
	/* Buffer damage on wallpaper/bottom also needs a blur cache refresh. */
	if (ls->current.committed != 0 || ls->initial_commit ||
	    ls->surface->current.committed != 0) {
		layer_maybe_dirty_blur(layer);
	}
}

/*
 * Layer-shell popups are created with xdg parent NULL, then assigned here via
 * zwlr_layer_surface_v1.get_popup. The xdg_shell new_popup handler parks them
 * under overlay until this runs; reparent onto the layer popup tree.
 */
static void layer_surface_new_popup(struct wl_listener *listener, void *data) {
	struct stonefly_layer_surface *layer = wl_container_of(listener, layer, new_popup);
	struct wlr_xdg_popup *xdg_popup = data;
	struct wlr_scene_tree *tree = xdg_popup->base->data;

	if (tree != NULL) {
		wlr_scene_node_reparent(&tree->node, layer->popups);
	}
}

static void layer_surface_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct stonefly_layer_surface *layer = wl_container_of(listener, layer, destroy);
	struct stonefly_server *server = layer->server;
	struct stonefly_output *output = layer->output;
	bool restore_focus =
		server->exclusive_layer == layer ||
		server->seat->keyboard_state.focused_surface == layer->layer_surface->surface;

	if (server->exclusive_layer == layer) {
		server->exclusive_layer = NULL;
	}

	wl_list_remove(&layer->link);
	wl_list_remove(&layer->map.link);
	wl_list_remove(&layer->unmap.link);
	wl_list_remove(&layer->commit.link);
	wl_list_remove(&layer->new_popup.link);
	wl_list_remove(&layer->destroy.link);

	/* scene_layer tree is destroyed with the wlr_layer_surface. */
	wlr_scene_node_destroy(&layer->popups->node);
	free(layer);

	if (output != NULL) {
		stonefly_layer_arrange_output(output);
	}
	if (restore_focus && server->exclusive_layer == NULL) {
		stonefly_focus_top(server);
	}
}

static struct wlr_output *pick_layer_output(struct stonefly_server *server) {
	struct wlr_output *under = wlr_output_layout_output_at(
		server->output_layout, server->cursor->x, server->cursor->y);
	if (under != NULL) {
		return under;
	}
	struct stonefly_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (output->wlr_output->enabled) {
			return output->wlr_output;
		}
	}
	return NULL;
}

static void server_new_layer_surface(struct wl_listener *listener, void *data) {
	struct stonefly_server *server = wl_container_of(listener, server, new_layer_surface);
	struct wlr_layer_surface_v1 *ls = data;

	if (ls->output == NULL) {
		ls->output = pick_layer_output(server);
	}
	if (ls->output == NULL) {
		SF_LOGW("layer surface with no output; destroying",
			sf_str("namespace", ls->namespace != NULL ? ls->namespace : ""));
		wlr_layer_surface_v1_destroy(ls);
		return;
	}

	struct stonefly_output *output = ls->output->data;
	if (output == NULL) {
		SF_LOGW("layer surface output not ready; destroying",
			sf_str("namespace", ls->namespace != NULL ? ls->namespace : ""));
		wlr_layer_surface_v1_destroy(ls);
		return;
	}

	enum zwlr_layer_shell_v1_layer layer_idx = ls->pending.layer;
	struct wlr_scene_tree *parent = stonefly_layer_tree(server, layer_idx);

	struct stonefly_layer_surface *layer = calloc(1, sizeof(*layer));
	if (layer == NULL) {
		wlr_layer_surface_v1_destroy(ls);
		return;
	}
	layer->kind = STONEFLY_NODE_LAYER;
	layer->server = server;
	layer->output = output;
	layer->layer_surface = ls;
	ls->data = layer;

	layer->scene_layer = wlr_scene_layer_surface_v1_create(parent, ls);
	layer->scene = layer->scene_layer->tree;
	layer->scene->node.data = layer;
	layer->popups = wlr_scene_tree_create(parent);
	ls->surface->data = layer->popups;

	wl_list_insert(&output->layers[layer_idx], &layer->link);

	layer->map.notify = layer_surface_map;
	wl_signal_add(&ls->surface->events.map, &layer->map);
	layer->unmap.notify = layer_surface_unmap;
	wl_signal_add(&ls->surface->events.unmap, &layer->unmap);
	layer->commit.notify = layer_surface_commit;
	wl_signal_add(&ls->surface->events.commit, &layer->commit);
	layer->new_popup.notify = layer_surface_new_popup;
	wl_signal_add(&ls->events.new_popup, &layer->new_popup);
	layer->destroy.notify = layer_surface_destroy;
	wl_signal_add(&ls->events.destroy, &layer->destroy);

	SF_LOGI("layer surface", sf_str("namespace", ls->namespace != NULL ? ls->namespace : ""),
		sf_str("output", ls->output->name));
	/* Do not configure until the client's first commit (initialized).
	 * Early configure aborts wlroots 0.20:
	 * assert(surface->initialized) in wlr_layer_surface_v1_configure. */
}

void stonefly_layer_setup(struct stonefly_server *server) {
	server->layers.background = wlr_scene_tree_create(&server->scene->tree);
	server->layers.bottom = wlr_scene_tree_create(&server->scene->tree);
	/* Optimized blur samples everything below (wallpaper / bottom). */
	server->blur_layer = wlr_scene_optimized_blur_create(&server->scene->tree, 0, 0);
	server->layers.toplevel = wlr_scene_tree_create(&server->scene->tree);
	server->layers.top = wlr_scene_tree_create(&server->scene->tree);
	server->layers.overlay = wlr_scene_tree_create(&server->scene->tree);

	server->layer_shell =
		wlr_layer_shell_v1_create(server->wl_display, STONEFLY_LAYER_SHELL_VERSION);
	server->new_layer_surface.notify = server_new_layer_surface;
	wl_signal_add(&server->layer_shell->events.new_surface, &server->new_layer_surface);
	server->exclusive_layer = NULL;

	SF_LOGI("layer shell", sf_i64("version", STONEFLY_LAYER_SHELL_VERSION));
}

void stonefly_layer_finish(struct stonefly_server *server) {
	wl_list_remove(&server->new_layer_surface.link);
}
