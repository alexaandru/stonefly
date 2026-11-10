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
#include "server/server.h"
#include "ssd/ssd.h"
#include "toplevel/toplevel.h"
#include "wm/geom.h"
#include "wm/layout_sm.h"
#include "wm/scratchpad.h"
#include "wm/snap_preview.h"
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

static void arrange_tiles_unlink(struct sf_toplevel *t) {
	if (t == NULL || t->arrange_link.next == NULL || wl_list_empty(&t->arrange_link)) {
		return;
	}
	wl_list_remove(&t->arrange_link);
	wl_list_init(&t->arrange_link);
}

/* Append only when not already linked — keeps packing order across focus. */
static void arrange_tiles_link(struct sf_workspace *ws, struct sf_toplevel *t) {
	if (ws == NULL || t == NULL) {
		return;
	}
	if (t->arrange_link.next == NULL) {
		wl_list_init(&t->arrange_link);
	}
	if (!wl_list_empty(&t->arrange_link)) {
		return;
	}
	wl_list_insert(ws->arrange_tiles.prev, &t->arrange_link);
}

void sf_arrange_unlink(struct sf_toplevel *toplevel) {
	arrange_tiles_unlink(toplevel);
}

void sf_arrange_break_out(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || !toplevel->arrange_managed) {
		return;
	}
	sf_toplevel_layout_geom(toplevel, &toplevel->geom_regain_box);
	toplevel->geom_regain_hold = true;
	toplevel->arrange_managed = false;
	arrange_tiles_unlink(toplevel);
	/* New float pose becomes the stacking restore target. */
	toplevel->arrange_float_box = toplevel->geom_regain_box;
	toplevel->arrange_float_saved = true;
	if (toplevel->xdg_toplevel != NULL && toplevel->xdg_toplevel->base->initialized) {
		wlr_xdg_toplevel_set_tiled(toplevel->xdg_toplevel, 0);
	}
	if (toplevel->workspace != NULL) {
		sf_arrange_schedule(toplevel->workspace);
	}
}

static void save_float_if_needed(struct sf_toplevel *t) {
	if (t->arrange_float_saved) {
		return;
	}
	sf_toplevel_layout_geom(t, &t->arrange_float_box);
	t->arrange_float_saved = true;
}

static void demote_edged_for_join(struct sf_toplevel *t) {
	/* Fullscreen stays out of the tile set. Edged leftovers (e.g. monocle
	 * box that GEOM-regained after a stacking hop) must demote so join
	 * can claim them again. */
	if (!sf_layout_sm_is_edged(&t->layout)) {
		return;
	}
	sf_toplevel_layout_float(t, false);
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
		if (sf_layout_sm_is_fullscreen(&t->layout)) {
			continue;
		}
		if (t->xdg_toplevel != NULL && t->xdg_toplevel->parent != NULL) {
			continue;
		}
		demote_edged_for_join(t);
		if (sf_layout_sm_is_fixed(&t->layout)) {
			continue;
		}
		save_float_if_needed(t);
		t->arrange_managed = true;
		t->geom_regain_hold = false;
		arrange_tiles_link(ws, t);
		if (t->scene_tree != NULL && !t->scene_tree->node.enabled) {
			wlr_scene_node_set_enabled(&t->scene_tree->node, true);
		}
	}
}

void sf_arrange_toggle_float(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->minimized || sf_toplevel_is_scratchpad(toplevel)) {
		return;
	}
	if (sf_layout_sm_is_fullscreen(&toplevel->layout)) {
		return;
	}
	if (toplevel->arrange_managed) {
		sf_arrange_break_out(toplevel);
		return;
	}
	struct sf_workspace *ws = toplevel->workspace;
	if (ws == NULL || !sf_arrange_is_tiling(sf_workspace_arrange(ws))) {
		return;
	}
	if (toplevel->xdg_toplevel != NULL && toplevel->xdg_toplevel->parent != NULL) {
		return;
	}
	demote_edged_for_join(toplevel);
	if (sf_layout_sm_is_fixed(&toplevel->layout)) {
		return;
	}
	save_float_if_needed(toplevel);
	toplevel->arrange_managed = true;
	toplevel->geom_regain_hold = false;
	arrange_tiles_link(ws, toplevel);
	sf_arrange_schedule(ws);
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
		arrange_tiles_unlink(t);
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
		/* Drop edged chrome left over from a full-usable monocle box. */
		if (sf_layout_sm_is_edged(&t->layout)) {
			sf_toplevel_layout_float(t, false);
		}
		sf_decor_update_geom(t);
	}
}

