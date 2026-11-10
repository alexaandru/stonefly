#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <scenefx/types/fx/clipped_region.h>
#include <scenefx/types/wlr_scene.h>

#include "config/config.h"
#include "decor/decor.h"
#include "log/log.h"
#include "ssd/ssd.h"
#include "ssd/ssd_layout.h"
#include "ssd/ssd_paint.h"
#include "ssd/ssd_policy.h"
#include "util/color.h"
#include "util/time.h"
#include "wm/wm.h"
#include "xdg/xdg.h"

enum {
	ssd_dblclick_ms = 400,
};

struct sf_ssd_buffer {
	struct wlr_buffer base;
	void *data;
	size_t stride;
};

static void ssd_buffer_destroy(struct wlr_buffer *wlr_buffer) {
	struct sf_ssd_buffer *buf = wl_container_of(wlr_buffer, buf, base);
	free(buf->data);
	free(buf);
}

/* 'AR24' LE — same bits as DRM_FORMAT_ARGB8888; avoid fourcc_code (signed
 * char shifts trip clang-tidy bugprone-signed-bitwise). */
static const uint32_t ssd_drm_format_argb8888 = 0x34325241u;

static bool ssd_buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buffer, uint32_t flags,
					     void **data, uint32_t *format, size_t *stride) {
	struct sf_ssd_buffer *buf = wl_container_of(wlr_buffer, buf, base);
	if ((flags & (uint32_t)WLR_BUFFER_DATA_PTR_ACCESS_WRITE) != 0) {
		return false;
	}
	*format = ssd_drm_format_argb8888;
	*data = buf->data;
	*stride = buf->stride;
	return true;
}

static void ssd_buffer_end_data_ptr_access(struct wlr_buffer *wlr_buffer) {
	(void)wlr_buffer;
}

static const struct wlr_buffer_impl ssd_buffer_impl = {
	.destroy = ssd_buffer_destroy,
	.begin_data_ptr_access = ssd_buffer_begin_data_ptr_access,
	.end_data_ptr_access = ssd_buffer_end_data_ptr_access,
};

static struct sf_ssd_buffer *ssd_buffer_create(int width, int height) {
	struct sf_ssd_buffer *buf = calloc(1, sizeof(*buf));
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
	wlr_buffer_init(&buf->base, &ssd_buffer_impl, width, height);
	return buf;
}

/* Pointer press state for titlebar buttons / double-click. */
static struct {
	struct sf_toplevel *toplevel;
	enum sf_ssd_button pressed;
	bool press_title;
	struct timespec last_click;
	struct sf_toplevel *last_bar;
} ssd_ptr;

void sf_ssd_setup(struct sf_server *server) {
	const struct sf_config *cfg = sf_cfg(server);
	sf_ssd_paint_ensure_font(cfg->ssd_font_size);
}

void sf_ssd_finish(struct sf_server *server) {
	(void)server;
	memset(&ssd_ptr, 0, sizeof(ssd_ptr));
	sf_ssd_paint_fini();
}

bool sf_ssd_should_show(const struct sf_toplevel *toplevel) {
	if (toplevel == NULL || !toplevel->want_ssd) {
		return false;
	}
	if (sf_layout_sm_is_fullscreen(&toplevel->layout) ||
	    sf_layout_sm_is_max_edges(&toplevel->layout) || toplevel->minimized) {
		return false;
	}
	return true;
}

int sf_ssd_top_inset(const struct sf_toplevel *toplevel) {
	if (!sf_ssd_should_show(toplevel)) {
		return 0;
	}
	int h = sf_cfg(toplevel->server)->ssd_height;
	return h < 16 ? 16 : h;
}

static void float_to_bgra_premul(const struct sf_rgba *c, uint8_t out[4]) {
	float a = c->a;
	if (a < 0.0f) {
		a = 0.0f;
	}
	if (a > 1.0f) {
		a = 1.0f;
	}
	out[0] = (uint8_t)(c->b * a * 255.0f + 0.5f);
	out[1] = (uint8_t)(c->g * a * 255.0f + 0.5f);
	out[2] = (uint8_t)(c->r * a * 255.0f + 0.5f);
	out[3] = (uint8_t)(a * 255.0f + 0.5f);
}

