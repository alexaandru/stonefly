#ifndef STONEFLY_SSD_LAYOUT_H
#define STONEFLY_SSD_LAYOUT_H

enum stonefly_ssd_button {
	STONEFLY_SSD_BTN_NONE = 0,
	STONEFLY_SSD_BTN_MINIMIZE,
	STONEFLY_SSD_BTN_MAXIMIZE,
	STONEFLY_SSD_BTN_CLOSE,
};

struct stonefly_ssd_layout {
	int width;
	int height;
	int title_left;
	int title_right; /* exclusive */
	int min_x;
	int max_x;
	int close_x;
};

void stonefly_ssd_layout_title(int width, int height, struct stonefly_ssd_layout *out);
enum stonefly_ssd_button stonefly_ssd_layout_hit(const struct stonefly_ssd_layout *lay, int x,
						 int y);

#endif /* STONEFLY_SSD_LAYOUT_H */
