#include <stdlib.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <scenefx/types/wlr_scene.h>

#include "config/config.h"
#include "layer/layer.h"
#include "server/server.h"
#include "wm/arrange.h"
#include "wm/drag_snap.h"
#include "wm/snap_preview.h"
#include "wm/wm.h"
#include "toplevel/toplevel.h"

static int iabs(int v) {
	return v < 0 ? -v : v;
}

void sf_drag_snap_reset(struct sf_drag_snap *drag) {
	if (drag == NULL) {
		return;
	}
	*drag = (struct sf_drag_snap){0};
	drag->stuck = SF_SNAP_NONE;
}

/* Port of peeble SnapGeom.resist (seat/op/move.go). */
static int resist_axis(int delta, int pos, int size, int usable_pos, int usable_size, int border,
		       int gap, int threshold, bool *lo, bool *hi, int *lo_delta, int *hi_delta) {
	int bb_pos = pos - border;
	int bb_size = size + 2 * border;
	int lo_edge = usable_pos + gap;
	int hi_edge = usable_pos + usable_size - gap;
	int push = 4 * threshold;

	int d_lo = bb_pos - lo_edge;
	if (*lo && iabs(delta - *lo_delta) > push) {
		*lo = false;
	} else if (!*lo && iabs(d_lo) <= threshold) {
		*lo = true;
		*lo_delta = delta;
	}
	if (*lo) {
		pos = lo_edge + border;
		bb_pos = lo_edge;
	}

	int d_hi = hi_edge - (bb_pos + bb_size);
	if (*hi && iabs(delta - *hi_delta) > push) {
		*hi = false;
	} else if (!*hi && iabs(d_hi) <= threshold) {
		*hi = true;
		*hi_delta = delta;
	}
	if (*hi) {
		pos = hi_edge - bb_size + border;
	}
	return pos;
}

static enum sf_snap stuck_edge(const struct sf_drag_snap *drag) {
	if (drag->lo_x) {
		return SF_SNAP_LEFT;
	}
	if (drag->hi_x) {
		return SF_SNAP_RIGHT;
	}
	if (drag->lo_y) {
		return SF_SNAP_TOP;
	}
	if (drag->hi_y) {
		return SF_SNAP_BOTTOM;
	}
	return SF_SNAP_NONE;
}

static bool pointer_usable(struct sf_server *server, struct wlr_box *box) {
	return sf_layout_usable_at(server, server->cursor->x, server->cursor->y, box);
}

void sf_drag_snap_prepare(struct sf_toplevel *toplevel) {
	struct sf_server *server = toplevel->server;
	struct sf_drag_snap *drag = &server->drag_snap;
	sf_drag_snap_reset(drag);
	sf_toplevel_clear_auto_place(toplevel);

	struct wlr_box geom;
	sf_toplevel_layout_geom(toplevel, &geom);

	if (sf_layout_sm_snap(&toplevel->layout) != SF_SNAP_NONE) {
		drag->started_snapped = true;
		/* Peeble PrepareDrag: restore PreSnap size, keep pointer ratio. */
		if (toplevel->layout.has_saved) {
			double rx = 0.5;
			double ry = 0.5;
			if (geom.width > 0) {
				rx = (server->cursor->x - geom.x) / (double)geom.width;
				if (rx < 0.0) {
					rx = 0.0;
				}
				if (rx > 1.0) {
					rx = 1.0;
				}
			}
			if (geom.height > 0) {
				ry = (server->cursor->y - geom.y) / (double)geom.height;
				if (ry < 0.0) {
					ry = 0.0;
				}
				if (ry > 1.0) {
					ry = 1.0;
				}
			}
			int w = toplevel->layout.saved.width;
			int h = toplevel->layout.saved.height;
			int x = (int)(server->cursor->x - rx * (double)w);
			int y = (int)(server->cursor->y - ry * (double)h);
			sf_toplevel_apply_saved_at(toplevel, x, y);
			sf_toplevel_ensure_on_screen(toplevel);
		} else {
			sf_toplevel_layout_float(toplevel, false);
		}
		return;
	}

	if (sf_layout_sm_is_max_edges(&toplevel->layout) ||
	    sf_layout_sm_is_max_gaps(&toplevel->layout)) {
		if (toplevel->layout.has_saved) {
			int w = toplevel->layout.saved.width;
			int x = (int)server->cursor->x - w / 2;
			int y = (int)server->cursor->y - 16;
			sf_toplevel_apply_saved_at(toplevel, x, y);
			sf_toplevel_ensure_on_screen(toplevel);
		} else {
			sf_toplevel_layout_float(toplevel, false);
		}
		return;
	}

	/* Tiled moves use SF_CURSOR_ARRANGE_SWAP (tile reorder). Never break out
	 * or magnetic-snap from a stacking drag path. */
	if (toplevel->arrange_managed) {
		return;
	}

	/* Floating: remember drag origin for capturePreSnap on release snap. */
	if (!toplevel->layout.has_saved && geom.width > 0 && geom.height > 0) {
		drag->drag_origin = geom;
		drag->has_drag_origin = true;
	}
}

