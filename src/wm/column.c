#include "wm/column.h"

#include <stdlib.h>
#include <string.h>

#include "config/config.h"
#include "output/output.h"
#include "server/server.h"
#include "toplevel/toplevel.h"
#include "wm/arrange_math.h"
#include "wm/wm.h"
#include "wm/workspace.h"

#define SF_COLUMN_MAX 64

static float default_width_frac(struct sf_workspace *ws) {
	if (ws == NULL || ws->output == NULL || ws->output->server == NULL) {
		return 0.5f;
	}
	float f = sf_cfg(ws->output->server)->column_ratio;
	if (f < 0.05f) {
		return 0.05f;
	}
	if (f > 4.0f) {
		return 4.0f;
	}
	return f;
}

static void row_unlink(struct sf_toplevel *t) {
	if (t == NULL) {
		return;
	}
	if (t->column_link.next != NULL && !wl_list_empty(&t->column_link)) {
		wl_list_remove(&t->column_link);
		wl_list_init(&t->column_link);
	}
	t->column = NULL;
}

static void column_destroy_empty(struct sf_column *col) {
	if (col == NULL) {
		return;
	}
	if (!wl_list_empty(&col->rows)) {
		return;
	}
	wl_list_remove(&col->link);
	free(col);
}

void sf_columns_clear(struct sf_workspace *ws) {
	if (ws == NULL) {
		return;
	}
	struct sf_column *col, *tmp;
	wl_list_for_each_safe(col, tmp, &ws->columns, link) {
		struct sf_toplevel *t, *tt;
		wl_list_for_each_safe(t, tt, &col->rows, column_link) {
			row_unlink(t);
		}
		wl_list_remove(&col->link);
		free(col);
	}
	wl_list_init(&ws->columns);
}

int sf_column_index(const struct sf_column *col) {
	if (col == NULL || col->ws == NULL) {
		return -1;
	}
	int i = 0;
	struct sf_column *c;
	wl_list_for_each(c, &col->ws->columns, link) {
		if (c == col) {
			return i;
		}
		i++;
	}
	return -1;
}

int sf_column_row_of(const struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->column == NULL) {
		return -1;
	}
	int i = 0;
	struct sf_toplevel *t;
	wl_list_for_each(t, &toplevel->column->rows, column_link) {
		if (t == toplevel) {
			return i;
		}
		i++;
	}
	return -1;
}

size_t sf_column_row_count(const struct sf_column *col) {
	if (col == NULL) {
		return 0;
	}
	size_t n = 0;
	struct sf_toplevel *t;
	wl_list_for_each(t, &col->rows, column_link) {
		n++;
	}
	return n;
}

size_t sf_column_visible_count(const struct sf_column *col) {
	if (col == NULL) {
		return 0;
	}
	size_t n = 0;
	struct sf_toplevel *t;
	wl_list_for_each(t, &col->rows, column_link) {
		if (!t->minimized) {
			n++;
		}
	}
	return n;
}

size_t sf_columns_count(const struct sf_workspace *ws) {
	if (ws == NULL) {
		return 0;
	}
	size_t n = 0;
	struct sf_column *c;
	wl_list_for_each(c, &ws->columns, link) {
		n++;
	}
	return n;
}

size_t sf_columns_visible_count(const struct sf_workspace *ws) {
	if (ws == NULL) {
		return 0;
	}
	size_t n = 0;
	struct sf_column *c;
	wl_list_for_each(c, &ws->columns, link) {
		if (sf_column_visible_count(c) > 0) {
			n++;
		}
	}
	return n;
}

struct sf_column *sf_columns_at(struct sf_workspace *ws, int index) {
	if (ws == NULL || index < 0) {
		return NULL;
	}
	int i = 0;
	struct sf_column *c;
	wl_list_for_each(c, &ws->columns, link) {
		if (i == index) {
			return c;
		}
		i++;
	}
	return NULL;
}

int sf_columns_focus_index(struct sf_workspace *ws) {
	if (ws == NULL || ws->output == NULL || ws->output->server == NULL) {
		return -1;
	}
	struct sf_toplevel *focus = sf_focused_toplevel(ws->output->server);
	if (focus == NULL || focus->workspace != ws || focus->column == NULL ||
	    focus->column->ws != ws) {
		return -1;
	}
	return sf_column_index(focus->column);
}

static struct sf_column *column_create(struct sf_workspace *ws) {
	struct sf_column *col = calloc(1, sizeof(*col));
	if (col == NULL) {
		return NULL;
	}
	col->ws = ws;
	wl_list_init(&col->rows);
	col->width_frac = default_width_frac(ws);
	col->width_frac_saved = 0.0f;
	col->full_width = false;
	col->top_gap_weight = 0.0f;
	col->bottom_gap_weight = 0.0f;
	return col;
}

