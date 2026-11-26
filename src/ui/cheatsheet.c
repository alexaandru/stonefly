#include "ui/ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <scenefx/types/fx/clipped_region.h>
#include <scenefx/types/wlr_scene.h>

#include "config/config.h"
#include "log/log.h"
#include "output/output.h"
#include "server/server.h"
#include "session/session.h"
#include "ui/cheatsheet_rows.h"
#include "ui/text.h"
#include "util/color.h"
#include "util/math.h"
#include "wm/workspace.h"

enum {
	cs_pad = 28, /* Umbriel kPad — keep text off the rounded rim */
	cs_col_gap = 28,
	cs_row_gap = 4,
	cs_title_gap = 14,
	cs_footer_gap = 12,
	cs_chord_pad_x = 6,
	cs_chord_pad_y = 2,
	cs_max_cols = 4,
	cs_argb8888 = 0x34325241u, /* 'AR24' LE */
	/* Panel fill alpha (~72%) so SceneFX window_blur reads through. */
	cs_bg_alpha = 0xb8,
};

/* Premultiplied BGRA palette. */
static const uint8_t col_bg[4] = {
	(uint8_t)((0x1a * cs_bg_alpha + 127) / 255),
	(uint8_t)((0x18 * cs_bg_alpha + 127) / 255),
	(uint8_t)((0x16 * cs_bg_alpha + 127) / 255),
	cs_bg_alpha,
};
static const uint8_t col_title[4] = {0xe8, 0xe4, 0xdc, 0xff};
static const uint8_t col_header[4] = {0xd0, 0xa0, 0x60, 0xff}; /* accent */
static const uint8_t col_chord_fg[4] = {0xff, 0xd0, 0x80, 0xff};
static const uint8_t col_chord_bg[4] = {0x38, 0x34, 0x30, 0xff};
static const uint8_t col_label[4] = {0xd8, 0xd4, 0xcc, 0xff};
static const uint8_t col_muted[4] = {0x90, 0x8c, 0x84, 0xff};

struct sf_ui_buffer {
	struct wlr_buffer base;
	void *data;
	size_t stride;
};

static void ui_buffer_destroy(struct wlr_buffer *wlr_buffer) {
	struct sf_ui_buffer *buf = wl_container_of(wlr_buffer, buf, base);
	free(buf->data);
	free(buf);
}

static bool ui_buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buffer, uint32_t flags,
					    void **data, uint32_t *format, size_t *stride) {
	struct sf_ui_buffer *buf = wl_container_of(wlr_buffer, buf, base);
	(void)flags;
	*format = cs_argb8888;
	*data = buf->data;
	*stride = buf->stride;
	return true;
}

static void ui_buffer_end_data_ptr_access(struct wlr_buffer *wlr_buffer) {
	(void)wlr_buffer;
}

static const struct wlr_buffer_impl ui_buffer_impl = {
	.destroy = ui_buffer_destroy,
	.begin_data_ptr_access = ui_buffer_begin_data_ptr_access,
	.end_data_ptr_access = ui_buffer_end_data_ptr_access,
};

