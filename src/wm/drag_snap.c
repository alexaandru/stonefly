#include <stdlib.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <scenefx/types/wlr_scene.h>

#include "config/config.h"
#include "layer/layer.h"
#include "server/server.h"
#include "wm/drag_snap.h"
#include "wm/snap_preview.h"
#include "wm/wm.h"
#include "toplevel/toplevel.h"

static int iabs(int v) {
	return v < 0 ? -v : v;
}

void stonefly_drag_snap_reset(struct stonefly_drag_snap *drag) {
	if (drag == NULL) {
		return;
	}
	*drag = (struct stonefly_drag_snap){0};
	drag->stuck = (int)STONEFLY_SNAP_NONE;
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

static enum stonefly_snap stuck_edge(const struct stonefly_drag_snap *drag) {
	if (drag->lo_x) {
		return STONEFLY_SNAP_LEFT;
	}
	if (drag->hi_x) {
		return STONEFLY_SNAP_RIGHT;
	}
	if (drag->lo_y) {
		return STONEFLY_SNAP_TOP;
	}
	if (drag->hi_y) {
		return STONEFLY_SNAP_BOTTOM;
	}
	return STONEFLY_SNAP_NONE;
}

static bool pointer_usable(struct stonefly_server *server, struct wlr_box *box) {
	return stonefly_layout_usable_at(server, server->cursor->x, server->cursor->y, box);
}

void stonefly_drag_snap_prepare(struct stonefly_toplevel *toplevel) {
	struct stonefly_server *server = toplevel->server;
	struct stonefly_drag_snap *drag = &server->drag_snap;
	stonefly_drag_snap_reset(drag);
	stonefly_toplevel_clear_auto_place(toplevel);

	struct wlr_box geom;
	stonefly_toplevel_layout_geom(toplevel, &geom);

	if (toplevel->snapped != STONEFLY_SNAP_NONE) {
		drag->started_snapped = true;
		/* Peeble PrepareDrag: restore PreSnap size, keep pointer ratio. */
		if (toplevel->has_saved_geom) {
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
			int w = toplevel->saved_geom.width;
			int h = toplevel->saved_geom.height;
			int x = (int)(server->cursor->x - rx * (double)w);
			int y = (int)(server->cursor->y - ry * (double)h);
			stonefly_toplevel_apply_saved_at(toplevel, x, y);
			stonefly_toplevel_ensure_on_screen(toplevel);
		} else {
			toplevel->snapped = STONEFLY_SNAP_NONE;
		}
		return;
	}

	if (toplevel->maximized_edges || toplevel->maximized_gaps) {
		if (toplevel->has_saved_geom) {
			int w = toplevel->saved_geom.width;
			int x = (int)server->cursor->x - w / 2;
			int y = (int)server->cursor->y - 16;
			stonefly_toplevel_apply_saved_at(toplevel, x, y);
			stonefly_toplevel_ensure_on_screen(toplevel);
		} else if (toplevel->maximized_edges) {
			wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, false);
			toplevel->maximized_edges = false;
			toplevel->maximized_gaps = false;
		} else {
			toplevel->maximized_gaps = false;
		}
		return;
	}

	/* Floating: remember drag origin for capturePreSnap on release snap. */
	if (!toplevel->has_saved_geom && geom.width > 0 && geom.height > 0) {
		drag->drag_origin = geom;
		drag->has_drag_origin = true;
	}
}

void stonefly_drag_snap_begin(struct stonefly_server *server, struct stonefly_toplevel *toplevel) {
	struct stonefly_drag_snap *drag = &server->drag_snap;
	drag->active = true;
	drag->start_cx = server->cursor->x;
	drag->start_cy = server->cursor->y;
	drag->start_x = toplevel->scene_tree->node.x;
	drag->start_y = toplevel->scene_tree->node.y;

	/* Prefer PreSnap size after PrepareDrag — xdg geometry may lag one commit. */
	if (drag->started_snapped && toplevel->has_saved_geom) {
		drag->width = toplevel->saved_geom.width;
		drag->height = toplevel->saved_geom.height;
	} else {
		struct wlr_box geom;
		stonefly_toplevel_layout_geom(toplevel, &geom);
		drag->width = geom.width;
		drag->height = geom.height;
	}
	drag->stuck = (int)STONEFLY_SNAP_NONE;
	drag->lo_x = drag->hi_x = drag->lo_y = drag->hi_y = false;
}

