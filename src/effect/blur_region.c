#include <pixman.h>
#include <wlr/util/box.h>

#include "effect/blur_region.h"

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
