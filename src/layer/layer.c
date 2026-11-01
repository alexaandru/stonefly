#include <stdint.h>
#include <stdlib.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <scenefx/types/fx/clipped_region.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>

#include "config/config.h"
#include "decor/decor.h"
#include "layer/layer.h"
#include "log/log.h"
#include "output/output.h"
#include "session/session.h"
#include "wm/wm.h"
#include "toplevel/toplevel.h"
#include "xdg/xdg.h"

/* Optimized blur caches wallpaper/bottom; refresh when those layers change. */
static void layer_maybe_dirty_blur(struct sf_layer_surface *layer) {
	if (layer == NULL || layer->layer_surface == NULL) {
		return;
	}
	enum zwlr_layer_shell_v1_layer z = layer->layer_surface->current.layer;
	if (z == ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND || z == ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM) {
		sf_decor_mark_blur_dirty(layer->server);
	}
}

struct layer_blur_mask_ctx {
	struct wlr_surface *surface;
	struct wlr_scene_buffer *buffer;
};

static void layer_blur_mask_iter(struct wlr_scene_buffer *buffer, int sx, int sy, void *data) {
	(void)sx;
	(void)sy;
	struct layer_blur_mask_ctx *ctx = data;
	if (ctx->buffer != NULL) {
		return;
	}
	struct wlr_scene_surface *ss = wlr_scene_surface_try_from_buffer(buffer);
	if (ss != NULL && ss->surface == ctx->surface) {
		ctx->buffer = buffer;
	}
}

static struct wlr_scene_buffer *layer_find_surface_buffer(struct sf_layer_surface *layer) {
	if (layer->scene == NULL || layer->layer_surface == NULL) {
		return NULL;
	}
	struct layer_blur_mask_ctx ctx = {
		.surface = layer->layer_surface->surface,
		.buffer = NULL,
	};
	wlr_scene_node_for_each_buffer(&layer->scene->node, layer_blur_mask_iter, &ctx);
	return ctx.buffer;
}

static void layer_blur_clamp(int surf_w, int surf_h, int *bx, int *by, int *bw, int *bh) {
	if (*bx < 0) {
		*bw += *bx;
		*bx = 0;
	}
	if (*by < 0) {
		*bh += *by;
		*by = 0;
	}
	if (*bx + *bw > surf_w) {
		*bw = surf_w - *bx;
	}
	if (*by + *bh > surf_h) {
		*bh = surf_h - *by;
	}
	if (*bw < 0) {
		*bw = 0;
	}
	if (*bh < 0) {
		*bh = 0;
	}
}

static bool layer_blur_box_from_region(const pixman_region32_t *region, int surf_w, int surf_h,
				       int *bx, int *by, int *bw, int *bh) {
	if (region == NULL || !pixman_region32_not_empty(region)) {
		return false;
	}
	pixman_box32_t *e = pixman_region32_extents(region);
	*bx = e->x1;
	*by = e->y1;
	*bw = e->x2 - e->x1;
	*bh = e->y2 - e->y1;
	layer_blur_clamp(surf_w, surf_h, bx, by, bw, bh);
	return *bw > 0 && *bh > 0;
}

static bool layer_blur_region_is_full(int bx, int by, int bw, int bh, int surf_w, int surf_h) {
	return bx <= 0 && by <= 0 && bw >= surf_w && bh >= surf_h;
}

/* Keep the smallest non-full candidate (bars/panels: cut soft-shadow pad). */
static void layer_blur_consider(int *bx, int *by, int *bw, int *bh, int cx, int cy, int cw, int ch,
				int surf_w, int surf_h) {
	if (cw <= 0 || ch <= 0 || layer_blur_region_is_full(cx, cy, cw, ch, surf_w, surf_h)) {
		return;
	}
	bool best_full = layer_blur_region_is_full(*bx, *by, *bw, *bh, surf_w, surf_h);
	int64_t area = (int64_t)cw * (int64_t)ch;
	int64_t best = (int64_t)(*bw) * (int64_t)(*bh);
	if (best_full || area < best) {
		*bx = cx;
		*by = cy;
		*bw = cw;
		*bh = ch;
	}
}

