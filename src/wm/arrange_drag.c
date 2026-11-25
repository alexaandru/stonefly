#include "wm/arrange.h"

#include <stdint.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/util/box.h>
#include <scenefx/types/wlr_scene.h>

#include "config/config.h"
#include "layer/layer.h"
#include "output/output.h"
#include "seat/seat.h"
#include "server/server.h"
#include "ssd/ssd.h"
#include "toplevel/toplevel.h"
#include "wm/arrange_internal.h"
#include "wm/column.h"
#include "wm/geom.h"
#include "wm/snap_preview.h"
#include "wm/wm.h"
#include "wm/workspace.h"

/* Cell-drag drop ops; stored on server->arrange_swap so finish matches reflow. */
enum sf_column_drop {
	SF_COLUMN_DROP_NONE = 0,
	SF_COLUMN_DROP_SWAP = 1,
	SF_COLUMN_DROP_INSERT = 2,
	SF_COLUMN_DROP_NEW_COL = 3,
};

void arrange_columns_drop_clear(struct sf_server *server) {
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
					  focus_col, ws->columns_focus_col, ws->columns_view_offset,
					  false, sf_cfg(server)->center_focused, col_left, col_w,
					  &offset)) {
		return;
	}
	ws->columns_view_offset = offset;
	ws->columns_focus_col = focus_col;

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
	arrange_apply_boxes(out_wins, out_boxes, n, false);
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
					double seam = (double)sf_cfg(server)->column_drop_seam;
					if (frac < seam) {
						*kind_out = SF_COLUMN_DROP_INSERT;
						*peer_out = rows[r];
						*col_out = col;
						*index_out = (int)r;
						return true;
					}
					if (frac > 1.0 - seam) {
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
	arrange_columns_drop_clear(server);
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
		arrange_columns_drop_clear(server);
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
	arrange_columns_drop_clear(server);
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
	arrange_columns_drop_clear(server);
	columns_home_clear(server);
	if (ws != NULL) {
		sf_arrange_schedule(ws);
	}
}
