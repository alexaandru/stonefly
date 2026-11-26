/* In-compositor UI text: fcft face + measure/paint into ARGB buffers. */
#ifndef SF_UI_TEXT_H
#define SF_UI_TEXT_H

#include <stdbool.h>
#include <stdint.h>

struct sf_ui_text_metrics {
	int width;  /* advance width */
	int height; /* ascent + descent */
	int ascent;
};

/*
 * Own face after SSD has called fcft_init. Does not call fcft_fini.
 * family NULL/empty → "sans". Reloads when size or family changes.
 */
bool sf_ui_text_init(int size_pt, const char *family);
void sf_ui_text_fini(void);
bool sf_ui_text_ready(void);

/* Measure UTF-8 line. Returns false if font missing or empty. */
bool sf_ui_text_measure(const char *utf8, struct sf_ui_text_metrics *out);

/*
 * Paint UTF-8 into premultiplied BGRA (LE ARGB8888) buffer.
 * fg_bgra is B,G,R,A. baseline_y is pixel row of the alphabetic baseline.
 */
void sf_ui_text_paint(uint8_t *pix, int buf_w, int buf_h, int x, int baseline_y, const char *utf8,
		      const uint8_t fg_bgra[4]);

int sf_ui_text_line_height(void);

#endif
