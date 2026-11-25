#ifndef SF_WM_ARRANGE_MATH_H
#define SF_WM_ARRANGE_MATH_H

#include <stdbool.h>
#include <stddef.h>
#include <wlr/util/box.h>

/* Workspace packing engines (distinct from per-window layout_sm).
 * Product set is stacking + columns only (umbriel polish over breadth).
 * Parse aliases master/tile/dwindle/monocle → columns (hard-cut; no engines). */
enum sf_arrange {
	/* Bind filter only: overview interactive (not a real workspace mode). */
	SF_ARRANGE_OVERVIEW = -3,
	/* Bind filter only: match any tiling engine (not a real workspace mode). */
	SF_ARRANGE_TILING = -2,
	SF_ARRANGE_UNSET = -1,
	SF_ARRANGE_STACKING = 0,
	SF_ARRANGE_COLUMNS = 1, /* umbriel-style scrollable strip */
};

enum sf_arrange sf_arrange_parse(const char *s);
const char *sf_arrange_name(enum sf_arrange arrange);
bool sf_arrange_is_tiling(enum sf_arrange arrange);

/* Umbriel scrolling center_focused: when focus moves, whether to park the
 * focused column in the middle of the viewport. */
enum sf_center_focused {
	SF_CENTER_FOCUSED_NEVER = 0,   /* just-enough reveal (default Umbriel) */
	SF_CENTER_FOCUSED_ALWAYS,      /* center focused column */
	SF_CENTER_FOCUSED_ON_OVERFLOW, /* center when focus+neighbor pair overflows */
};

enum sf_center_focused sf_center_focused_parse(const char *s);
const char *sf_center_focused_name(enum sf_center_focused mode);

/* Fill out[0..n-1] with content boxes inside usable (already gap-inset by caller).
 * n==0 is no-op. Returns false if usable invalid. */

/* Columns: each window is one column. Width uses umbriel slot math
 * frac*(usable+gap)-gap so equal fractions fit without a leftover gap overflow.
 * view_offset is subtracted from x (scroll). Place column i at
 * usable.x + i*(col_w + gap) - view_offset, full usable height. gap >= 0. */

bool sf_arrange_math_columns(const struct wlr_box *usable, size_t n, double col_ratio, int gap,
			     int view_offset, struct wlr_box *out);

/* Pixel width for one column extent fraction (umbriel scrolling slot). */
int sf_arrange_math_column_width_px(int usable_w, float frac, int gap);

/* First-class column strip (umbriel): per-column width_frac, optional full_width.
 * Lone column fills usable. Writes out_left/out_width[0..ncol). Scroll follows
 * center mode: never = just-enough; always = center focus; on_overflow = center
 * when focus and the neighbor on prev_focus_col's side cannot share the
 * viewport (else just-enough). Underfull strips stay left-aligned (offset 0).
 * prev_focus_col == focus_col or >= ncol means no side. lock_offset: only clamp
 * prev_offset (live pan). gap >= 0. */
bool sf_arrange_math_column_strip(const struct wlr_box *usable, size_t ncol,
				  const float *width_frac, const bool *full_width, int gap,
				  size_t focus_col, size_t prev_focus_col, int prev_offset,
				  bool lock_offset, enum sf_center_focused center, int *out_left,
				  int *out_width, int *out_view_offset);

/* Weighted rows inside one column box. weights > 0; last row takes remainder. */
bool sf_arrange_math_column_rows(const struct wlr_box *col_box, size_t nrows, const float *weights,
				 int gap, struct wlr_box *out);

#endif
