#include "wm/geom.h"

void sf_box_inset_gap(struct wlr_box *box, int gap) {
	const int g = gap;
	if (box->width > 2 * g) {
		box->x += g;
		box->width -= 2 * g;
	}
	if (box->height > 2 * g) {
		box->y += g;
		box->height -= 2 * g;
	}
}

/* Peeble snapBorderBox: gap between two half-snaps is exactly GapSize. */
struct wlr_box sf_wm_snap_box(enum sf_snap edge, const struct wlr_box *usable, int gap) {
	const int g = gap;
	struct wlr_box u = *usable;
	int half_w = (u.width - 3 * g) / 2;
	int half_h = (u.height - 3 * g) / 2;
	int full_w = u.width - 2 * g;
	int full_h = u.height - 2 * g;
	struct wlr_box box = {0};

	switch (edge) {
	case SF_SNAP_LEFT:
		box.x = u.x + g;
		box.y = u.y + g;
		box.width = half_w;
		box.height = full_h;
		break;
	case SF_SNAP_RIGHT:
		box.x = u.x + 2 * g + half_w;
		box.y = u.y + g;
		box.width = half_w;
		box.height = full_h;
		break;
	case SF_SNAP_TOP:
		box.x = u.x + g;
		box.y = u.y + g;
		box.width = full_w;
		box.height = half_h;
		break;
	case SF_SNAP_BOTTOM:
		box.x = u.x + g;
		box.y = u.y + 2 * g + half_h;
		box.width = full_w;
		box.height = half_h;
		break;
	case SF_SNAP_NONE:
		break;
	}
	return box;
}
