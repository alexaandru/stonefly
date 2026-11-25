#ifndef SF_BLUR_REGION_H
#define SF_BLUR_REGION_H

#include <stddef.h>
#include <pixman.h>

struct wlr_box;

/* Clamp a surface-local box into [0,surf)×[0,surf). Floors bw/bh to 0. */
void sf_blur_clamp_box(int surf_w, int surf_h, int *bx, int *by, int *bw, int *bh);

/* pixman region → clamped boxes (surface-local). Returns count (≤ cap). */
size_t sf_blur_region_boxes(const pixman_region32_t *region, int surf_w, int surf_h,
			    struct wlr_box *out, size_t cap);

#endif