static void ssd_fill_color(struct sf_toplevel *toplevel, bool focused, uint8_t out[4]) {
	const struct sf_config *cfg = sf_cfg(toplevel->server);
	const struct sf_rgba *src = focused ? &cfg->border_active : &cfg->border_inactive;
	float_to_bgra_premul(src, out);
}

void sf_ssd_destroy(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->ssd == NULL) {
		return;
	}
	struct sf_ssd *ssd = toplevel->ssd;
	if (ssd_ptr.toplevel == toplevel) {
		ssd_ptr.toplevel = NULL;
		ssd_ptr.pressed = SF_SSD_BTN_NONE;
		ssd_ptr.press_title = false;
	}
	if (ssd_ptr.last_bar == toplevel) {
		ssd_ptr.last_bar = NULL;
	}
	if (ssd->scene_buf != NULL) {
		wlr_scene_node_destroy(&ssd->scene_buf->node);
		ssd->scene_buf = NULL;
	}
	if (ssd->buffer != NULL) {
		wlr_buffer_drop(ssd->buffer);
		ssd->buffer = NULL;
		ssd->pixels = NULL;
	}
	free(ssd);
	toplevel->ssd = NULL;
}

static bool ssd_content_size(struct sf_toplevel *toplevel, int *out_w, int *out_h) {
	struct wlr_box *geo = &toplevel->xdg_toplevel->base->geometry;
	int w = geo->width;
	int h = geo->height;
	if (w <= 0 || h <= 0) {
		struct wlr_surface *surf = toplevel->xdg_toplevel->base->surface;
		if (surf != NULL) {
			w = surf->current.width;
			h = surf->current.height;
		}
	}
	if (w <= 0 || h <= 0) {
		return false;
	}
	*out_w = w;
	*out_h = h;
	return true;
}