static void column_link_at(struct sf_workspace *ws, struct sf_column *col, int index) {
	size_t n = sf_columns_count(ws);
	if (index < 0) {
		index = 0;
	}
	if ((size_t)index >= n) {
		wl_list_insert(ws->columns.prev, &col->link);
		return;
	}
	struct sf_column *at = sf_columns_at(ws, index);
	if (at == NULL) {
		wl_list_insert(ws->columns.prev, &col->link);
		return;
	}
	wl_list_insert(at->link.prev, &col->link);
}

struct sf_column *sf_columns_insert_new(struct sf_workspace *ws, struct sf_toplevel *toplevel,
					int index) {
	if (ws == NULL || toplevel == NULL) {
		return NULL;
	}
	if (toplevel->column != NULL) {
		sf_column_remove_row(toplevel);
	}
	struct sf_column *col = column_create(ws);
	if (col == NULL) {
		return NULL;
	}
	column_link_at(ws, col, index);
	toplevel->column_row_weight = 1.0f;
	toplevel->column_extent_saved = 0.0f;
	wl_list_init(&toplevel->column_link);
	wl_list_insert(&col->rows, &toplevel->column_link);
	toplevel->column = col;
	toplevel->arrange_managed = true;
	return col;
}

bool sf_column_insert_row(struct sf_column *col, struct sf_toplevel *toplevel, int row_index) {
	if (col == NULL || toplevel == NULL) {
		return false;
	}
	if (toplevel->column != NULL) {
		if (toplevel->column == col) {
			/* Reorder within the same column. */
			row_unlink(toplevel);
			toplevel->column = NULL;
		} else {
			sf_column_remove_row(toplevel);
		}
	}
	size_t nrow = sf_column_row_count(col);
	if (row_index < 0) {
		row_index = 0;
	}
	if ((size_t)row_index > nrow) {
		row_index = (int)nrow;
	}

	float weight = 1.0f;
	if (row_index == 0 && col->top_gap_weight > 0.0f) {
		weight = col->top_gap_weight;
		col->top_gap_weight = 0.0f;
	} else if ((size_t)row_index == nrow && col->bottom_gap_weight > 0.0f) {
		weight = col->bottom_gap_weight;
		col->bottom_gap_weight = 0.0f;
	}
	if (weight < 0.05f) {
		weight = 0.05f;
	}
	toplevel->column_row_weight = weight;

	wl_list_init(&toplevel->column_link);
	if ((size_t)row_index >= nrow) {
		wl_list_insert(col->rows.prev, &toplevel->column_link);
	} else {
		struct sf_toplevel *at = NULL;
		int i = 0;
		wl_list_for_each(at, &col->rows, column_link) {
			if (i == row_index) {
				wl_list_insert(at->column_link.prev, &toplevel->column_link);
				break;
			}
			i++;
		}
	}
	toplevel->column = col;
	toplevel->arrange_managed = true;
	return true;
}

void sf_column_remove_row(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->column == NULL) {
		return;
	}
	struct sf_column *col = toplevel->column;
	row_unlink(toplevel);
	column_destroy_empty(col);
}

void sf_column_detach_row(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->column == NULL) {
		return;
	}
	row_unlink(toplevel);
}

void sf_column_destroy_if_empty(struct sf_column *col) {
	column_destroy_empty(col);
}

bool sf_columns_swap_rows(struct sf_toplevel *a, struct sf_toplevel *b) {
	if (a == NULL || b == NULL || a == b || a->column == NULL || b->column == NULL) {
		return false;
	}
	struct sf_column *ca = a->column;
	struct sf_column *cb = b->column;
	int ra = sf_column_row_of(a);
	int rb = sf_column_row_of(b);
	if (ra < 0 || rb < 0) {
		return false;
	}
	float wa = a->column_row_weight;
	float wb = b->column_row_weight;
	float ea = a->column_extent_saved;
	float eb = b->column_extent_saved;

	if (ca == cb) {
		struct sf_toplevel *rows[SF_COLUMN_MAX];
		size_t n = 0;
		struct sf_toplevel *t;
		wl_list_for_each(t, &ca->rows, column_link) {
			if (n >= SF_COLUMN_MAX) {
				return false;
			}
			rows[n++] = t;
		}
		if ((size_t)ra >= n || (size_t)rb >= n) {
			return false;
		}
		rows[ra] = b;
		rows[rb] = a;
		while (!wl_list_empty(&ca->rows)) {
			t = wl_container_of(ca->rows.next, t, column_link);
			row_unlink(t);
		}
		for (size_t i = 0; i < n; i++) {
			wl_list_init(&rows[i]->column_link);
			wl_list_insert(ca->rows.prev, &rows[i]->column_link);
			rows[i]->column = ca;
		}
		a->column_row_weight = wa > 0.0f ? wa : 1.0f;
		b->column_row_weight = wb > 0.0f ? wb : 1.0f;
		a->column_extent_saved = ea;
		b->column_extent_saved = eb;
		return true;
	}

	/* Different columns: park both, then land in each other's cells. */
	row_unlink(a);
	row_unlink(b);
	bool ok_a = sf_column_insert_row(cb, a, rb);
	bool ok_b = ok_a && sf_column_insert_row(ca, b, ra);
	if (!ok_a || !ok_b) {
		/* Best-effort restore; columns may briefly have been empty. */
		if (a->column != NULL) {
			row_unlink(a);
		}
		if (b->column != NULL) {
			row_unlink(b);
		}
		sf_column_insert_row(ca, a, ra);
		sf_column_insert_row(cb, b, rb);
		column_destroy_empty(ca);
		column_destroy_empty(cb);
		return false;
	}
	a->column_row_weight = wa > 0.0f ? wa : 1.0f;
	b->column_row_weight = wb > 0.0f ? wb : 1.0f;
	a->column_extent_saved = ea;
	b->column_extent_saved = eb;
	return true;
}

