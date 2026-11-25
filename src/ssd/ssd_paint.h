#ifndef SF_SSD_PAINT_H
#define SF_SSD_PAINT_H

#include <stdbool.h>
#include <stdint.h>

struct sf_ssd_layout;

/*
 * Premultiplied BGRA (little-endian ARGB8888).
 * Optional app icon: premultiplied ARGB8888 source (icon_pix); scaled into
 * lay->icon_* when icon_side > 0 and icon_w/h > 0. Other formats skipped by caller.
 */
void sf_ssd_paint_titlebar(uint8_t *pix, int buf_w, int buf_h, const char *title, bool focused,
			   bool maximized, const uint8_t fill[4], const struct sf_ssd_layout *lay,
			   uint32_t *icon_pix, int icon_w, int icon_h, int icon_stride);

/* Shared fcft face for title text. NULL if load failed. */
bool sf_ssd_paint_init(void);
/* Reload face when ssd_font_size changes (no-op if size already loaded). */
bool sf_ssd_paint_ensure_font(int size_pt);
void sf_ssd_paint_fini(void);

#endif /* SF_SSD_PAINT_H */
