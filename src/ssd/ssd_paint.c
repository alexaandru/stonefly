#define _POSIX_C_SOURCE 200809L

#include <fcft/fcft.h>
#include <pixman.h>
#include <stdlib.h>
#include <string.h>

#include "log/log.h"
#include "ssd/ssd_layout.h"
#include "ssd/ssd_paint.h"

enum {
	btn_icon_num = 2,
	btn_icon_den = 5, /* ~0.4 of box */
	ssd_font_pt_default = 17,
	ssd_font_pt_min = 8,
	ssd_font_pt_max = 72,
};

static struct fcft_font *title_font;
static int title_font_pt;
static bool fcft_ready;

bool stonefly_ssd_paint_ensure_font(int size_pt) {
	if (size_pt < ssd_font_pt_min) {
		size_pt = ssd_font_pt_min;
	}
	if (size_pt > ssd_font_pt_max) {
		size_pt = ssd_font_pt_max;
	}
	if (title_font != NULL && title_font_pt == size_pt) {
		return true;
	}
	if (!fcft_ready) {
		fcft_init(FCFT_LOG_COLORIZE_NEVER, false, FCFT_LOG_CLASS_ERROR);
		fcft_ready = true;
	}
	if (title_font != NULL) {
		fcft_destroy(title_font);
		title_font = NULL;
	}
	/* "size=NN" — avoid snprintf size_t/int tidy noise (pt is 8..72). */
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
	title_font = fcft_from_name(1, names, attrs);
	if (title_font == NULL) {
		SF_LOGW("ssd: fcft font load failed; title text disabled", sf_i64("size", size_pt));
		title_font_pt = 0;
		return false;
	}
	title_font_pt = size_pt;
	return true;
}

bool stonefly_ssd_paint_init(void) {
	return stonefly_ssd_paint_ensure_font(ssd_font_pt_default);
}

void stonefly_ssd_paint_fini(void) {
	if (title_font != NULL) {
		fcft_destroy(title_font);
		title_font = NULL;
	}
	title_font_pt = 0;
	if (fcft_ready) {
		fcft_fini();
		fcft_ready = false;
	}
}

static int mini(int a, int b) {
	return a < b ? a : b;
}

static int maxi(int a, int b) {
	return a > b ? a : b;
}

static void fill_rect(uint8_t *pix, int buf_w, int buf_h, int x, int y, int w, int h,
		      const uint8_t c[4]) {
	for (int row = y; row < y + h; row++) {
		if (row < 0 || row >= buf_h) {
			continue;
		}
		for (int col = x; col < x + w; col++) {
			if (col < 0 || col >= buf_w) {
				continue;
			}
			int i = (row * buf_w + col) * 4;
			pix[i] = c[0];
			pix[i + 1] = c[1];
			pix[i + 2] = c[2];
			pix[i + 3] = c[3];
		}
	}
}

static void stroke_rect(uint8_t *pix, int buf_w, int buf_h, int x, int y, int w, int h,
			const uint8_t c[4]) {
	if (w < 1 || h < 1) {
		return;
	}
	fill_rect(pix, buf_w, buf_h, x, y, w, 1, c);
	fill_rect(pix, buf_w, buf_h, x, y + h - 1, w, 1, c);
	fill_rect(pix, buf_w, buf_h, x, y, 1, h, c);
	fill_rect(pix, buf_w, buf_h, x + w - 1, y, 1, h, c);
}

