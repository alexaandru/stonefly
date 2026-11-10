#include "wm/arrange.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>
#include <scenefx/types/wlr_scene.h>

#include "config/config.h"
#include "decor/decor.h"
#include "layer/layer.h"
#include "log/log.h"
#include "output/output.h"
#include "ssd/ssd.h"
#include "wm/geom.h"
#include "wm/layout_sm.h"
#include "wm/scratchpad.h"
#include "wm/wm.h"
#include "wm/workspace.h"

#define SF_ARRANGE_MAX 64

static const enum sf_arrange arrange_cycle_order[] = {
	SF_ARRANGE_STACKING, SF_ARRANGE_COLUMNS, SF_ARRANGE_MASTER,
	SF_ARRANGE_DWINDLE,  SF_ARRANGE_MONOCLE,
};

enum sf_arrange sf_workspace_arrange(const struct sf_workspace *ws) {
	if (ws == NULL) {
		return SF_ARRANGE_STACKING;
	}
	if (ws->arrange != SF_ARRANGE_UNSET) {
		return ws->arrange;
	}
	if (ws->output != NULL && ws->output->arrange != SF_ARRANGE_UNSET) {
		return ws->output->arrange;
	}
	if (ws->output == NULL || ws->output->server == NULL) {
		return SF_ARRANGE_STACKING;
	}
	return ws->output->server->config.arrange;
}

bool sf_arrange_toplevel_managed(const struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->minimized || !toplevel->arrange_managed) {
		return false;
	}
	if (sf_toplevel_is_scratchpad(toplevel)) {
		return false;
	}
	if (sf_layout_sm_is_fixed(&toplevel->layout)) {
		return false;
	}
	if (toplevel->xdg_toplevel != NULL && toplevel->xdg_toplevel->parent != NULL) {
		return false;
	}
	return toplevel->workspace != NULL;
}

void sf_arrange_break_out(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || !toplevel->arrange_managed) {
		return;
	}
	sf_toplevel_layout_geom(toplevel, &toplevel->geom_regain_box);
	toplevel->geom_regain_hold = true;
	toplevel->arrange_managed = false;
	if (toplevel->workspace != NULL) {
		sf_arrange_schedule(toplevel->workspace);
	}
}

void sf_arrange_join_workspace(struct sf_workspace *ws) {
	if (ws == NULL || ws->output == NULL || ws->output->server == NULL) {
		return;
	}
	struct sf_server *server = ws->output->server;
	struct sf_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link) {
		if (t->workspace != ws || t->minimized || sf_toplevel_is_scratchpad(t)) {
			continue;
		}
		if (sf_layout_sm_is_fixed(&t->layout)) {
			continue;
		}
		if (t->xdg_toplevel != NULL && t->xdg_toplevel->parent != NULL) {
			continue;
		}
		t->arrange_managed = true;
		t->geom_regain_hold = false;
	}
}

static size_t collect_managed(struct sf_workspace *ws, struct sf_toplevel **out, size_t cap) {
	if (ws == NULL || ws->output == NULL || ws->output->server == NULL) {
		return 0;
	}
	size_t n = 0;
	struct sf_toplevel *t;
	wl_list_for_each(t, &ws->output->server->toplevels, link) {
		if (t->workspace != ws || !sf_arrange_toplevel_managed(t)) {
			continue;
		}
		if (n < cap) {
			out[n] = t;
		}
		n++;
	}
	return n;
}

static size_t focus_index(struct sf_toplevel **wins, size_t n, struct sf_toplevel *focus) {
	if (focus == NULL) {
		return 0;
	}
	for (size_t i = 0; i < n; i++) {
		if (wins[i] == focus) {
			return i;
		}
	}
	return 0;
}

/* Keep focused column visible inside usable (niri scroll). */
static void columns_scroll_to_focus(struct sf_workspace *ws, const struct wlr_box *usable, size_t n,
				    size_t focus_idx, int col_w, int gap) {
	if (n == 0 || usable == NULL) {
		return;
	}
	int stride = col_w + gap;
	int col_x = usable->x + (int)focus_idx * stride - ws->columns_view_offset;
	int col_r = col_x + col_w;
	int view_l = usable->x;
	int view_r = usable->x + usable->width;
	if (col_x < view_l) {
		ws->columns_view_offset -= view_l - col_x;
	} else if (col_r > view_r) {
		ws->columns_view_offset += col_r - view_r;
	}
	if (ws->columns_view_offset < 0) {
		ws->columns_view_offset = 0;
	}
}

static void apply_boxes(struct sf_toplevel **wins, const struct wlr_box *boxes, size_t n,
			enum sf_arrange kind) {
	for (size_t i = 0; i < n; i++) {
		struct sf_toplevel *t = wins[i];
		const struct wlr_box *box = &boxes[i];
		if (box->width <= 0 || box->height <= 0) {
			/* Monocle non-focused: park under focused, zero size skip. */
			if (kind == SF_ARRANGE_MONOCLE) {
				wlr_scene_node_set_enabled(&t->scene_tree->node, false);
			}
			continue;
		}
		if (!t->scene_tree->node.enabled) {
			wlr_scene_node_set_enabled(&t->scene_tree->node, true);
		}
		sf_toplevel_clear_auto_place(t);
		sf_toplevel_move_resize(t, box, true);
		if (t->xdg_toplevel != NULL && t->xdg_toplevel->base->initialized) {
			uint32_t edges = (uint32_t)WLR_EDGE_TOP | (uint32_t)WLR_EDGE_BOTTOM |
					 (uint32_t)WLR_EDGE_LEFT | (uint32_t)WLR_EDGE_RIGHT;
			wlr_xdg_toplevel_set_tiled(t->xdg_toplevel, edges);
		}
		sf_decor_update_geom(t);
	}
}

