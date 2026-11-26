#include "ui/text.h"

#include <fcft/fcft.h>
#include <pixman.h>
#include <stdlib.h>
#include <string.h>

#include "log/log.h"

enum {
	ui_font_pt_default = 12,
	ui_font_pt_min = 8,
	ui_font_pt_max = 32,
};

static struct fcft_font *ui_font;
static int ui_font_pt;
static bool ui_fcft_inited_here;

static size_t utf8_to_utf32(const char *s, uint32_t *out, size_t max) {
	size_t n = 0;
	const unsigned char *p = (const unsigned char *)s;
	while (*p != '\0' && n < max) {
		uint32_t b0 = p[0];
		uint32_t cp;
		if ((b0 & 0x80u) == 0) {
			cp = b0;
			p += 1;
		} else if ((b0 & 0xe0u) == 0xc0u && p[1] != 0) {
			cp = (b0 & 0x1fu) << 6u | ((uint32_t)p[1] & 0x3fu);
			p += 2;
		} else if ((b0 & 0xf0u) == 0xe0u && p[1] != 0 && p[2] != 0) {
			cp = (b0 & 0x0fu) << 12u | ((uint32_t)p[1] & 0x3fu) << 6u |
			     ((uint32_t)p[2] & 0x3fu);
			p += 3;
		} else if ((b0 & 0xf8u) == 0xf0u && p[1] != 0 && p[2] != 0 && p[3] != 0) {
			cp = (b0 & 0x07u) << 18u | ((uint32_t)p[1] & 0x3fu) << 12u |
			     ((uint32_t)p[2] & 0x3fu) << 6u | ((uint32_t)p[3] & 0x3fu);
			p += 4;
		} else {
			p++;
			continue;
		}
		out[n++] = cp;
	}
	return n;
}

static int measure_run(const struct fcft_glyph *const *glyphs, size_t count) {
	int w = 0;
	for (size_t i = 0; i < count; i++) {
		if (glyphs[i] != NULL) {
			w += glyphs[i]->advance.x;
		}
	}
	return w;
}

bool sf_ui_text_init(int size_pt) {
	if (size_pt < ui_font_pt_min) {
		size_pt = ui_font_pt_min;
	}
	if (size_pt > ui_font_pt_max) {
		size_pt = ui_font_pt_max;
	}
	if (ui_font != NULL && ui_font_pt == size_pt) {
		return true;
	}
	/*
	 * SSD owns global fcft_init/fini. If UI somehow runs first (tests /
	 * odd init order), init here and remember so fini can tear it down.
	 */
	if (ui_font == NULL && !ui_fcft_inited_here) {
		/* fcft_init is safe to call once; SSD may already have done it. */
		fcft_init(FCFT_LOG_COLORIZE_NEVER, false, FCFT_LOG_CLASS_ERROR);
		ui_fcft_inited_here = true;
	}
	if (ui_font != NULL) {
		fcft_destroy(ui_font);
		ui_font = NULL;
	}
	char attrs[8];
	attrs[0] = 's';
	attrs[1] = 'i';
	attrs[2] = 'z';
	attrs[3] = 'e';
	attrs[4] = '=';
	if (size_pt >= 10) {
		attrs[5] = (char)('0' + size_pt / 10);
		attrs[6] = (char)('0' + size_pt % 10);
		attrs[7] = '\0';
	} else {
		attrs[5] = (char)('0' + size_pt);
		attrs[6] = '\0';
	}
	const char *names[] = {"sans"};
	ui_font = fcft_from_name(1, names, attrs);
	if (ui_font == NULL) {
		SF_LOGW("ui: fcft font load failed", sf_i64("size", size_pt));
		ui_font_pt = 0;
		return false;
	}
	ui_font_pt = size_pt;
	return true;
}

void sf_ui_text_fini(void) {
	if (ui_font != NULL) {
		fcft_destroy(ui_font);
		ui_font = NULL;
	}
	ui_font_pt = 0;
	/*
	 * Never fcft_fini here — SSD still owns the global. The ui_fcft_inited_here
	 * flag only records that we called init; SSD fini tears the library down.
	 */
	ui_fcft_inited_here = false;
}

bool sf_ui_text_ready(void) {
	return ui_font != NULL;
}