bool sf_columns_consume(struct sf_toplevel *toplevel, int dir) {
	if (toplevel == NULL || toplevel->column == NULL || (dir != -1 && dir != 1)) {
		return false;
	}
	struct sf_column *src = toplevel->column;
	struct sf_workspace *ws = src->ws;
	/* Skip neighbors that only hold minimized rows (not on the painted strip). */
	int di = sf_column_index(src) + dir;
	struct sf_column *dst = NULL;
	while ((dst = sf_columns_at(ws, di)) != NULL) {
		if (sf_column_visible_count(dst) > 0) {
			break;
		}
		di += dir;
	}
	if (dst == NULL) {
		return false;
	}
	/* Umbriel: row keeps source column extent for a later expel. */
	float remembered = src->width_frac_saved > 0.0f ? src->width_frac_saved : src->width_frac;
	sf_column_remove_row(toplevel);
	size_t nrow = sf_column_row_count(dst);
	sf_column_insert_row(dst, toplevel, (int)nrow);
	toplevel->column_extent_saved = remembered > 0.0f ? remembered : 0.0f;
	return true;
}

bool sf_columns_expel(struct sf_toplevel *toplevel, int dir) {
	if (toplevel == NULL || toplevel->column == NULL || (dir != -1 && dir != 1)) {
		return false;
	}
	struct sf_column *src = toplevel->column;
	/* Any sibling (including minimized) can be peeled away from; visible-only
	 * would strand a sole visible row sharing a column with minimized ghosts. */
	if (sf_column_row_count(src) <= 1) {
		return false;
	}
	struct sf_workspace *ws = src->ws;
	int si = sf_column_index(src);
	int home_row = sf_column_row_of(toplevel);
	float remembered = toplevel->column_extent_saved;
	float weight = toplevel->column_row_weight;
	/* Detach without destroying src — siblings remain; analyzer cannot prove
	 * remove_row leaves src alive across the OOM reinsert path. */
	sf_column_detach_row(toplevel);
	int dest = si + (dir > 0 ? 1 : 0);
	struct sf_column *col = sf_columns_insert_new(ws, toplevel, dest);
	if (col == NULL) {
		/* OOM / insert failure: never leave a detached managed row. */
		if (!sf_column_insert_row(src, toplevel, home_row >= 0 ? home_row : 0)) {
			toplevel->arrange_managed = false;
		}
		return false;
	}
	sf_column_destroy_if_empty(src);
	col->width_frac = remembered > 0.0f ? remembered : default_width_frac(ws);
	toplevel->column_row_weight = weight > 0.0f ? weight : 1.0f;
	toplevel->column_extent_saved = 0.0f;
	return true;
}

bool sf_columns_consume_from(struct sf_toplevel *toplevel, int dir) {
	if (toplevel == NULL || toplevel->column == NULL || (dir != -1 && dir != 1)) {
		return false;
	}
	struct sf_column *dst = toplevel->column;
	struct sf_workspace *ws = dst->ws;
	int di = sf_column_index(dst);
	int si = di + dir;
	struct sf_column *src = NULL;
	while ((src = sf_columns_at(ws, si)) != NULL) {
		if (sf_column_visible_count(src) > 0) {
			break;
		}
		si += dir;
	}
	if (src == NULL) {
		return false;
	}
	struct sf_toplevel *pulled = NULL;
	struct sf_toplevel *t;
	wl_list_for_each(t, &src->rows, column_link) {
		if (!t->minimized) {
			pulled = t;
			break;
		}
	}
	if (pulled == NULL) {
		return false;
	}
	float remembered = src->width_frac_saved > 0.0f ? src->width_frac_saved : src->width_frac;
	int focus_row = sf_column_row_of(toplevel);
	sf_column_remove_row(pulled);
	/* dst may have shifted if src was left and emptied. */
	di = sf_column_index(dst);
	if (di < 0) {
		return false;
	}
	dst = sf_columns_at(ws, di);
	if (dst == NULL) {
		return false;
	}
	int landing = focus_row + 1;
	if (landing < 0) {
		landing = (int)sf_column_row_count(dst);
	}
	sf_column_insert_row(dst, pulled, landing);
	pulled->column_extent_saved = remembered > 0.0f ? remembered : 0.0f;
	return true;
}

