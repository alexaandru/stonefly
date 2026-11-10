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

/* Split box into n panes stacked vertically; remainder px → last pane. */
static void split_stack_y(const struct wlr_box *box, size_t n, struct wlr_box *out) {
	int base;
	int rem;
	int y;
	size_t i;

	if (n == 0) {
		return;
	}
	base = box->height / (int)n;
	rem = box->height % (int)n;
	y = box->y;
	for (i = 0; i < n; i++) {
		int h = base + (i + 1 == n ? rem : 0);

		out[i] = (struct wlr_box){
			.x = box->x,
			.y = y,
			.width = box->width,
			.height = h,
		};
		y += h;
	}
}

/* Split box into n panes side-by-side; remainder px → last pane. */
static void split_stack_x(const struct wlr_box *box, size_t n, struct wlr_box *out) {
	int base;
	int rem;
	int x;
	size_t i;

	if (n == 0) {
		return;
	}
	base = box->width / (int)n;
	rem = box->width % (int)n;
	x = box->x;
	for (i = 0; i < n; i++) {
		int w = base + (i + 1 == n ? rem : 0);

		out[i] = (struct wlr_box){
			.x = x,
			.y = box->y,
			.width = w,
			.height = box->height,
		};
		x += w;
	}
}

static void master_lr(const struct wlr_box *usable, size_t n, size_t mc, double factor,
		      bool master_on_left, struct wlr_box *out) {
	size_t stack_n = n - mc;
	struct wlr_box master_area;
	struct wlr_box stack_area;

	if (stack_n == 0) {
		split_stack_y(usable, mc, out);
		return;
	}

	master_area = *usable;
	stack_area = *usable;
	master_area.width = span_factor(usable->width, factor);
	stack_area.width = usable->width - master_area.width;
	if (master_on_left) {
		master_area.x = usable->x;
		stack_area.x = usable->x + master_area.width;
	} else {
		stack_area.x = usable->x;
		master_area.x = usable->x + stack_area.width;
	}
	split_stack_y(&master_area, mc, out);
	split_stack_y(&stack_area, stack_n, out + mc);
}

static void master_tb(const struct wlr_box *usable, size_t n, size_t mc, double factor,
		      bool master_on_top, struct wlr_box *out) {
	size_t stack_n = n - mc;
	struct wlr_box master_area;
	struct wlr_box stack_area;

	if (stack_n == 0) {
		split_stack_x(usable, mc, out);
		return;
	}

	master_area = *usable;
	stack_area = *usable;
	master_area.height = span_factor(usable->height, factor);
	stack_area.height = usable->height - master_area.height;
	if (master_on_top) {
		master_area.y = usable->y;
		stack_area.y = usable->y + master_area.height;
	} else {
		stack_area.y = usable->y;
		master_area.y = usable->y + stack_area.height;
	}
	split_stack_x(&master_area, mc, out);
	split_stack_x(&stack_area, stack_n, out + mc);
}

static void master_center(const struct wlr_box *usable, size_t n, size_t mc, double factor,
			  struct wlr_box *out) {
	size_t stack_n = n - mc;
	struct wlr_box center;
	size_t left_n;
	size_t right_n;
	int side_total;
	int left_w;
	int right_w;

	if (stack_n == 0) {
		split_stack_y(usable, mc, out);
		return;
	}

	center = *usable;
	center.width = span_factor(usable->width, factor);
	side_total = usable->width - center.width;
	left_w = side_total / 2;
	right_w = side_total - left_w;
	center.x = usable->x + left_w;

	left_n = stack_n / 2;
	right_n = stack_n - left_n;

	split_stack_y(&center, mc, out);

	if (left_n > 0) {
		struct wlr_box left = {
			.x = usable->x,
			.y = usable->y,
			.width = left_w,
			.height = usable->height,
		};

		split_stack_y(&left, left_n, out + mc);
	}
	if (right_n > 0) {
		struct wlr_box right = {
			.x = usable->x + left_w + center.width,
			.y = usable->y,
			.width = right_w,
			.height = usable->height,
		};

		split_stack_y(&right, right_n, out + mc + left_n);
	}
}

bool sf_arrange_math_master(const struct wlr_box *usable, size_t n, enum sf_master_pos pos,
			    double factor, int master_count, struct wlr_box *out) {
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
	if (master_count < 1) {
		master_count = 1;
	}
	if ((size_t)master_count > n) {
		master_count = (int)n;
	}
	mc = (size_t)master_count;

	switch (pos) {
	case SF_MASTER_LEFT:
		master_lr(usable, n, mc, factor, true, out);
		break;
	case SF_MASTER_RIGHT:
		master_lr(usable, n, mc, factor, false, out);
		break;
	case SF_MASTER_TOP:
		master_tb(usable, n, mc, factor, true, out);
		break;
	case SF_MASTER_BOTTOM:
		master_tb(usable, n, mc, factor, false, out);
		break;
	case SF_MASTER_CENTER:
		master_center(usable, n, mc, factor, out);
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
			 struct wlr_box *out) {
	struct wlr_box first;
	struct wlr_box rest;

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
		first.width = box.width / 2;
		rest.width = box.width - first.width;
		rest.x = box.x + first.width;
	} else {
		first.height = box.height / 2;
		rest.height = box.height - first.height;
		rest.y = box.y + first.height;
	}
	out[start] = first;
	dwindle_into(rest, start + 1, count - 1, !vertical, out);
}

bool sf_arrange_math_dwindle(const struct wlr_box *usable, size_t n, struct wlr_box *out) {
	if (!usable_ok(usable)) {
		return false;
	}
	if (n == 0) {
		return true;
	}
	if (out == NULL) {
		return false;
	}
	dwindle_into(*usable, 0, n, true, out);
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
	return SF_ARRANGE_STACKING;
}

const char *sf_arrange_name(enum sf_arrange arrange) {
	switch (arrange) {
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
