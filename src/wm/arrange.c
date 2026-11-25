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
#include "wm/arrange_internal.h"
#include "wm/column.h"
#include "wm/dialog.h"
#include "wm/geom.h"
#include "wm/layout_sm.h"
#include "wm/place_sm.h"
#include "wm/rules.h"
#include "wm/scratchpad.h"
#include "wm/wm.h"
#include "wm/workspace.h"

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
		/* Overview is its own context — tiling-only binds stay dormant. */
		if (current == SF_ARRANGE_OVERVIEW) {
			return false;
		}
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
	/* Do not freeze a packed tile cell as the stacking preferred size. */
	if (t->column != NULL || t->arrange_managed) {
		return;
	}
	struct wlr_box geom;
	sf_toplevel_layout_geom(t, &geom);
	if (geom.width < 50 || geom.height < 50) {
		return;
	}
	/* Refresh every stacking→columns join so a user resize while stacking
	 * is what leave restores later (not a stale cascade seed). */
	t->arrange_float_box = geom;
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
			arrange_columns_drop_clear(server);
		}
		if (last_in_col && server->arrange_swap.drop_column == col) {
			arrange_columns_drop_clear(server);
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
	sf_toplevel_set_tiled_edges(toplevel, 0);
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
	sf_toplevel_set_tiled_edges(toplevel, 0);
	/* Rule float: restore pre-tile size. User float after a prior sized
	 * float session: restore that box. First user float: keep cell geom. */
	bool restore_float = toplevel->arrange_float_saved &&
			     toplevel->arrange_float_box.width > 0 &&
			     toplevel->arrange_float_box.height > 0 &&
			     (!keep_current_size || toplevel->arrange_float_custom);
	if (restore_float) {
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
	if (sf_toplevel_modal_attached(toplevel) || !sf_toplevel_looks_tiled(toplevel)) {
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
		int at = 0;
		enum sf_tile_insert how = sf_cfg(ws->output->server)->tile_insert;
		switch (how) {
		case SF_TILE_INSERT_BEGIN:
			at = 0;
			break;
		case SF_TILE_INSERT_END:
			at = (int)sf_columns_count(ws);
			break;
		case SF_TILE_INSERT_AFTER_FOCUS:
		default:
			at = sf_columns_focus_index(ws) + 1;
			break;
		}
		sf_columns_insert_new(ws, toplevel, at);
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
	/* Remember the float box the user left so the next float restores it. */
	sf_arrange_remember_float_box(toplevel);
	toplevel->arrange_float_custom = true;
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

/* Tile-cell geom is a bad stacking preferred size (tall/narrow / off-strip). */
static bool geom_looks_tiled(const struct sf_toplevel *t, const struct wlr_box *geom,
			     const struct wlr_box *usable) {
	if (geom == NULL || usable == NULL || geom->width <= 0 || geom->height <= 0) {
		return true;
	}
	if (t->arrange_prop_w > 0 && t->arrange_prop_h > 0 && geom->width == t->arrange_prop_w &&
	    geom->height == t->arrange_prop_h) {
		return true;
	}
	/* Full usable height (column row) or off-strip left of usable. */
	if (usable->height > 0 && geom->height >= usable->height - 8) {
		return true;
	}
	if (usable->width > 0 && geom->x + geom->width <= usable->x + 4) {
		return true;
	}
	if (usable->width > 0 && geom->x >= usable->x + usable->width - 4) {
		return true;
	}
	return false;
}

static void leave_stacking_box(struct sf_toplevel *t, size_t cascade_i,
			       const struct wlr_box *usable) {
	struct wlr_box box = {0};
	bool have = false;
	if (t->arrange_float_saved && t->arrange_float_box.width >= 50 &&
	    t->arrange_float_box.height >= 50 &&
	    !geom_looks_tiled(t, &t->arrange_float_box, usable)) {
		box = t->arrange_float_box;
		have = true;
	}
	if (!have) {
		struct wlr_box cur;
		sf_toplevel_layout_geom(t, &cur);
		if (cur.width >= 50 && cur.height >= 50 && !geom_looks_tiled(t, &cur, usable)) {
			box = cur;
			have = true;
		}
	}
	if (!have) {
		int mw = 0;
		int mh = 0;
		sf_toplevel_min_size(t, &mw, &mh);
		int uw = usable->width > 0 ? usable->width : 1280;
		int uh = usable->height > 0 ? usable->height : 720;
		box.width = mw > 50 ? mw : (uw * 45) / 100;
		box.height = mh > 50 ? mh : (uh * 45) / 100;
		if (box.width < 320) {
			box.width = 320;
		}
		if (box.height < 240) {
			box.height = 240;
		}
		if (box.width > uw) {
			box.width = uw;
		}
		if (box.height > uh) {
			box.height = uh;
		}
	}
	int x = 0;
	int y = 0;
	sf_toplevel_cascade_origin(t, cascade_i, &x, &y);
	box.x = x;
	box.y = y;
	sf_toplevel_move_resize(t, &box, true);
	t->arrange_float_box = box;
	t->arrange_float_saved = true;
	sf_toplevel_ensure_on_screen(t);
	sf_toplevel_layout_geom(t, &t->arrange_float_box);
}

static void leave_tiling(struct sf_workspace *ws) {
	if (ws == NULL || ws->output == NULL || ws->output->server == NULL) {
		return;
	}
	struct sf_server *server = ws->output->server;
	if (server->arrange_swap.active) {
		sf_reset_cursor_mode(server);
	}
	/* Capture every cell before remove_row renumbers neighbors (same as
	 * workspace_park_members). One-pass remember+unlink collapsed slots
	 * and invented multi-row columns on the next join. */
	struct sf_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link) {
		if (t->workspace == ws && t->column != NULL) {
			sf_arrange_slot_remember(t);
		}
	}
	struct wlr_box usable = {0};
	if (ws->output->usable.width > 0) {
		usable = ws->output->usable;
	} else {
		usable = ws->output->full;
	}
	sf_box_inset_gap(&usable, sf_cfg(server)->gap_size);
	size_t cascade_i = 0;
	wl_list_for_each(t, &server->toplevels, link) {
		if (t->workspace != ws) {
			continue;
		}
		t->arrange_managed = false;
		/* Stacking has no transient tile overlay; keep layout mode
		 * (snap / gap-max / edge-max / FS) so arrange switch does not
		 * silently unmax. */
		t->arrange_max_edges_rejoin = false;
		sf_column_remove_row(t);
		sf_toplevel_set_tiled_edges(t, 0);
		if (t->scene_tree != NULL && !t->scene_tree->node.enabled && !t->minimized) {
			wlr_scene_node_set_enabled(&t->scene_tree->node, true);
		}
		if (t->minimized || sf_toplevel_is_scratchpad(t)) {
			sf_decor_update_geom(t);
			continue;
		}
		if (sf_layout_sm_is_fixed(&t->layout)) {
			sf_toplevel_ensure_on_screen(t);
			sf_decor_update_geom(t);
			continue;
		}
		leave_stacking_box(t, cascade_i, &usable);
		cascade_i++;
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
		if (sf_toplevel_modal_attached(t) || !sf_toplevel_looks_tiled(t)) {
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
	if (sf_toplevel_is_initialized(t)) {
		sf_toplevel_set_size(t, box->width, box->height);
		uint32_t edges = (uint32_t)WLR_EDGE_TOP | (uint32_t)WLR_EDGE_BOTTOM |
				 (uint32_t)WLR_EDGE_LEFT | (uint32_t)WLR_EDGE_RIGHT;
		sf_toplevel_set_tiled_edges(t, edges);
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

/* Pop/slide already running — do not treat as a parked opener. */
static bool appear_running(const struct sf_toplevel *t) {
	return t != NULL && t->anim.active && !t->anim_hide && !t->anim.morph;
}

/* Parked for first show: neighbors must wait so push shares the opener clock. */
static bool appear_parked(const struct sf_toplevel *t) {
	return awaiting_appear(t) && !appear_running(t);
}

static void apply_box_now(struct sf_toplevel *t, const struct wlr_box *box, bool morph) {
	t->arrange_deferred = false;
	if (t->scene_tree != NULL && !t->scene_tree->node.enabled) {
		wlr_scene_node_set_enabled(&t->scene_tree->node, true);
	}
	sf_toplevel_clear_auto_place(t);
	sf_toplevel_move_resize(t, box, morph);
	if (sf_toplevel_is_initialized(t)) {
		uint32_t edges = (uint32_t)WLR_EDGE_TOP | (uint32_t)WLR_EDGE_BOTTOM |
				 (uint32_t)WLR_EDGE_LEFT | (uint32_t)WLR_EDGE_RIGHT;
		sf_toplevel_set_tiled_edges(t, edges);
	}
	sf_decor_update_geom(t);
}

void arrange_apply_boxes(struct sf_toplevel **wins, const struct wlr_box *boxes, size_t n,
			 bool morph) {
	bool has_parked = false;
	struct sf_toplevel *leader = NULL;
	for (size_t i = 0; i < n; i++) {
		if (appear_parked(wins[i])) {
			has_parked = true;
		}
		if (leader == NULL && appear_running(wins[i]) && wins[i]->arrange_appear_pending) {
			leader = wins[i];
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
		if (awaiting_appear(t) && appear_running(t)) {
			/* Consume/repack mid-pop: retarget the full cell (xy+wh).
			 * Deferring neighbors here left them at the old column x
			 * with the new row height until the next spawn. */
			sf_toplevel_move_resize(t, box, false);
			continue;
		}
		if (awaiting_appear(t)) {
			/* Do not clear_auto_place / enable — that would START_SHOW
			 * at the natural buffer size before the cell configure. */
			apply_box_preappear(t, box);
			continue;
		}
		if (has_parked) {
			/* Hold neighbor geom until the opener's show slide starts
			 * so push + slide share one clock (umbriel windows_move). */
			t->arrange_deferred = true;
			t->arrange_deferred_box = *box;
			continue;
		}
		apply_box_now(t, box, morph);
		if (leader != NULL && t->anim.active) {
			sf_anim_sync_clock(t, leader);
		}
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
	arrange_apply_boxes(wins, boxes, n, morph);

	/* Do not re-raise floats here. Focus raise owns z-order: selecting a
	 * window must leave it visible, even under a covering sticky float.
	 * Pinned / hide-slide stay on top via sf_pinned_restack. */
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
 * Pan focus retarget (sf.opt.pan_focus). keep = no-op; center = centre-most
 * visible cell; warp = center + pointer warp; under_pointer = prefer the
 * visible strip cell under the cursor, else centre-most. Continuous pan may
 * retarget under_pointer each tick among visible cells.
 */
static void columns_pan_sync_focus(struct sf_workspace *ws, struct sf_toplevel **wins,
				   const struct wlr_box *boxes, size_t n,
				   const struct wlr_box *usable) {
	if (ws == NULL || ws->output == NULL || wins == NULL || boxes == NULL || usable == NULL ||
	    n == 0) {
		return;
	}
	struct sf_server *server = ws->output->server;
	enum sf_pan_focus mode = sf_cfg(server)->pan_focus;
	if (mode == SF_PAN_FOCUS_KEEP) {
		return;
	}
	struct sf_toplevel *focus = sf_focused_toplevel(server);
	for (size_t i = 0; i < n; i++) {
		if (wins[i] == focus && pan_box_mostly_visible(&boxes[i], usable)) {
			return;
		}
	}
	struct sf_toplevel *best = NULL;
	if (mode == SF_PAN_FOCUS_UNDER_POINTER && server->cursor != NULL) {
		struct wlr_surface *surface = NULL;
		double sx = 0.0;
		double sy = 0.0;
		struct sf_toplevel *under = sf_desktop_toplevel_at(
			server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
		for (size_t i = 0; under != NULL && i < n; i++) {
			struct wlr_box inter;
			if (wins[i] != under) {
				continue;
			}
			if (wlr_box_intersection(&inter, &boxes[i], usable) && inter.width > 0 &&
			    inter.height > 0) {
				best = under;
			}
			break;
		}
	}
	if (best == NULL) {
		double cx = (double)usable->x + (double)usable->width / 2.0;
		double cy = (double)usable->y + (double)usable->height / 2.0;
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
	}
	if (best != NULL && best != focus) {
		sf_focus_toplevel(best);
		if (mode == SF_PAN_FOCUS_WARP) {
			sf_pointer_center(server, best);
		}
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
	if (!sf_arrange_math_column_strip(
		    &usable, ncol, fracs, fulls, gap, 0, ws->columns_focus_col, want, true,
		    sf_cfg(server)->center_focused, col_left, col_w, &offset)) {
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
	arrange_apply_boxes(wins, boxes, n, false);
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

void sf_arrange_consume_from(struct sf_server *server, const char *dir) {
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
	if (sf_columns_consume_from(focus, delta)) {
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
	enum sf_focus_cycle cycle = sf_cfg(server)->focus_cycle;
	if (cycle == SF_FOCUS_CYCLE_MRU) {
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
	/* Sticky floats sit outside the strip; leave them out and Tab can never
	 * return from a tile. Append visible workspace floats into the same
	 * spatial ring (focus raises, so the target stays visible). */
	struct sf_toplevel *ft;
	wl_list_for_each(ft, &server->toplevels, link) {
		if (n >= SF_ARRANGE_MAX) {
			break;
		}
		if (ft->workspace != ws || !ft->arrange_floating || ft->minimized ||
		    ft->anim_hide || ft->scene_tree == NULL) {
			continue;
		}
		if (sf_toplevel_is_scratchpad(ft) || sf_toplevel_focus_obscured(ft)) {
			continue;
		}
		wins[n++] = ft;
	}
	if (n < 2) {
		return false;
	}

	/* strip: walk columns→rows collect order (plus appended floats). */
	if (cycle == SF_FOCUS_CYCLE_STRIP) {
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
			fi = 0;
		}
		size_t next = backward ? (fi + n - 1) % n : (fi + 1) % n;
		sf_focus_toplevel(wins[next]);
		sf_pointer_follow_focus(server);
		return true;
	}

	/*
	 * spatial (default): stable clockwise ring from the usable centre
	 * (atan2, y-down → clockwise is increasing angle). Independent of
	 * column/row membership order so drag-reorder does not reverse Tab.
	 * Ties: distance, then id.
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
