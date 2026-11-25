#include <pixman.h>
#include <stdlib.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/util/box.h>

#include "effect/blur_plates.h"

static void blur_plate_destroy_notify(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_blur_plate *plate = wl_container_of(listener, plate, destroy);
	wl_list_remove(&plate->destroy.link);
	wl_list_remove(&plate->link);
	free(plate);
}

static void blur_clamp(int surf_w, int surf_h, int *bx, int *by, int *bw, int *bh) {
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

size_t sf_blur_region_boxes(const pixman_region32_t *region, int surf_w, int surf_h,
			    struct wlr_box *out, size_t cap) {
	if (region == NULL || out == NULL || cap == 0 || surf_w <= 0 || surf_h <= 0 ||
	    !pixman_region32_not_empty(region)) {
		return 0;
	}
	int n_rects = 0;
	pixman_box32_t *rects = pixman_region32_rectangles(region, &n_rects);
	size_t n = 0;
	for (int i = 0; i < n_rects && n < cap; i++) {
		int bx = rects[i].x1;
		int by = rects[i].y1;
		int bw = rects[i].x2 - rects[i].x1;
		int bh = rects[i].y2 - rects[i].y1;
		blur_clamp(surf_w, surf_h, &bx, &by, &bw, &bh);
		if (bw <= 0 || bh <= 0) {
			continue;
		}
		out[n++] = (struct wlr_box){.x = bx, .y = by, .width = bw, .height = bh};
	}
	return n;
}

void sf_blur_plates_clear(struct wl_list *plates) {
	if (plates == NULL) {
		return;
	}
	struct sf_blur_plate *plate, *tmp;
	wl_list_for_each_safe(plate, tmp, plates, link) {
		wlr_scene_node_destroy(&plate->blur->node);
	}
}

static struct sf_blur_plate *blur_plate_create(struct wlr_scene_tree *parent,
					       struct wl_list *plates, int w, int h) {
	struct sf_blur_plate *plate = calloc(1, sizeof(*plate));
	if (plate == NULL) {
		return NULL;
	}
	plate->blur = wlr_scene_blur_create(parent, w, h);
	if (plate->blur == NULL) {
		free(plate);
		return NULL;
	}
	wlr_scene_blur_set_corner_radii(plate->blur, corner_radii_none());
	plate->destroy.notify = blur_plate_destroy_notify;
	wl_signal_add(&plate->blur->node.events.destroy, &plate->destroy);
	wl_list_insert(plates->prev, &plate->link);
	wlr_scene_node_lower_to_bottom(&plate->blur->node);
	return plate;
}

void sf_blur_plates_sync(struct wlr_scene_tree *parent, struct wl_list *plates,
			 const struct wlr_box *boxes, size_t n, bool on, bool xray,
			 struct wlr_scene_buffer *mask) {
	if (parent == NULL || plates == NULL) {
		return;
	}
	if (!on || n == 0 || boxes == NULL) {
		sf_blur_plates_clear(plates);
		return;
	}

	size_t have = 0;
	struct sf_blur_plate *plate;
	wl_list_for_each(plate, plates, link) {
		have++;
	}
	if (have != n) {
		sf_blur_plates_clear(plates);
		for (size_t i = 0; i < n; i++) {
			if (blur_plate_create(parent, plates, boxes[i].width, boxes[i].height) ==
			    NULL) {
				sf_blur_plates_clear(plates);
				return;
			}
		}
	}

	struct wlr_scene_buffer *use_mask = (n == 1) ? mask : NULL;
	size_t i = 0;
	wl_list_for_each(plate, plates, link) {
		const struct wlr_box *box = &boxes[i];
		wlr_scene_node_set_enabled(&plate->blur->node, box->width > 0 && box->height > 0);
		if (box->width > 0 && box->height > 0) {
			wlr_scene_blur_set_size(plate->blur, box->width, box->height);
			wlr_scene_node_set_position(&plate->blur->node, box->x, box->y);
			wlr_scene_blur_set_should_only_blur_bottom_layer(plate->blur, xray);
			if (use_mask != NULL &&
			    wlr_scene_blur_get_transparency_mask_source(plate->blur) != use_mask) {
				wlr_scene_blur_set_transparency_mask_source(plate->blur, use_mask);
			}
			wlr_scene_node_lower_to_bottom(&plate->blur->node);
		}
		i++;
	}
}
