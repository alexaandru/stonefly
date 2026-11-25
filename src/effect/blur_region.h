#ifndef SF_BLUR_REGION_H
#define SF_BLUR_REGION_H

#include <stddef.h>
#include <pixman.h>

struct wlr_box;

/* pixman region → clamped boxes (surface-local). Returns count (≤ cap). */
size_t sf_blur_region_boxes(const pixman_region32_t *region, int surf_w, int surf_h,
			    struct wlr_box *out, size_t cap);

#endif