void stonefly_drag_snap_motion(struct stonefly_server *server) {
	struct stonefly_drag_snap *drag = &server->drag_snap;
	struct stonefly_toplevel *toplevel = server->grabbed_toplevel;
	if (!drag->active || toplevel == NULL) {
		return;
	}

	int dx = (int)(server->cursor->x - drag->start_cx);
	int dy = (int)(server->cursor->y - drag->start_cy);
	int x = drag->start_x + dx;
	int y = drag->start_y + dy;

	struct wlr_box usable;
	const struct stonefly_config *cfg = stonefly_cfg(server);
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

	enum stonefly_snap edge = stuck_edge(drag);
	if ((int)edge != drag->stuck) {
		drag->stuck = (int)edge;
		if (edge == STONEFLY_SNAP_NONE) {
			stonefly_snap_preview_hide(server);
		} else if (pointer_usable(server, &usable)) {
			stonefly_snap_preview_show(server, edge, &usable);
		}
	} else if (edge != STONEFLY_SNAP_NONE && pointer_usable(server, &usable)) {
		/* Multi-mon: refresh preview geom as usable under pointer moves. */
		stonefly_snap_preview_show(server, edge, &usable);
	}
}

/* Peeble snapDragCandidate: closest edge with 0 ≤ dist ≤ threshold. */
static bool snap_candidate(struct stonefly_toplevel *toplevel, const struct wlr_box *usable,
			   enum stonefly_snap *out) {
	struct wlr_box geom;
	stonefly_toplevel_layout_geom(toplevel, &geom);
	const struct stonefly_config *cfg = stonefly_cfg(toplevel->server);
	const int b = cfg->border_thickness;
	const int g = cfg->gap_size;
	const int threshold = cfg->snap_threshold;

	int left = geom.x - b;
	int right = geom.x + geom.width + b;
	int top = geom.y - b;
	int bottom = geom.y + geom.height + b;

	struct {
		enum stonefly_snap edge;
		int dist;
	} cands[] = {
		{STONEFLY_SNAP_LEFT, left - (usable->x + g)},
		{STONEFLY_SNAP_RIGHT, usable->x + usable->width - g - right},
		{STONEFLY_SNAP_TOP, top - (usable->y + g)},
		{STONEFLY_SNAP_BOTTOM, usable->y + usable->height - g - bottom},
	};

	int best = threshold + 1;
	bool ok = false;
	enum stonefly_snap edge = STONEFLY_SNAP_NONE;
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

void stonefly_drag_snap_finish(struct stonefly_server *server) {
	struct stonefly_drag_snap *drag = &server->drag_snap;
	struct stonefly_toplevel *toplevel = server->grabbed_toplevel;
	stonefly_snap_preview_hide(server);

	if (!drag->active || toplevel == NULL) {
		stonefly_drag_snap_reset(drag);
		return;
	}

	struct wlr_box usable;
	enum stonefly_snap edge = STONEFLY_SNAP_NONE;
	if (pointer_usable(server, &usable) && snap_candidate(toplevel, &usable, &edge)) {
		if (drag->has_drag_origin && !toplevel->has_saved_geom) {
			stonefly_toplevel_capture_geom(toplevel, &drag->drag_origin);
		}
		stonefly_toplevel_snap_in(toplevel, edge, &usable);
		stonefly_drag_snap_reset(drag);
		return;
	}

	if (drag->started_snapped) {
		/* Dragged off every snap edge: restore PreSnap. */
		if (toplevel->has_saved_geom) {
			stonefly_toplevel_restore_saved(toplevel);
		} else {
			toplevel->snapped = STONEFLY_SNAP_NONE;
			stonefly_toplevel_ensure_on_screen(toplevel);
		}
		stonefly_drag_snap_reset(drag);
		return;
	}

	/* Floated from max/snap mid-prepare without re-snap: drop PreSnap
	 * when peeble would (release position is the new float). */
	if (drag->has_drag_origin) {
		/* Never committed to a snap; leave floating where released. */
	}
	stonefly_toplevel_ensure_on_screen(toplevel);
	stonefly_drag_snap_reset(drag);
}