/* Exclusive-edge shells (bars): zone is the opaque strip; buffer often taller
 * for a soft shadow. */
static bool layer_blur_box_exclusive(struct wlr_layer_surface_v1 *ls, int surf_w, int surf_h,
				     int *bx, int *by, int *bw, int *bh) {
	int32_t ex = ls->current.exclusive_zone;
	if (ex <= 0 || surf_w <= 0 || surf_h <= 0) {
		return false;
	}
	*bx = 0;
	*by = 0;
	*bw = surf_w;
	*bh = surf_h;
	uint32_t anchor = (uint32_t)ls->current.anchor;
	bool top = (anchor & (uint32_t)ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) != 0;
	bool bottom = (anchor & (uint32_t)ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) != 0;
	bool left = (anchor & (uint32_t)ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT) != 0;
	bool right = (anchor & (uint32_t)ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT) != 0;
	if (top && !bottom) {
		*bh = ex < surf_h ? ex : surf_h;
	} else if (bottom && !top) {
		*bh = ex < surf_h ? ex : surf_h;
		*by = surf_h - *bh;
	} else if (left && !right) {
		*bw = ex < surf_w ? ex : surf_w;
	} else if (right && !left) {
		*bw = ex < surf_w ? ex : surf_w;
		*bx = surf_w - *bw;
	} else {
		return false;
	}
	return *bw > 0 && *bh > 0 && !layer_blur_region_is_full(*bx, *by, *bw, *bh, surf_w, surf_h);
}

/* Fallback when the effect region is empty or a single full-surface rect. */
static void layer_blur_box_fallback(struct sf_layer_surface *layer, int surf_w, int surf_h, int *bx,
				    int *by, int *bw, int *bh) {
	*bx = 0;
	*by = 0;
	*bw = surf_w;
	*bh = surf_h;
	if (surf_w <= 0 || surf_h <= 0 || layer->layer_surface == NULL) {
		return;
	}
	int cx, cy, cw, ch;
	if (layer_blur_box_exclusive(layer->layer_surface, surf_w, surf_h, &cx, &cy, &cw, &ch)) {
		layer_blur_consider(bx, by, bw, bh, cx, cy, cw, ch, surf_w, surf_h);
	}
	struct wlr_surface *surf = layer->layer_surface->surface;
	if (layer_blur_box_from_region(&surf->opaque_region, surf_w, surf_h, &cx, &cy, &cw, &ch)) {
		layer_blur_consider(bx, by, bw, bh, cx, cy, cw, ch, surf_w, surf_h);
	}
	/* Empty input_region means "whole surface" in wlroots — skip. */
	if (pixman_region32_not_empty(&surf->input_region) &&
	    layer_blur_box_from_region(&surf->input_region, surf_w, surf_h, &cx, &cy, &cw, &ch)) {
		layer_blur_consider(bx, by, bw, bh, cx, cy, cw, ch, surf_w, surf_h);
	}
}

#define SF_LAYER_BLUR_MAX 64

/*
 * Collect plate boxes. Prefer each effect-region rect (Noctalia switcher sends
 * panel + tiles). A single full-surface region falls back to exclusive/opaque/
 * input shrink so bars still clip the soft-shadow strip.
 */
