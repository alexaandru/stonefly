#ifndef STONEFLY_DRAG_SNAP_H
#define STONEFLY_DRAG_SNAP_H

#include <stdbool.h>
#include <wlr/util/box.h>

struct stonefly_server;
struct stonefly_toplevel;

/* Peeble seat/op/move.go + wm.SnapDraggedWindow / PrepareDrag.
 * stuck holds enum stonefly_snap values (STONEFLY_SNAP_*). */
struct stonefly_drag_snap {
	bool active;
	double start_cx, start_cy;
	int start_x, start_y;
	int width, height;

	bool lo_x, hi_x, lo_y, hi_y;
	int lo_x_delta, hi_x_delta, lo_y_delta, hi_y_delta;

	int stuck; /* enum stonefly_snap */
	bool started_snapped;
	bool has_drag_origin;
	struct wlr_box drag_origin;
};

void stonefly_drag_snap_reset(struct stonefly_drag_snap *drag);

/* Float snapped/max for move; record drag origin. Call before grab starts. */
void stonefly_drag_snap_prepare(struct stonefly_toplevel *toplevel);

/* Begin stick tracking after prepare + grab fields are set. */
void stonefly_drag_snap_begin(struct stonefly_server *server,
		struct stonefly_toplevel *toplevel);

/* Apply magnetic resistance; update stuck edge + preview. */
void stonefly_drag_snap_motion(struct stonefly_server *server);

/* Release: snap / unsnap / ensure-on-screen; hide preview. */
void stonefly_drag_snap_finish(struct stonefly_server *server);

#endif
