#ifndef SF_BLUR_PLATES_H
#define SF_BLUR_PLATES_H

#include <stdbool.h>
#include <stddef.h>
#include <wayland-server-core.h>

#include "effect/blur_region.h"

struct wlr_box;
struct wlr_scene_blur;
struct wlr_scene_buffer;
struct wlr_scene_tree;

enum {
	SF_BLUR_PLATE_MAX = 64
};

struct sf_blur_plate {
	struct wl_list link;
	struct wlr_scene_blur *blur;
	struct wl_listener destroy;
};

void sf_blur_plates_clear(struct wl_list *plates);

/* Sync plate list under parent to boxes. mask only when n == 1. */
void sf_blur_plates_sync(struct wlr_scene_tree *parent, struct wl_list *plates,
			 const struct wlr_box *boxes, size_t n, bool on, bool xray,
			 struct wlr_scene_buffer *mask);

#endif