static struct sf_ui_buffer *ui_buffer_create(int width, int height) {
	if (width < 1 || height < 1) {
		return NULL;
	}
	struct sf_ui_buffer *buf = calloc(1, sizeof(*buf));
	if (buf == NULL) {
		return NULL;
	}
	size_t stride = (size_t)width * 4;
	buf->data = calloc((size_t)height, stride);
	if (buf->data == NULL) {
		free(buf);
		return NULL;
	}
	buf->stride = stride;
	wlr_buffer_init(&buf->base, &ui_buffer_impl, width, height);
	return buf;
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

struct display_line {
	bool is_header;
	enum sf_cheatsheet_group group;
	char chord[96];
	char label[80];
};

static size_t build_display_lines(const struct sf_cheatsheet_row *rows, size_t n_rows,
				  struct display_line **out_lines) {
	*out_lines = NULL;
	size_t cap = n_rows + (size_t)SF_CS_GROUP_COUNT * 2 + 4;
	struct display_line *lines = calloc(cap, sizeof *lines);
	if (lines == NULL) {
		return 0;
	}
	size_t n = 0;
	size_t n_groups = 0;
	const enum sf_cheatsheet_group *order = sf_cheatsheet_group_order(&n_groups);
	for (size_t gi = 0; gi < n_groups; gi++) {
		enum sf_cheatsheet_group g = order[gi];
		bool any = false;
		for (size_t i = 0; i < n_rows; i++) {
			if (rows[i].group == g) {
				any = true;
				break;
			}
		}
		if (!any) {
			continue;
		}
		if (n + 1 < cap) {
			if (n > 0) {
				/* blank spacer before next group */
				lines[n].is_header = false;
				lines[n].group = g;
				lines[n].chord[0] = '\0';
				lines[n].label[0] = '\0';
				n++;
			}
			lines[n].is_header = true;
			lines[n].group = g;
			lines[n].chord[0] = '\0';
			snprintf(lines[n].label, sizeof lines[n].label, "%s",
				 sf_cheatsheet_group_title(g));
			n++;
		}
		for (size_t i = 0; i < n_rows && n < cap; i++) {
			if (rows[i].group != g) {
				continue;
			}
			lines[n].is_header = false;
			lines[n].group = g;
			snprintf(lines[n].chord, sizeof lines[n].chord, "%s", rows[i].chord);
			snprintf(lines[n].label, sizeof lines[n].label, "%s", rows[i].label);
			n++;
		}
	}
	if (n == 0 && n < cap) {
		lines[n].is_header = false;
		lines[n].chord[0] = '\0';
		snprintf(lines[n].label, sizeof lines[n].label, "no keybinds configured");
		n++;
	}
	*out_lines = lines;
	return n;
}

static int line_height(void) {
	int h = sf_ui_text_line_height();
	return h + cs_row_gap + 2 * cs_chord_pad_y;
}

static void measure_line(const struct display_line *line, int *chord_w, int *label_w) {
	*chord_w = 0;
	*label_w = 0;
	struct sf_ui_text_metrics m;
	if (line->is_header) {
		if (sf_ui_text_measure(line->label, &m)) {
			*label_w = m.width;
		}
		return;
	}
	if (line->chord[0] != '\0' && sf_ui_text_measure(line->chord, &m)) {
		*chord_w = m.width + 2 * cs_chord_pad_x;
	}
	if (line->label[0] != '\0' && sf_ui_text_measure(line->label, &m)) {
		*label_w = m.width;
	}
}

static int columns_needed(const int *block_sizes, size_t n_blocks, int limit) {
	int columns = 1;
	int used = 0;
	for (size_t i = 0; i < n_blocks; i++) {
		int size = block_sizes[i];
		if (used > 0 && used + size > limit) {
			columns++;
			used = 0;
		}
		used += size;
	}
	return columns;
}

static int balanced_height(const int *block_sizes, size_t n_blocks, int num_cols) {
	int low = 0;
	int high = 0;
	for (size_t i = 0; i < n_blocks; i++) {
		if (block_sizes[i] > low) {
			low = block_sizes[i];
		}
		high += block_sizes[i];
	}
	int cols = num_cols < 1 ? 1 : num_cols;
	while (low < high) {
		int mid = low + (high - low) / 2;
		if (columns_needed(block_sizes, n_blocks, mid) <= cols) {
			high = mid;
		} else {
			low = mid + 1;
		}
	}
	return low;
}

struct col_slice {
	size_t start;
	size_t count;
	int width; /* chord+gap+label max */
};

static void paint_line(uint8_t *pix, int buf_w, int buf_h, int x, int y, int lh,
		       const struct display_line *line, int max_chord_w) {
	/* Center text vertically in the row slot. */
	struct sf_ui_text_metrics m;
	int ascent = sf_ui_text_measure("Ag", &m) ? m.ascent : lh * 3 / 4;
	int baseline = y + (lh + ascent) / 2;

	if (line->is_header) {
		sf_ui_text_paint(pix, buf_w, buf_h, x, baseline, line->label, col_header);
		return;
	}
	if (line->chord[0] == '\0' && line->label[0] == '\0') {
		return;
	}
	int chord_w = 0;
	int label_w = 0;
	measure_line(line, &chord_w, &label_w);
	if (chord_w > 0) {
		int pill_h = sf_ui_text_line_height() + 2 * cs_chord_pad_y;
		int pill_y = y + (lh - pill_h) / 2;
		fill_rect(pix, buf_w, buf_h, x, pill_y, chord_w, pill_h, col_chord_bg);
		sf_ui_text_paint(pix, buf_w, buf_h, x + cs_chord_pad_x, baseline, line->chord,
				 col_chord_fg);
	}
	int label_x = x + max_chord_w + 10;
	if (line->label[0] != '\0') {
		const uint8_t *fg = (chord_w == 0 && label_w > 0) ? col_muted : col_label;
		sf_ui_text_paint(pix, buf_w, buf_h, label_x, baseline, line->label, fg);
	}
}

static void cheatsheet_destroy_tree(struct sf_server *server) {
	if (server->cheatsheet.tree != NULL) {
		wlr_scene_node_destroy(&server->cheatsheet.tree->node);
		server->cheatsheet.tree = NULL;
	}
	server->cheatsheet.scene_buf = NULL;
	/* Buffer destroyed with scene node drop / our destroy path — drop ref. */
	if (server->cheatsheet.buffer != NULL) {
		wlr_buffer_drop(server->cheatsheet.buffer);
		server->cheatsheet.buffer = NULL;
	}
}

static struct sf_output *cheatsheet_output(struct sf_server *server) {
	struct sf_output *out = sf_output_at_cursor(server);
	if (out != NULL) {
		return out;
	}
	if (!wl_list_empty(&server->outputs)) {
		return wl_container_of(server->outputs.next, out, link);
	}
	return NULL;
}

static void cheatsheet_render(struct sf_server *server) {
	cheatsheet_destroy_tree(server);
	if (!sf_ui_text_ready() && !sf_ui_text_init(12)) {
		SF_LOGW("cheatsheet: no font; skip");
		return;
	}

	const struct sf_config *cfg = sf_cfg(server);
	size_t n_rows = 0;
	struct sf_cheatsheet_row *rows =
		sf_cheatsheet_rows_build(cfg->keybinds, cfg->n_keybinds, &n_rows);

	struct display_line *lines = NULL;
	size_t n_lines = build_display_lines(rows, n_rows, &lines);
	free(rows);
	if (lines == NULL || n_lines == 0) {
		free(lines);
		return;
	}

	struct sf_output *output = cheatsheet_output(server);
	/* Umbriel: center / fit against the full output layout box (not usable). */
	int out_w = 1200;
	int out_h = 800;
	int out_x = 0;
	int out_y = 0;
	double scale = 1.0;
	if (output != NULL) {
		struct wlr_box box = output->full;
		if (output->wlr_output != NULL && server->output_layout != NULL) {
			wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);
		}
		if (box.width > 0 && box.height > 0) {
			out_x = box.x;
			out_y = box.y;
			out_w = box.width;
			out_h = box.height;
		}
		if (output->wlr_output != NULL) {
			scale = output->wlr_output->scale;
			if (scale < 1.0) {
				scale = 1.0;
			}
			/* Crisp integer buffer scale (ceil). */
			scale = (double)((int)(scale + 0.999));
		}
	}
	const int inner = cfg->corner_radius;
	const int border = cfg->border_thickness > 0 ? cfg->border_thickness : 0;
	const int outer = sf_outer_corner_radius(inner, border);
	const float sigma = cfg->shadow_blur_sigma > 0.0f ? cfg->shadow_blur_sigma : 0.0f;
	/* Height budget only — never shrink width below content + pads. */
	int margin = 120;
	int max_panel_h = out_h - margin;
	if (max_panel_h < 200) {
		max_panel_h = out_h > 100 ? out_h - 40 : 400;
	}

