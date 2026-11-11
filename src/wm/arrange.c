#include "wm/arrange.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>
#include <scenefx/types/wlr_scene.h>

#include "config/config.h"
#include "decor/decor.h"
#include "layer/layer.h"
#include "log/log.h"
#include "output/output.h"
#include "seat/seat.h"
#include "server/server.h"
#include "ssd/ssd.h"
#include "toplevel/toplevel.h"
#include "wm/column.h"
#include "wm/geom.h"
#include "wm/layout_sm.h"
#include "wm/scratchpad.h"
#include "wm/snap_preview.h"
#include "wm/wm.h"
#include "wm/workspace.h"

#define SF_ARRANGE_MAX 64

static const enum sf_arrange arrange_cycle_order[] = {
	SF_ARRANGE_STACKING,
	SF_ARRANGE_COLUMNS,
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

bool sf_arrange_bind_matches(enum sf_arrange filter, enum sf_arrange current) {
	if (filter == SF_ARRANGE_UNSET) {
		return true;
	}
	if (filter == SF_ARRANGE_TILING) {
		return sf_arrange_is_tiling(current);
	}
	return filter == current;
}

bool sf_arrange_toplevel_managed(const struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->minimized || !toplevel->arrange_managed) {
		return false;
	}
	return toplevel->column != NULL;
}

static void save_float_if_needed(struct sf_toplevel *t) {
	if (t->arrange_float_saved) {
		return;
	}
	sf_toplevel_layout_geom(t, &t->arrange_float_box);
	t->arrange_float_saved = true;
}

static void demote_edged_for_join(struct sf_toplevel *t) {
	if (sf_layout_sm_is_fullscreen(&t->layout)) {
		return;
	}
	if (!sf_layout_sm_is_edged(&t->layout)) {
		return;
	}
	sf_toplevel_layout_float(t, false);
}

void sf_arrange_unlink(struct sf_toplevel *toplevel) {
	sf_column_remove_row(toplevel);
	if (toplevel != NULL) {
		toplevel->arrange_managed = false;
	}
}

void sf_arrange_break_out(struct sf_toplevel *toplevel) {
	if (toplevel == NULL) {
		return;
	}
	if (!toplevel->arrange_managed && toplevel->column == NULL) {
		toplevel->arrange_floating = true;
		toplevel->arrange_max_edges_rejoin = false;
		return;
	}
	sf_toplevel_layout_geom(toplevel, &toplevel->geom_regain_box);
	toplevel->geom_regain_hold = true;
	toplevel->arrange_managed = false;
	toplevel->arrange_floating = true;
	toplevel->arrange_max_edges_rejoin = false;
	toplevel->arrange_float_box = toplevel->geom_regain_box;
	toplevel->arrange_float_saved = true;
	struct sf_workspace *ws = toplevel->workspace;
	sf_column_remove_row(toplevel);
	if (toplevel->xdg_toplevel != NULL && toplevel->xdg_toplevel->base->initialized) {
		wlr_xdg_toplevel_set_tiled(toplevel->xdg_toplevel, 0);
	}
	if (ws != NULL) {
		sf_arrange_schedule(ws);
	}
}

void sf_arrange_join_toplevel(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->workspace == NULL || toplevel->minimized ||
	    sf_toplevel_is_scratchpad(toplevel) || toplevel->arrange_floating) {
		return;
	}
	if (sf_layout_sm_is_fullscreen(&toplevel->layout)) {
		return;
	}
	if (toplevel->xdg_toplevel != NULL && toplevel->xdg_toplevel->parent != NULL) {
		return;
	}
	demote_edged_for_join(toplevel);
	if (sf_layout_sm_is_fixed(&toplevel->layout)) {
		return;
	}
	struct sf_workspace *ws = toplevel->workspace;
	if (sf_workspace_arrange(ws) != SF_ARRANGE_COLUMNS) {
		return;
	}
	save_float_if_needed(toplevel);
	toplevel->arrange_floating = false;
	toplevel->geom_regain_hold = false;
	if (toplevel->column != NULL && toplevel->column->ws == ws) {
		toplevel->arrange_managed = true;
	} else {
		int after = sf_columns_focus_index(ws);
		sf_columns_insert_new(ws, toplevel, after + 1);
	}
	if (toplevel->scene_tree != NULL && !toplevel->scene_tree->node.enabled) {
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
	}
}