void sf_ssd_update(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->scene_tree == NULL) {
		return;
	}

	bool show = sf_ssd_should_show(toplevel);
	if (toplevel->decoration != NULL && toplevel->decoration->toplevel != NULL &&
	    toplevel->decoration->toplevel->base->initialized) {
		enum wlr_xdg_toplevel_decoration_v1_mode set_mode =
			show ? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
			     : WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
		if (toplevel->ssd_set_mode != (int)set_mode) {
			wlr_xdg_toplevel_decoration_v1_set_mode(toplevel->decoration, set_mode);
			toplevel->ssd_set_mode = (int)set_mode;
		}
	}

	if (!show) {
		sf_ssd_destroy(toplevel);
		return;
	}

	int content_w, content_h;
	if (!ssd_content_size(toplevel, &content_w, &content_h)) {
		return;
	}

	const struct sf_config *cfg = sf_cfg(toplevel->server);
	int border = cfg->border_thickness;
	if (border < 0) {
		border = 0;
	}
	int height = cfg->ssd_height < 16 ? 16 : cfg->ssd_height;
	int width = content_w + 2 * border;
	if (width < 1) {
		width = 1;
	}

	struct sf_ssd *ssd = toplevel->ssd;
	if (ssd == NULL) {
		ssd = calloc(1, sizeof(*ssd));
		if (ssd == NULL) {
			return;
		}
		ssd->kind = SF_NODE_SSD;
		ssd->toplevel = toplevel;
		toplevel->ssd = ssd;
	}

	bool need_buf = ssd->buffer == NULL || ssd->width != width || ssd->height != height;
	if (need_buf) {
		if (ssd->scene_buf != NULL) {
			wlr_scene_node_destroy(&ssd->scene_buf->node);
			ssd->scene_buf = NULL;
		}
		if (ssd->buffer != NULL) {
			wlr_buffer_drop(ssd->buffer);
			ssd->buffer = NULL;
			ssd->pixels = NULL;
		}
		struct sf_ssd_buffer *buf = ssd_buffer_create(width, height);
		if (buf == NULL) {
			sf_ssd_destroy(toplevel);
			return;
		}
		ssd->buffer = &buf->base;
		ssd->pixels = buf->data;
		ssd->width = width;
		ssd->height = height;
		ssd->scene_buf = wlr_scene_buffer_create(toplevel->scene_tree, ssd->buffer);
		if (ssd->scene_buf == NULL) {
			sf_ssd_destroy(toplevel);
			return;
		}
		ssd->scene_buf->node.data = ssd;
	}

	struct sf_ssd_layout lay;
	sf_ssd_layout_title(width, height, &lay);

	sf_ssd_paint_ensure_font(cfg->ssd_font_size);

	bool focused = sf_focused_toplevel(toplevel->server) == toplevel;
	uint8_t fill[4];
	ssd_fill_color(toplevel, focused, fill);
	const char *title = toplevel->xdg_toplevel->title;
	bool maximized = sf_layout_sm_is_max_gaps(&toplevel->layout) ||
			 sf_layout_sm_is_max_edges(&toplevel->layout);
	sf_ssd_paint_titlebar(ssd->pixels, width, height, title, focused, maximized, fill, &lay);

	/* Force scene to re-upload after CPU paint (same buffer pointer). */
	wlr_scene_buffer_set_buffer(ssd->scene_buf, NULL);
	wlr_scene_buffer_set_buffer(ssd->scene_buf, ssd->buffer);

	wlr_scene_node_set_position(&ssd->scene_buf->node, -border, -height);
	/*
	 * SSD spans the outer chrome width under the border rim. Round top
	 * corners to the outer radius so the bar matches the rim silhouette;
	 * then raise the border above the SSD so urgency/focus color is not
	 * clipped by the titlebar.
	 */
	int inner = cfg->corner_radius;
	int outer = inner > 0 ? inner + border : 0;
	wlr_scene_buffer_set_corner_radii(ssd->scene_buf, corner_radii_top(outer));
	wlr_scene_node_raise_to_top(&ssd->scene_buf->node);
	if (toplevel->border != NULL) {
		wlr_scene_node_raise_to_top(&toplevel->border->node);
	}
	wlr_scene_node_set_enabled(&ssd->scene_buf->node, true);
	ssd->visible = true;
}

void sf_ssd_set_focused(struct sf_toplevel *toplevel, bool focused) {
	if (toplevel == NULL || toplevel->ssd == NULL || !toplevel->ssd->visible ||
	    toplevel->ssd->pixels == NULL) {
		return;
	}
	struct sf_ssd *ssd = toplevel->ssd;
	struct sf_ssd_layout lay;
	sf_ssd_layout_title(ssd->width, ssd->height, &lay);
	sf_ssd_paint_ensure_font(sf_cfg(toplevel->server)->ssd_font_size);
	uint8_t fill[4];
	ssd_fill_color(toplevel, focused, fill);
	const char *title = toplevel->xdg_toplevel->title;
	bool maximized = sf_layout_sm_is_max_gaps(&toplevel->layout) ||
			 sf_layout_sm_is_max_edges(&toplevel->layout);
	sf_ssd_paint_titlebar(ssd->pixels, ssd->width, ssd->height, title, focused, maximized, fill,
			      &lay);
	wlr_scene_buffer_set_buffer(ssd->scene_buf, NULL);
	wlr_scene_buffer_set_buffer(ssd->scene_buf, ssd->buffer);
}