static size_t collect_managed(struct sf_workspace *ws, struct sf_toplevel **out, size_t cap) {
	if (ws == NULL) {
		return 0;
	}
	size_t n = 0;
	struct sf_toplevel *t;
	/* Walk stable tile order — never server->toplevels (MRU / focus). */
	wl_list_for_each(t, &ws->arrange_tiles, arrange_link) {
		if (!sf_arrange_toplevel_managed(t)) {
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

static void reenable_managed(struct sf_toplevel **wins, size_t n) {
	for (size_t i = 0; i < n; i++) {
		struct sf_toplevel *t = wins[i];
		if (t->scene_tree != NULL && !t->scene_tree->node.enabled) {
			wlr_scene_node_set_enabled(&t->scene_tree->node, true);
		}
	}
}

static void apply_boxes(struct sf_toplevel **wins, const struct wlr_box *boxes, size_t n,
			enum sf_arrange kind, bool morph) {
	for (size_t i = 0; i < n; i++) {
		struct sf_toplevel *t = wins[i];
		const struct wlr_box *box = &boxes[i];
		if (box->width <= 0 || box->height <= 0) {
			if (kind == SF_ARRANGE_MONOCLE && t->scene_tree != NULL) {
				wlr_scene_node_set_enabled(&t->scene_tree->node, false);
			}
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
	/* Do not repack mid tile-swap drag — that would yank the grabbed window. */
	struct sf_server *server = ws->output->server;
	if (server->cursor_mode == SF_CURSOR_ARRANGE_SWAP && server->grabbed_toplevel != NULL &&
	    server->grabbed_toplevel->workspace == ws) {
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

	/* Leaving monocle (or any prior hide) must show everyone before pack. */
	if (kind != SF_ARRANGE_MONOCLE) {
		reenable_managed(wins, n);
	}

	struct wlr_box boxes[SF_ARRANGE_MAX];
	memset(boxes, 0, sizeof boxes);
	struct sf_toplevel *focus = sf_focused_toplevel(ws->output->server);
	size_t fi = focus_index(wins, n, focus);

	bool ok = false;
	switch (kind) {
	case SF_ARRANGE_MASTER:
		ok = sf_arrange_math_master(&usable, n, cfg->master_pos, cfg->master_factor,
					    cfg->master_count, gap, boxes);
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
		ok = sf_arrange_math_dwindle(&usable, n, cfg->dwindle_factor, gap, boxes);
		break;
	case SF_ARRANGE_MONOCLE:
		ok = sf_arrange_math_monocle(&usable, n, fi, boxes);
		break;
	case SF_ARRANGE_STACKING:
	case SF_ARRANGE_UNSET:
	case SF_ARRANGE_TILING:
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
	/* Instant reflow while Super+RMB factor-dragging; morph otherwise. */
	bool morph = server->cursor_mode != SF_CURSOR_ARRANGE_RESIZE;
	apply_boxes(wins, boxes, n, kind, morph);
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
	/* Only managed members; skip empty / single-tile. */
	size_t n = 0;
	struct sf_toplevel *t;
	wl_list_for_each(t, &ws->arrange_tiles, arrange_link) {
		if (sf_arrange_toplevel_managed(t)) {
			n++;
		}
	}
	if (n < 2) {
		return;
	}
	if (delta > 0) {
		/* Clockwise: last → first (columns: last becomes 1st, rest shift right). */
		struct sf_toplevel *last =
			wl_container_of(ws->arrange_tiles.prev, last, arrange_link);
		wl_list_remove(&last->arrange_link);
		wl_list_insert(&ws->arrange_tiles, &last->arrange_link);
	} else {
		/* Counter-clockwise: first → last. */
		struct sf_toplevel *first =
			wl_container_of(ws->arrange_tiles.next, first, arrange_link);
		wl_list_remove(&first->arrange_link);
		wl_list_insert(ws->arrange_tiles.prev, &first->arrange_link);
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
	size_t n = collect_managed(ws, wins, SF_ARRANGE_MAX);
	if (n > SF_ARRANGE_MAX) {
		n = SF_ARRANGE_MAX;
	}
	if (n < 2) {
		return;
	}
	size_t fi = focus_index(wins, n, focus);

	/* Monocle: arrows cycle the hidden stack. */
	if (sf_workspace_arrange(ws) == SF_ARRANGE_MONOCLE) {
		size_t next =
			(d == SF_FOCUS_LEFT || d == SF_FOCUS_UP) ? (fi + n - 1) % n : (fi + 1) % n;
		sf_focus_toplevel(wins[next]);
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

static struct sf_toplevel *swap_peer_at(struct sf_workspace *ws, struct sf_toplevel *grab,
					double lx, double ly) {
	if (ws == NULL || grab == NULL) {
		return NULL;
	}
	struct sf_toplevel *t;
	wl_list_for_each(t, &ws->arrange_tiles, arrange_link) {
		if (t == grab || !sf_arrange_toplevel_managed(t) || t->scene_tree == NULL) {
			continue;
		}
		/* Peers stay on their tile boxes while the grab follows the pointer. */
		struct wlr_box b;
		sf_toplevel_layout_geom(t, &b);
		if (lx >= (double)b.x && lx < (double)(b.x + b.width) && ly >= (double)b.y &&
		    ly < (double)(b.y + b.height)) {
			return t;
		}
	}
	return NULL;
}

static bool arrange_swap_order(struct sf_toplevel *a, struct sf_toplevel *b) {
	if (a == NULL || b == NULL || a == b || a->workspace == NULL ||
	    a->workspace != b->workspace) {
		return false;
	}
	struct sf_workspace *ws = a->workspace;
	struct sf_toplevel *wins[SF_ARRANGE_MAX];
	size_t n = collect_managed(ws, wins, SF_ARRANGE_MAX);
	if (n > SF_ARRANGE_MAX) {
		n = SF_ARRANGE_MAX;
	}
	size_t ia = n;
	size_t ib = n;
	for (size_t i = 0; i < n; i++) {
		if (wins[i] == a) {
			ia = i;
		}
		if (wins[i] == b) {
			ib = i;
		}
	}
	if (ia >= n || ib >= n) {
		return false;
	}
	wins[ia] = b;
	wins[ib] = a;
	wl_list_init(&ws->arrange_tiles);
	for (size_t i = 0; i < n; i++) {
		wl_list_init(&wins[i]->arrange_link);
		wl_list_insert(ws->arrange_tiles.prev, &wins[i]->arrange_link);
	}
	return true;
}

void sf_arrange_swap_begin(struct sf_server *server, struct sf_toplevel *toplevel) {
	if (server == NULL || toplevel == NULL || !toplevel->arrange_managed ||
	    toplevel->scene_tree == NULL) {
		return;
	}
	sf_toplevel_clear_auto_place(toplevel);
	server->arrange_swap.active = true;
	server->arrange_swap.hover = NULL;
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

	struct sf_toplevel *hover =
		swap_peer_at(grab->workspace, grab, server->cursor->x, server->cursor->y);
	server->arrange_swap.hover = hover;
	if (hover != NULL) {
		struct wlr_box box;
		sf_toplevel_layout_geom(hover, &box);
		sf_snap_preview_show_box(server, &box);
	} else {
		sf_snap_preview_hide(server);
	}
}

void sf_arrange_swap_finish(struct sf_server *server) {
	if (server == NULL) {
		return;
	}
	struct sf_toplevel *grab = server->grabbed_toplevel;
	struct sf_toplevel *hover = server->arrange_swap.hover;
	struct sf_workspace *ws = grab != NULL ? grab->workspace : NULL;
	sf_snap_preview_hide(server);
	server->arrange_swap.active = false;
	server->arrange_swap.hover = NULL;
	if (grab == NULL || ws == NULL || !grab->arrange_managed) {
		if (ws != NULL) {
			sf_arrange_schedule(ws);
		}
		return;
	}
	if (hover != NULL && hover->arrange_managed && hover->workspace == ws) {
		arrange_swap_order(grab, hover);
	}
	sf_arrange_schedule(ws);
}

static float clamp_factor_f(float v) {
	if (v < 0.05f) {
		return 0.05f;
	}
	if (v > 0.95f) {
		return 0.95f;
	}
	return v;
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
	enum sf_arrange kind = sf_workspace_arrange(ws);
	if (kind == SF_ARRANGE_MONOCLE || !sf_arrange_is_tiling(kind)) {
		return;
	}

	struct wlr_box usable;
	if (!sf_output_usable_box(ws->output, &usable) || usable.width <= 0 || usable.height <= 0) {
		return;
	}
	const struct sf_config *cfg = sf_cfg(server);
	sf_box_inset_gap(&usable, cfg->gap_size);
	if (usable.width <= 0 || usable.height <= 0) {
		return;
	}

	bool vertical = false;
	float start;
	int span;
	switch (kind) {
	case SF_ARRANGE_COLUMNS:
		start = cfg->column_ratio;
		span = usable.width;
		vertical = false;
		break;
	case SF_ARRANGE_MASTER:
		start = cfg->master_factor;
		if (cfg->master_pos == SF_MASTER_TOP || cfg->master_pos == SF_MASTER_BOTTOM) {
			vertical = true;
			span = usable.height;
		} else {
			vertical = false;
			span = usable.width;
		}
		break;
	case SF_ARRANGE_DWINDLE:
		start = cfg->dwindle_factor;
		span = usable.width;
		vertical = false;
		break;
	case SF_ARRANGE_MONOCLE:
	case SF_ARRANGE_STACKING:
	case SF_ARRANGE_UNSET:
	case SF_ARRANGE_TILING:
		return;
	}
	if (span < 1) {
		span = 1;
	}

	sf_toplevel_clear_auto_place(toplevel);
	server->arrange_resize.active = true;
	server->arrange_resize.vertical = vertical;
	server->arrange_resize.start_cx = server->cursor->x;
	server->arrange_resize.start_cy = server->cursor->y;
	server->arrange_resize.start_factor = start;
	server->arrange_resize.span = span;
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
	enum sf_arrange kind = sf_workspace_arrange(ws);
	struct sf_config *cfg = &server->config;
	int span = server->arrange_resize.span;
	if (span < 1) {
		span = 1;
	}
	double delta = server->arrange_resize.vertical
			       ? (server->cursor->y - server->arrange_resize.start_cy)
			       : (server->cursor->x - server->arrange_resize.start_cx);
	float next = server->arrange_resize.start_factor + (float)(delta / (double)span);

	switch (kind) {
	case SF_ARRANGE_COLUMNS:
		cfg->column_ratio = clamp_col_ratio_f(next);
		break;
	case SF_ARRANGE_MASTER:
		/* Right/bottom master: drag toward stack (positive axis) shrinks master. */
		if (cfg->master_pos == SF_MASTER_RIGHT || cfg->master_pos == SF_MASTER_BOTTOM) {
			next = server->arrange_resize.start_factor - (float)(delta / (double)span);
		}
		cfg->master_factor = clamp_factor_f(next);
		break;
	case SF_ARRANGE_DWINDLE:
		cfg->dwindle_factor = clamp_factor_f(next);
		break;
	case SF_ARRANGE_MONOCLE:
	case SF_ARRANGE_STACKING:
	case SF_ARRANGE_UNSET:
	case SF_ARRANGE_TILING:
		return;
	}
	/* Live reflow while still tiled. */
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