	int lh = line_height();
	/* Block sizes: groups held whole (header + rows + optional spacer). */
	int block_sizes[SF_CS_GROUP_COUNT + 1];
	size_t n_blocks = 0;
	size_t block_starts[SF_CS_GROUP_COUNT + 1];
	{
		size_t i = 0;
		while (i < n_lines && n_blocks < SF_CS_GROUP_COUNT + 1) {
			block_starts[n_blocks] = i;
			enum sf_cheatsheet_group g = lines[i].group;
			int count = 0;
			while (i < n_lines && lines[i].group == g) {
				count++;
				i++;
			}
			block_sizes[n_blocks++] = count;
		}
	}

	int chrome_lines = 2; /* title + footer */
	int chrome_h = chrome_lines * lh + cs_title_gap + cs_footer_gap + 2 * cs_pad;
	int max_body_h = max_panel_h - chrome_h;
	if (max_body_h < lh * 4) {
		max_body_h = lh * 4;
	}
	int limit_lines = max_body_h / lh;
	if (limit_lines < 1) {
		limit_lines = 1;
	}

	int best_cols = 1;
	int best_h = balanced_height(block_sizes, n_blocks, 1);
	for (int cols = 2; cols <= cs_max_cols; cols++) {
		int h = balanced_height(block_sizes, n_blocks, cols);
		if (h < best_h) {
			best_h = h;
			best_cols = cols;
		}
		if (h <= limit_lines) {
			best_cols = cols;
			best_h = h;
			break;
		}
	}
	int col_limit = best_h;
	if (col_limit < 1) {
		col_limit = limit_lines;
	}