void sf_workspace_apply_arrange(struct sf_workspace *ws) {
	if (ws == NULL || ws->output == NULL) {
		return;
	}
	enum sf_arrange kind = sf_workspace_arrange(ws);
	if (!sf_arrange_is_tiling(kind)) {
		return;
	}

	struct wlr_box usable;
	if (!sf_output_usable_box(ws->output, &usable) || usable.width <= 0 || usable.height <= 0) {
		return;
	}
	const struct sf_config *cfg = sf_cfg(ws->output->server);
	int gap = cfg->gap_size;
	sf_box_inset_gap(&usable, gap);
	if (usable.width <= 0 || usable.height <= 0) {
		return;
	}

	struct sf_toplevel *wins[SF_ARRANGE_MAX];
	size_t n = collect_managed(ws, wins, SF_ARRANGE_MAX);
	if (n > SF_ARRANGE_MAX) {
		SF_LOGW("arrange truncated", sf_i64("n", (int64_t)n));
		n = SF_ARRANGE_MAX;
	}
	if (n == 0) {
		return;
	}

	/* Content boxes already gap-inset; SSD titleband eats top of each pane. */
	struct wlr_box boxes[SF_ARRANGE_MAX];
	memset(boxes, 0, sizeof boxes);
	struct sf_toplevel *focus = sf_focused_toplevel(ws->output->server);
	size_t fi = focus_index(wins, n, focus);

	bool ok = false;
	switch (kind) {
	case SF_ARRANGE_MASTER:
		ok = sf_arrange_math_master(&usable, n, cfg->master_pos, cfg->master_factor,
					    cfg->master_count, boxes);
		break;
	case SF_ARRANGE_COLUMNS: {
		double ratio = cfg->column_ratio;
		int col_w = (int)(usable.width * ratio);
		if (col_w < 1) {
			col_w = 1;
		}
		columns_scroll_to_focus(ws, &usable, n, fi, col_w, gap);
		ok = sf_arrange_math_columns(&usable, n, ratio, gap, ws->columns_view_offset,
					     boxes);
		break;
	}
	case SF_ARRANGE_DWINDLE:
		ok = sf_arrange_math_dwindle(&usable, n, boxes);
		break;
	case SF_ARRANGE_MONOCLE:
		ok = sf_arrange_math_monocle(&usable, n, fi, boxes);
		break;
	case SF_ARRANGE_STACKING:
	case SF_ARRANGE_UNSET:
		break;
	}
	if (!ok) {
		return;
	}

	for (size_t i = 0; i < n; i++) {
		int inset = sf_ssd_top_inset(wins[i]);
		if (inset > 0 && boxes[i].height > inset) {
			boxes[i].y += inset;
			boxes[i].height -= inset;
		}
	}
	apply_boxes(wins, boxes, n, kind);
}

static void arrange_idle_cb(void *data) {
	struct sf_workspace *ws = data;
	ws->arrange_idle = NULL;
	sf_workspace_apply_arrange(ws);
}

void sf_arrange_schedule(struct sf_workspace *ws) {
	if (ws == NULL || ws->output == NULL || ws->output->server == NULL) {
		return;
	}
	if (!sf_arrange_is_tiling(sf_workspace_arrange(ws))) {
		return;
	}
	if (ws->arrange_idle != NULL) {
		return;
	}
	struct wl_event_loop *loop = wl_display_get_event_loop(ws->output->server->wl_display);
	if (loop == NULL) {
		sf_workspace_apply_arrange(ws);
		return;
	}
	ws->arrange_idle = wl_event_loop_add_idle(loop, arrange_idle_cb, ws);
}

void sf_arrange_set(struct sf_server *server, const char *name) {
	if (server == NULL) {
		return;
	}
	struct sf_output *output = sf_output_at_cursor(server);
	if (output == NULL || output->active == NULL) {
		return;
	}
	enum sf_arrange next = sf_arrange_parse(name);
	if (next == SF_ARRANGE_UNSET) {
		next = SF_ARRANGE_STACKING;
	}
	struct sf_workspace *ws = output->active;
	ws->arrange = next;
	if (sf_arrange_is_tiling(next)) {
		sf_arrange_join_workspace(ws);
	} else {
		struct sf_toplevel *t;
		wl_list_for_each(t, &server->toplevels, link) {
			if (t->workspace == ws) {
				t->arrange_managed = false;
				if (t->xdg_toplevel != NULL && t->xdg_toplevel->base->initialized) {
					wlr_xdg_toplevel_set_tiled(t->xdg_toplevel, 0);
				}
				if (t->scene_tree != NULL && !t->scene_tree->node.enabled &&
				    !t->minimized) {
					wlr_scene_node_set_enabled(&t->scene_tree->node, true);
				}
			}
		}
	}
	SF_LOGI("arrange set", sf_str("name", sf_arrange_name(next)));
	sf_workspace_apply_arrange(ws);
}

void sf_arrange_cycle(struct sf_server *server, int delta) {
	if (server == NULL || delta == 0) {
		return;
	}
	struct sf_output *output = sf_output_at_cursor(server);
	if (output == NULL || output->active == NULL) {
		return;
	}
	enum sf_arrange cur = sf_workspace_arrange(output->active);
	size_t n = sizeof arrange_cycle_order / sizeof arrange_cycle_order[0];
	size_t idx = 0;
	for (size_t i = 0; i < n; i++) {
		if (arrange_cycle_order[i] == cur) {
			idx = i;
			break;
		}
	}
	if (delta > 0) {
		idx = (idx + 1) % n;
	} else {
		idx = (idx + n - 1) % n;
	}
	sf_arrange_set(server, sf_arrange_name(arrange_cycle_order[idx]));
}
