#ifndef SF_WM_GEOM_H
#define SF_WM_GEOM_H

#include <wlr/util/box.h>

#include "wm/snap.h"

/* Inset box by gap on every side (clamped). */
void sf_box_inset_gap(struct wlr_box *box, int gap);

/* Grow +1 logical px on non-integer scale (peeble fractional hairline). */
void sf_box_cover_fractional_gap(struct wlr_box *box, float scale);

/* Center content of size `content` inside `tile` (xdg-shell FS letterbox). */
int sf_fullscreen_center_offset(int tile, int content);

/* Peeble snapBorderBox geometry (layout coords). */
struct wlr_box sf_wm_snap_box(enum sf_snap edge, const struct wlr_box *usable, int gap);

#endif
