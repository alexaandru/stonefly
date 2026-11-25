#ifndef SF_SSD_LAYOUT_H
#define SF_SSD_LAYOUT_H

enum sf_ssd_button {
	SF_SSD_BTN_NONE = 0,
	SF_SSD_BTN_MINIMIZE,
	SF_SSD_BTN_MAXIMIZE,
	SF_SSD_BTN_CLOSE,
};

struct sf_ssd_layout {
	int width;
	int height;
	int icon_x;    /* left of app icon; unused when icon_side == 0 */
	int icon_side; /* square side in titlebar px; 0 = no icon */
	int title_left;
	int title_right; /* exclusive */
	int min_x;
	int max_x;
	int close_x;
};

/* icon_side 0 skips the app-icon slot; otherwise reserves a square left of the title. */
void sf_ssd_layout_title(int width, int height, int icon_side, struct sf_ssd_layout *out);
enum sf_ssd_button sf_ssd_layout_hit(const struct sf_ssd_layout *lay, int x, int y);

#endif /* SF_SSD_LAYOUT_H */
