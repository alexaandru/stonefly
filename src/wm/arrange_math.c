#include "wm/arrange_math.h"

#include <string.h>

static bool usable_ok(const struct wlr_box *usable) {
	return usable != NULL && usable->width > 0 && usable->height > 0;
}

static double clamp_factor(double factor) {
	if (factor < 0.05) {
		return 0.05;
	}
	if (factor > 0.95) {
		return 0.95;
	}
	return factor;
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

/* First span from factor; remainder goes to the other side. */
static int span_factor(int total, double factor) {
	int a;

	if (total <= 0) {
		return 0;
	}
	if (total == 1) {
		return 1;
	}
	a = (int)(total * factor);
	if (a < 1) {
		a = 1;
	}
	if (a > total - 1) {
		a = total - 1;
	}
	return a;
}

/* Split box into n panes stacked vertically with gap between; rem → last. */
static void split_stack_y(const struct wlr_box *box, size_t n, int gap, struct wlr_box *out) {
	int content;
	int base;
	int rem;
	int y;
	size_t i;

	if (n == 0) {
		return;
	}
	if (n == 1) {
		out[0] = *box;
		return;
	}
	content = box->height - (int)(n - 1) * gap;
	if (content < (int)n) {
		content = (int)n;
	}
	base = content / (int)n;
	rem = content % (int)n;
	y = box->y;
	for (i = 0; i < n; i++) {
		int h = base + (i + 1 == n ? rem : 0);

		out[i] = (struct wlr_box){
			.x = box->x,
			.y = y,
			.width = box->width,
			.height = h,
		};
		y += h + gap;
	}
}

/* Split box into n panes side-by-side with gap between; rem → last. */
static void split_stack_x(const struct wlr_box *box, size_t n, int gap, struct wlr_box *out) {
	int content;
	int base;
	int rem;
	int x;
	size_t i;

	if (n == 0) {
		return;
	}
	if (n == 1) {
		out[0] = *box;
		return;
	}
	content = box->width - (int)(n - 1) * gap;
	if (content < (int)n) {
		content = (int)n;
	}
	base = content / (int)n;
	rem = content % (int)n;
	x = box->x;
	for (i = 0; i < n; i++) {
		int w = base + (i + 1 == n ? rem : 0);

		out[i] = (struct wlr_box){
			.x = x,
			.y = box->y,
			.width = w,
			.height = box->height,
		};
		x += w + gap;
	}
}

static void master_lr(const struct wlr_box *usable, size_t n, size_t mc, double factor, int gap,
		      bool master_on_left, struct wlr_box *out) {
	size_t stack_n = n - mc;
	struct wlr_box master_area;
	struct wlr_box stack_area;
	int content;

	if (stack_n == 0) {
		split_stack_y(usable, mc, gap, out);
		return;
	}

	content = usable->width - gap;
	master_area = *usable;
	stack_area = *usable;
	master_area.width = span_factor(content, factor);
	stack_area.width = content - master_area.width;
	if (master_on_left) {
		master_area.x = usable->x;
		stack_area.x = usable->x + master_area.width + gap;
	} else {
		stack_area.x = usable->x;
		master_area.x = usable->x + stack_area.width + gap;
	}
	split_stack_y(&master_area, mc, gap, out);
	split_stack_y(&stack_area, stack_n, gap, out + mc);
}

static void master_tb(const struct wlr_box *usable, size_t n, size_t mc, double factor, int gap,
		      bool master_on_top, struct wlr_box *out) {
	size_t stack_n = n - mc;
	struct wlr_box master_area;
	struct wlr_box stack_area;
	int content;

	if (stack_n == 0) {
		split_stack_x(usable, mc, gap, out);
		return;
	}

	content = usable->height - gap;
	master_area = *usable;
	stack_area = *usable;
	master_area.height = span_factor(content, factor);
	stack_area.height = content - master_area.height;
	if (master_on_top) {
		master_area.y = usable->y;
		stack_area.y = usable->y + master_area.height + gap;
	} else {
		stack_area.y = usable->y;
		master_area.y = usable->y + stack_area.height + gap;
	}
	split_stack_x(&master_area, mc, gap, out);
	split_stack_x(&stack_area, stack_n, gap, out + mc);
}

static void master_center(const struct wlr_box *usable, size_t n, size_t mc, double factor, int gap,
			  struct wlr_box *out) {
	size_t stack_n = n - mc;
	struct wlr_box center;
	size_t left_n;
	size_t right_n;
	int seams;
	int content;
	int side_total;
	int left_w;
	int right_w;

	if (stack_n == 0) {
		split_stack_y(usable, mc, gap, out);
		return;
	}

	left_n = stack_n / 2;
	right_n = stack_n - left_n;
	seams = (left_n > 0 ? 1 : 0) + (right_n > 0 ? 1 : 0);
	content = usable->width - seams * gap;

	center = *usable;
	center.width = span_factor(content, factor);
	side_total = content - center.width;
	left_w = side_total / 2;
	right_w = side_total - left_w;
	/* Empty left half of side_total stays when left_n==0 (no seam there). */
	center.x = usable->x + left_w + (left_n > 0 ? gap : 0);

	split_stack_y(&center, mc, gap, out);

	if (left_n > 0) {
		struct wlr_box left = {
			.x = usable->x,
			.y = usable->y,
			.width = left_w,
			.height = usable->height,
		};

		split_stack_y(&left, left_n, gap, out + mc);
	}
	if (right_n > 0) {
		struct wlr_box right = {
			.x = center.x + center.width + gap,
			.y = usable->y,
			.width = right_w,
			.height = usable->height,
		};

		split_stack_y(&right, right_n, gap, out + mc + left_n);
	}
}

bool sf_arrange_math_master(const struct wlr_box *usable, size_t n, enum sf_master_pos pos,
			    double factor, int master_count, int gap, struct wlr_box *out) {
	size_t mc;

	if (!usable_ok(usable)) {
		return false;
	}
	if (n == 0) {
		return true;
	}
	if (out == NULL) {
		return false;
	}

	factor = clamp_factor(factor);
	if (gap < 0) {
		gap = 0;
	}
	if (master_count < 1) {
		master_count = 1;
	}
	if ((size_t)master_count > n) {
		master_count = (int)n;
	}
	mc = (size_t)master_count;

	switch (pos) {
	case SF_MASTER_LEFT:
		master_lr(usable, n, mc, factor, gap, true, out);
		break;
	case SF_MASTER_RIGHT:
		master_lr(usable, n, mc, factor, gap, false, out);
		break;
	case SF_MASTER_TOP:
		master_tb(usable, n, mc, factor, gap, true, out);
		break;
	case SF_MASTER_BOTTOM:
		master_tb(usable, n, mc, factor, gap, false, out);
		break;
	case SF_MASTER_CENTER:
		master_center(usable, n, mc, factor, gap, out);
		break;
	}
	return true;
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
	col_w = (int)(usable->width * col_ratio);
	if (col_w < 1) {
		col_w = 1;
	}

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

static void dwindle_into(struct wlr_box box, size_t start, size_t count, bool vertical,
			 double factor, int gap, struct wlr_box *out) {
	struct wlr_box first;
	struct wlr_box rest;
	int content;

	if (count == 0) {
		return;
	}
	if (count == 1) {
		out[start] = box;
		return;
	}

	first = box;
	rest = box;
	if (vertical) {
		content = box.width - gap;
		first.width = span_factor(content, factor);
		rest.width = content - first.width;
		rest.x = box.x + first.width + gap;
	} else {
		content = box.height - gap;
		first.height = span_factor(content, factor);
		rest.height = content - first.height;
		rest.y = box.y + first.height + gap;
	}
	out[start] = first;
	dwindle_into(rest, start + 1, count - 1, !vertical, factor, gap, out);
}

bool sf_arrange_math_dwindle(const struct wlr_box *usable, size_t n, double factor, int gap,
			     struct wlr_box *out) {
	if (!usable_ok(usable)) {
		return false;
	}
	if (n == 0) {
		return true;
	}
	if (out == NULL) {
		return false;
	}
	if (gap < 0) {
		gap = 0;
	}
	factor = clamp_factor(factor);
	dwindle_into(*usable, 0, n, true, factor, gap, out);
	return true;
}

bool sf_arrange_math_monocle(const struct wlr_box *usable, size_t n, size_t focus_idx,
			     struct wlr_box *out) {
	size_t i;
	size_t fi;

	if (!usable_ok(usable)) {
		return false;
	}
	if (n == 0) {
		return true;
	}
	if (out == NULL) {
		return false;
	}

	fi = focus_idx < n ? focus_idx : 0;
	for (i = 0; i < n; i++) {
		if (i == fi) {
			out[i] = *usable;
		} else {
			out[i] = (struct wlr_box){0};
		}
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
	if (strcmp(s, "master") == 0 || strcmp(s, "tile") == 0) {
		return SF_ARRANGE_MASTER;
	}
	if (strcmp(s, "dwindle") == 0) {
		return SF_ARRANGE_DWINDLE;
	}
	if (strcmp(s, "monocle") == 0) {
		return SF_ARRANGE_MONOCLE;
	}
	if (strcmp(s, "unset") == 0 || strcmp(s, "inherit") == 0) {
		return SF_ARRANGE_UNSET;
	}
	if (strcmp(s, "tiling") == 0) {
		return SF_ARRANGE_TILING;
	}
	return SF_ARRANGE_STACKING;
}

const char *sf_arrange_name(enum sf_arrange arrange) {
	switch (arrange) {
	case SF_ARRANGE_TILING:
		return "tiling";
	case SF_ARRANGE_UNSET:
		return "inherit";
	case SF_ARRANGE_STACKING:
		return "stacking";
	case SF_ARRANGE_COLUMNS:
		return "columns";
	case SF_ARRANGE_MASTER:
		return "master";
	case SF_ARRANGE_DWINDLE:
		return "dwindle";
	case SF_ARRANGE_MONOCLE:
		return "monocle";
	}
	return "stacking";
}

bool sf_arrange_is_tiling(enum sf_arrange arrange) {
	return arrange == SF_ARRANGE_COLUMNS || arrange == SF_ARRANGE_MASTER ||
	       arrange == SF_ARRANGE_DWINDLE || arrange == SF_ARRANGE_MONOCLE;
}

enum sf_master_pos sf_master_pos_parse(const char *s) {
	if (s == NULL || s[0] == '\0' || strcmp(s, "left") == 0) {
		return SF_MASTER_LEFT;
	}
	if (strcmp(s, "right") == 0) {
		return SF_MASTER_RIGHT;
	}
	if (strcmp(s, "top") == 0) {
		return SF_MASTER_TOP;
	}
	if (strcmp(s, "bottom") == 0) {
		return SF_MASTER_BOTTOM;
	}
	if (strcmp(s, "center") == 0) {
		return SF_MASTER_CENTER;
	}
	return SF_MASTER_LEFT;
}

const char *sf_master_pos_name(enum sf_master_pos pos) {
	switch (pos) {
	case SF_MASTER_LEFT:
		return "left";
	case SF_MASTER_RIGHT:
		return "right";
	case SF_MASTER_TOP:
		return "top";
	case SF_MASTER_BOTTOM:
		return "bottom";
	case SF_MASTER_CENTER:
		return "center";
	}
	return "left";
}
