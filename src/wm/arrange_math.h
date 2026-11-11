#ifndef SF_WM_ARRANGE_MATH_H
#define SF_WM_ARRANGE_MATH_H

#include <stdbool.h>
#include <stddef.h>
#include <wlr/util/box.h>

/* Workspace packing engines (distinct from per-window layout_sm).
 * Product set is stacking + columns only (umbriel polish over breadth). */
enum sf_arrange {
	/* Bind filter only: match any tiling engine (not a real workspace mode). */
	SF_ARRANGE_TILING = -2,
	SF_ARRANGE_UNSET = -1,
	SF_ARRANGE_STACKING = 0,
	SF_ARRANGE_COLUMNS = 1, /* umbriel-style scrollable strip */
};

enum sf_master_pos {
	SF_MASTER_LEFT = 0,
	SF_MASTER_RIGHT,
	SF_MASTER_TOP,
	SF_MASTER_BOTTOM,
	SF_MASTER_CENTER,
};

enum sf_arrange sf_arrange_parse(const char *s);
const char *sf_arrange_name(enum sf_arrange arrange);
bool sf_arrange_is_tiling(enum sf_arrange arrange);

/* Fill out[0..n-1] with content boxes inside usable (already gap-inset by caller).
 * n==0 is no-op. Returns false if usable invalid. */

bool sf_arrange_math_master(const struct wlr_box *usable, size_t n, enum sf_master_pos pos,
			    double factor, int master_count, int gap, struct wlr_box *out);

/* Niri-style columns: each window is one column of width ~usable.w * col_ratio
 * (clamped). view_offset is subtracted from x (scroll). focused_idx used only
 * for documentation — caller adjusts view_offset so focused is visible.
 * Columns keep their width when n grows (no equal reflow of old columns).
 * For v1 simplicity: all columns get the same width = max(min_w, usable.w * ratio).
 * Place column i at usable.x + i*(col_w + gap) - view_offset, full usable height.
 * gap is inter-column gap in px (>=0). */

bool sf_arrange_math_columns(const struct wlr_box *usable, size_t n, double col_ratio, int gap,
			     int view_offset, struct wlr_box *out);

/* First-class column strip (umbriel): per-column width_frac, optional full_width.
 * Lone column fills usable. Writes out_left/out_width[0..ncol) and centers
 * view_offset on focus_col. gap >= 0. */
bool sf_arrange_math_column_strip(const struct wlr_box *usable, size_t ncol,
				  const float *width_frac, const bool *full_width, int gap,
				  size_t focus_col, int *out_left, int *out_width,
				  int *out_view_offset);

/* Weighted rows inside one column box. weights > 0; last row takes remainder. */
bool sf_arrange_math_column_rows(const struct wlr_box *col_box, size_t nrows, const float *weights,
				 int gap, struct wlr_box *out);

/* Binary dwindle: alternate vertical/horizontal splits starting with vertical
 * (left/right). factor is first/rest split (clamped). Window 0 gets first
 * portion of (span-gap), rest recurse into the other half. gap >= 0. */

bool sf_arrange_math_dwindle(const struct wlr_box *usable, size_t n, double factor, int gap,
			     struct wlr_box *out);

/* Monocle: out[focus_idx] = usable; other out[i] = {0,0,0,0} (caller may hide
 * or stack). If focus_idx >= n, use 0. */

bool sf_arrange_math_monocle(const struct wlr_box *usable, size_t n, size_t focus_idx,
			     struct wlr_box *out);

enum sf_master_pos sf_master_pos_parse(const char *s);
const char *sf_master_pos_name(enum sf_master_pos pos);

#endif
