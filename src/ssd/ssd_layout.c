#define _POSIX_C_SOURCE 200809L

#include <stddef.h>

#include "ssd/ssd_layout.h"

enum {
	button_box = 28,
	button_gap = 2,
	button_pad_r = 4,
	title_pad_l = 10,
	title_pad_r = 8,
};

static int mini(int a, int b) {
	return a < b ? a : b;
}

static int maxi(int a, int b) {
	return a > b ? a : b;
}

void stonefly_ssd_layout_title(int width, int height, struct stonefly_ssd_layout *out) {
	if (width < 1) {
		width = 1;
	}
	if (height < 1) {
		height = 1;
	}
	out->width = width;
	out->height = height;
	out->title_left = title_pad_l;
	int box = mini(button_box, height);
	out->close_x = width - button_pad_r - box;
	out->max_x = out->close_x - button_gap - box;
	out->min_x = out->max_x - button_gap - box;
	out->title_right = maxi(out->min_x - title_pad_r, out->title_left);
}

enum stonefly_ssd_button stonefly_ssd_layout_hit(const struct stonefly_ssd_layout *lay, int x,
						 int y) {
	if (lay == NULL || y < 0 || y >= lay->height || x < 0 || x >= lay->width) {
		return STONEFLY_SSD_BTN_NONE;
	}
	int box = mini(button_box, lay->height);
	if (x >= lay->close_x && x < lay->close_x + box) {
		return STONEFLY_SSD_BTN_CLOSE;
	}
	if (x >= lay->max_x && x < lay->max_x + box) {
		return STONEFLY_SSD_BTN_MAXIMIZE;
	}
	if (x >= lay->min_x && x < lay->min_x + box) {
		return STONEFLY_SSD_BTN_MINIMIZE;
	}
	return STONEFLY_SSD_BTN_NONE;
}