static size_t layer_blur_boxes(struct sf_layer_surface *layer, int surf_w, int surf_h,
			       struct wlr_box *out, size_t cap) {
	if (cap == 0 || surf_w <= 0 || surf_h <= 0) {
		return 0;
	}
	if (pixman_region32_not_empty(&layer->blur_region)) {
		int n_rects = 0;
		pixman_box32_t *rects = pixman_region32_rectangles(&layer->blur_region, &n_rects);
		size_t n = 0;
		bool any_partial = false;
		for (int i = 0; i < n_rects && n < cap; i++) {
			int bx = rects[i].x1;
			int by = rects[i].y1;
			int bw = rects[i].x2 - rects[i].x1;
			int bh = rects[i].y2 - rects[i].y1;
			layer_blur_clamp(surf_w, surf_h, &bx, &by, &bw, &bh);
			if (bw <= 0 || bh <= 0) {
				continue;
			}
			if (!layer_blur_region_is_full(bx, by, bw, bh, surf_w, surf_h)) {
				any_partial = true;
			}
			out[n++] = (struct wlr_box){.x = bx, .y = by, .width = bw, .height = bh};
		}
		if (n > 0 && (n > 1 || any_partial)) {
			return n;
		}
	}
	int bx, by, bw, bh;
	layer_blur_box_fallback(layer, surf_w, surf_h, &bx, &by, &bw, &bh);
	if (bw <= 0 || bh <= 0) {
		return 0;
	}
	out[0] = (struct wlr_box){.x = bx, .y = by, .width = bw, .height = bh};
	return 1;
}

struct sf_layer_blur_plate {
	struct wl_list link;
	struct sf_layer_surface *layer;
	struct wlr_scene_blur *blur;
	struct wl_listener destroy;
};

static void layer_blur_plate_destroy_notify(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_layer_blur_plate *plate = wl_container_of(listener, plate, destroy);
	wl_list_remove(&plate->destroy.link);
	wl_list_remove(&plate->link);
	free(plate);
}

static void layer_blur_plates_clear(struct sf_layer_surface *layer) {
	struct sf_layer_blur_plate *plate, *tmp;
	wl_list_for_each_safe(plate, tmp, &layer->blur_plates, link) {
		/* destroy notify frees plate. */
		wlr_scene_node_destroy(&plate->blur->node);
	}
}

static struct sf_layer_blur_plate *layer_blur_plate_create(struct sf_layer_surface *layer, int w,
							   int h) {
	if (layer->scene == NULL) {
		return NULL;
	}
	struct sf_layer_blur_plate *plate = calloc(1, sizeof(*plate));
	if (plate == NULL) {
		return NULL;
	}
	plate->layer = layer;
	plate->blur = wlr_scene_blur_create(layer->scene, w, h);
	if (plate->blur == NULL) {
		free(plate);
		return NULL;
	}
	wlr_scene_blur_set_corner_radii(plate->blur, corner_radii_none());
	plate->destroy.notify = layer_blur_plate_destroy_notify;
	wl_signal_add(&plate->blur->node.events.destroy, &plate->destroy);
	wl_list_insert(layer->blur_plates.prev, &plate->link);
	wlr_scene_node_lower_to_bottom(&plate->blur->node);
	return plate;
}

static void layer_blur_plate_configure(struct sf_layer_blur_plate *plate, const struct wlr_box *box,
				       bool on, bool xray, struct wlr_scene_buffer *mask) {
	wlr_scene_node_set_enabled(&plate->blur->node, on && box->width > 0 && box->height > 0);
	if (!on || box->width <= 0 || box->height <= 0) {
		return;
	}
	wlr_scene_blur_set_size(plate->blur, box->width, box->height);
	wlr_scene_node_set_position(&plate->blur->node, box->x, box->y);
	wlr_scene_blur_set_should_only_blur_bottom_layer(plate->blur, xray);
	if (mask != NULL && wlr_scene_blur_get_transparency_mask_source(plate->blur) != mask) {
		wlr_scene_blur_set_transparency_mask_source(plate->blur, mask);
	}
	wlr_scene_node_lower_to_bottom(&plate->blur->node);
}

