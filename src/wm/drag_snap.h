#ifndef SF_DRAG_SNAP_H
#define SF_DRAG_SNAP_H

#include <stdbool.h>
#include <wlr/util/box.h>

#include "wm/snap.h"

struct sf_server;
struct sf_toplevel;

/* Peeble seat/op/move.go + wm.SnapDraggedWindow / PrepareDrag. */
struct sf_drag_snap {
	bool active;
	double start_cx, start_cy;
	int start_x, start_y;
	int width, height;

	bool lo_x, hi_x, lo_y, hi_y;
	int lo_x_delta, hi_x_delta, lo_y_delta, hi_y_delta;

	enum sf_snap stuck;
	bool started_snapped;
	bool has_drag_origin;
	struct wlr_box drag_origin;
};

void sf_drag_snap_reset(struct sf_drag_snap *drag);

/* Float snapped/max for move; record drag origin. Call before grab starts. */
void sf_drag_snap_prepare(struct sf_toplevel *toplevel);

/* Begin stick tracking after prepare + grab fields are set. */
void sf_drag_snap_begin(struct sf_server *server, struct sf_toplevel *toplevel);

/* Apply magnetic resistance; update stuck edge + preview. */
void sf_drag_snap_motion(struct sf_server *server);

/* Release: snap / unsnap / ensure-on-screen; hide preview. */
void sf_drag_snap_finish(struct sf_server *server);

#endif
