#ifndef STONEFLY_SSD_PAINT_H
#define STONEFLY_SSD_PAINT_H

#include <stdbool.h>
#include <stdint.h>

struct stonefly_ssd_layout;

/* Premultiplied BGRA (little-endian ARGB8888). */
void stonefly_ssd_paint_titlebar(uint8_t *pix, int buf_w, int buf_h, const char *title,
				 bool focused, bool maximized, const uint8_t fill[4],
				 const struct stonefly_ssd_layout *lay);

/* Shared fcft face for title text. NULL if load failed. */
bool stonefly_ssd_paint_init(void);
void stonefly_ssd_paint_fini(void);

#endif /* STONEFLY_SSD_PAINT_H */
