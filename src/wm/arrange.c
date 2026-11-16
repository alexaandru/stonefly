#include "wm/arrange.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>
#include <wlr/util/edges.h>
#include <scenefx/types/wlr_scene.h>

#include "anim/anim.h"
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
#include "wm/place_sm.h"
#include "wm/rules.h"
#include "wm/scratchpad.h"
#include "wm/snap_preview.h"
#include "wm/wm.h"
#include "wm/workspace.h"

#define SF_ARRANGE_MAX 64

static const enum sf_arrange arrange_cycle_order[] = {
	SF_ARRANGE_STACKING,
	SF_ARRANGE_COLUMNS,
};

static void columns_drop_clear(struct sf_server *server);

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

void sf_arrange_remember_float_box(struct sf_toplevel *toplevel) {
	if (toplevel == NULL) {
		return;
	}
	sf_toplevel_layout_geom(toplevel, &toplevel->arrange_float_box);
	toplevel->arrange_float_saved = true;
}

void sf_arrange_slot_remember(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->column == NULL) {
		return;
	}
	int col = sf_column_index(toplevel->column);
	int row = sf_column_row_of(toplevel);
	if (col < 0 || row < 0) {
		return;
	}
	toplevel->arrange_slot_col = col;
	toplevel->arrange_slot_row = row;
	toplevel->arrange_slot_frac = toplevel->column->width_frac;
	toplevel->arrange_slot_full = toplevel->column->full_width;
	toplevel->arrange_slot_saved = true;
}

void sf_arrange_slot_clear(struct sf_toplevel *toplevel) {
	if (toplevel == NULL) {
		return;
	}
	toplevel->arrange_slot_saved = false;
	toplevel->arrange_slot_col = 0;
	toplevel->arrange_slot_row = 0;
	toplevel->arrange_slot_frac = 0.0f;
	toplevel->arrange_slot_full = false;
}

/* Rebuild strip cell from arrange_slot_*. Creates missing columns by index. */
static bool join_restore_slot(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || !toplevel->arrange_slot_saved || toplevel->workspace == NULL) {
		return false;
	}
	struct sf_workspace *ws = toplevel->workspace;
	int col_i = toplevel->arrange_slot_col;
	int row_i = toplevel->arrange_slot_row;
	if (col_i < 0) {
		col_i = 0;
	}
	if (row_i < 0) {
		row_i = 0;
	}
	struct sf_column *col = sf_columns_at(ws, col_i);
	if (col == NULL) {
		col = sf_columns_insert_new(ws, toplevel, col_i);
		if (col == NULL) {
			return false;
		}
		if (toplevel->arrange_slot_frac > 0.0f) {
			col->width_frac = toplevel->arrange_slot_frac;
		}
		if (toplevel->arrange_slot_full) {
			sf_column_set_full_width(col, true);
		}
		return true;
	}
	if (!sf_column_insert_row(col, toplevel, row_i)) {
		return false;
	}
	return toplevel->column != NULL;
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
	if (toplevel == NULL) {
		return;
	}
	/* Do not clear arrange_slot_* — workspace/monitor moves must round-trip
	 * the home cell. Destroy / close / scratch detach call slot_clear. */
	struct sf_server *server = toplevel->server;
	struct sf_column *col = toplevel->column;
	bool last_in_col = col != NULL && sf_column_row_count(col) <= 1;
	if (server != NULL && server->arrange_swap.active) {
		if (server->arrange_swap.drop_peer == toplevel ||
		    server->arrange_swap.hover == toplevel) {
			columns_drop_clear(server);
		}
		if (last_in_col && server->arrange_swap.drop_column == col) {
			columns_drop_clear(server);
		}
		if (last_in_col && server->arrange_swap.home_column == col) {
			server->arrange_swap.home_column = NULL;
			server->arrange_swap.home_was_sole = true;
		}
	}
	sf_column_remove_row(toplevel);
	toplevel->arrange_managed = false;
	toplevel->arrange_deferred = false;
	toplevel->arrange_appear_pending = false;
	if (toplevel->xdg_toplevel != NULL && toplevel->xdg_toplevel->base->initialized) {
		wlr_xdg_toplevel_set_tiled(toplevel->xdg_toplevel, 0);
	}
}