int sf_ui_text_line_height(void) {
	if (ui_font == NULL) {
		return ui_font_pt_default + 4;
	}
	return ui_font->ascent + ui_font->descent;
}

bool sf_ui_text_measure(const char *utf8, struct sf_ui_text_metrics *out) {
	if (out == NULL) {
		return false;
	}
	memset(out, 0, sizeof *out);
	if (ui_font == NULL || utf8 == NULL || utf8[0] == '\0') {
		return false;
	}
	uint32_t cps[256];
	size_t n = utf8_to_utf32(utf8, cps, 256);
	if (n == 0) {
		return false;
	}
	struct fcft_text_run *run =
		fcft_rasterize_text_run_utf32(ui_font, n, cps, FCFT_SUBPIXEL_DEFAULT);
	const struct fcft_glyph *const *glyphs;
	size_t count;
	struct fcft_text_run *owned = run;
	const struct fcft_glyph *single[256];
	if (run != NULL) {
		glyphs = run->glyphs;
		count = run->count;
	} else {
		count = 0;
		for (size_t i = 0; i < n; i++) {
			single[count++] =
				fcft_rasterize_char_utf32(ui_font, cps[i], FCFT_SUBPIXEL_DEFAULT);
		}
		glyphs = single;
	}
	out->width = measure_run(glyphs, count);
	out->ascent = ui_font->ascent;
	out->height = ui_font->ascent + ui_font->descent;
	if (owned != NULL) {
		fcft_text_run_destroy(owned);
	}
	return true;
}

void sf_ui_text_paint(uint8_t *pix, int buf_w, int buf_h, int x, int baseline_y, const char *utf8,
		      const uint8_t fg_bgra[4]) {
	if (ui_font == NULL || pix == NULL || utf8 == NULL || utf8[0] == '\0' || fg_bgra == NULL) {
		return;
	}
	uint32_t cps[256];
	size_t n = utf8_to_utf32(utf8, cps, 256);
	if (n == 0) {
		return;
	}
	struct fcft_text_run *run =
		fcft_rasterize_text_run_utf32(ui_font, n, cps, FCFT_SUBPIXEL_DEFAULT);
	const struct fcft_glyph *const *glyphs;
	size_t count;
	struct fcft_text_run *owned = run;
	const struct fcft_glyph *single[256];
	if (run != NULL) {
		glyphs = run->glyphs;
		count = run->count;
	} else {
		count = 0;
		for (size_t i = 0; i < n; i++) {
			single[count++] =
				fcft_rasterize_char_utf32(ui_font, cps[i], FCFT_SUBPIXEL_DEFAULT);
		}
		glyphs = single;
	}

	pixman_image_t *dst =
		pixman_image_create_bits(PIXMAN_a8r8g8b8, buf_w, buf_h, (uint32_t *)pix, buf_w * 4);
	if (dst == NULL) {
		if (owned != NULL) {
			fcft_text_run_destroy(owned);
		}
		return;
	}
	pixman_color_t fg = {
		.red = (uint16_t)fg_bgra[2] * 0x0101,
		.green = (uint16_t)fg_bgra[1] * 0x0101,
		.blue = (uint16_t)fg_bgra[0] * 0x0101,
		.alpha = (uint16_t)fg_bgra[3] * 0x0101,
	};
	int pen_x = x;
	for (size_t i = 0; i < count; i++) {
		const struct fcft_glyph *g = glyphs[i];
		if (g == NULL || g->pix == NULL) {
			if (g != NULL) {
				pen_x += g->advance.x;
			}
			continue;
		}
		int dx = pen_x + g->x;
		int dy = baseline_y - g->y;
		if (g->is_color_glyph) {
			pixman_image_composite32(PIXMAN_OP_OVER, g->pix, NULL, dst, 0, 0, 0, 0, dx,
						 dy, g->width, g->height);
		} else {
			pixman_image_t *src = pixman_image_create_solid_fill(&fg);
			if (src != NULL) {
				pixman_image_composite32(PIXMAN_OP_OVER, src, g->pix, dst, 0, 0, 0,
							 0, dx, dy, g->width, g->height);
				pixman_image_unref(src);
			}
		}
		pen_x += g->advance.x;
	}
	pixman_image_unref(dst);
	if (owned != NULL) {
		fcft_text_run_destroy(owned);
	}
}