void sf_arrange_join_workspace(struct sf_workspace *ws) {
	if (ws == NULL || ws->output == NULL || ws->output->server == NULL) {
		return;
	}
	struct sf_server *server = ws->output->server;
	struct sf_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link) {
		if (t->workspace != ws || t->arrange_managed || t->arrange_floating ||
		    t->minimized) {
			continue;
		}
		sf_arrange_join_toplevel(t);
	}
}

void sf_arrange_float(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->minimized || sf_toplevel_is_scratchpad(toplevel)) {
		return;
	}
	if (toplevel->arrange_managed || toplevel->column != NULL) {
		sf_arrange_break_out(toplevel);
	} else {
		toplevel->arrange_floating = true;
		toplevel->arrange_max_edges_rejoin = false;
	}
	sf_decor_refresh_border(toplevel);
}

void sf_arrange_unfloat(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->minimized || sf_toplevel_is_scratchpad(toplevel)) {
		return;
	}
	struct sf_workspace *ws = toplevel->workspace;
	if (ws == NULL || !sf_arrange_is_tiling(sf_workspace_arrange(ws))) {
		return;
	}
	toplevel->arrange_floating = false;
	toplevel->arrange_max_edges_rejoin = false;
	if (sf_layout_sm_is_max_edges(&toplevel->layout) ||
	    sf_layout_sm_is_max_gaps(&toplevel->layout)) {
		sf_toplevel_layout_float(toplevel, false);
	}
	sf_arrange_join_toplevel(toplevel);
	sf_arrange_schedule(ws);
	sf_decor_refresh_border(toplevel);
}

void sf_arrange_toggle_float(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->minimized || sf_toplevel_is_scratchpad(toplevel)) {
		return;
	}
	if (toplevel->arrange_managed && !toplevel->arrange_floating) {
		sf_arrange_float(toplevel);
		return;
	}
	sf_arrange_unfloat(toplevel);
}

static void leave_tiling(struct sf_workspace *ws) {
	if (ws == NULL || ws->output == NULL || ws->output->server == NULL) {
		return;
	}
	struct sf_server *server = ws->output->server;
	struct sf_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link) {
		if (t->workspace != ws) {
			continue;
		}
		t->arrange_managed = false;
		t->arrange_max_edges_rejoin = false;
		sf_column_remove_row(t);
		if (t->xdg_toplevel != NULL && t->xdg_toplevel->base->initialized) {
			wlr_xdg_toplevel_set_tiled(t->xdg_toplevel, 0);
		}
		if (t->scene_tree != NULL && !t->scene_tree->node.enabled && !t->minimized) {
			wlr_scene_node_set_enabled(&t->scene_tree->node, true);
		}
		if (t->arrange_float_saved) {
			sf_toplevel_move_resize(t, &t->arrange_float_box, true);
			t->arrange_float_saved = false;
		}
		if (sf_layout_sm_is_edged(&t->layout)) {
			sf_toplevel_layout_float(t, false);
		}
		sf_decor_update_geom(t);
	}
	sf_columns_clear(ws);
}

static void rejoin_strays(struct sf_workspace *ws) {
	if (ws == NULL || ws->output == NULL || ws->output->server == NULL) {
		return;
	}
	struct sf_server *server = ws->output->server;
	struct sf_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link) {
		if (t->workspace != ws || t->minimized || t->arrange_floating ||
		    sf_toplevel_is_scratchpad(t)) {
			continue;
		}
		if (t->column != NULL) {
			continue;
		}
		if (sf_layout_sm_is_fixed(&t->layout)) {
			continue;
		}
		if (t->xdg_toplevel != NULL && t->xdg_toplevel->parent != NULL) {
			continue;
		}
		sf_arrange_join_toplevel(t);
	}
}