void sf_arrange_break_out(struct sf_toplevel *toplevel, bool keep_current_size) {
	if (toplevel == NULL) {
		return;
	}
	/* Mid cell-drag: abort (restore home) before floating so finish cannot
	 * re-insert a window that is already arrange_floating. */
	if (toplevel->server != NULL && toplevel->server->arrange_swap.active &&
	    toplevel->server->grabbed_toplevel == toplevel) {
		sf_reset_cursor_mode(toplevel->server);
	}
	if (!toplevel->arrange_managed && toplevel->column == NULL) {
		toplevel->arrange_floating = true;
		toplevel->arrange_max_edges_rejoin = false;
		return;
	}
	/* Keep strip address so unfloat / unmax land back in the same cell. */
	sf_arrange_slot_remember(toplevel);
	struct wlr_box cur;
	sf_toplevel_layout_geom(toplevel, &cur);
	toplevel->geom_regain_box = cur;
	toplevel->geom_regain_hold = true;
	toplevel->arrange_managed = false;
	toplevel->arrange_floating = true;
	toplevel->arrange_max_edges_rejoin = false;
	struct sf_workspace *ws = toplevel->workspace;
	sf_column_remove_row(toplevel);
	if (toplevel->xdg_toplevel != NULL && toplevel->xdg_toplevel->base->initialized) {
		wlr_xdg_toplevel_set_tiled(toplevel->xdg_toplevel, 0);
	}
	/* Rule float after lone-column pack: restore pre-tile size. User float
	 * keeps the slot geom so Super+v does not shrink to cascade size. */
	if (!keep_current_size && toplevel->arrange_float_saved &&
	    toplevel->arrange_float_box.width > 0 && toplevel->arrange_float_box.height > 0) {
		sf_toplevel_move_resize(toplevel, &toplevel->arrange_float_box, false);
	} else {
		toplevel->arrange_float_box = cur;
		toplevel->arrange_float_saved = true;
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
	/* Transient max-edges overlay stays out until release/unmax rejoins. */
	if (sf_layout_sm_is_max_edges(&toplevel->layout) || toplevel->arrange_max_edges_rejoin) {
		return;
	}
	if (toplevel->xdg_toplevel != NULL && toplevel->xdg_toplevel->parent != NULL) {
		return;
	}
	/* Do not pack into a full-width lone column before SoftPlace decides float. */
	if (sf_rules_wants_float(toplevel) || sf_rules_float_still_possible(toplevel)) {
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
	} else if (join_restore_slot(toplevel)) {
		/* Restored prior column+row from float / max-edges / leave_tiling. */
	} else {
		int after = sf_columns_focus_index(ws);
		sf_columns_insert_new(ws, toplevel, after + 1);
	}
	/* Do not enable the scene here. Map parks disabled until appear parks
	 * off-screen; enabling at the packed cell flashes chrome at rest and
	 * makes awaiting_appear false so neighbors morph before the slide. */
	/* Mark before the first pack so neighbors defer even if place has already
	 * settled (LAYOUT_OWNED) or arm_appear has not run yet. */
	if (toplevel->scene_tree != NULL && !toplevel->scene_tree->node.enabled) {
		toplevel->arrange_appear_pending = true;
	}
}

void sf_arrange_join_workspace(struct sf_workspace *ws) {
	if (ws == NULL || ws->output == NULL || ws->output->server == NULL) {
		return;
	}
	struct sf_server *server = ws->output->server;
	struct sf_toplevel *pending[SF_ARRANGE_MAX];
	size_t n = 0;
	struct sf_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link) {
		if (t->workspace != ws || t->arrange_managed || t->arrange_floating ||
		    t->minimized || t->column != NULL) {
			continue;
		}
		if (n < SF_ARRANGE_MAX) {
			pending[n++] = t;
		}
	}
	/* Slot-saved first, in strip order, so multi-row columns rebuild correctly. */
	for (size_t i = 1; i < n; i++) {
		struct sf_toplevel *key = pending[i];
		size_t j = i;
		while (j > 0) {
			struct sf_toplevel *prev = pending[j - 1];
			bool key_slot = key->arrange_slot_saved;
			bool prev_slot = prev->arrange_slot_saved;
			bool before = false;
			if (key_slot && !prev_slot) {
				before = true;
			} else if (key_slot && prev_slot) {
				before = key->arrange_slot_col < prev->arrange_slot_col ||
					 (key->arrange_slot_col == prev->arrange_slot_col &&
					  key->arrange_slot_row < prev->arrange_slot_row);
			}
			if (!before) {
				break;
			}
			pending[j] = prev;
			j--;
		}
		pending[j] = key;
	}
	for (size_t i = 0; i < n; i++) {
		sf_arrange_join_toplevel(pending[i]);
	}
}

void sf_arrange_float(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->minimized || sf_toplevel_is_scratchpad(toplevel)) {
		return;
	}
	if (toplevel->arrange_managed || toplevel->column != NULL) {
		sf_arrange_break_out(toplevel, true);
	} else {
		toplevel->arrange_floating = true;
		toplevel->arrange_max_edges_rejoin = false;
	}
	sf_decor_refresh_border(toplevel);
}