static void stroke_line(uint8_t *pix, int buf_w, int buf_h, int x0, int y0, int x1, int y1,
			const uint8_t c[4]) {
	int dx = abs(x1 - x0);
	int dy = -abs(y1 - y0);
	int sx = x0 < x1 ? 1 : -1;
	int sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;

	for (;;) {
		fill_rect(pix, buf_w, buf_h, x0, y0, 1, 1, c);
		if (x0 == x1 && y0 == y1) {
			return;
		}
		int e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

/* Premul BGRA from opaque NRGBA. */
static void nrgba_to_bgra(uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint8_t out[4]) {
	out[0] = (uint8_t)((uint16_t)b * a / 255);
	out[1] = (uint8_t)((uint16_t)g * a / 255);
	out[2] = (uint8_t)((uint16_t)r * a / 255);
	out[3] = a;
}

static void draw_min(uint8_t *pix, int buf_w, int buf_h, int x, int box, int icon,
		     const uint8_t fg[4]) {
	int cx = x + box / 2;
	int cy = buf_h / 2;
	fill_rect(pix, buf_w, buf_h, cx - icon / 2, cy, icon, maxi(1, icon / 5), fg);
}

static void draw_max(uint8_t *pix, int buf_w, int buf_h, int x, int box, int icon, bool restore,
		     const uint8_t fg[4]) {
	int cx = x + box / 2;
	int cy = buf_h / 2;
	if (restore) {
		int o = maxi(1, icon / 4);
		stroke_rect(pix, buf_w, buf_h, cx - icon / 2 + o, cy - icon / 2, icon - o, icon - o,
			    fg);
		stroke_rect(pix, buf_w, buf_h, cx - icon / 2, cy - icon / 2 + o, icon - o, icon - o,
			    fg);
		return;
	}
	stroke_rect(pix, buf_w, buf_h, cx - icon / 2, cy - icon / 2, icon, icon, fg);
}

static void draw_close(uint8_t *pix, int buf_w, int buf_h, int x, int box, int icon,
		       const uint8_t fg[4]) {
	int cx = x + box / 2;
	int cy = buf_h / 2;
	int half = icon / 2;
	stroke_line(pix, buf_w, buf_h, cx - half, cy - half, cx + half, cy + half, fg);
	stroke_line(pix, buf_w, buf_h, cx + half, cy - half, cx - half, cy + half, fg);
}

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

static void blit_glyphs(uint8_t *pix, int buf_w, int buf_h, int x, int baseline,
			const struct fcft_glyph *const *glyphs, size_t count, pixman_color_t fg) {
	pixman_image_t *dst =
		pixman_image_create_bits(PIXMAN_a8r8g8b8, buf_w, buf_h, (uint32_t *)pix, buf_w * 4);
	if (dst == NULL) {
		return;
	}
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
		int dy = baseline - g->y;
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
}

static void draw_title(uint8_t *pix, int buf_w, int buf_h, const char *title, int left, int right,
		       const uint8_t fg_bgra[4]) {
	if (title_font == NULL || title == NULL || title[0] == '\0' || right <= left) {
		return;
	}
	uint32_t cps[256];
	size_t n = utf8_to_utf32(title, cps, 256);
	if (n == 0) {
		return;
	}

	struct fcft_text_run *run =
		fcft_rasterize_text_run_utf32(title_font, n, cps, FCFT_SUBPIXEL_DEFAULT);
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
			const struct fcft_glyph *g = fcft_rasterize_char_utf32(
				title_font, cps[i], FCFT_SUBPIXEL_DEFAULT);
			single[count++] = g;
		}
		glyphs = single;
	}

	int max_w = right - left;
	while (count > 0 && measure_run(glyphs, count) > max_w) {
		count--;
	}

	/* 16.16 fixed-point color for pixman solid fill. */
	pixman_color_t fg = {
		.red = (uint16_t)fg_bgra[2] * 0x0101,
		.green = (uint16_t)fg_bgra[1] * 0x0101,
		.blue = (uint16_t)fg_bgra[0] * 0x0101,
		.alpha = (uint16_t)fg_bgra[3] * 0x0101,
	};
	int baseline = (buf_h + title_font->ascent - title_font->descent) / 2;
	blit_glyphs(pix, buf_w, buf_h, left, baseline, glyphs, count, fg);

	if (owned != NULL) {
		fcft_text_run_destroy(owned);
	}
}

void stonefly_ssd_paint_titlebar(uint8_t *pix, int buf_w, int buf_h, const char *title,
				 bool focused, bool maximized, const uint8_t fill[4],
				 const struct stonefly_ssd_layout *lay) {
	if (pix == NULL || buf_w < 1 || buf_h < 1 || lay == NULL) {
		return;
	}
	memset(pix, 0, (size_t)buf_w * (size_t)buf_h * 4);
	fill_rect(pix, buf_w, buf_h, 0, 0, buf_w, buf_h, fill);

	/* Black glyphs on neon/charcoal fill (unfocused slightly softer). */
	uint8_t fg[4];
	if (focused) {
		nrgba_to_bgra(0x00, 0x00, 0x00, 0xff, fg);
	} else {
		nrgba_to_bgra(0x33, 0x33, 0x33, 0xff, fg);
	}

	draw_title(pix, buf_w, buf_h, title, lay->title_left, lay->title_right, fg);

	int box = mini(28, lay->height);
	int icon = maxi(1, box * btn_icon_num / btn_icon_den);
	draw_min(pix, buf_w, buf_h, lay->min_x, box, icon, fg);
	draw_max(pix, buf_w, buf_h, lay->max_x, box, icon, maximized, fg);
	draw_close(pix, buf_w, buf_h, lay->close_x, box, icon, fg);
}