void sf_layer_refresh_blur(struct sf_layer_surface *layer) {
	if (layer == NULL || !layer->client_blur) {
		return;
	}
	bool on = layer->layer_surface != NULL && layer->layer_surface->surface->mapped;
	if (!on || layer->layer_surface == NULL || layer->scene == NULL) {
		struct sf_layer_blur_plate *plate;
		wl_list_for_each(plate, &layer->blur_plates, link) {
			wlr_scene_node_set_enabled(&plate->blur->node, false);
		}
		return;
	}
	struct wlr_surface *surf = layer->layer_surface->surface;
	int w = surf->current.width;
	int h = surf->current.height;
	struct wlr_box boxes[SF_LAYER_BLUR_MAX];
	size_t n = 0;
	if (w > 0 && h > 0) {
		n = layer_blur_boxes(layer, w, h, boxes, SF_LAYER_BLUR_MAX);
	}
	size_t have = 0;
	struct sf_layer_blur_plate *plate;
	wl_list_for_each(plate, &layer->blur_plates, link) {
		have++;
	}
	if (have != n) {
		layer_blur_plates_clear(layer);
		for (size_t i = 0; i < n; i++) {
			if (layer_blur_plate_create(layer, boxes[i].width, boxes[i].height) ==
			    NULL) {
				layer_blur_plates_clear(layer);
				layer->client_blur = false;
				pixman_region32_clear(&layer->blur_region);
				return;
			}
		}
	}
	bool xray = sf_cfg(layer->server)->blur_xray;
	/* Mask dst matches the blur node box; a full-surface mask stretched into a
	 * sub-rect plate mis-stencils. Only attach for a single plate (bars). */
	struct wlr_scene_buffer *mask = n == 1 ? layer_find_surface_buffer(layer) : NULL;
	size_t i = 0;
	wl_list_for_each(plate, &layer->blur_plates, link) {
		layer_blur_plate_configure(plate, &boxes[i], n > 0, xray, mask);
		i++;
	}
}

void sf_layer_set_client_blur(struct sf_layer_surface *layer, const pixman_region32_t *region) {
	if (layer == NULL) {
		return;
	}
	bool want = region != NULL && pixman_region32_not_empty(region);
	if (want) {
		pixman_region32_copy(&layer->blur_region, region);
	} else {
		pixman_region32_clear(&layer->blur_region);
	}
	if (layer->client_blur == want) {
		sf_layer_refresh_blur(layer);
		return;
	}
	layer->client_blur = want;
	if (!want) {
		layer_blur_plates_clear(layer);
	}
	sf_layer_refresh_blur(layer);
}

#define SF_LAYER_SHELL_VERSION 5