static void apply_boxes(struct sf_toplevel **wins, const struct wlr_box *boxes, size_t n,
			bool morph) {
	for (size_t i = 0; i < n; i++) {
		struct sf_toplevel *t = wins[i];
		const struct wlr_box *box = &boxes[i];
		if (box->width <= 0 || box->height <= 0) {
			continue;
		}
		if (t->scene_tree != NULL && !t->scene_tree->node.enabled) {
			wlr_scene_node_set_enabled(&t->scene_tree->node, true);
		}
		sf_toplevel_clear_auto_place(t);
		sf_toplevel_move_resize(t, box, morph);
		if (t->xdg_toplevel != NULL && t->xdg_toplevel->base->initialized) {
			uint32_t edges = (uint32_t)WLR_EDGE_TOP | (uint32_t)WLR_EDGE_BOTTOM |
					 (uint32_t)WLR_EDGE_LEFT | (uint32_t)WLR_EDGE_RIGHT;
			wlr_xdg_toplevel_set_tiled(t->xdg_toplevel, edges);
		}
		sf_decor_update_geom(t);
	}
}

void sf_workspace_apply_arrange(struct sf_workspace *ws) {
	if (ws == NULL || ws->output == NULL || ws->output->server == NULL) {
		return;
	}
	enum sf_arrange kind = sf_workspace_arrange(ws);
	if (!sf_arrange_is_tiling(kind)) {
		return;
	}
	struct sf_server *server = ws->output->server;
	if (server->cursor_mode == SF_CURSOR_ARRANGE_SWAP && server->grabbed_toplevel != NULL &&
	    server->grabbed_toplevel->workspace == ws) {
		return;
	}
	rejoin_strays(ws);

	struct wlr_box usable;
	if (!sf_output_usable_box(ws->output, &usable) || usable.width <= 0 || usable.height <= 0) {
		return;
	}
	const struct sf_config *cfg = sf_cfg(server);
	int gap = cfg->gap_size;
	sf_box_inset_gap(&usable, gap);
	if (usable.width <= 0 || usable.height <= 0) {
		return;
	}

	struct sf_toplevel *wins[SF_ARRANGE_MAX];
	struct wlr_box boxes[SF_ARRANGE_MAX];
	memset(boxes, 0, sizeof boxes);
	struct sf_toplevel *focus = sf_focused_toplevel(server);
	size_t n = sf_columns_apply(ws, &usable, gap, focus, wins, boxes, SF_ARRANGE_MAX);
	if (n == 0) {
		return;
	}
	for (size_t i = 0; i < n; i++) {
		int inset = sf_ssd_top_inset(wins[i]);
		if (inset > 0 && boxes[i].height > inset) {
			boxes[i].y += inset;
			boxes[i].height -= inset;
		}
	}
	bool morph = server->cursor_mode != SF_CURSOR_ARRANGE_RESIZE;
	apply_boxes(wins, boxes, n, morph);

	struct sf_toplevel *ft;
	wl_list_for_each(ft, &server->toplevels, link) {
		if (ft->workspace == ws && ft->arrange_floating && !ft->minimized &&
		    ft->scene_tree != NULL) {
			wlr_scene_node_raise_to_top(&ft->scene_tree->node);
		}
	}
	sf_pinned_restack(server);
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
	if (next == SF_ARRANGE_UNSET || next == SF_ARRANGE_TILING) {
		next = SF_ARRANGE_STACKING;
	}
	struct sf_workspace *ws = output->active;
	enum sf_arrange prev = sf_workspace_arrange(ws);
	ws->arrange = next;
	if (sf_arrange_is_tiling(next)) {
		sf_arrange_join_workspace(ws);
		SF_LOGI("arrange set", sf_str("name", sf_arrange_name(next)));
		sf_workspace_apply_arrange(ws);
	} else {
		leave_tiling(ws);
		SF_LOGI("arrange set", sf_str("name", sf_arrange_name(next)),
			sf_str("from", sf_arrange_name(prev)));
	}
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

void sf_arrange_consume(struct sf_server *server, const char *dir) {
	if (server == NULL || dir == NULL) {
		return;
	}
	int delta = 0;
	if (strcasecmp(dir, "left") == 0) {
		delta = -1;
	} else if (strcasecmp(dir, "right") == 0) {
		delta = 1;
	} else {
		return;
	}
	struct sf_toplevel *focus = sf_focused_toplevel(server);
	if (focus == NULL || focus->column == NULL || focus->workspace == NULL) {
		return;
	}
	if (sf_workspace_arrange(focus->workspace) != SF_ARRANGE_COLUMNS) {
		return;
	}
	if (sf_columns_consume(focus, delta)) {
		sf_arrange_schedule(focus->workspace);
		return;
	}
	if (sf_columns_expel(focus, delta)) {
		sf_arrange_schedule(focus->workspace);
	}
}

void sf_arrange_rotate(struct sf_server *server, int delta) {
	if (server == NULL || delta == 0) {
		return;
	}
	struct sf_output *output = sf_output_at_cursor(server);
	if (output == NULL || output->active == NULL) {
		return;
	}
	struct sf_workspace *ws = output->active;
	if (!sf_arrange_is_tiling(sf_workspace_arrange(ws))) {
		return;
	}
	size_t ncol = sf_columns_count(ws);
	if (ncol < 2) {
		return;
	}
	if (delta > 0) {
		struct sf_column *last = wl_container_of(ws->columns.prev, last, link);
		wl_list_remove(&last->link);
		wl_list_insert(&ws->columns, &last->link);
	} else {
		struct sf_column *first = wl_container_of(ws->columns.next, first, link);
		wl_list_remove(&first->link);
		wl_list_insert(ws->columns.prev, &first->link);
	}
	sf_arrange_schedule(ws);
}

enum sf_focus_dir {
	SF_FOCUS_LEFT = 0,
	SF_FOCUS_RIGHT,
	SF_FOCUS_UP,
	SF_FOCUS_DOWN,
};

static bool parse_focus_dir(const char *dir, enum sf_focus_dir *out) {
	if (dir == NULL || out == NULL) {
		return false;
	}
	if (strcasecmp(dir, "left") == 0) {
		*out = SF_FOCUS_LEFT;
		return true;
	}
	if (strcasecmp(dir, "right") == 0) {
		*out = SF_FOCUS_RIGHT;
		return true;
	}
	if (strcasecmp(dir, "up") == 0) {
		*out = SF_FOCUS_UP;
		return true;
	}
	if (strcasecmp(dir, "down") == 0) {
		*out = SF_FOCUS_DOWN;
		return true;
	}
	return false;
}

void sf_arrange_focus_dir(struct sf_server *server, const char *dir) {
	if (server == NULL) {
		return;
	}
	enum sf_focus_dir d;
	if (!parse_focus_dir(dir, &d)) {
		return;
	}
	struct sf_toplevel *focus = sf_focused_toplevel(server);
	if (focus == NULL || focus->workspace == NULL) {
		return;
	}
	struct sf_workspace *ws = focus->workspace;
	if (!sf_arrange_is_tiling(sf_workspace_arrange(ws))) {
		return;
	}

	struct sf_toplevel *wins[SF_ARRANGE_MAX];
	size_t n = sf_columns_collect(ws, wins, SF_ARRANGE_MAX);
	if (n > SF_ARRANGE_MAX) {
		n = SF_ARRANGE_MAX;
	}
	if (n < 2) {
		return;
	}

	struct wlr_box fb;
	sf_toplevel_layout_geom(focus, &fb);
	int fcx = fb.x + fb.width / 2;
	int fcy = fb.y + fb.height / 2;

	struct sf_toplevel *best = NULL;
	int best_score = -1;
	for (size_t i = 0; i < n; i++) {
		if (wins[i] == focus) {
			continue;
		}
		struct wlr_box b;
		sf_toplevel_layout_geom(wins[i], &b);
		int cx = b.x + b.width / 2;
		int cy = b.y + b.height / 2;
		int dx = cx - fcx;
		int dy = cy - fcy;
		bool ok = false;
		int primary = 0;
		int secondary = 0;
		switch (d) {
		case SF_FOCUS_LEFT:
			ok = dx < 0;
			primary = -dx;
			secondary = dy < 0 ? -dy : dy;
			break;
		case SF_FOCUS_RIGHT:
			ok = dx > 0;
			primary = dx;
			secondary = dy < 0 ? -dy : dy;
			break;
		case SF_FOCUS_UP:
			ok = dy < 0;
			primary = -dy;
			secondary = dx < 0 ? -dx : dx;
			break;
		case SF_FOCUS_DOWN:
			ok = dy > 0;
			primary = dy;
			secondary = dx < 0 ? -dx : dx;
			break;
		}
		if (!ok) {
			continue;
		}
		int score = primary * 1000 + secondary;
		if (best == NULL || score < best_score) {
			best = wins[i];
			best_score = score;
		}
	}
	if (best != NULL) {
		sf_focus_toplevel(best);
	}
}

bool sf_arrange_focus_cycle(struct sf_server *server, bool backward) {
	if (server == NULL) {
		return false;
	}
	struct sf_toplevel *focus = sf_focused_toplevel(server);
	struct sf_workspace *ws = NULL;
	if (focus != NULL) {
		ws = focus->workspace;
	} else {
		struct sf_output *output = sf_output_at_cursor(server);
		if (output != NULL) {
			ws = output->active;
		}
	}
	if (ws == NULL || !sf_arrange_is_tiling(sf_workspace_arrange(ws))) {
		return false;
	}

	struct sf_toplevel *wins[SF_ARRANGE_MAX];
	size_t n = sf_columns_collect(ws, wins, SF_ARRANGE_MAX);
	if (n > SF_ARRANGE_MAX) {
		n = SF_ARRANGE_MAX;
	}
	if (n < 2) {
		return false;
	}
	size_t fi = 0;
	bool found = false;
	for (size_t i = 0; i < n; i++) {
		if (wins[i] == focus) {
			fi = i;
			found = true;
			break;
		}
	}
	if (!found) {
		return false;
	}
	size_t next = backward ? (fi + n - 1) % n : (fi + 1) % n;
	sf_focus_toplevel(wins[next]);
	sf_pointer_follow_focus(server);
	return true;
}

/* Cell-drag drop ops; stored on server->arrange_swap so finish matches preview. */
enum sf_column_drop {
	SF_COLUMN_DROP_NONE = 0,
	SF_COLUMN_DROP_SWAP = 1,
	SF_COLUMN_DROP_INSERT = 2,
	SF_COLUMN_DROP_NEW_COL = 3,
};

#define SF_COLUMN_DROP_SEAM 0.15

static int hint_band(int gap) {
	int b = gap > 0 ? gap : 8;
	if (b < 8) {
		b = 8;
	}
	return b;
}

static int column_paint_width(struct sf_column *col, struct sf_toplevel *grab, size_t ncol,
			      const struct wlr_box *usable) {
	bool alone = ncol == 1 && col == grab->column && sf_column_row_count(col) <= 1;
	int cw;
	if (alone || ncol == 1 || col->full_width) {
		cw = usable->width;
	} else {
		cw = (int)(usable->width * (double)col->width_frac);
	}
	return cw < 1 ? 1 : cw;
}

static void columns_drop_clear(struct sf_server *server) {
	server->arrange_swap.hover = NULL;
	server->arrange_swap.drop_kind = SF_COLUMN_DROP_NONE;
	server->arrange_swap.drop_peer = NULL;
	server->arrange_swap.drop_column = NULL;
	server->arrange_swap.drop_index = 0;
	server->arrange_swap.drop_hint = (struct wlr_box){0};
	server->arrange_swap.drop_valid = false;
}

static bool columns_drop_at(struct sf_server *server, struct sf_toplevel *grab, double lx,
			    double ly, enum sf_column_drop *kind_out, struct sf_toplevel **peer_out,
			    struct sf_column **col_out, int *index_out, struct wlr_box *hint_out) {
	struct sf_workspace *ws = grab->workspace;
	*kind_out = SF_COLUMN_DROP_NONE;
	*peer_out = NULL;
	*col_out = NULL;
	*index_out = 0;
	*hint_out = (struct wlr_box){0};
	if (ws == NULL || ws->output == NULL) {
		return false;
	}

	struct wlr_box usable;
	if (!sf_output_usable_box(ws->output, &usable) || usable.width <= 0) {
		return false;
	}
	int gap = sf_cfg(server)->gap_size;
	sf_box_inset_gap(&usable, gap);
	int band = hint_band(gap);

	/* Peer cell: middle = swap, top/bottom seams = insert. */
	struct sf_toplevel *wins[SF_ARRANGE_MAX];
	size_t n = sf_columns_collect(ws, wins, SF_ARRANGE_MAX);
	if (n > SF_ARRANGE_MAX) {
		n = SF_ARRANGE_MAX;
	}
	for (size_t i = 0; i < n; i++) {
		struct sf_toplevel *t = wins[i];
		if (t == grab || t->column == NULL || t->workspace != ws) {
			continue;
		}
		struct wlr_box b;
		sf_toplevel_layout_geom(t, &b);
		if (b.width <= 0 || b.height <= 0) {
			continue;
		}
		if (lx < (double)b.x || lx >= (double)(b.x + b.width) || ly < (double)b.y ||
		    ly >= (double)(b.y + b.height)) {
			continue;
		}
		double frac = (ly - (double)b.y) / (double)b.height;
		int row = sf_column_row_of(t);
		if (row < 0) {
			continue;
		}
		if (frac < SF_COLUMN_DROP_SEAM) {
			*kind_out = SF_COLUMN_DROP_INSERT;
			*peer_out = t;
			*col_out = t->column;
			*index_out = row;
			*hint_out = (struct wlr_box){
				.x = b.x,
				.y = b.y - band / 2,
				.width = b.width,
				.height = band,
			};
			return true;
		}
		if (frac > 1.0 - SF_COLUMN_DROP_SEAM) {
			*kind_out = SF_COLUMN_DROP_INSERT;
			*peer_out = t;
			*col_out = t->column;
			*index_out = row + 1;
			*hint_out = (struct wlr_box){
				.x = b.x,
				.y = b.y + b.height - band / 2,
				.width = b.width,
				.height = band,
			};
			return true;
		}
		*kind_out = SF_COLUMN_DROP_SWAP;
		*peer_out = t;
		*col_out = t->column;
		*index_out = row;
		*hint_out = b;
		return true;
	}

	/* Strip gaps / edges → new column. */
	size_t ncol = sf_columns_count(ws);
	if (ncol == 0) {
		*kind_out = SF_COLUMN_DROP_NEW_COL;
		*index_out = 0;
		*hint_out = (struct wlr_box){
			.x = usable.x,
			.y = usable.y,
			.width = band,
			.height = usable.height,
		};
		return true;
	}

	int view = ws->columns_view_offset;
	int x = 0;
	int k = 0;
	struct sf_column *col;
	wl_list_for_each(col, &ws->columns, link) {
		int cw = column_paint_width(col, grab, ncol, &usable);
		int left = usable.x + x - view;
		int right = left + cw;
		if (lx < (double)left) {
			*kind_out = SF_COLUMN_DROP_NEW_COL;
			*col_out = col;
			*index_out = k;
			*hint_out = (struct wlr_box){
				.x = left - band / 2,
				.y = usable.y,
				.width = band,
				.height = usable.height,
			};
			return true;
		}
		if (lx < (double)right) {
			/* Inside a column but not on a peer cell (e.g. own cell): no-op. */
			return false;
		}
		x += cw + (k + 1 < (int)ncol ? gap : 0);
		k++;
	}
	*kind_out = SF_COLUMN_DROP_NEW_COL;
	*index_out = (int)ncol;
	*hint_out = (struct wlr_box){
		.x = usable.x + x - view - band / 2,
		.y = usable.y,
		.width = band,
		.height = usable.height,
	};
	return true;
}

static void columns_drop_apply(struct sf_toplevel *grab, enum sf_column_drop kind,
			       struct sf_toplevel *peer, struct sf_column *col, int index) {
	struct sf_workspace *ws = grab->workspace;
	switch (kind) {
	case SF_COLUMN_DROP_SWAP:
		if (peer != NULL) {
			sf_columns_swap_rows(grab, peer);
		}
		break;
	case SF_COLUMN_DROP_INSERT:
		if (col != NULL) {
			sf_column_insert_row(col, grab, index);
		}
		break;
	case SF_COLUMN_DROP_NEW_COL:
		if (ws != NULL) {
			sf_columns_insert_new(ws, grab, index);
		}
		break;
	case SF_COLUMN_DROP_NONE:
	default:
		break;
	}
}

void sf_arrange_swap_begin(struct sf_server *server, struct sf_toplevel *toplevel) {
	if (server == NULL || toplevel == NULL || !toplevel->arrange_managed ||
	    toplevel->scene_tree == NULL) {
		return;
	}
	sf_toplevel_clear_auto_place(toplevel);
	server->arrange_swap.active = true;
	columns_drop_clear(server);
	server->grab_x = server->cursor->x - (double)toplevel->scene_tree->node.x;
	server->grab_y = server->cursor->y - (double)toplevel->scene_tree->node.y;
	sf_toplevel_raise(toplevel);
}

void sf_arrange_swap_motion(struct sf_server *server) {
	if (server == NULL || !server->arrange_swap.active) {
		return;
	}
	struct sf_toplevel *grab = server->grabbed_toplevel;
	if (grab == NULL || grab->scene_tree == NULL || grab->workspace == NULL) {
		return;
	}
	int x = (int)(server->cursor->x - server->grab_x);
	int y = (int)(server->cursor->y - server->grab_y);
	wlr_scene_node_set_position(&grab->scene_tree->node, x, y);
	grab->resting_x = x;
	grab->resting_y = y;

	enum sf_column_drop kind = SF_COLUMN_DROP_NONE;
	struct sf_toplevel *peer = NULL;
	struct sf_column *col = NULL;
	int index = 0;
	struct wlr_box hint = {0};
	bool ok = false;
	if (sf_workspace_arrange(grab->workspace) == SF_ARRANGE_COLUMNS) {
		ok = columns_drop_at(server, grab, server->cursor->x, server->cursor->y, &kind,
				     &peer, &col, &index, &hint);
	}
	server->arrange_swap.hover = peer;
	server->arrange_swap.drop_kind = (int)kind;
	server->arrange_swap.drop_peer = peer;
	server->arrange_swap.drop_column = col;
	server->arrange_swap.drop_index = index;
	server->arrange_swap.drop_hint = hint;
	server->arrange_swap.drop_valid = ok;
	if (ok && kind != SF_COLUMN_DROP_NONE && hint.width > 0 && hint.height > 0) {
		sf_snap_preview_show_box(server, &hint);
	} else {
		sf_snap_preview_hide(server);
	}
}

void sf_arrange_swap_finish(struct sf_server *server) {
	if (server == NULL) {
		return;
	}
	struct sf_toplevel *grab = server->grabbed_toplevel;
	struct sf_workspace *ws = grab != NULL ? grab->workspace : NULL;
	bool drop_valid = server->arrange_swap.drop_valid;
	enum sf_column_drop kind = (enum sf_column_drop)server->arrange_swap.drop_kind;
	struct sf_toplevel *peer = server->arrange_swap.drop_peer;
	struct sf_column *col = server->arrange_swap.drop_column;
	int index = server->arrange_swap.drop_index;
	sf_snap_preview_hide(server);
	server->arrange_swap.active = false;
	columns_drop_clear(server);
	if (grab == NULL || ws == NULL || !grab->arrange_managed) {
		if (ws != NULL) {
			sf_arrange_schedule(ws);
		}
		return;
	}
	if (sf_workspace_arrange(ws) == SF_ARRANGE_COLUMNS && drop_valid) {
		columns_drop_apply(grab, kind, peer, col, index);
	}
	sf_arrange_schedule(ws);
}

static float clamp_col_ratio_f(float v) {
	if (v < 0.05f) {
		return 0.05f;
	}
	if (v > 1.0f) {
		return 1.0f;
	}
	return v;
}

void sf_arrange_resize_begin(struct sf_server *server, struct sf_toplevel *toplevel) {
	if (server == NULL || toplevel == NULL || !toplevel->arrange_managed ||
	    toplevel->workspace == NULL || toplevel->workspace->output == NULL) {
		return;
	}
	struct sf_workspace *ws = toplevel->workspace;
	if (!sf_arrange_is_tiling(sf_workspace_arrange(ws))) {
		return;
	}
	struct wlr_box usable;
	if (!sf_output_usable_box(ws->output, &usable) || usable.width <= 0 || usable.height <= 0) {
		return;
	}
	const struct sf_config *cfg = sf_cfg(server);
	sf_box_inset_gap(&usable, cfg->gap_size);
	if (usable.width <= 0) {
		return;
	}
	float start = cfg->column_ratio;
	if (toplevel->column != NULL) {
		start = toplevel->column->width_frac;
	}
	sf_toplevel_clear_auto_place(toplevel);
	server->arrange_resize.active = true;
	server->arrange_resize.vertical = false;
	server->arrange_resize.start_cx = server->cursor->x;
	server->arrange_resize.start_cy = server->cursor->y;
	server->arrange_resize.start_factor = start;
	server->arrange_resize.span = usable.width;
}

void sf_arrange_resize_motion(struct sf_server *server) {
	if (server == NULL || !server->arrange_resize.active) {
		return;
	}
	struct sf_toplevel *grab = server->grabbed_toplevel;
	if (grab == NULL || grab->workspace == NULL) {
		return;
	}
	struct sf_workspace *ws = grab->workspace;
	int span = server->arrange_resize.span;
	if (span < 1) {
		span = 1;
	}
	double delta = server->cursor->x - server->arrange_resize.start_cx;
	float next = server->arrange_resize.start_factor + (float)(delta / (double)span);
	next = clamp_col_ratio_f(next);
	if (grab->column != NULL) {
		grab->column->width_frac = next;
		grab->column->full_width = false;
		grab->column->width_frac_saved = 0.0f;
	}
	server->config.column_ratio = next;
	sf_workspace_apply_arrange(ws);
}

void sf_arrange_resize_finish(struct sf_server *server) {
	if (server == NULL) {
		return;
	}
	struct sf_toplevel *grab = server->grabbed_toplevel;
	struct sf_workspace *ws = grab != NULL ? grab->workspace : NULL;
	server->arrange_resize.active = false;
	if (ws != NULL) {
		sf_arrange_schedule(ws);
	}
}
