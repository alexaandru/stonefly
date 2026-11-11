/* First-class scrolling columns (umbriel Column + ScrollingLayout::m_columns).
 * Workspace owns an ordered strip of columns; each column owns ordered rows. */
#ifndef SF_WM_COLUMN_H
#define SF_WM_COLUMN_H

#include <stdbool.h>
#include <stddef.h>
#include <wayland-server-core.h>
#include <wlr/util/box.h>

struct sf_toplevel;
struct sf_workspace;
struct sf_server;

struct sf_column {
	struct wl_list link; /* sf_workspace.columns */
	struct sf_workspace *ws;
	/* Top → bottom rows (sf_toplevel.column_link). */
	struct wl_list rows;
	/* Primary extent fraction (default cfg->column_ratio). */
	float width_frac;
	/* Prior frac while full_width (umbriel savedWidthFrac). */
	float width_frac_saved;
	bool full_width;
	/* Free cross-axis weight at column ends (umbriel gap weights). */
	float top_gap_weight;
	float bottom_gap_weight;
};

/* Destroy every column on the workspace (leave tiling / destroy ws). */
void sf_columns_clear(struct sf_workspace *ws);

/* Index of column in ws->columns, or -1. */
int sf_column_index(const struct sf_column *col);

/* Row index of toplevel in its column, or -1. */
int sf_column_row_of(const struct sf_toplevel *toplevel);

size_t sf_column_row_count(const struct sf_column *col);
size_t sf_columns_count(const struct sf_workspace *ws);

struct sf_column *sf_columns_at(struct sf_workspace *ws, int index);

/* New column at strip index with toplevel as sole row. Returns the column. */
struct sf_column *sf_columns_insert_new(struct sf_workspace *ws, struct sf_toplevel *toplevel,
					int index);

/* Insert toplevel as a row in col at row_index (0..nrow). Claims end gap weight. */
bool sf_column_insert_row(struct sf_column *col, struct sf_toplevel *toplevel, int row_index);

/* Detach from its column; destroy the column if empty. */
void sf_column_remove_row(struct sf_toplevel *toplevel);

/* Move into neighbor column (dir -1|+1). Drops empty source. */
bool sf_columns_consume(struct sf_toplevel *toplevel, int dir);

/* Peel a multi-row window into a new column beside (dir -1|+1). */
bool sf_columns_expel(struct sf_toplevel *toplevel, int dir);

/* Pull neighbor column's first row into toplevel's column (dir -1|+1). */
bool sf_columns_consume_from(struct sf_toplevel *toplevel, int dir);

void sf_column_set_full_width(struct sf_column *col, bool want);
void sf_column_toggle_full_width(struct sf_column *col);

/* Pack columns into boxes[0..n-1] matching collect order (columns→rows).
 * Writes view offset onto ws->columns_view_offset. Returns row count. */
size_t sf_columns_apply(struct sf_workspace *ws, const struct wlr_box *usable, int gap,
			struct sf_toplevel *focus, struct sf_toplevel **out_wins,
			struct wlr_box *out_boxes, size_t cap);

/* Walk strip into out[] (managed, non-minimized). */
size_t sf_columns_collect(struct sf_workspace *ws, struct sf_toplevel **out, size_t cap);

/* Focus column index for insert-after-focus; -1 if none. */
int sf_columns_focus_index(struct sf_workspace *ws);

#endif