struct wlr_scene_tree *sf_layer_tree(struct sf_server *server,
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

bool sf_output_usable_box(struct sf_output *output, struct wlr_box *box) {
	if (output == NULL || box == NULL) {
		return false;
	}
	*box = output->usable;
	return box->width > 0 && box->height > 0;
}

bool sf_layout_usable_at(struct sf_server *server, double lx, double ly, struct wlr_box *box) {
	struct wlr_output *wlr_output = wlr_output_layout_output_at(server->output_layout, lx, ly);
	if (wlr_output == NULL) {
		return false;
	}
	struct sf_output *output = wlr_output->data;
	if (output == NULL) {
		wlr_output_layout_get_box(server->output_layout, wlr_output, box);
		return box->width > 0 && box->height > 0;
	}
	return sf_output_usable_box(output, box);
}

static void layer_focus_interactive(struct sf_output *output) {
	struct sf_server *server = output->server;
	if (sf_session_locked(server)) {
		return;
	}
	static const enum zwlr_layer_shell_v1_layer above[] = {
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP,
	};

	for (size_t i = 0; i < sizeof(above) / sizeof(above[0]); i++) {
		struct sf_layer_surface *layer;
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
				struct wlr_surface *root = wlr_surface_get_root_surface(prev);
				struct sf_toplevel *t;
				wl_list_for_each(t, &server->toplevels, link) {
					if (sf_toplevel_wlr_surface(t) == root) {
						sf_toplevel_set_activated(t, false);
						sf_decor_set_focused(t, false);
						break;
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
		sf_focus_top(server);
	}
}

static void layer_arrange_list(struct sf_output *output, struct wl_list *list,
			       struct wlr_box *usable, bool exclusive) {
	struct wlr_box full = output->full;
	struct sf_layer_surface *layer;
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

void sf_layer_arrange_output(struct sf_output *output) {
	if (output == NULL || output->wlr_output == NULL) {
		return;
	}
	if (!output->wlr_output->enabled) {
		return;
	}

	struct sf_server *server = output->server;
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
		sf_wm_relayout_output(output);
	}

	layer_focus_interactive(output);
}

static void layer_surface_map(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_layer_surface *layer = wl_container_of(listener, layer, map);
	if (layer->output != NULL) {
		sf_layer_arrange_output(layer->output);
	}
	layer_maybe_dirty_blur(layer);
	sf_layer_refresh_blur(layer);
}

static void layer_surface_unmap(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_layer_surface *layer = wl_container_of(listener, layer, unmap);
	struct sf_server *server = layer->server;

	if (server->exclusive_layer == layer) {
		server->exclusive_layer = NULL;
	}
	if (layer->output != NULL) {
		sf_layer_arrange_output(layer->output);
	}
	if (server->seat->keyboard_state.focused_surface == layer->layer_surface->surface) {
		sf_focus_top(server);
	}
	layer_maybe_dirty_blur(layer);
	sf_layer_refresh_blur(layer);
}

static void layer_surface_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_layer_surface *layer = wl_container_of(listener, layer, commit);
	struct wlr_layer_surface_v1 *ls = layer->layer_surface;
	struct wlr_output *wlr_output = ls->output;

	if (wlr_output == NULL) {
		return;
	}
	struct sf_output *output = wlr_output->data;
	if (output == NULL) {
		return;
	}
	layer->output = output;

	struct wlr_scene_tree *tree = sf_layer_tree(layer->server, ls->current.layer);
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
		sf_layer_arrange_output(output);
	}
	/* Buffer damage on wallpaper/bottom also needs a blur cache refresh. */
	if (ls->current.committed != 0 || ls->initial_commit ||
	    ls->surface->current.committed != 0) {
		layer_maybe_dirty_blur(layer);
	}
	if (layer->client_blur) {
		sf_layer_refresh_blur(layer);
	}
}

/*
 * Layer-shell popups are created with xdg parent NULL, then assigned here via
 * zwlr_layer_surface_v1.get_popup. The xdg_shell new_popup handler parks them
 * under overlay until this runs; reparent onto the layer popup tree.
 */
static void layer_surface_new_popup(struct wl_listener *listener, void *data) {
	struct sf_layer_surface *layer = wl_container_of(listener, layer, new_popup);
	struct wlr_xdg_popup *xdg_popup = data;
	struct wlr_scene_tree *tree = xdg_popup->base->data;

	if (tree != NULL) {
		wlr_scene_node_reparent(&tree->node, layer->popups);
	}
	/* Parent was often NULL at xdg new_popup; re-constrain once the layer
	 * owns it. wlroots 0.20 asserts schedule_configure (and unconstrain
	 * that schedules one) until the surface is initialized — Noctalia
	 * taskbar tooltips hit get_popup before the first commit. Defer to
	 * xdg_popup_commit when still early; configure here only if the
	 * client already committed. */
	if (xdg_popup->base != NULL && xdg_popup->base->initialized) {
		sf_xdg_popup_unconstrain(layer->server, xdg_popup);
		wlr_xdg_surface_schedule_configure(xdg_popup->base);
	}
}

static void layer_surface_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_layer_surface *layer = wl_container_of(listener, layer, destroy);
	struct sf_server *server = layer->server;
	struct sf_output *output = layer->output;
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

	/* scene_layer (and blur plate children) are already freed: wlroots
	 * registers its destroy listener before ours on layer_surface destroy.
	 * Plate destroy notifies should have emptied blur_plates; detach any
	 * stragglers without touching scene nodes. */
	struct sf_layer_blur_plate *plate, *plate_tmp;
	wl_list_for_each_safe(plate, plate_tmp, &layer->blur_plates, link) {
		wl_list_remove(&plate->destroy.link);
		wl_list_init(&plate->destroy.link);
		wl_list_remove(&plate->link);
		free(plate);
	}
	pixman_region32_fini(&layer->blur_region);
	wlr_scene_node_destroy(&layer->popups->node);
	free(layer);

	if (output != NULL) {
		sf_layer_arrange_output(output);
	}
	if (restore_focus && server->exclusive_layer == NULL) {
		sf_focus_top(server);
	}
}