void sf_drag_snap_begin(struct sf_server *server, struct sf_toplevel *toplevel) {
	struct sf_drag_snap *drag = &server->drag_snap;
	drag->active = true;
	drag->start_cx = server->cursor->x;
	drag->start_cy = server->cursor->y;
	drag->start_x = toplevel->scene_tree->node.x;
	drag->start_y = toplevel->scene_tree->node.y;

	/* Prefer PreSnap size after PrepareDrag — xdg geometry may lag one commit. */
	if (drag->started_snapped && toplevel->layout.has_saved) {
		drag->width = toplevel->layout.saved.width;
		drag->height = toplevel->layout.saved.height;
	} else {
		struct wlr_box geom;
		sf_toplevel_layout_geom(toplevel, &geom);
		drag->width = geom.width;
		drag->height = geom.height;
	}
	drag->stuck = SF_SNAP_NONE;
	drag->lo_x = drag->hi_x = drag->lo_y = drag->hi_y = false;
}

void sf_drag_snap_motion(struct sf_server *server) {
	struct sf_drag_snap *drag = &server->drag_snap;
	struct sf_toplevel *toplevel = server->grabbed_toplevel;
	if (!drag->active || toplevel == NULL) {
		return;
	}

	int dx = (int)(server->cursor->x - drag->start_cx);
	int dy = (int)(server->cursor->y - drag->start_cy);
	int x = drag->start_x + dx;
	int y = drag->start_y + dy;

	struct wlr_box usable;
	const struct sf_config *cfg = sf_cfg(server);
	const int threshold = cfg->snap_threshold;
	const int border = cfg->border_thickness;
	const int gap = cfg->gap_size;

	if (threshold > 0 && pointer_usable(server, &usable) && drag->width > 0 &&
	    drag->height > 0) {
		x = resist_axis(dx, x, drag->width, usable.x, usable.width, border, gap, threshold,
				&drag->lo_x, &drag->hi_x, &drag->lo_x_delta, &drag->hi_x_delta);
		y = resist_axis(dy, y, drag->height, usable.y, usable.height, border, gap,
				threshold, &drag->lo_y, &drag->hi_y, &drag->lo_y_delta,
				&drag->hi_y_delta);
	} else {
		drag->lo_x = drag->hi_x = drag->lo_y = drag->hi_y = false;
	}

	wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y);
	toplevel->resting_x = x;
	toplevel->resting_y = y;

	enum sf_snap edge = stuck_edge(drag);
	if (edge != drag->stuck) {
		drag->stuck = edge;
		if (edge == SF_SNAP_NONE) {
			sf_snap_preview_hide(server);
		} else if (pointer_usable(server, &usable)) {
			sf_snap_preview_show(server, edge, &usable);
		}
	} else if (edge != SF_SNAP_NONE && pointer_usable(server, &usable)) {
		/* Multi-mon: refresh preview geom as usable under pointer moves. */
		sf_snap_preview_show(server, edge, &usable);
	}
}

/* Peeble snapDragCandidate: closest edge with 0 ≤ dist ≤ threshold. */
static bool snap_candidate(struct sf_toplevel *toplevel, const struct wlr_box *usable,
			   enum sf_snap *out) {
	struct wlr_box geom;
	sf_toplevel_layout_geom(toplevel, &geom);
	const struct sf_config *cfg = sf_cfg(toplevel->server);
	const int b = cfg->border_thickness;
	const int g = cfg->gap_size;
	const int threshold = cfg->snap_threshold;

	int left = geom.x - b;
	int right = geom.x + geom.width + b;
	int top = geom.y - b;
	int bottom = geom.y + geom.height + b;

	struct {
		enum sf_snap edge;
		int dist;
	} cands[] = {
		{SF_SNAP_LEFT, left - (usable->x + g)},
		{SF_SNAP_RIGHT, usable->x + usable->width - g - right},
		{SF_SNAP_TOP, top - (usable->y + g)},
		{SF_SNAP_BOTTOM, usable->y + usable->height - g - bottom},
	};

	int best = threshold + 1;
	bool ok = false;
	enum sf_snap edge = SF_SNAP_NONE;
	for (size_t i = 0; i < sizeof(cands) / sizeof(cands[0]); i++) {
		int d = cands[i].dist;
		if (d >= 0 && d <= threshold && d < best) {
			best = d;
			edge = cands[i].edge;
			ok = true;
		}
	}
	if (ok) {
		*out = edge;
	}
	return ok;
}

void sf_drag_snap_finish(struct sf_server *server) {
	struct sf_drag_snap *drag = &server->drag_snap;
	struct sf_toplevel *toplevel = server->grabbed_toplevel;
	sf_snap_preview_hide(server);

	if (!drag->active || toplevel == NULL) {
		sf_drag_snap_reset(drag);
		return;
	}

	struct wlr_box usable;
	enum sf_snap edge = SF_SNAP_NONE;
	if (pointer_usable(server, &usable) && snap_candidate(toplevel, &usable, &edge)) {
		if (drag->has_drag_origin && !toplevel->layout.has_saved) {
			sf_toplevel_capture_geom(toplevel, &drag->drag_origin);
		}
		sf_toplevel_snap_in(toplevel, edge, &usable);
		sf_drag_snap_reset(drag);
		return;
	}

	if (drag->started_snapped) {
		/* Dragged off every snap edge: restore PreSnap. */
		if (toplevel->layout.has_saved) {
			sf_toplevel_restore_saved(toplevel);
		} else {
			sf_toplevel_layout_float(toplevel, false);
			sf_toplevel_ensure_on_screen(toplevel);
		}
		sf_drag_snap_reset(drag);
		return;
	}

	/* Floated from max/snap mid-prepare without re-snap: drop PreSnap
	 * when peeble would (release position is the new float). */
	if (drag->has_drag_origin) {
		/* Never committed to a snap; leave floating where released. */
	}
	sf_toplevel_ensure_on_screen(toplevel);
	sf_drag_snap_reset(drag);
}