	/* Assign blocks to columns. */
	struct col_slice cols[cs_max_cols];
	memset(cols, 0, sizeof cols);
	int n_cols = 0;
	int used = 0;
	size_t line_i = 0;
	for (size_t b = 0; b < n_blocks; b++) {
		if (n_cols == 0 ||
		    (used > 0 && used + block_sizes[b] > col_limit && n_cols < best_cols)) {
			if (n_cols > 0) {
				n_cols++;
			} else {
				n_cols = 1;
			}
			if (n_cols > best_cols) {
				n_cols = best_cols;
			}
			cols[n_cols - 1].start = block_starts[b];
			cols[n_cols - 1].count = 0;
			used = 0;
		}
		cols[n_cols - 1].count += (size_t)block_sizes[b];
		used += block_sizes[b];
		line_i += (size_t)block_sizes[b];
	}
	(void)line_i;
	if (n_cols < 1) {
		n_cols = 1;
		cols[0].start = 0;
		cols[0].count = n_lines;
	}

	/* Per-column max chord width and total width. */
	int body_w = 0;
	int body_h = 0;
	int max_chords[cs_max_cols];
	memset(max_chords, 0, sizeof max_chords);
	body_w = 0;
	body_h = 0;
	for (int c = 0; c < n_cols; c++) {
		int max_chord = 0;
		for (size_t i = 0; i < cols[c].count; i++) {
			int cw = 0, lw = 0;
			measure_line(&lines[cols[c].start + i], &cw, &lw);
			if (cw > max_chord) {
				max_chord = cw;
			}
		}
		max_chords[c] = max_chord;
		int col_w = 0;
		int col_h = 0;
		for (size_t i = 0; i < cols[c].count; i++) {
			const struct display_line *L = &lines[cols[c].start + i];
			int cw = 0, lw = 0;
			measure_line(L, &cw, &lw);
			int w = L->is_header ? lw : max_chord + 10 + lw;
			if (w > col_w) {
				col_w = w;
			}
			col_h += lh;
		}
		cols[c].width = col_w;
		if (col_h > body_h) {
			body_h = col_h;
		}
		body_w += col_w;
		if (c + 1 < n_cols) {
			body_w += cs_col_gap;
		}
	}

	struct sf_ui_text_metrics title_m = {0}, foot_m = {0};
	const char *title = "Keybinds";
	/* Keep the footer shorter than the body so it does not force a wide panel
	 * (or get clamped into the right pad). Umbriel: "press any key to close". */
	const char *footer = "Super+Shift+/ · press any key to close";
	(void)sf_ui_text_measure(title, &title_m);
	(void)sf_ui_text_measure(footer, &foot_m);

	/* Panel width is content + pads on both sides — never shrink below that. */
	int content_w = body_w;
	if (title_m.width > content_w) {
		content_w = title_m.width;
	}
	if (foot_m.width > content_w) {
		content_w = foot_m.width;
	}
	int panel_w = content_w + 2 * cs_pad;
	int panel_h = chrome_h + body_h;
	if (panel_h > max_panel_h) {
		panel_h = max_panel_h;
	}

