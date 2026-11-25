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

static int strip_clamp_offset(int offset, int max_off) {
	if (offset < 0) {
		return 0;
	}
	if (offset > max_off) {
		return max_off;
	}
	return offset;
}

static int strip_center_offset(int fl, int fw, int usable_w) {
	return fl - (usable_w - fw) / 2;
}

static int strip_just_enough(int offset, int fl, int fr, int usable_w, int max_off) {
	if (fr > offset + usable_w) {
		offset = fr - usable_w;
	}
	if (fl < offset) {
		offset = fl;
	}
	return strip_clamp_offset(offset, max_off);
}

/* Umbriel OnOverflow: center when focus and the immediate neighbor on the side
 * focus came from cannot share the viewport. */
static bool strip_pair_overflows(size_t focus_col, size_t prev_focus_col, size_t ncol,
				 const int *out_left, const int *out_width, int usable_w) {
	size_t neighbor;
	size_t first;
	size_t last;
	int span;

	if (prev_focus_col >= ncol || prev_focus_col == focus_col) {
		return false;
	}
	/* Immediate neighbor toward the side focus came from (not the prior
	 * column itself — a jump across the strip uses the same pair spacing). */
	if (prev_focus_col > focus_col) {
		neighbor = focus_col + 1;
		if (neighbor >= ncol) {
			neighbor = ncol - 1;
		}
	} else {
		neighbor = focus_col > 0 ? focus_col - 1 : 0;
	}
	if (neighbor == focus_col) {
		return false;
	}
	first = focus_col < neighbor ? focus_col : neighbor;
	last = focus_col > neighbor ? focus_col : neighbor;
	span = out_left[last] + out_width[last] - out_left[first];
	return span > usable_w;
}

bool sf_arrange_math_column_strip(const struct wlr_box *usable, size_t ncol,
				  const float *width_frac, const bool *full_width, int gap,
				  size_t focus_col, size_t prev_focus_col, int prev_offset,
				  bool lock_offset, enum sf_center_focused center, int *out_left,
				  int *out_width, int *out_view_offset) {
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
	int offset = strip_clamp_offset(prev_offset, max_off);
	if (!lock_offset) {
		int fl = out_left[focus_col];
		int fw = out_width[focus_col];
		int fr = fl + fw;
		bool want_center = false;

		switch (center) {
		case SF_CENTER_FOCUSED_ALWAYS:
			/* Wide columns pan; never park them in the middle. */
			want_center = fw <= usable->width;
			break;
		case SF_CENTER_FOCUSED_ON_OVERFLOW:
			/* Underfull strips stay left-aligned (no recenter). */
			want_center = max_off > 0 && fw <= usable->width &&
				      strip_pair_overflows(focus_col, prev_focus_col, ncol,
							   out_left, out_width, usable->width);
			break;
		case SF_CENTER_FOCUSED_NEVER:
		default:
			break;
		}

		if (want_center) {
			offset = strip_clamp_offset(strip_center_offset(fl, fw, usable->width),
						    max_off);
		} else {
			offset = strip_just_enough(offset, fl, fr, usable->width, max_off);
		}
	}
	*out_view_offset = offset;
	return true;
}

enum sf_center_focused sf_center_focused_parse(const char *s) {
	if (s == NULL || s[0] == '\0' || strcmp(s, "never") == 0 || strcmp(s, "off") == 0 ||
	    strcmp(s, "false") == 0) {
		return SF_CENTER_FOCUSED_NEVER;
	}
	if (strcmp(s, "always") == 0 || strcmp(s, "on") == 0 || strcmp(s, "true") == 0) {
		return SF_CENTER_FOCUSED_ALWAYS;
	}
	if (strcmp(s, "on_overflow") == 0 || strcmp(s, "overflow") == 0) {
		return SF_CENTER_FOCUSED_ON_OVERFLOW;
	}
	return SF_CENTER_FOCUSED_NEVER;
}

const char *sf_center_focused_name(enum sf_center_focused mode) {
	switch (mode) {
	case SF_CENTER_FOCUSED_ALWAYS:
		return "always";
	case SF_CENTER_FOCUSED_ON_OVERFLOW:
		return "on_overflow";
	case SF_CENTER_FOCUSED_NEVER:
	default:
		return "never";
	}
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