void sf_column_set_full_width(struct sf_column *col, bool want) {
	if (col == NULL) {
		return;
	}
	if (want) {
		if (!col->full_width) {
			col->width_frac_saved = col->width_frac;
		}
		col->full_width = true;
	} else {
		col->full_width = false;
		if (col->width_frac_saved > 0.0f) {
			col->width_frac = col->width_frac_saved;
			col->width_frac_saved = 0.0f;
		}
	}
}

void sf_column_toggle_full_width(struct sf_column *col) {
	if (col == NULL) {
		return;
	}
	sf_column_set_full_width(col, !col->full_width);
}

size_t sf_columns_collect(struct sf_workspace *ws, struct sf_toplevel **out, size_t cap) {
	if (ws == NULL || out == NULL) {
		return 0;
	}
	size_t n = 0;
	struct sf_column *col;
	wl_list_for_each(col, &ws->columns, link) {
		struct sf_toplevel *t;
		wl_list_for_each(t, &col->rows, column_link) {
			if (t->minimized) {
				continue;
			}
			if (n < cap) {
				out[n] = t;
			}
			n++;
		}
	}
	return n;
}

size_t sf_columns_apply(struct sf_workspace *ws, const struct wlr_box *usable, int gap,
			struct sf_toplevel *focus, struct sf_toplevel **out_wins,
			struct wlr_box *out_boxes, size_t cap) {
	if (ws == NULL || usable == NULL || out_wins == NULL || out_boxes == NULL || cap == 0) {
		return 0;
	}
	if (gap < 0) {
		gap = 0;
	}

	/* Only columns with a visible row count for the strip. A lone visible
	 * column fills usable even if other columns only hold minimized rows. */
	struct sf_column *cols[SF_COLUMN_MAX];
	float fracs[SF_COLUMN_MAX];
	bool fulls[SF_COLUMN_MAX];
	size_t ncol = 0;
	struct sf_column *c;
	wl_list_for_each(c, &ws->columns, link) {
		if (sf_column_visible_count(c) == 0) {
			continue;
		}
		if (ncol >= SF_COLUMN_MAX) {
			break;
		}
		cols[ncol] = c;
		fracs[ncol] = c->width_frac;
		fulls[ncol] = c->full_width;
		ncol++;
	}
	if (ncol == 0) {
		ws->columns_view_offset = 0;
		return 0;
	}

	size_t focus_col = 0;
	if (focus != NULL && focus->column != NULL) {
		for (size_t k = 0; k < ncol; k++) {
			if (cols[k] == focus->column) {
				focus_col = k;
				break;
			}
		}
	}

	int col_w[SF_COLUMN_MAX];
	int col_left[SF_COLUMN_MAX];
	int offset = 0;
	if (!sf_arrange_math_column_strip(usable, ncol, fracs, fulls, gap, focus_col, col_left,
					  col_w, &offset)) {
		ws->columns_view_offset = 0;
		return 0;
	}
	ws->columns_view_offset = offset;

	size_t n = 0;
	for (size_t k = 0; k < ncol; k++) {
		struct sf_toplevel *rows[SF_COLUMN_MAX];
		float weights[SF_COLUMN_MAX];
		size_t nrows = 0;
		struct sf_toplevel *t;
		wl_list_for_each(t, &cols[k]->rows, column_link) {
			if (t->minimized) {
				continue;
			}
			if (nrows >= SF_COLUMN_MAX) {
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
			.x = usable->x + col_left[k] - offset,
			.y = usable->y,
			.width = col_w[k],
			.height = usable->height,
		};
		struct wlr_box row_boxes[SF_COLUMN_MAX];
		if (!sf_arrange_math_column_rows(&col_box, nrows, weights, gap, row_boxes)) {
			continue;
		}
		for (size_t r = 0; r < nrows; r++) {
			if (n < cap) {
				out_wins[n] = rows[r];
				out_boxes[n] = row_boxes[r];
			}
			n++;
		}
	}
	return n > cap ? cap : n;
}