	/*
	 * Paint in logical pixels (match SSD). Output scale is handled by the
	 * scene / backend; keep scale for a future crisp buffer path.
	 */
	(void)scale;
	int buf_w = panel_w < 1 ? 1 : panel_w;
	int buf_h = panel_h < 1 ? 1 : panel_h;

	struct sf_ui_buffer *ubuf = ui_buffer_create(buf_w, buf_h);
	if (ubuf == NULL) {
		free(lines);
		return;
	}
	uint8_t *pix = ubuf->data;
	/* Translucent fill — SceneFX blur plate sits under this buffer. */
	fill_rect(pix, buf_w, buf_h, 0, 0, buf_w, buf_h, col_bg);

	int y = cs_pad;
	int title_base = y + title_m.ascent;
	sf_ui_text_paint(pix, buf_w, buf_h, cs_pad, title_base, title, col_title);
	y += lh + cs_title_gap;

	int x = cs_pad;
	for (int c = 0; c < n_cols; c++) {
		int row_y = y;
		for (size_t i = 0; i < cols[c].count; i++) {
			if (row_y + lh > panel_h - cs_pad - lh) {
				break;
			}
			paint_line(pix, buf_w, buf_h, x, row_y, lh, &lines[cols[c].start + i],
				   max_chords[c]);
			row_y += lh;
		}
		x += cols[c].width + cs_col_gap;
	}

	int foot_y = panel_h - cs_pad - lh;
	sf_ui_text_paint(pix, buf_w, buf_h, cs_pad, foot_y + foot_m.ascent, footer, col_muted);

	server->cheatsheet.tree = wlr_scene_tree_create(server->layers.overlay);
	if (server->cheatsheet.tree == NULL) {
		wlr_buffer_drop(&ubuf->base);
		free(lines);
		return;
	}
	struct wlr_scene_tree *tree = server->cheatsheet.tree;

	/* Float chrome stack (bottom → top): blur → shadow → content → rim. */
	struct wlr_scene_blur *blur = NULL;
	if (cfg->window_blur) {
		blur = wlr_scene_blur_create(tree, panel_w, panel_h);
		if (blur != NULL) {
			wlr_scene_blur_set_should_only_blur_bottom_layer(blur, cfg->blur_xray);
			wlr_scene_blur_set_corner_radii(blur, corner_radii_all(inner));
			wlr_scene_node_set_position(&blur->node, 0, 0);
		}
	}

	struct wlr_scene_shadow *shadow = NULL;
	if (sigma > 0.0f) {
		struct sf_rgba shadow_c;
		sf_rgba_premul(&cfg->shadow_color, 1.0f, &shadow_c);
		int sw = panel_w + 2 * border + 2 * (int)sigma;
		int sh = panel_h + 2 * border + 2 * (int)sigma;
		shadow =
			wlr_scene_shadow_create(tree, sw, sh, outer, sigma, sf_rgba_vec(&shadow_c));
		if (shadow != NULL) {
			wlr_scene_node_set_position(&shadow->node, -border - (int)sigma,
						    -border - (int)sigma);
			wlr_scene_shadow_set_clipped_region(
				shadow, (struct clipped_region){
						.area =
							{
								.x = (int)sigma,
								.y = (int)sigma,
								.width = panel_w + 2 * border,
								.height = panel_h + 2 * border,
							},
						.corners = corner_radii_all(outer),
					});
		}
	}

	server->cheatsheet.buffer = &ubuf->base;
	server->cheatsheet.scene_buf = wlr_scene_buffer_create(tree, server->cheatsheet.buffer);
	if (server->cheatsheet.scene_buf == NULL) {
		cheatsheet_destroy_tree(server);
		free(lines);
		return;
	}
	wlr_scene_buffer_set_dest_size(server->cheatsheet.scene_buf, panel_w, panel_h);
	wlr_scene_buffer_set_corner_radii(server->cheatsheet.scene_buf, corner_radii_all(inner));

