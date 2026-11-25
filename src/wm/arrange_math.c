#include "wm/arrange_math.h"

#include <math.h>
#include <string.h>

static bool usable_ok(const struct wlr_box *usable) {
	return usable != NULL && usable->width > 0 && usable->height > 0;
}

static double clamp_col_ratio(double ratio) {
	if (ratio < 0.05) {
		return 0.05;
	}
	if (ratio > 1.0) {
		return 1.0;
	}
	return ratio;
}

/* Umbriel scrolling: width = round(frac * (viewport + gap)) - gap so N equal
 * fractions of 1/N fit the viewport with inter-column gaps (no +gap overflow). */
int sf_arrange_math_column_width_px(int usable_w, float frac, int gap) {
	if (usable_w < 1) {
		return 1;
	}
	if (gap < 0) {
		gap = 0;
	}
	if (frac < 0.05f) {
		frac = 0.05f;
	}
	if (frac > 4.0f) {
		frac = 4.0f;
	}
	double slot = (double)usable_w + (double)gap;
	int w = (int)lround((double)frac * slot) - gap;
	if (w < 1) {
		w = 1;
	}
	return w;
}

bool sf_arrange_math_columns(const struct wlr_box *usable, size_t n, double col_ratio, int gap,
			     int view_offset, struct wlr_box *out) {
	int col_w;
	size_t i;

	if (!usable_ok(usable)) {
		return false;
	}
	if (n == 0) {
		return true;
	}
	if (out == NULL) {
		return false;
	}

	col_ratio = clamp_col_ratio(col_ratio);
	if (gap < 0) {
		gap = 0;
	}
	col_w = sf_arrange_math_column_width_px(usable->width, (float)col_ratio, gap);

	for (i = 0; i < n; i++) {
		out[i] = (struct wlr_box){
			.x = usable->x + (int)i * (col_w + gap) - view_offset,
			.y = usable->y,
			.width = col_w,
			.height = usable->height,
		};
	}
	return true;
}

bool sf_arrange_math_column_strip(const struct wlr_box *usable, size_t ncol,
				  const float *width_frac, const bool *full_width, int gap,
				  size_t focus_col, int prev_offset, bool lock_offset,
				  int *out_left, int *out_width, int *out_view_offset) {
	if (!usable_ok(usable) || out_left == NULL || out_width == NULL ||
	    out_view_offset == NULL) {
		return false;
	}
	if (ncol == 0) {
		*out_view_offset = 0;
		return true;
	}
	if (width_frac == NULL || full_width == NULL) {
		return false;
	}
	if (gap < 0) {
		gap = 0;
	}
	if (focus_col >= ncol) {
		focus_col = 0;
	}

	/* Cumulative slot rounding (umbriel columnWidth) so adjacent fracs share
	 * boundaries without 1px drift. */
	double slot = (double)usable->width + (double)gap;
	double slot_start = 0.0;
	int x = 0;
	for (size_t k = 0; k < ncol; k++) {
		int w;
		if (ncol == 1 || full_width[k]) {
			w = usable->width;
			slot_start = 0.0;
		} else {
			float f = width_frac[k];
			if (f < 0.05f) {
				f = 0.05f;
			}
			if (f > 4.0f) {
				f = 4.0f;
			}
			double slot_end = slot_start + (double)f * slot;
			w = (int)lround(slot_end) - (int)lround(slot_start) - gap;
			slot_start = slot_end;
		}
		if (w < 1) {
			w = 1;
		}
		out_width[k] = w;
		out_left[k] = x;
		x += w + (k + 1 < ncol ? gap : 0);
	}

	int max_off = x > usable->width ? x - usable->width : 0;
	int offset = prev_offset;
	if (offset < 0) {
		offset = 0;
	}
	if (offset > max_off) {
		offset = max_off;
	}
	if (!lock_offset) {
		/* Just-enough: nudge only so focus is fully visible. */
		int fl = out_left[focus_col];
		int fr = fl + out_width[focus_col];
		if (fr > offset + usable->width) {
			offset = fr - usable->width;
		}
		if (fl < offset) {
			offset = fl;
		}
		if (offset < 0) {
			offset = 0;
		}
		if (offset > max_off) {
			offset = max_off;
		}
	}
	*out_view_offset = offset;
	return true;
}

bool sf_arrange_math_column_rows(const struct wlr_box *col_box, size_t nrows, const float *weights,
				 int gap, struct wlr_box *out) {
	if (col_box == NULL || col_box->width <= 0 || col_box->height <= 0) {
		return false;
	}
	if (nrows == 0) {
		return true;
	}
	if (out == NULL || weights == NULL) {
		return false;
	}
	if (gap < 0) {
		gap = 0;
	}

	double wsum = 0.0;
	for (size_t r = 0; r < nrows; r++) {
		float w = weights[r];
		if (w < 0.05f) {
			w = 0.05f;
		}
		wsum += (double)w;
	}

	int y = col_box->y;
	int content = col_box->height - (int)(nrows - 1) * gap;
	if (content < (int)nrows) {
		content = (int)nrows;
	}
	int used = 0;
	for (size_t r = 0; r < nrows; r++) {
		int h;
		if (r + 1 == nrows) {
			h = content - used;
		} else if (wsum > 0.0) {
			float w = weights[r] < 0.05f ? 0.05f : weights[r];
			h = (int)((double)content * (double)w / wsum);
			if (h < 1) {
				h = 1;
			}
			used += h;
		} else {
			h = content / (int)nrows;
			used += h;
		}
		out[r] = (struct wlr_box){
			.x = col_box->x,
			.y = y,
			.width = col_box->width,
			.height = h,
		};
		y += h + gap;
	}
	return true;
}

enum sf_arrange sf_arrange_parse(const char *s) {
	if (s == NULL || s[0] == '\0' || strcmp(s, "stacking") == 0) {
		return SF_ARRANGE_STACKING;
	}
	if (strcmp(s, "columns") == 0 || strcmp(s, "scroller") == 0) {
		return SF_ARRANGE_COLUMNS;
	}
	/* Hard-cut: old engine names alias to columns (no master/dwindle/monocle). */
	if (strcmp(s, "master") == 0 || strcmp(s, "tile") == 0 || strcmp(s, "dwindle") == 0 ||
	    strcmp(s, "monocle") == 0) {
		return SF_ARRANGE_COLUMNS;
	}
	if (strcmp(s, "unset") == 0 || strcmp(s, "inherit") == 0) {
		return SF_ARRANGE_UNSET;
	}
	if (strcmp(s, "tiling") == 0) {
		return SF_ARRANGE_TILING;
	}
	if (strcmp(s, "overview") == 0) {
		return SF_ARRANGE_OVERVIEW;
	}
	return SF_ARRANGE_STACKING;
}

const char *sf_arrange_name(enum sf_arrange arrange) {
	switch (arrange) {
	case SF_ARRANGE_OVERVIEW:
		return "overview";
	case SF_ARRANGE_TILING:
		return "tiling";
	case SF_ARRANGE_UNSET:
		return "inherit";
	case SF_ARRANGE_STACKING:
		return "stacking";
	case SF_ARRANGE_COLUMNS:
		return "columns";
	}
	return "stacking";
}

bool sf_arrange_is_tiling(enum sf_arrange arrange) {
	return arrange == SF_ARRANGE_COLUMNS;
}
