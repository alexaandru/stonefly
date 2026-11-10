#ifndef SF_WM_GEOM_H
#define SF_WM_GEOM_H

#include <wlr/util/box.h>

#include "wm/snap.h"

/* Inset box by gap on every side (clamped). */
void sf_box_inset_gap(struct wlr_box *box, int gap);

/* Peeble snapBorderBox geometry (layout coords). */
struct wlr_box sf_wm_snap_box(enum sf_snap edge, const struct wlr_box *usable, int gap);

#endif
