#include <stdlib.h>
#include <scenefx/types/fx/clipped_region.h>
#include <scenefx/types/wlr_scene.h>

#include "config/config.h"
#include "server/server.h"
#include "util/color.h"
#include "wm/snap_preview.h"
#include "wm/wm.h"
#include "toplevel/toplevel.h"

static void preview_colors(struct sf_server *server, float fill[4], float rim[4]) {
	const struct sf_config *cfg = sf_cfg(server);
	struct sf_rgba premul;
	sf_rgba_premul(&cfg->border_active, cfg->snap_preview_opacity, &premul);
	fill[0] = premul.r;
	fill[1] = premul.g;
	fill[2] = premul.b;
	fill[3] = premul.a;
	rim[0] = cfg->border_active.r;
	rim[1] = cfg->border_active.g;
	rim[2] = cfg->border_active.b;
	rim[3] = cfg->border_active.a;
}

void sf_snap_preview_init(struct sf_server *server) {
	struct sf_snap_preview *p = calloc(1, sizeof(*p));
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
	p->edge = SF_SNAP_NONE;
	server->snap_preview = p;
}

void sf_snap_preview_finish(struct sf_server *server) {
	struct sf_snap_preview *p = server->snap_preview;
	if (p == NULL) {
		return;
	}
	if (p->tree != NULL) {
		wlr_scene_node_destroy(&p->tree->node);
	}
	free(p);
	server->snap_preview = NULL;
}

void sf_snap_preview_hide(struct sf_server *server) {
	struct sf_snap_preview *p = server->snap_preview;
	if (p == NULL || !p->visible) {
		return;
	}
	wlr_scene_node_set_enabled(&p->tree->node, false);
	p->visible = false;
	p->edge = SF_SNAP_NONE;
}

void sf_snap_preview_show(struct sf_server *server, enum sf_snap edge,
			  const struct wlr_box *usable) {
	if (server->snap_preview == NULL) {
		sf_snap_preview_init(server);
	}
	struct sf_snap_preview *p = server->snap_preview;
	if (p == NULL || usable == NULL || edge == SF_SNAP_NONE) {
		sf_snap_preview_hide(server);
		return;
	}

	const struct sf_config *cfg = sf_cfg(server);
	struct wlr_box box = sf_wm_snap_box(edge, usable, cfg->gap_size);
	if (box.width <= 0 || box.height <= 0) {
		sf_snap_preview_hide(server);
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