static struct wlr_output *pick_layer_output(struct sf_server *server) {
	struct wlr_output *under = wlr_output_layout_output_at(
		server->output_layout, server->cursor->x, server->cursor->y);
	if (under != NULL) {
		return under;
	}
	struct sf_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (output->wlr_output->enabled) {
			return output->wlr_output;
		}
	}
	return NULL;
}

static void server_new_layer_surface(struct wl_listener *listener, void *data) {
	struct sf_server *server = wl_container_of(listener, server, new_layer_surface);
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

	struct sf_output *output = ls->output->data;
	if (output == NULL) {
		SF_LOGW("layer surface output not ready; destroying",
			sf_str("namespace", ls->namespace != NULL ? ls->namespace : ""));
		wlr_layer_surface_v1_destroy(ls);
		return;
	}

	enum zwlr_layer_shell_v1_layer layer_idx = ls->pending.layer;
	struct wlr_scene_tree *parent = sf_layer_tree(server, layer_idx);

	struct sf_layer_surface *layer = calloc(1, sizeof(*layer));
	if (layer == NULL) {
		wlr_layer_surface_v1_destroy(ls);
		return;
	}
	layer->kind = SF_NODE_LAYER;
	layer->server = server;
	layer->output = output;
	layer->layer_surface = ls;
	ls->data = layer;
	pixman_region32_init(&layer->blur_region);
	wl_list_init(&layer->blur_plates);

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

void sf_layer_setup(struct sf_server *server) {
	server->layers.background = wlr_scene_tree_create(&server->scene->tree);
	server->layers.bottom = wlr_scene_tree_create(&server->scene->tree);
	/* Optimized blur samples everything below (wallpaper / bottom). */
	server->blur_layer = wlr_scene_optimized_blur_create(&server->scene->tree, 0, 0);
	server->layers.toplevel = wlr_scene_tree_create(&server->scene->tree);
	/* Overview filmstrip: above windows, below top/bar (bar stays usable). */
	server->layers.overview = wlr_scene_tree_create(&server->scene->tree);
	server->layers.top = wlr_scene_tree_create(&server->scene->tree);
	/* Above top (Noctalia bar) so F11 / xdg fullscreen covers shell chrome. */
	server->layers.fullscreen = wlr_scene_tree_create(&server->scene->tree);
	server->layers.overlay = wlr_scene_tree_create(&server->scene->tree);
	/* IME candidate popups above shell overlay; lock still created later. */
	server->layers.ime = wlr_scene_tree_create(&server->scene->tree);

	server->overview.tree = server->layers.overview;
	wl_list_init(&server->overview.cards);
	wl_list_init(&server->overview.dims);
	if (server->overview.tree != NULL) {
		wlr_scene_node_set_enabled(&server->overview.tree->node, false);
	}

	server->layer_shell = wlr_layer_shell_v1_create(server->wl_display, SF_LAYER_SHELL_VERSION);
	server->new_layer_surface.notify = server_new_layer_surface;
	wl_signal_add(&server->layer_shell->events.new_surface, &server->new_layer_surface);
	server->exclusive_layer = NULL;

	SF_LOGI("layer shell", sf_i64("version", SF_LAYER_SHELL_VERSION));
}

void sf_layer_finish(struct sf_server *server) {
	wl_list_remove(&server->new_layer_surface.link);
}