void sf_ssd_decoration_apply(struct sf_toplevel *toplevel,
			     struct wlr_xdg_toplevel_decoration_v1 *wlr_deco) {
	if (wlr_deco == NULL || wlr_deco->toplevel == NULL ||
	    !wlr_deco->toplevel->base->initialized) {
		return;
	}

	if (toplevel == NULL) {
		/* Decoration before toplevel link; CSD until apply retries. */
		wlr_xdg_toplevel_decoration_v1_set_mode(
			wlr_deco, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
		return;
	}

	const struct sf_config *cfg = sf_cfg(toplevel->server);
	enum sf_ssd_mode mode = toplevel->rule_ssd_set ? toplevel->rule_ssd_mode : cfg->ssd_mode;
	int requested = (int)wlr_deco->requested_mode;
	bool want = sf_ssd_want(mode, requested);

	toplevel->want_ssd = want;
	toplevel->decoration = wlr_deco;
	/* Mode flip + titlebar live in ssd_update (via decor_update_geom). */
	sf_decor_update_geom(toplevel);
	if (sf_ssd_should_show(toplevel)) {
		sf_toplevel_ensure_on_screen(toplevel);
	}
}

bool sf_ssd_node_is_titlebar(struct wlr_scene_node *node, struct sf_toplevel **out_toplevel) {
	if (node == NULL || node->data == NULL) {
		return false;
	}
	enum sf_node_kind kind = *(enum sf_node_kind *)node->data;
	if (kind != SF_NODE_SSD) {
		return false;
	}
	struct sf_ssd *ssd = node->data;
	if (out_toplevel != NULL) {
		*out_toplevel = ssd->toplevel;
	}
	return true;
}

void sf_ssd_pointer_reset(void) {
	ssd_ptr.toplevel = NULL;
	ssd_ptr.pressed = SF_SSD_BTN_NONE;
	ssd_ptr.press_title = false;
}

bool sf_ssd_pointer_button(struct sf_server *server, struct sf_toplevel *toplevel, double sx,
			   double sy, const struct wlr_pointer_button_event *event) {
	if (server == NULL || toplevel == NULL || toplevel->ssd == NULL || event == NULL) {
		return false;
	}

	struct sf_ssd_layout lay;
	sf_ssd_layout_title(toplevel->ssd->width, toplevel->ssd->height, &lay);
	int lx = (int)(sx + 0.5);
	int ly = (int)(sy + 0.5);
	enum sf_ssd_button hit = sf_ssd_layout_hit(&lay, lx, ly);

	if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
		if (event->button == BTN_RIGHT) {
			/* Winmenu not implemented yet. */
			sf_focus_toplevel(toplevel);
			return true;
		}
		if (event->button != BTN_LEFT) {
			return true;
		}
		sf_focus_toplevel(toplevel);
		ssd_ptr.toplevel = toplevel;
		ssd_ptr.pressed = hit;
		ssd_ptr.press_title = hit == SF_SSD_BTN_NONE;
		if (ssd_ptr.press_title) {
			struct timespec now;
			clock_gettime(CLOCK_MONOTONIC, &now);
			if (ssd_ptr.last_bar == toplevel) {
				int64_t dt =
					sf_timespec_ms(&now) - sf_timespec_ms(&ssd_ptr.last_click);
				if (dt >= 0 && dt < ssd_dblclick_ms) {
					ssd_ptr.last_click = (struct timespec){0};
					ssd_ptr.last_bar = NULL;
					ssd_ptr.press_title = false;
					ssd_ptr.pressed = SF_SSD_BTN_NONE;
					sf_toplevel_toggle_max_gaps(toplevel);
					return true;
				}
			}
			ssd_ptr.last_click = now;
			ssd_ptr.last_bar = toplevel;
			sf_begin_interactive(toplevel, SF_CURSOR_MOVE, 0);
		}
		return true;
	}

	if (event->state != WL_POINTER_BUTTON_STATE_RELEASED || event->button != BTN_LEFT) {
		return false;
	}

	enum sf_ssd_button pressed = ssd_ptr.pressed;
	bool same = ssd_ptr.toplevel == toplevel;
	ssd_ptr.pressed = SF_SSD_BTN_NONE;
	ssd_ptr.press_title = false;
	ssd_ptr.toplevel = NULL;

	if (!same || pressed == SF_SSD_BTN_NONE || pressed != hit) {
		return true;
	}

	switch (pressed) {
	case SF_SSD_BTN_CLOSE:
		wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);
		break;
	case SF_SSD_BTN_MINIMIZE:
		sf_toplevel_minimize(toplevel);
		break;
	case SF_SSD_BTN_MAXIMIZE:
		sf_toplevel_toggle_max_gaps(toplevel);
		break;
	case SF_SSD_BTN_NONE:
		break;
	}
	return true;
}
