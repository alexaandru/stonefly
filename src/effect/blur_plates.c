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