	struct wlr_scene_rect *rim = NULL;
	if (border > 0) {
		struct sf_rgba rim_c;
		sf_rgba_premul(&cfg->border_float_active, 1.0f, &rim_c);
		rim = wlr_scene_rect_create(tree, panel_w + 2 * border, panel_h + 2 * border,
					    sf_rgba_vec(&rim_c));
		if (rim != NULL) {
			wlr_scene_rect_set_corner_radii(rim, corner_radii_all(outer));
			wlr_scene_node_set_position(&rim->node, -border, -border);
			wlr_scene_rect_set_clipped_region(
				rim, (struct clipped_region){
					     .area =
						     {
							     .x = border,
							     .y = border,
							     .width = panel_w,
							     .height = panel_h,
						     },
					     .corners = corner_radii_all(inner),
				     });
			wlr_scene_node_raise_to_top(&rim->node);
		}
	}

	/* Restack: blur bottom, then shadow, content, rim on top. */
	if (blur != NULL) {
		wlr_scene_node_lower_to_bottom(&blur->node);
	}
	if (shadow != NULL) {
		wlr_scene_node_raise_to_top(&shadow->node);
	}
	wlr_scene_node_raise_to_top(&server->cheatsheet.scene_buf->node);
	if (rim != NULL) {
		wlr_scene_node_raise_to_top(&rim->node);
	}

	/*
	 * Optical center on the full output box. Do not pin to the top-left when
	 * the panel is larger than the output (common in a nested guest window):
	 * negative offsets crop evenly so the panel stays centered.
	 */
	int pos_x = out_x + (out_w - panel_w) / 2;
	int pos_y = out_y + (out_h - panel_h) / 2;
	wlr_scene_node_set_position(&tree->node, pos_x, pos_y);
	wlr_scene_node_raise_to_top(&tree->node);

	free(lines);
}

bool sf_cheatsheet_is_action(enum sf_action action) {
	return action == SF_ACTION_CHEATSHEET_TOGGLE || action == SF_ACTION_CHEATSHEET_OPEN ||
	       action == SF_ACTION_CHEATSHEET_CLOSE;
}

bool sf_cheatsheet_visible(const struct sf_server *server) {
	return server != NULL && server->cheatsheet.tree != NULL;
}

void sf_cheatsheet_hide(struct sf_server *server) {
	if (server == NULL) {
		return;
	}
	server->cheatsheet.show_pending = false;
	cheatsheet_destroy_tree(server);
}

void sf_cheatsheet_show(struct sf_server *server) {
	if (server == NULL) {
		return;
	}
	server->cheatsheet.show_pending = false;
	if (sf_session_locked(server)) {
		return;
	}
	cheatsheet_render(server);
}

void sf_cheatsheet_toggle(struct sf_server *server) {
	if (sf_cheatsheet_visible(server)) {
		sf_cheatsheet_hide(server);
	} else {
		sf_cheatsheet_show(server);
	}
}

void sf_cheatsheet_show_on_startup(struct sf_server *server) {
	if (server == NULL) {
		return;
	}
	if (!sf_cfg(server)->show_cheatsheet) {
		return;
	}
	if (sf_session_locked(server)) {
		server->cheatsheet.show_pending = true;
		return;
	}
	sf_cheatsheet_show(server);
}

void sf_cheatsheet_on_config_reload(struct sf_server *server) {
	if (server == NULL) {
		return;
	}
	if (!sf_cfg(server)->show_cheatsheet && sf_cheatsheet_visible(server)) {
		sf_cheatsheet_hide(server);
		return;
	}
	if (sf_cheatsheet_visible(server)) {
		cheatsheet_render(server);
	}
}

void sf_cheatsheet_on_output_change(struct sf_server *server) {
	if (server == NULL || !sf_cheatsheet_visible(server)) {
		return;
	}
	cheatsheet_render(server);
}

void sf_cheatsheet_on_lock(struct sf_server *server) {
	sf_cheatsheet_hide(server);
}

void sf_cheatsheet_dismiss_key(struct sf_server *server, enum sf_action matched,
			       bool modifier_only) {
	if (!sf_cheatsheet_visible(server) || modifier_only) {
		return;
	}
	if (sf_cheatsheet_is_action(matched)) {
		return;
	}
	sf_cheatsheet_hide(server);
}

void sf_cheatsheet_dismiss_pointer(struct sf_server *server, enum sf_action matched) {
	if (!sf_cheatsheet_visible(server)) {
		return;
	}
	if (sf_cheatsheet_is_action(matched)) {
		return;
	}
	sf_cheatsheet_hide(server);
}