void sf_arrange_float_from_rule(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->minimized || sf_toplevel_is_scratchpad(toplevel)) {
		return;
	}
	if (toplevel->arrange_managed || toplevel->column != NULL) {
		sf_arrange_break_out(toplevel, false);
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
	if (server->arrange_swap.active) {
		sf_reset_cursor_mode(server);
	}
	struct sf_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link) {
		if (t->workspace != ws) {
			continue;
		}
		/* Remember strip cells before tear-down so columns↔stacking
		 * round-trips restore rows (not one-client-per-column). */
		if (t->column != NULL) {
			sf_arrange_slot_remember(t);
		}
		t->arrange_managed = false;
		/* Stacking has no transient tile overlay; keep layout mode
		 * (snap / gap-max / edge-max / FS) so arrange switch does not
		 * silently unmax. */
		t->arrange_max_edges_rejoin = false;
		sf_column_remove_row(t);
		if (t->xdg_toplevel != NULL && t->xdg_toplevel->base->initialized) {
			wlr_xdg_toplevel_set_tiled(t->xdg_toplevel, 0);
		}
		if (t->scene_tree != NULL && !t->scene_tree->node.enabled && !t->minimized) {
			wlr_scene_node_set_enabled(&t->scene_tree->node, true);
		}
		/* Restore pre-tile float box only for ordinary tiles — not for
		 * max/snap/fs overlays (that was a silent demotion). */
		if (!sf_layout_sm_is_fixed(&t->layout) && t->arrange_float_saved) {
			sf_toplevel_move_resize(t, &t->arrange_float_box, true);
			t->arrange_float_saved = false;
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

static void apply_box_preappear(struct sf_toplevel *t, const struct wlr_box *box) {
	/* Size + resting only: keep scene disabled so appear can slide at the
	 * final cell once the client commits this configure. */
	t->arrange_appear_pending = true;
	t->resting_x = box->x;
	t->resting_y = box->y;
	/* Only reset the give-up counter when the proposed cell size changes.
	 * Re-pack while waiting (second map, idle arrange) used to zero waits
	 * every time — foot rarely hits exact cell height, so appear never
	 * started and the scene stayed disabled (taskbar yes, screen no). */
	if (t->arrange_prop_w != box->width || t->arrange_prop_h != box->height) {
		t->arrange_prop_waits = 0;
	}
	t->arrange_prop_w = box->width;
	t->arrange_prop_h = box->height;
	if (t->scene_tree != NULL) {
		wlr_scene_node_set_position(&t->scene_tree->node, box->x, box->y);
		wlr_scene_node_set_enabled(&t->scene_tree->node, false);
	}
	if (t->xdg_toplevel != NULL && t->xdg_toplevel->base->initialized) {
		wlr_xdg_toplevel_set_size(t->xdg_toplevel, box->width, box->height);
		uint32_t edges = (uint32_t)WLR_EDGE_TOP | (uint32_t)WLR_EDGE_BOTTOM |
				 (uint32_t)WLR_EDGE_LEFT | (uint32_t)WLR_EDGE_RIGHT;
		wlr_xdg_toplevel_set_tiled(t->xdg_toplevel, edges);
	}
	sf_decor_update_geom(t);
}

static bool awaiting_appear(const struct sf_toplevel *t) {
	if (t == NULL) {
		return false;
	}
	if (t->arrange_appear_pending) {
		return true;
	}
	if (sf_place_sm_is_holding(&t->place)) {
		return true;
	}
	/* Map parks disabled until show start. Ignore place phase: LAYOUT_OWNED
	 * settle must not let neighbors morph before the opener slides. */
	if (t->arrange_managed && t->scene_tree != NULL && !t->scene_tree->node.enabled &&
	    !t->anim.active) {
		return true;
	}
	return false;
}

static void apply_box_now(struct sf_toplevel *t, const struct wlr_box *box, bool morph) {
	t->arrange_deferred = false;
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

static void apply_boxes(struct sf_toplevel **wins, const struct wlr_box *boxes, size_t n,
			bool morph) {
	bool has_opener = false;
	for (size_t i = 0; i < n; i++) {
		if (awaiting_appear(wins[i])) {
			has_opener = true;
			break;
		}
	}
	for (size_t i = 0; i < n; i++) {
		struct sf_toplevel *t = wins[i];
		const struct wlr_box *box = &boxes[i];
		if (box->width <= 0 || box->height <= 0) {
			continue;
		}
		t->arrange_prop_w = box->width;
		t->arrange_prop_h = box->height;
		if (awaiting_appear(t)) {
			if (t->anim.active && !t->anim_hide && !t->anim.morph) {
				/* Appear slide already running: retarget resting only. */
				t->resting_x = box->x;
				t->resting_y = box->y;
				t->anim.to_x = box->x;
				t->anim.to_y = box->y;
				continue;
			}
			/* Do not clear_auto_place / enable — that would START_SHOW
			 * at the natural buffer size before the cell configure. */
			apply_box_preappear(t, box);
			continue;
		}
		if (has_opener) {
			/* Hold neighbor geom until the opener's show slide starts
			 * so push + slide share one clock (umbriel windows_move). */
			t->arrange_deferred = true;
			t->arrange_deferred_box = *box;
			continue;
		}
		apply_box_now(t, box, morph);
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

void sf_arrange_flush(struct sf_workspace *ws) {
	if (ws == NULL || ws->output == NULL || ws->output->server == NULL) {
		return;
	}
	if (!sf_arrange_is_tiling(sf_workspace_arrange(ws))) {
		return;
	}
	if (ws->arrange_idle != NULL) {
		wl_event_source_remove(ws->arrange_idle);
		ws->arrange_idle = NULL;
	}
	sf_workspace_apply_arrange(ws);
}

void sf_arrange_release_deferred(struct sf_workspace *ws) {
	if (ws == NULL || ws->output == NULL || ws->output->server == NULL) {
		return;
	}
	struct sf_server *server = ws->output->server;
	bool morph = server->cursor_mode != SF_CURSOR_ARRANGE_RESIZE;
	/* Opener whose appear slide just started — share one clock with push. */
	struct sf_toplevel *leader = NULL;
	struct sf_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link) {
		if (t->workspace != ws || !t->arrange_appear_pending) {
			continue;
		}
		if (t->anim.active && !t->anim_hide && !t->anim.morph) {
			leader = t;
			break;
		}
	}
	wl_list_for_each(t, &server->toplevels, link) {
		if (t->workspace != ws || !t->arrange_deferred) {
			continue;
		}
		if (t->arrange_deferred_box.width <= 0 || t->arrange_deferred_box.height <= 0) {
			t->arrange_deferred = false;
			continue;
		}
		apply_box_now(t, &t->arrange_deferred_box, morph);
		if (leader != NULL && t->anim.active) {
			sf_anim_sync_clock(t, leader);
		}
	}
}

/* Majority of width still in the viewport — keep keyboard focus on that cell. */
static bool pan_box_mostly_visible(const struct wlr_box *box, const struct wlr_box *usable) {
	if (box == NULL || usable == NULL || box->width <= 0 || box->height <= 0) {
		return false;
	}
	struct wlr_box inter;
	if (!wlr_box_intersection(&inter, box, usable) || inter.width <= 0 || inter.height <= 0) {
		return false;
	}
	return inter.width * 2 >= box->width;
}

/*
 * Pan is view-first: keep focus while its cell is still mostly on-screen; once
 * it scrolls off, focus the centre-most visible tiled cell. No pointer warp —
 * continuous swipe must not yank the cursor (cursor_follows_focus stays for
 * keybinds).
 */
static void columns_pan_sync_focus(struct sf_workspace *ws, struct sf_toplevel **wins,
				   const struct wlr_box *boxes, size_t n,
				   const struct wlr_box *usable) {
	if (ws == NULL || ws->output == NULL || wins == NULL || boxes == NULL || usable == NULL ||
	    n == 0) {
		return;
	}
	struct sf_server *server = ws->output->server;
	struct sf_toplevel *focus = sf_focused_toplevel(server);
	for (size_t i = 0; i < n; i++) {
		if (wins[i] == focus && pan_box_mostly_visible(&boxes[i], usable)) {
			return;
		}
	}
	double cx = (double)usable->x + (double)usable->width / 2.0;
	double cy = (double)usable->y + (double)usable->height / 2.0;
	struct sf_toplevel *best = NULL;
	double best_d = 0.0;
	for (size_t i = 0; i < n; i++) {
		struct wlr_box inter;
		if (!wlr_box_intersection(&inter, &boxes[i], usable) || inter.width <= 0 ||
		    inter.height <= 0) {
			continue;
		}
		double bx = (double)boxes[i].x + (double)boxes[i].width / 2.0;
		double by = (double)boxes[i].y + (double)boxes[i].height / 2.0;
		double dx = bx - cx;
		double dy = by - cy;
		double d = dx * dx + dy * dy;
		if (best == NULL || d < best_d) {
			best = wins[i];
			best_d = d;
		}
	}
	if (best != NULL && best != focus) {
		sf_focus_toplevel(best);
	}
}

void sf_arrange_columns_pan(struct sf_workspace *ws, double delta_px) {
	if (ws == NULL || ws->output == NULL || ws->output->server == NULL) {
		return;
	}
	if (sf_workspace_arrange(ws) != SF_ARRANGE_COLUMNS) {
		return;
	}
	if (delta_px == 0.0) {
		return;
	}
	struct sf_server *server = ws->output->server;
	if (server->cursor_mode == SF_CURSOR_ARRANGE_SWAP) {
		return;
	}
	struct wlr_box usable;
	if (!sf_output_usable_box(ws->output, &usable) || usable.width <= 0 || usable.height <= 0) {
		return;
	}
	int gap = sf_cfg(server)->gap_size;
	sf_box_inset_gap(&usable, gap);
	if (usable.width <= 0 || usable.height <= 0) {
		return;
	}

	struct sf_column *cols[SF_ARRANGE_MAX];
	float fracs[SF_ARRANGE_MAX];
	bool fulls[SF_ARRANGE_MAX];
	size_t ncol = 0;
	struct sf_column *c;
	wl_list_for_each(c, &ws->columns, link) {
		if (sf_column_visible_count(c) == 0) {
			continue;
		}
		if (ncol >= SF_ARRANGE_MAX) {
			break;
		}
		cols[ncol] = c;
		fracs[ncol] = c->width_frac;
		fulls[ncol] = c->full_width;
		ncol++;
	}
	if (ncol < 2) {
		return;
	}

	/* Content follows the finger: positive delta → windows move right →
	 * view offset decreases (umbriel natural strip pan). */
	int want = ws->columns_view_offset - (int)lround(delta_px);
	int col_w[SF_ARRANGE_MAX];
	int col_left[SF_ARRANGE_MAX];
	int offset = 0;
	if (!sf_arrange_math_column_strip(&usable, ncol, fracs, fulls, gap, 0, want, true, col_left,
					  col_w, &offset)) {
		return;
	}
	if (offset == ws->columns_view_offset) {
		return;
	}
	ws->columns_view_offset = offset;

	struct sf_toplevel *wins[SF_ARRANGE_MAX];
	struct wlr_box boxes[SF_ARRANGE_MAX];
	size_t n = 0;
	for (size_t k = 0; k < ncol; k++) {
		struct sf_toplevel *rows[SF_ARRANGE_MAX];
		float weights[SF_ARRANGE_MAX];
		size_t nrows = 0;
		struct sf_toplevel *t;
		wl_list_for_each(t, &cols[k]->rows, column_link) {
			if (t->minimized || sf_layout_sm_is_fullscreen(&t->layout)) {
				continue;
			}
			if (nrows >= SF_ARRANGE_MAX) {
				break;
			}
			rows[nrows] = t;
			weights[nrows] = t->column_row_weight;
			nrows++;
		}
		if (nrows == 0) {
			continue;
		}
		struct wlr_box col_box = {
			.x = usable.x + col_left[k] - offset,
			.y = usable.y,
			.width = col_w[k],
			.height = usable.height,
		};
		struct wlr_box row_boxes[SF_ARRANGE_MAX];
		if (!sf_arrange_math_column_rows(&col_box, nrows, weights, gap, row_boxes)) {
			continue;
		}
		for (size_t r = 0; r < nrows && n < SF_ARRANGE_MAX; r++) {
			int inset = sf_ssd_top_inset(rows[r]);
			if (inset > 0 && row_boxes[r].height > inset) {
				row_boxes[r].y += inset;
				row_boxes[r].height -= inset;
			}
			wins[n] = rows[r];
			boxes[n] = row_boxes[r];
			n++;
		}
	}
	/* Instant track — no morph while the finger is moving. */
	apply_boxes(wins, boxes, n, false);
	columns_pan_sync_focus(ws, wins, boxes, n, &usable);
}

void sf_arrange_set(struct sf_server *server, const char *name) {
	if (server == NULL) {
		return;
	}
	if (server->arrange_swap.active) {
		sf_reset_cursor_mode(server);
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
	if (sf_columns_visible_count(ws) < 2) {
		return;
	}
	/* Rotate among columns that still paint; skip minimized-only shells. */
	if (delta > 0) {
		struct sf_column *c, *pick = NULL;
		wl_list_for_each_reverse(c, &ws->columns, link) {
			if (sf_column_visible_count(c) > 0) {
				pick = c;
				break;
			}
		}
		if (pick == NULL) {
			return;
		}
		wl_list_remove(&pick->link);
		wl_list_insert(&ws->columns, &pick->link);
	} else {
		struct sf_column *c, *pick = NULL;
		wl_list_for_each(c, &ws->columns, link) {
			if (sf_column_visible_count(c) > 0) {
				pick = c;
				break;
			}
		}
		if (pick == NULL) {
			return;
		}
		wl_list_remove(&pick->link);
		wl_list_insert(ws->columns.prev, &pick->link);
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
	if (focus->arrange_max_edges_rejoin) {
		sf_toplevel_release_transient_max_edges(focus);
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
		if (wins[i] == focus || sf_toplevel_focus_obscured(wins[i])) {
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

	/* Max-edges overlay is out of the strip; drop it home so Tab walks cells. */
	if (focus != NULL && focus->arrange_max_edges_rejoin) {
		sf_toplevel_release_transient_max_edges(focus);
	}

	struct sf_toplevel *wins[SF_ARRANGE_MAX];
	size_t n_all = sf_columns_collect(ws, wins, SF_ARRANGE_MAX);
	if (n_all > SF_ARRANGE_MAX) {
		n_all = SF_ARRANGE_MAX;
	}
	/* Drop windows buried under exclusive fullscreen (Tab must not land
	 * on something the user cannot see). */
	size_t n = 0;
	for (size_t i = 0; i < n_all; i++) {
		if (sf_toplevel_focus_obscured(wins[i])) {
			continue;
		}
		wins[n++] = wins[i];
	}
	if (n < 2) {
		return false;
	}

	/*
	 * Stable clockwise ring from the usable centre (atan2, y-down → clockwise
	 * is increasing angle). Independent of column/row membership order so
	 * drag-reorder does not reverse Tab. Ties: distance, then id.
	 */
	struct wlr_box usable;
	double ox = 0.0, oy = 0.0;
	if (ws->output != NULL && sf_output_usable_box(ws->output, &usable) && usable.width > 0 &&
	    usable.height > 0) {
		ox = (double)usable.x + (double)usable.width / 2.0;
		oy = (double)usable.y + (double)usable.height / 2.0;
	} else {
		struct wlr_box fb;
		sf_toplevel_layout_geom(focus != NULL ? focus : wins[0], &fb);
		ox = (double)fb.x + (double)fb.width / 2.0;
		oy = (double)fb.y + (double)fb.height / 2.0;
	}

	struct sf_focus_ring {
		struct sf_toplevel *t;
		double ang;
		double dist;
		uint64_t id;
	} ring[SF_ARRANGE_MAX];
	for (size_t i = 0; i < n; i++) {
		struct wlr_box b;
		sf_toplevel_layout_geom(wins[i], &b);
		double cx = (double)b.x + (double)b.width / 2.0;
		double cy = (double)b.y + (double)b.height / 2.0;
		double dx = cx - ox;
		double dy = cy - oy;
		ring[i].t = wins[i];
		ring[i].ang = atan2(dy, dx);
		ring[i].dist = dx * dx + dy * dy;
		ring[i].id = wins[i]->id;
	}
	for (size_t i = 1; i < n; i++) {
		struct sf_focus_ring key = ring[i];
		size_t j = i;
		while (j > 0) {
			struct sf_focus_ring *p = &ring[j - 1];
			bool before =
				key.ang < p->ang ||
				(key.ang == p->ang &&
				 (key.dist < p->dist || (key.dist == p->dist && key.id < p->id)));
			if (!before) {
				break;
			}
			ring[j] = *p;
			j--;
		}
		ring[j] = key;
	}

	size_t fi = 0;
	bool found = false;
	for (size_t i = 0; i < n; i++) {
		if (ring[i].t == focus) {
			fi = i;
			found = true;
			break;
		}
	}
	if (!found) {
		fi = 0;
	}
	size_t next = backward ? (fi + n - 1) % n : (fi + 1) % n;
	sf_focus_toplevel(ring[next].t);
	sf_pointer_follow_focus(server);
	return true;
}

/* Cell-drag drop ops; stored on server->arrange_swap so finish matches reflow. */
enum sf_column_drop {
	SF_COLUMN_DROP_NONE = 0,
	SF_COLUMN_DROP_SWAP = 1,
	SF_COLUMN_DROP_INSERT = 2,
	SF_COLUMN_DROP_NEW_COL = 3,
};

#define SF_COLUMN_DROP_SEAM 0.15

static void columns_drop_clear(struct sf_server *server) {
	server->arrange_swap.hover = NULL;
	server->arrange_swap.drop_kind = SF_COLUMN_DROP_NONE;
	server->arrange_swap.drop_peer = NULL;
	server->arrange_swap.drop_column = NULL;
	server->arrange_swap.drop_index = 0;
	server->arrange_swap.drop_hint = (struct wlr_box){0};
	server->arrange_swap.drop_valid = false;
}

static void columns_home_clear(struct sf_server *server) {
	server->arrange_swap.home_valid = false;
	server->arrange_swap.home_was_sole = false;
	server->arrange_swap.home_column = NULL;
	server->arrange_swap.home_col = 0;
	server->arrange_swap.home_row = 0;
	server->arrange_swap.home_weight = 1.0f;
	server->arrange_swap.home_extent = 0.0f;
	server->arrange_swap.home_width_frac = 0.5f;
	server->arrange_swap.home_full_width = false;
}

static bool column_on_workspace(struct sf_workspace *ws, struct sf_column *col) {
	if (ws == NULL || col == NULL || col->ws != ws) {
		return false;
	}
	return sf_column_index(col) >= 0;
}

static bool toplevel_on_server(struct sf_server *server, struct sf_toplevel *t) {
	if (server == NULL || t == NULL) {
		return false;
	}
	struct sf_toplevel *x;
	wl_list_for_each(x, &server->toplevels, link) {
		if (x == t) {
			return true;
		}
	}
	return false;
}

static bool columns_restore_home(struct sf_server *server, struct sf_toplevel *toplevel) {
	if (server == NULL || toplevel == NULL || !server->arrange_swap.home_valid) {
		return false;
	}
	struct sf_workspace *ws = toplevel->workspace;
	if (ws == NULL) {
		return false;
	}
	float weight = server->arrange_swap.home_weight;
	float extent = server->arrange_swap.home_extent;
	int row = server->arrange_swap.home_row;
	int col_i = server->arrange_swap.home_col;
	struct sf_column *home = server->arrange_swap.home_column;
	bool home_alive = column_on_workspace(ws, home);
	if (server->arrange_swap.home_was_sole || !home_alive) {
		struct sf_column *col = sf_columns_insert_new(ws, toplevel, col_i);
		if (col == NULL) {
			return false;
		}
		col->width_frac = server->arrange_swap.home_width_frac;
		col->full_width = server->arrange_swap.home_full_width;
		if (col->full_width) {
			col->width_frac_saved = server->arrange_swap.home_width_frac;
		}
	} else {
		if (!sf_column_insert_row(home, toplevel, row)) {
			return false;
		}
	}
	toplevel->column_row_weight = weight > 0.0f ? weight : 1.0f;
	toplevel->column_extent_saved = extent;
	return true;
}

static int column_paint_width(struct sf_column *col, size_t ncol, const struct wlr_box *usable,
			      int gap) {
	int cw;
	if (ncol == 1 || col->full_width) {
		cw = usable->width;
	} else {
		cw = sf_arrange_math_column_width_px(usable->width, col->width_frac, gap);
	}
	return cw < 1 ? 1 : cw;
}

/* Live reflow with optional insert/new-col spacer. Grab is already unlinked. */
static void columns_drag_reflow(struct sf_server *server, struct sf_toplevel *grab,
				enum sf_column_drop kind, struct sf_column *ph_col, int ph_index,
				struct wlr_box *out_spacer) {
	if (out_spacer != NULL) {
		*out_spacer = (struct wlr_box){0};
	}
	struct sf_workspace *ws = grab->workspace;
	if (ws == NULL || ws->output == NULL) {
		return;
	}
	struct wlr_box usable;
	if (!sf_output_usable_box(ws->output, &usable) || usable.width <= 0 || usable.height <= 0) {
		return;
	}
	int gap = sf_cfg(server)->gap_size;
	sf_box_inset_gap(&usable, gap);
	if (usable.width <= 0 || usable.height <= 0) {
		return;
	}

	struct sf_column *cols[SF_ARRANGE_MAX];
	float fracs[SF_ARRANGE_MAX];
	bool fulls[SF_ARRANGE_MAX];
	size_t ncol = 0;
	struct sf_column *c;
	wl_list_for_each(c, &ws->columns, link) {
		size_t visible = 0;
		struct sf_toplevel *t;
		wl_list_for_each(t, &c->rows, column_link) {
			if (!t->minimized && t != grab) {
				visible++;
			}
		}
		if (visible == 0) {
			continue;
		}
		if (ncol >= SF_ARRANGE_MAX) {
			break;
		}
		cols[ncol] = c;
		fracs[ncol] = c->width_frac;
		fulls[ncol] = c->full_width;
		ncol++;
	}

	float spacer_frac = server->arrange_swap.home_width_frac;
	if (spacer_frac < 0.05f) {
		spacer_frac = 0.5f;
	}
	size_t focus_col = 0;
	size_t strip_n = ncol;
	float strip_fracs[SF_ARRANGE_MAX];
	bool strip_fulls[SF_ARRANGE_MAX];
	int map_real[SF_ARRANGE_MAX];
	int spacer_strip = -1;
	if (kind == SF_COLUMN_DROP_NEW_COL && strip_n + 1 <= SF_ARRANGE_MAX) {
		int at = ph_index;
		if (at < 0) {
			at = 0;
		}
		if ((size_t)at > strip_n) {
			at = (int)strip_n;
		}
		size_t o = 0;
		for (size_t i = 0; i <= strip_n; i++) {
			if ((int)i == at) {
				spacer_strip = (int)o;
				strip_fracs[o] = spacer_frac;
				strip_fulls[o] = false;
				map_real[o] = -1;
				o++;
			}
			if (i < strip_n) {
				strip_fracs[o] = fracs[i];
				strip_fulls[o] = fulls[i];
				map_real[o] = (int)i;
				o++;
			}
		}
		strip_n = o;
		if (spacer_strip >= 0) {
			focus_col = (size_t)spacer_strip;
		}
	} else {
		for (size_t i = 0; i < strip_n; i++) {
			strip_fracs[i] = fracs[i];
			strip_fulls[i] = fulls[i];
			map_real[i] = (int)i;
		}
		if (kind == SF_COLUMN_DROP_INSERT && ph_col != NULL) {
			for (size_t i = 0; i < ncol; i++) {
				if (cols[i] == ph_col) {
					focus_col = i;
					break;
				}
			}
		}
	}

	int col_w[SF_ARRANGE_MAX];
	int col_left[SF_ARRANGE_MAX];
	int offset = 0;
	if (strip_n == 0) {
		return;
	}
	if (!sf_arrange_math_column_strip(&usable, strip_n, strip_fracs, strip_fulls, gap,
					  focus_col, ws->columns_view_offset, false, col_left,
					  col_w, &offset)) {
		return;
	}
	ws->columns_view_offset = offset;

	float ph_weight = server->arrange_swap.home_weight;
	if (ph_weight < 0.05f) {
		ph_weight = 1.0f;
	}

	struct sf_toplevel *out_wins[SF_ARRANGE_MAX];
	struct wlr_box out_boxes[SF_ARRANGE_MAX];
	size_t n = 0;
	for (size_t s = 0; s < strip_n; s++) {
		struct wlr_box col_box = {
			.x = usable.x + col_left[s] - offset,
			.y = usable.y,
			.width = col_w[s],
			.height = usable.height,
		};
		if (map_real[s] < 0) {
			if (out_spacer != NULL) {
				*out_spacer = col_box;
			}
			continue;
		}
		struct sf_column *col = cols[map_real[s]];
		struct sf_toplevel *rows[SF_ARRANGE_MAX];
		float weights[SF_ARRANGE_MAX];
		size_t nrows = 0;
		struct sf_toplevel *t;
		wl_list_for_each(t, &col->rows, column_link) {
			if (t->minimized || t == grab) {
				continue;
			}
			if (nrows >= SF_ARRANGE_MAX) {
				break;
			}
			rows[nrows] = t;
			weights[nrows] = t->column_row_weight;
			nrows++;
		}
		bool insert_here = kind == SF_COLUMN_DROP_INSERT && col == ph_col;
		size_t pack_n = nrows;
		float pack_w[SF_ARRANGE_MAX];
		int spacer_slot = -1;
		if (insert_here && pack_n + 1 <= SF_ARRANGE_MAX) {
			int at = ph_index;
			if (at < 0) {
				at = 0;
			}
			if ((size_t)at > nrows) {
				at = (int)nrows;
			}
			size_t o = 0;
			for (size_t r = 0; r <= nrows; r++) {
				if ((int)r == at) {
					spacer_slot = (int)o;
					pack_w[o++] = ph_weight;
				}
				if (r < nrows) {
					pack_w[o++] = weights[r] < 0.05f ? 0.05f : weights[r];
				}
			}
			pack_n = o;
		} else {
			for (size_t r = 0; r < nrows; r++) {
				pack_w[r] = weights[r] < 0.05f ? 0.05f : weights[r];
			}
		}
		if (pack_n == 0) {
			continue;
		}
		struct wlr_box row_boxes[SF_ARRANGE_MAX];
		if (!sf_arrange_math_column_rows(&col_box, pack_n, pack_w, gap, row_boxes)) {
			continue;
		}
		size_t real_i = 0;
		for (size_t r = 0; r < pack_n; r++) {
			if ((int)r == spacer_slot) {
				if (out_spacer != NULL) {
					*out_spacer = row_boxes[r];
				}
				continue;
			}
			if (real_i >= nrows || n >= SF_ARRANGE_MAX) {
				break;
			}
			out_wins[n] = rows[real_i++];
			out_boxes[n] = row_boxes[r];
			n++;
		}
	}
	for (size_t i = 0; i < n; i++) {
		int inset = sf_ssd_top_inset(out_wins[i]);
		if (inset > 0 && out_boxes[i].height > inset) {
			out_boxes[i].y += inset;
			out_boxes[i].height -= inset;
		}
	}
	apply_boxes(out_wins, out_boxes, n, false);
	if (grab->scene_tree != NULL) {
		wlr_scene_node_raise_to_top(&grab->scene_tree->node);
	}
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

	size_t ncol = 0;
	struct sf_column *cols[SF_ARRANGE_MAX];
	struct sf_column *c;
	wl_list_for_each(c, &ws->columns, link) {
		size_t visible = 0;
		struct sf_toplevel *t;
		wl_list_for_each(t, &c->rows, column_link) {
			if (!t->minimized && t != grab) {
				visible++;
			}
		}
		if (visible == 0) {
			continue;
		}
		if (ncol >= SF_ARRANGE_MAX) {
			break;
		}
		cols[ncol++] = c;
	}
	if (ncol == 0) {
		*kind_out = SF_COLUMN_DROP_NEW_COL;
		*col_out = NULL;
		/* Painted strip index for live spacer; apply maps to structural. */
		*index_out = 0;
		return true;
	}

	int view = ws->columns_view_offset;
	int x = 0;
	for (size_t k = 0; k < ncol; k++) {
		int cw = column_paint_width(cols[k], ncol, &usable, gap);
		int left = usable.x + x - view;
		int right = left + cw;
		if (lx < (double)left) {
			*kind_out = SF_COLUMN_DROP_NEW_COL;
			*col_out = cols[k];
			/* Painted index: spacer/reflow; col is insert-before target. */
			*index_out = (int)k;
			return true;
		}
		if (lx < (double)right) {
			struct sf_column *col = cols[k];
			struct sf_toplevel *rows[SF_ARRANGE_MAX];
			struct wlr_box boxes[SF_ARRANGE_MAX];
			size_t nrows = 0;
			struct sf_toplevel *t;
			wl_list_for_each(t, &col->rows, column_link) {
				if (t->minimized || t == grab) {
					continue;
				}
				if (nrows >= SF_ARRANGE_MAX) {
					break;
				}
				sf_toplevel_layout_geom(t, &boxes[nrows]);
				rows[nrows] = t;
				nrows++;
			}
			if (nrows == 0) {
				*kind_out = SF_COLUMN_DROP_INSERT;
				*col_out = col;
				*index_out = 0; /* painted slot; map on apply */
				return true;
			}
			/* Above first / below last / in inter-row gap → insert.
			 * Index is painted (visible-only) so reflow spacer slots match. */
			if (ly < (double)boxes[0].y) {
				*kind_out = SF_COLUMN_DROP_INSERT;
				*col_out = col;
				*peer_out = rows[0];
				*index_out = 0;
				return true;
			}
			for (size_t r = 0; r < nrows; r++) {
				struct wlr_box b = boxes[r];
				int bottom = b.y + b.height;
				int next_top =
					r + 1 < nrows ? boxes[r + 1].y : usable.y + usable.height;
				if (ly >= (double)b.y && ly < (double)bottom) {
					double frac = (ly - (double)b.y) / (double)b.height;
					if (frac < SF_COLUMN_DROP_SEAM) {
						*kind_out = SF_COLUMN_DROP_INSERT;
						*peer_out = rows[r];
						*col_out = col;
						*index_out = (int)r;
						return true;
					}
					if (frac > 1.0 - SF_COLUMN_DROP_SEAM) {
						*kind_out = SF_COLUMN_DROP_INSERT;
						*peer_out = rows[r];
						*col_out = col;
						*index_out = (int)r + 1;
						return true;
					}
					*kind_out = SF_COLUMN_DROP_SWAP;
					*peer_out = rows[r];
					*col_out = col;
					*index_out = (int)r;
					*hint_out = b;
					return true;
				}
				if (ly >= (double)bottom && ly < (double)next_top) {
					*kind_out = SF_COLUMN_DROP_INSERT;
					*peer_out = rows[r];
					*col_out = col;
					*index_out = (int)r + 1;
					return true;
				}
			}
			*kind_out = SF_COLUMN_DROP_INSERT;
			*col_out = col;
			*peer_out = rows[nrows - 1];
			*index_out = (int)nrows;
			return true;
		}
		x += cw + (k + 1 < ncol ? gap : 0);
	}
	*kind_out = SF_COLUMN_DROP_NEW_COL;
	*col_out = NULL;
	*index_out = (int)ncol; /* after last painted column */
	return true;
}

/* Map painted NEW_COL index (+ optional insert-before col) to structural strip index. */
static int new_col_structural_index(struct sf_workspace *ws, struct sf_column *before,
				    int painted) {
	if (ws == NULL) {
		return 0;
	}
	if (before != NULL) {
		int i = sf_column_index(before);
		return i >= 0 ? i : 0;
	}
	if (painted <= 0) {
		return 0;
	}
	/* After last visible: structural index past the last visible column. */
	int last_vis = -1;
	int vis = 0;
	struct sf_column *c;
	wl_list_for_each(c, &ws->columns, link) {
		if (sf_column_visible_count(c) == 0) {
			continue;
		}
		last_vis = sf_column_index(c);
		vis++;
		if (vis >= painted) {
			break;
		}
	}
	if (last_vis < 0) {
		return (int)sf_columns_count(ws);
	}
	return last_vis + 1;
}

/* Map painted INSERT slot (among non-minimized rows) to structural row index. */
static int insert_structural_index(struct sf_column *col, int painted) {
	if (col == NULL) {
		return 0;
	}
	if (painted <= 0) {
		int i = 0;
		struct sf_toplevel *t;
		wl_list_for_each(t, &col->rows, column_link) {
			if (!t->minimized) {
				return i;
			}
			i++;
		}
		return i;
	}
	int vis = 0;
	int i = 0;
	struct sf_toplevel *t;
	wl_list_for_each(t, &col->rows, column_link) {
		if (t->minimized) {
			i++;
			continue;
		}
		vis++;
		i++;
		if (vis == painted) {
			return i;
		}
	}
	return i;
}

static void columns_drop_apply(struct sf_server *server, struct sf_toplevel *grab,
			       enum sf_column_drop kind, struct sf_toplevel *peer,
			       struct sf_column *col, int index) {
	struct sf_workspace *ws = grab->workspace;
	float weight = server->arrange_swap.home_weight;
	float extent = server->arrange_swap.home_extent;
	switch (kind) {
	case SF_COLUMN_DROP_SWAP:
		if (peer == NULL || peer->column == NULL) {
			columns_restore_home(server, grab);
			break;
		}
		{
			struct sf_column *cb = peer->column;
			int rb = sf_column_row_of(peer);
			float pw = peer->column_row_weight;
			float pe = peer->column_extent_saved;
			sf_column_detach_row(peer);
			if (!sf_column_insert_row(cb, grab, rb)) {
				sf_column_insert_row(cb, peer, rb);
				columns_restore_home(server, grab);
				break;
			}
			grab->column_row_weight = weight > 0.0f ? weight : 1.0f;
			grab->column_extent_saved = extent;
			if (!columns_restore_home(server, peer)) {
				sf_columns_insert_new(ws, peer, (int)sf_columns_count(ws));
			}
			peer->column_row_weight = pw > 0.0f ? pw : 1.0f;
			peer->column_extent_saved = pe;
			sf_column_destroy_if_empty(cb);
		}
		break;
	case SF_COLUMN_DROP_INSERT: {
		int structural = insert_structural_index(col, index);
		if (col != NULL && sf_column_insert_row(col, grab, structural)) {
			grab->column_row_weight = weight > 0.0f ? weight : 1.0f;
			grab->column_extent_saved = extent;
		} else {
			columns_restore_home(server, grab);
		}
	} break;
	case SF_COLUMN_DROP_NEW_COL:
		if (ws != NULL) {
			int structural = new_col_structural_index(ws, col, index);
			struct sf_column *nc = sf_columns_insert_new(ws, grab, structural);
			if (nc != NULL) {
				nc->width_frac = server->arrange_swap.home_width_frac > 0.0f
							 ? server->arrange_swap.home_width_frac
							 : nc->width_frac;
				grab->column_row_weight = weight > 0.0f ? weight : 1.0f;
				grab->column_extent_saved = extent;
			} else {
				columns_restore_home(server, grab);
			}
		}
		break;
	case SF_COLUMN_DROP_NONE:
	default:
		columns_restore_home(server, grab);
		break;
	}
}

void sf_arrange_swap_begin(struct sf_server *server, struct sf_toplevel *toplevel) {
	if (server == NULL || toplevel == NULL || !toplevel->arrange_managed ||
	    toplevel->scene_tree == NULL || toplevel->workspace == NULL) {
		return;
	}
	sf_toplevel_clear_auto_place(toplevel);
	columns_drop_clear(server);
	columns_home_clear(server);
	server->arrange_swap.active = true;
	server->grab_x = server->cursor->x - (double)toplevel->scene_tree->node.x;
	server->grab_y = server->cursor->y - (double)toplevel->scene_tree->node.y;
	sf_toplevel_raise(toplevel);

	if (sf_workspace_arrange(toplevel->workspace) == SF_ARRANGE_COLUMNS &&
	    toplevel->column != NULL) {
		struct sf_column *col = toplevel->column;
		server->arrange_swap.home_valid = true;
		server->arrange_swap.home_column = col;
		server->arrange_swap.home_col = sf_column_index(col);
		server->arrange_swap.home_row = sf_column_row_of(toplevel);
		server->arrange_swap.home_weight = toplevel->column_row_weight;
		server->arrange_swap.home_extent = toplevel->column_extent_saved;
		server->arrange_swap.home_width_frac = col->width_frac;
		server->arrange_swap.home_full_width = col->full_width;
		server->arrange_swap.home_was_sole = sf_column_row_count(col) <= 1;
		sf_column_remove_row(toplevel);
		/* Sole row destroyed the home column; restore as insert_new. */
		if (server->arrange_swap.home_was_sole) {
			server->arrange_swap.home_column = NULL;
		}
		columns_drag_reflow(server, toplevel, SF_COLUMN_DROP_NONE, NULL, 0, NULL);
	}
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
	server->arrange_swap.drop_valid = ok;

	struct wlr_box spacer = {0};
	if (sf_workspace_arrange(grab->workspace) == SF_ARRANGE_COLUMNS) {
		columns_drag_reflow(server, grab, ok ? kind : SF_COLUMN_DROP_NONE, col, index,
				    &spacer);
	}
	wlr_scene_node_set_position(&grab->scene_tree->node, x, y);
	grab->resting_x = x;
	grab->resting_y = y;
	sf_toplevel_raise(grab);

	if (ok && kind == SF_COLUMN_DROP_SWAP && hint.width > 0) {
		server->arrange_swap.drop_hint = hint;
		sf_snap_preview_show_box(server, &hint);
	} else if (ok && (kind == SF_COLUMN_DROP_INSERT || kind == SF_COLUMN_DROP_NEW_COL) &&
		   spacer.width > 0 && spacer.height > 0) {
		server->arrange_swap.drop_hint = spacer;
		sf_snap_preview_show_box(server, &spacer);
	} else {
		server->arrange_swap.drop_hint = (struct wlr_box){0};
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
	if (grab == NULL || ws == NULL) {
		columns_drop_clear(server);
		columns_home_clear(server);
		return;
	}
	/* Refuse apply after float/break_out or when drop targets died mid-drag. */
	if (grab->arrange_floating || grab->column != NULL) {
		drop_valid = false;
	} else if (drop_valid) {
		bool stale = false;
		if (kind == SF_COLUMN_DROP_SWAP) {
			stale = !toplevel_on_server(server, peer) ||
				(peer != NULL && peer->column == NULL);
		} else if (kind == SF_COLUMN_DROP_INSERT || kind == SF_COLUMN_DROP_NEW_COL) {
			stale = col != NULL && !column_on_workspace(ws, col);
		}
		if (stale) {
			drop_valid = false;
		}
	}
	if (sf_workspace_arrange(ws) == SF_ARRANGE_COLUMNS) {
		if (drop_valid && kind != SF_COLUMN_DROP_NONE) {
			columns_drop_apply(server, grab, kind, peer, col, index);
		} else if (server->arrange_swap.home_valid && grab->column == NULL &&
			   !grab->arrange_floating) {
			columns_restore_home(server, grab);
		}
	}
	columns_drop_clear(server);
	columns_home_clear(server);
	sf_arrange_schedule(ws);
}

void sf_arrange_swap_abort(struct sf_server *server) {
	if (server == NULL || !server->arrange_swap.active) {
		return;
	}
	struct sf_toplevel *grab = server->grabbed_toplevel;
	struct sf_workspace *ws = grab != NULL ? grab->workspace : NULL;
	sf_snap_preview_hide(server);
	server->arrange_swap.active = false;
	if (grab != NULL && grab->column == NULL && server->arrange_swap.home_valid &&
	    !grab->arrange_floating) {
		columns_restore_home(server, grab);
	}
	columns_drop_clear(server);
	columns_home_clear(server);
	if (ws != NULL) {
		sf_arrange_schedule(ws);
	}
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
	/* Slot unit matches sf_arrange_math_column_width_px (usable + gap). */
	server->arrange_resize.span = usable.width + cfg->gap_size;
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
