#include <stdlib.h>
#include <scenefx/types/fx/clipped_region.h>
#include <scenefx/types/wlr_scene.h>

#include "config/config.h"
#include "server/server.h"
#include "wm/snap_preview.h"
#include "wm/wm.h"
#include "toplevel/toplevel.h"

static void preview_colors(struct stonefly_server *server, float fill[4], float rim[4]) {
	const struct stonefly_config *cfg = stonefly_cfg(server);
	const float *active = cfg->border_active;
	const float a = cfg->snap_preview_opacity;
	/* Premultiplied fill at preview opacity. */
	fill[0] = active[0] * a;
	fill[1] = active[1] * a;
	fill[2] = active[2] * a;
	fill[3] = active[3] * a;
	rim[0] = active[0];
	rim[1] = active[1];
	rim[2] = active[2];
	rim[3] = active[3];
}

void stonefly_snap_preview_init(struct stonefly_server *server) {
	struct stonefly_snap_preview *p = calloc(1, sizeof(*p));
	if (p == NULL) {
		return;
	}
	p->server = server;
	p->tree = wlr_scene_tree_create(server->layers.overlay);
	float fill[4], rim[4];
	preview_colors(server, fill, rim);
	p->fill = wlr_scene_rect_create(p->tree, 0, 0, fill);
	p->rim = wlr_scene_rect_create(p->tree, 0, 0, rim);
	wlr_scene_node_set_enabled(&p->tree->node, false);
	p->visible = false;
	p->edge = STONEFLY_SNAP_NONE;
	server->snap_preview = p;
}

void stonefly_snap_preview_finish(struct stonefly_server *server) {
	struct stonefly_snap_preview *p = server->snap_preview;
	if (p == NULL) {
		return;
	}
	if (p->tree != NULL) {
		wlr_scene_node_destroy(&p->tree->node);
	}
	free(p);
	server->snap_preview = NULL;
}

void stonefly_snap_preview_hide(struct stonefly_server *server) {
	struct stonefly_snap_preview *p = server->snap_preview;
	if (p == NULL || !p->visible) {
		return;
	}
	wlr_scene_node_set_enabled(&p->tree->node, false);
	p->visible = false;
	p->edge = STONEFLY_SNAP_NONE;
}

void stonefly_snap_preview_show(struct stonefly_server *server, int edge,
				const struct wlr_box *usable) {
	if (server->snap_preview == NULL) {
		stonefly_snap_preview_init(server);
	}
	struct stonefly_snap_preview *p = server->snap_preview;
	if (p == NULL || usable == NULL || edge == STONEFLY_SNAP_NONE) {
		stonefly_snap_preview_hide(server);
		return;
	}

	const struct stonefly_config *cfg = stonefly_cfg(server);
	struct wlr_box box = stonefly_wm_snap_box((enum stonefly_snap)edge, usable, cfg->gap_size);
	if (box.width <= 0 || box.height <= 0) {
		stonefly_snap_preview_hide(server);
		return;
	}

	const int bw = cfg->snap_preview_border;
	wlr_scene_rect_set_size(p->fill, box.width, box.height);
	wlr_scene_rect_set_size(p->rim, box.width, box.height);
	wlr_scene_node_set_position(&p->fill->node, 0, 0);
	wlr_scene_node_set_position(&p->rim->node, 0, 0);

	if (bw > 0 && box.width > 2 * bw && box.height > 2 * bw) {
		wlr_scene_rect_set_clipped_region(
			p->rim, (struct clipped_region){
					.area =
						{
							.x = bw,
							.y = bw,
							.width = box.width - 2 * bw,
							.height = box.height - 2 * bw,
						},
					.corners = corner_radii_all(0),
				});
		wlr_scene_node_set_enabled(&p->rim->node, true);
	} else {
		wlr_scene_node_set_enabled(&p->rim->node, false);
	}

	wlr_scene_node_set_position(&p->tree->node, box.x, box.y);
	wlr_scene_node_set_enabled(&p->tree->node, true);
	wlr_scene_node_raise_to_top(&p->tree->node);
	p->visible = true;
	p->edge = edge;
}
