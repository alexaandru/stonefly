#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>
#include <scenefx/types/fx/clipped_region.h>
#include <scenefx/types/wlr_scene.h>

#include "anim/anim.h"
#include "config/config.h"
#include "decor/decor.h"
#include "layer/layer.h"
#include "log/log.h"
#include "output/output.h"
#include "server/server.h"
#include "ssd/ssd.h"
#include "util/color.h"
#include "util/math.h"
#include "util/time.h"
#include "toplevel/toplevel.h"
#include "wm/geom.h"
#include "wm/layout_sm.h"
#include "wm/wm.h"
#include "wm/workspace.h"

#include <wlr/types/wlr_buffer.h>

struct sf_ghost {
	struct wl_list link;
	struct sf_server *server;
	struct wlr_scene_tree *tree;
	struct wlr_scene_buffer *buffer;
	struct wlr_buffer *locked;
	int width, height;
	int resting_x, resting_y;
	bool anim_active;
	bool anim_hide;
	bool anim_pop; /* tiled close: scale+fade instead of edge slide */
	struct timespec anim_start;
	int64_t anim_dur_ns;
	int anim_from_x, anim_from_y;
	int anim_to_x, anim_to_y;
	int anim_from_w, anim_from_h;
	int anim_to_w, anim_to_h;
	float anim_fade_from;
	float anim_fade_to;
	float base_opacity;
};

void sf_decor_setup_scene(struct sf_server *server) {
	/* Blur node is created in sf_layer_setup (z-order). */
	(void)server;
}

void sf_decor_apply_blur_params(struct sf_server *server) {
	if (server == NULL || server->scene == NULL) {
		return;
	}
	const struct sf_config *cfg = sf_cfg(server);
	int passes = cfg->blur_num_passes;
	int radius = cfg->blur_radius;
	if (passes < 1) {
		passes = 1;
	}
	if (radius < 1) {
		radius = 1;
	}
	wlr_scene_set_blur_data(server->scene, passes, radius, cfg->blur_noise,
				cfg->blur_brightness, cfg->blur_contrast, cfg->blur_saturation);
}

void sf_decor_update_blur_size(struct sf_server *server) {
	if (server->blur_layer == NULL) {
		return;
	}
	struct wlr_box layout = {0};
	wlr_output_layout_get_box(server->output_layout, NULL, &layout);
	if (layout.width <= 0 || layout.height <= 0) {
		return;
	}
	/* Match mango: optimized blur covers the layout box, not just (0,0). */
	wlr_scene_node_set_position(&server->blur_layer->node, layout.x, layout.y);
	wlr_scene_optimized_blur_set_size(server->blur_layer, layout.width, layout.height);
}

void sf_decor_mark_blur_dirty(struct sf_server *server) {
	if (server == NULL || server->blur_layer == NULL) {
		return;
	}
	wlr_scene_optimized_blur_mark_dirty(server->blur_layer);
}

/* Border/shadow/corners off for edge-max, fullscreen, window_rule borderless,
 * and floats flush with any usable edge (no room for chrome on all sides).
 * Blur is separate (stays on for edge-max / quick terminal; off only for true
 * fullscreen). */
bool sf_toplevel_room_for_chrome(const struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->server == NULL || toplevel->scene_tree == NULL ||
	    toplevel->xdg_toplevel == NULL) {
		return true;
	}
	/* Tiled strip cells keep borders even when a lone column fills usable. */
	if (toplevel->arrange_managed) {
		return true;
	}
	if (sf_layout_sm_is_max_edges(&toplevel->layout) ||
	    sf_layout_sm_is_fullscreen(&toplevel->layout)) {
		return false;
	}
	struct wlr_box geom = {
		.x = toplevel->scene_tree->node.x,
		.y = toplevel->scene_tree->node.y,
		.width = toplevel->xdg_toplevel->base->geometry.width,
		.height = toplevel->xdg_toplevel->base->geometry.height,
	};
	if (geom.width <= 0 || geom.height <= 0) {
		geom.width = toplevel->xdg_toplevel->base->surface->current.width;
		geom.height = toplevel->xdg_toplevel->base->surface->current.height;
	}
	if (geom.width <= 0 || geom.height <= 0) {
		return true;
	}
	double cx = geom.x + geom.width / 2.0;
	double cy = geom.y + geom.height / 2.0;
	struct wlr_box usable;
	if (!sf_layout_usable_at(toplevel->server, cx, cy, &usable) || usable.width <= 0 ||
	    usable.height <= 0) {
		return true;
	}
	const struct sf_config *cfg = sf_cfg(toplevel->server);
	int need = cfg->border_thickness;
	if (need < 1) {
		need = 1;
	}
	const int tol = 2;
	if (geom.x < usable.x + need - tol) {
		return false;
	}
	if (geom.y < usable.y + need - tol) {
		return false;
	}
	if (geom.x + geom.width > usable.x + usable.width - need + tol) {
		return false;
	}
	if (geom.y + geom.height > usable.y + usable.height - need + tol) {
		return false;
	}
	return true;
}

static bool decor_chrome_on(const struct sf_toplevel *toplevel) {
	return !sf_layout_sm_is_max_edges(&toplevel->layout) &&
	       !sf_layout_sm_is_fullscreen(&toplevel->layout) && !toplevel->borderless &&
	       sf_toplevel_room_for_chrome(toplevel);
}

static bool decor_blur_on(const struct sf_toplevel *toplevel) {
	const struct sf_config *cfg = sf_cfg(toplevel->server);
	return (cfg->window_blur || toplevel->client_blur) &&
	       !sf_layout_sm_is_fullscreen(&toplevel->layout);
}

/*
 * Config corner_radius matches CSD content rounding (Ghostty/GTK). The neon
 * rim sits outside that, so outer = inner + border (dwl/scenefx convention).
 * Using outer=config and hole=config-border left a crescent: hole cut inside
 * the client's opaque arc and showed wallpaper through CSD AA.
 * border_overlap_px: pull the hole in so the rim covers the 1px AA hairline.
 */
static int outer_corner_radius(int inner, int border) {
	return inner > 0 ? inner + border : 0;
}

enum {
	border_overlap_px = 1
};

void sf_decor_refresh_blur(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->blur == NULL) {
		return;
	}
	wlr_scene_node_set_enabled(&toplevel->blur->node, decor_blur_on(toplevel));
}

static void decor_set_chrome_enabled(struct sf_toplevel *toplevel, bool on) {
	sf_decor_refresh_blur(toplevel);
	if (toplevel->border != NULL) {
		wlr_scene_node_set_enabled(&toplevel->border->node, on);
	}
	if (toplevel->shadow != NULL) {
		wlr_scene_node_set_enabled(&toplevel->shadow->node, on);
	}
}

/*
 * Which outer window corners this buffer covers. Geometry TL is at (0,0) in
 * xdg_scene_tree space (see decor_update_geom). Foot CSD splits titlebar +
 * body into two buffers — corner_radii_all on both rounds the join and looks
 * like an hourglass; GTK often ships one buffer so all-four is fine.
 * When SSD is shown the visual top is the titlebar, so client top radii stay
 * 0 (same hourglass avoidance at the SSD/content join).
 */
static struct fx_corner_radii corners_for_buffer(int sx, int sy, int bw, int bh,
						 const struct wlr_box *geo, int radius,
						 bool round_top) {
	if (radius <= 0 || bw <= 0 || bh <= 0 || geo->width <= 0 || geo->height <= 0) {
		return corner_radii_none();
	}

	const int left = 0;
	const int top = 0;
	const int right = geo->width - 1;
	const int bottom = geo->height - 1;
	const int x1 = sx + bw;
	const int y1 = sy + bh;
	const int top_r = round_top ? radius : 0;

	return corner_radii_new(
		(left >= sx && left < x1 && top >= sy && top < y1) ? top_r : 0,
		(right >= sx && right < x1 && top >= sy && top < y1) ? top_r : 0,
		(right >= sx && right < x1 && bottom >= sy && bottom < y1) ? radius : 0,
		(left >= sx && left < x1 && bottom >= sy && bottom < y1) ? radius : 0);
}

/* Focused uses client_opacity; unfocused uses inactive_opacity.
 * Fullscreen stays opaque; edge-max (quick terminal) still gets opacity so
 * blur can show through. */
static float toplevel_chrome_opacity(struct sf_toplevel *toplevel, bool focused) {
	if (sf_layout_sm_is_fullscreen(&toplevel->layout)) {
		return 1.0f;
	}
	const struct sf_config *cfg = sf_cfg(toplevel->server);
	float base = focused ? cfg->client_opacity : cfg->inactive_opacity;
	if (toplevel->anim.active && toplevel->anim.pop) {
		float fade = toplevel->anim.fade;
		if (fade < 0.0f) {
			fade = 0.0f;
		}
		if (fade > 1.0f) {
			fade = 1.0f;
		}
		base *= fade;
	}
	return base;
}

struct apply_buffers_ctx {
	struct sf_toplevel *toplevel;
	float opacity;
	bool chrome;
	int radius;
	/* When >0, buffer is dest_sized to this presented box (pop / undersized
	 * tile stretch). Corner tests must use these extents, not geo — else
	 * radii drop off mid-pop and square client corners thin the border. */
	int present_w;
	int present_h;
};

static void iter_apply_buffers(struct wlr_scene_buffer *buffer, int sx, int sy, void *user_data) {
	struct apply_buffers_ctx *ctx = user_data;

	wlr_scene_buffer_set_opacity(buffer, ctx->opacity);

	int bw = buffer->dst_width;
	int bh = buffer->dst_height;
	if (bw <= 0 || bh <= 0) {
		struct wlr_scene_surface *ss = wlr_scene_surface_try_from_buffer(buffer);
		if (ss != NULL) {
			if (bw <= 0) {
				bw = ss->surface->current.width;
			}
			if (bh <= 0) {
				bh = ss->surface->current.height;
			}
		}
	}

	struct wlr_box *geo = &ctx->toplevel->xdg_toplevel->base->geometry;
	/* Same radius as CSD / border hole — do not use outer. */
	int radius = ctx->chrome ? ctx->radius : 0;
	bool round_top = !sf_ssd_should_show(ctx->toplevel);
	struct wlr_box corner_geo = *geo;
	if (ctx->present_w > 0 && ctx->present_h > 0) {
		struct wlr_scene_surface *ss = wlr_scene_surface_try_from_buffer(buffer);
		struct wlr_surface *root = ctx->toplevel->xdg_toplevel->base->surface;
		/* dest_size present box is smaller than geo mid-pop; coverage
		 * tests against geo->width then drop radii and square client
		 * corners sit on the rounded border rim. */
		if (ss != NULL && root != NULL && ss->surface == root) {
			corner_geo.width = ctx->present_w;
			corner_geo.height = ctx->present_h;
			sx = 0;
			sy = 0;
			bw = ctx->present_w;
			bh = ctx->present_h;
		}
	}
	wlr_scene_buffer_set_corner_radii(
		buffer, corners_for_buffer(sx, sy, bw, bh, &corner_geo, radius, round_top));
}

static void decor_apply_buffers_focused(struct sf_toplevel *toplevel, bool focused) {
	if (toplevel->xdg_scene_tree == NULL) {
		return;
	}
	bool chrome = decor_chrome_on(toplevel);
	decor_set_chrome_enabled(toplevel, chrome);
	const struct sf_config *cfg = sf_cfg(toplevel->server);
	int present_w = 0;
	int present_h = 0;
	if (toplevel->anim.active && toplevel->anim.pop && toplevel->anim.prop_w > 0 &&
	    toplevel->anim.prop_h > 0) {
		present_w = toplevel->anim.prop_w;
		present_h = toplevel->anim.prop_h;
	}
	struct apply_buffers_ctx ctx = {
		.toplevel = toplevel,
		.opacity = toplevel_chrome_opacity(toplevel, focused),
		.chrome = chrome,
		.radius = cfg->corner_radius,
		.present_w = present_w,
		.present_h = present_h,
	};
	wlr_scene_node_for_each_buffer(&toplevel->xdg_scene_tree->node, iter_apply_buffers, &ctx);
}

void sf_decor_apply_buffers(struct sf_toplevel *toplevel) {
	bool focused = sf_focused_toplevel(toplevel->server) == toplevel;
	decor_apply_buffers_focused(toplevel, focused);
}

/*
 * Umbriel presented_crop: map the window-geometry region of the committed
 * buffer onto the presented (scaled) box. A bare dest_size on the full
 * surface fights the xdg helper's (-geo.x,-geo.y) placement and leaves the
 * border growing ahead of the client (wallpaper gap).
 */
static struct wlr_fbox pop_cropped_source(const struct wlr_fbox *base,
					  const struct wlr_box *geometry,
					  const struct wlr_box *content, const struct wlr_box *clip,
					  int surface_w, int surface_h) {
	struct wlr_fbox empty = {0};
	if (content->width <= 0 || content->height <= 0 || geometry->width <= 0 ||
	    geometry->height <= 0 || surface_w <= 0 || surface_h <= 0 || clip->width <= 0 ||
	    clip->height <= 0) {
		return empty;
	}
	double fx = (double)geometry->width / (double)content->width;
	double fy = (double)geometry->height / (double)content->height;
	double sx = (double)geometry->x + ((double)clip->x - (double)geometry->x) * fx;
	double sy = (double)geometry->y + ((double)clip->y - (double)geometry->y) * fy;
	double sw = (double)clip->width * fx;
	double sh = (double)clip->height * fy;
	double bx = base->width / (double)surface_w;
	double by = base->height / (double)surface_h;
	struct wlr_fbox src = {
		.x = base->x + sx * bx,
		.y = base->y + sy * by,
		.width = sw * bx,
		.height = sh * by,
	};
	if (src.x < base->x) {
		src.width -= base->x - src.x;
		src.x = base->x;
	}
	if (src.y < base->y) {
		src.height -= base->y - src.y;
		src.y = base->y;
	}
	double max_w = base->x + base->width - src.x;
	double max_h = base->y + base->height - src.y;
	if (src.width > max_w) {
		src.width = max_w;
	}
	if (src.height > max_h) {
		src.height = max_h;
	}
	if (src.width <= 0.0 || src.height <= 0.0) {
		return empty;
	}
	return src;
}

static void decor_clip_xdg_surface(struct sf_toplevel *toplevel, const struct wlr_box *clip);

/*
 * Umbriel ViewPresentation::applyCrop / resetCrop.
 * umbrielfx has wlr_scene_surface_get_size (scene units = width/scale).
 * Stock SceneFX has no public scale field → scale 1.0 path only.
 */
static void sf_scene_surface_get_size(const struct wlr_scene_surface *ss, int *width, int *height) {
	*width = ss->surface->current.width;
	*height = ss->surface->current.height;
}

struct pop_dest_ctx {
	struct wlr_surface *root;
	struct wlr_box geometry;
	struct wlr_box content;
	struct wlr_box clip;
};

static void iter_pop_dest_size(struct wlr_scene_buffer *buffer, int sx, int sy, void *user_data) {
	(void)sx;
	(void)sy;
	struct pop_dest_ctx *ctx = user_data;
	struct wlr_scene_surface *ss = wlr_scene_surface_try_from_buffer(buffer);
	if (ss == NULL || ss->surface != ctx->root) {
		return;
	}
	struct wlr_fbox base = {0};
	wlr_surface_get_buffer_source_box(ss->surface, &base);
	/* Umbriel: geometry/content/clip are scene units; pass get_size, not
	 * raw surface->current when scale ≠ 1 (stock = 1.0). */
	int width = 0;
	int height = 0;
	sf_scene_surface_get_size(ss, &width, &height);
	struct wlr_fbox src =
		pop_cropped_source(&base, &ctx->geometry, &ctx->content, &ctx->clip, width, height);
	if (src.width <= 0.0 || src.height <= 0.0) {
		return;
	}
	wlr_scene_buffer_set_source_box(buffer, &src);
	wlr_scene_buffer_set_dest_size(buffer, ctx->clip.width, ctx->clip.height);
}

void sf_decor_pop_set_dest_size(struct sf_toplevel *toplevel, int present_w, int present_h) {
	if (toplevel == NULL || toplevel->xdg_scene_tree == NULL ||
	    toplevel->xdg_toplevel == NULL || toplevel->xdg_toplevel->base == NULL) {
		return;
	}
	if (present_w <= 0 || present_h <= 0) {
		return;
	}
	struct wlr_box *geo = &toplevel->xdg_toplevel->base->geometry;
	if (geo->width <= 0 || geo->height <= 0) {
		return;
	}
	struct pop_dest_ctx ctx = {
		.root = toplevel->xdg_toplevel->base->surface,
		.geometry = *geo,
		.content = {.x = 0, .y = 0, .width = present_w, .height = present_h},
		.clip = {.x = geo->x, .y = geo->y, .width = present_w, .height = present_h},
	};
	if (ctx.root == NULL) {
		return;
	}
	wlr_scene_node_for_each_buffer(&toplevel->xdg_scene_tree->node, iter_pop_dest_size, &ctx);
	toplevel->present_stretch = true;
}

static void pop_present_idle_cb(void *data) {
	struct sf_toplevel *toplevel = data;
	toplevel->pop_present_idle = NULL;
	toplevel->pop_idle_kind = 0;
	if (toplevel->scene_tree == NULL || !toplevel->anim.active || !toplevel->anim.pop) {
		return;
	}
	/* Re-apply after commit: scene helper resets dest_size (umbriel sync). */
	sf_decor_update_geom(toplevel);
	sf_decor_apply_buffers(toplevel);
	sf_anim_schedule(toplevel->server);
}

void sf_decor_pop_cancel_reapply(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->pop_present_idle == NULL) {
		return;
	}
	wl_event_source_remove(toplevel->pop_present_idle);
	toplevel->pop_present_idle = NULL;
	toplevel->pop_idle_kind = 0;
}

void sf_decor_pop_schedule_reapply(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->server == NULL || toplevel->server->wl_display == NULL) {
		return;
	}
	if (!toplevel->anim.active || !toplevel->anim.pop) {
		return;
	}
	if (toplevel->pop_present_idle != NULL) {
		return;
	}
	struct wl_event_loop *loop = wl_display_get_event_loop(toplevel->server->wl_display);
	if (loop == NULL) {
		return;
	}
	toplevel->pop_idle_kind = 1;
	toplevel->pop_present_idle = wl_event_loop_add_idle(loop, pop_present_idle_cb, toplevel);
}

static void apply_border_color(struct sf_toplevel *toplevel, const struct sf_rgba *src,
			       float opacity) {
	if (toplevel->border == NULL) {
		return;
	}
	struct sf_rgba rgba;
	sf_rgba_premul(src, opacity, &rgba);
	wlr_scene_rect_set_color(toplevel->border, sf_rgba_vec(&rgba));
}

static void apply_shadow_opacity(struct sf_toplevel *toplevel, float opacity) {
	if (toplevel->shadow == NULL) {
		return;
	}
	const struct sf_config *cfg = sf_cfg(toplevel->server);
	struct sf_rgba shadow;
	sf_rgba_premul(&cfg->shadow_color, opacity, &shadow);
	wlr_scene_shadow_set_color(toplevel->shadow, sf_rgba_vec(&shadow));
}

void sf_decor_init_toplevel(struct sf_toplevel *toplevel) {
	const struct sf_config *cfg = sf_cfg(toplevel->server);
	const int inner = cfg->corner_radius;
	const int border = cfg->border_thickness;
	const int outer = outer_corner_radius(inner, border);
	const float sigma = cfg->shadow_blur_sigma;

	toplevel->blur = wlr_scene_blur_create(toplevel->scene_tree, 0, 0);
	/* blur_xray: sample optimized wallpaper only. Off: blur real backdrop. */
	wlr_scene_blur_set_should_only_blur_bottom_layer(toplevel->blur, cfg->blur_xray);
	wlr_scene_blur_set_corner_radii(toplevel->blur, corner_radii_all(inner));
	sf_decor_refresh_blur(toplevel);

	/* Opaque FS letterbox plate (umbriel ViewPresentation backdrop). */
	static const float fs_backdrop_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
	toplevel->fs_backdrop =
		wlr_scene_rect_create(toplevel->scene_tree, 0, 0, fs_backdrop_color);
	wlr_scene_node_set_enabled(&toplevel->fs_backdrop->node, false);

	/* New windows start unfocused until focus_toplevel runs. */
	float start_opacity = cfg->inactive_opacity;
	struct sf_rgba inactive;
	sf_rgba_premul(&cfg->border_inactive, start_opacity, &inactive);
	toplevel->border =
		wlr_scene_rect_create(toplevel->scene_tree, 0, 0, sf_rgba_vec(&inactive));
	/* Hole/client = corner_radius; outer rim = inner + border. */
	wlr_scene_rect_set_corner_radii(toplevel->border, corner_radii_all(outer));
	wlr_scene_node_set_position(&toplevel->border->node, -border, -border);
	wlr_scene_rect_set_clipped_region(
		toplevel->border,
		(struct clipped_region){
			.area = {.x = border, .y = border, .width = 0, .height = 0},
			.corners = corner_radii_all(inner),
		});

	struct sf_rgba shadow;
	sf_rgba_premul(&cfg->shadow_color, start_opacity, &shadow);
	toplevel->shadow = wlr_scene_shadow_create(toplevel->scene_tree, 0, 0, outer, sigma,
						   sf_rgba_vec(&shadow));
	wlr_scene_node_set_position(&toplevel->shadow->node, -border - (int)sigma,
				    -border - (int)sigma);

	/* xdg content on top; fs_backdrop < blur < shadow < border < xdg. */
	wlr_scene_node_lower_to_bottom(&toplevel->border->node);
	wlr_scene_node_lower_to_bottom(&toplevel->shadow->node);
	wlr_scene_node_lower_to_bottom(&toplevel->blur->node);
	if (toplevel->fs_backdrop != NULL) {
		wlr_scene_node_lower_to_bottom(&toplevel->fs_backdrop->node);
	}
}

/* Clip the subsurface tree inside wlr_scene_xdg_surface_create's wrapper.
 * set_clip on the outer tree is a no-op — only the subsurface tree accepts it.
 * Pass clip == NULL to clear (empty box = no crop). */
static void decor_clip_xdg_surface(struct sf_toplevel *toplevel, const struct wlr_box *clip) {
	if (toplevel->xdg_scene_tree == NULL) {
		return;
	}
	/* scenefx equal-check dereferences clip; never pass NULL. */
	struct wlr_box none = {0};
	if (clip == NULL) {
		clip = &none;
	}
	struct wlr_scene_node *child;
	wl_list_for_each(child, &toplevel->xdg_scene_tree->children, link) {
		wlr_scene_subsurface_tree_set_clip(child, clip);
		return;
	}
	SF_LOGW("decor clip: no subsurface child", sf_i64("id", (int64_t)toplevel->id));
}

/*
 * Layout-presented size (umbriel: borders follow the tile even before the
 * client redraws). An oversized committed buffer at a scrolled-off column
 * slot used to paint ~100px under the next cell (Super+w×3: natural ~1148
 * still mapped while pack wanted 1044 at x=-1044).
 */
bool sf_decor_present_content_size(struct sf_toplevel *toplevel, int *w, int *h) {
	if (toplevel == NULL || toplevel->xdg_toplevel == NULL || w == NULL || h == NULL) {
		return false;
	}
	struct wlr_box *geo = &toplevel->xdg_toplevel->base->geometry;
	int pw = geo->width;
	int ph = geo->height;
	if (pw <= 0 || ph <= 0) {
		struct wlr_surface *surf = toplevel->xdg_toplevel->base->surface;
		if (surf != NULL) {
			pw = surf->current.width;
			ph = surf->current.height;
		}
	}
	if (pw <= 0 || ph <= 0) {
		return false;
	}
	/* Fullscreen must show the client's full buffer. Column arrange_prop
	 * survives FS (strip membership kept) and used to crop Netflix to the
	 * old cell — bottom transport + sides chopped. */
	if (sf_layout_sm_is_fullscreen(&toplevel->layout)) {
		*w = pw;
		*h = ph;
		return true;
	}
	if (toplevel->anim.active && toplevel->anim.pop && toplevel->anim.prop_w > 0 &&
	    toplevel->anim.prop_h > 0) {
		/* Live presented size while scale+fade runs. */
		pw = toplevel->anim.prop_w;
		ph = toplevel->anim.prop_h;
	} else if (toplevel->anim.active && toplevel->anim.morph && toplevel->anim.to_w > 0 &&
		   toplevel->anim.to_h > 0) {
		pw = toplevel->anim.to_w;
		ph = toplevel->anim.to_h;
	} else if (toplevel->arrange_prop_w > 0 && toplevel->arrange_prop_h > 0 &&
		   (toplevel->arrange_managed || toplevel->arrange_appear_pending)) {
		/* Prefer committed geometry when it already matches the packed
		 * cell within snap tolerance — terminals often land ±1–3px off
		 * arrange_prop; keeping the cell size left a few-px right-edge
		 * wobble when pop cleared stretch. */
		const int tol = 3;
		int dw = pw - toplevel->arrange_prop_w;
		int dh = ph - toplevel->arrange_prop_h;
		if (dw < 0) {
			dw = -dw;
		}
		if (dh < 0) {
			dh = -dh;
		}
		if (dw > tol || dh > tol) {
			pw = toplevel->arrange_prop_w;
			ph = toplevel->arrange_prop_h;
		}
		/* else keep pw/ph = geo */
	}
	*w = pw;
	*h = ph;
	return true;
}

void sf_decor_update_geom(struct sf_toplevel *toplevel) {
	/* Nested-session output teardown detaches scene chrome while the xdg
	 * object can still commit; skip until destroy. */
	if (toplevel == NULL || toplevel->xdg_scene_tree == NULL ||
	    toplevel->xdg_toplevel == NULL) {
		return;
	}
	struct wlr_box *geo = &toplevel->xdg_toplevel->base->geometry;
	if (geo->width <= 0 || geo->height <= 0) {
		return;
	}

	int present_w;
	int present_h;
	if (!sf_decor_present_content_size(toplevel, &present_w, &present_h)) {
		present_w = geo->width;
		present_h = geo->height;
	}
	const bool fs = sf_layout_sm_is_fullscreen(&toplevel->layout);
	const bool pop = toplevel->anim.active && toplevel->anim.pop;
	/* Pop stretch is applied after set_clip. Settled paths must not write
	 * dest_size: wlroots 0.20 parks the buffer at (clip.x, clip.y), and an
	 * equal clip early-outs, so a full-surface dest survives until commit. */
	const struct wlr_box *clip_ptr = NULL;
	struct wlr_box clip;
	if (pop) {
		clip = (struct wlr_box){
			.x = geo->x,
			.y = geo->y,
			.width = present_w,
			.height = present_h,
		};
		clip_ptr = &clip;
	} else {
		if (toplevel->present_stretch) {
			/* Null first. The resting clip often equals the last pop clip,
			 * and set_clip then returns without surface_reconfigure. */
			decor_clip_xdg_surface(toplevel, NULL);
			toplevel->present_stretch = false;
		}
		if (!fs) {
			int clip_w = geo->width;
			int clip_h = geo->height;
			if (present_w < clip_w) {
				clip_w = present_w;
			}
			if (present_h < clip_h) {
				clip_h = present_h;
			}
			clip = (struct wlr_box){
				.x = geo->x,
				.y = geo->y,
				.width = clip_w,
				.height = clip_h,
			};
			clip_ptr = &clip;
		}
	}

	const bool chrome = decor_chrome_on(toplevel);
	const struct sf_config *cfg = sf_cfg(toplevel->server);
	const int border = chrome ? cfg->border_thickness : 0;
	const int inner = chrome ? cfg->corner_radius : 0;
	const int outer = chrome ? outer_corner_radius(inner, border) : 0;
	const float sigma = chrome ? cfg->shadow_blur_sigma : 0.0f;
	/* Titlebar sits above content; border/shadow/blur wrap both. */
	const int ssd_h = chrome ? sf_ssd_top_inset(toplevel) : 0;
	struct fx_corner_radii outer_corners = corner_radii_all(outer);
	struct fx_corner_radii inner_corners = corner_radii_all(inner);

	/*
	 * Do NOT shift xdg_scene_tree by -geo. wlr_scene_xdg_surface_create
	 * already places its subsurface child at (-geo.x,-geo.y) on every
	 * commit so geometry TL sits at the xdg tree origin. Fullscreen may
	 * still offset the xdg tree to letterbox a mid-transition buffer
	 * (umbriel / xdg-shell: center + opaque plate).
	 */
	int xdg_ox = 0;
	int xdg_oy = 0;
	if (fs && toplevel->fs_backdrop != NULL) {
		struct wlr_box tile = {0};
		struct sf_output *out = sf_output_for_toplevel(toplevel);
		if (out != NULL && out->full.width > 0 && out->full.height > 0) {
			tile = out->full;
			sf_box_cover_fractional_gap(&tile,
						    sf_output_scale_factor(toplevel->server));
		}
		if (tile.width > 0 && tile.height > 0) {
			wlr_scene_rect_set_size(toplevel->fs_backdrop, tile.width, tile.height);
			wlr_scene_node_set_position(&toplevel->fs_backdrop->node, 0, 0);
			wlr_scene_node_set_enabled(&toplevel->fs_backdrop->node, true);
			wlr_scene_node_lower_to_bottom(&toplevel->fs_backdrop->node);
			/* Letterbox only while the committed buffer is still short of
			 * the tile. Once geo fills the output, offset is 0 — full bleed. */
			xdg_ox = sf_fullscreen_center_offset(tile.width, geo->width);
			xdg_oy = sf_fullscreen_center_offset(tile.height, geo->height);
		} else {
			wlr_scene_node_set_enabled(&toplevel->fs_backdrop->node, false);
		}
	} else if (toplevel->fs_backdrop != NULL) {
		wlr_scene_node_set_enabled(&toplevel->fs_backdrop->node, false);
	}
	wlr_scene_node_set_position(&toplevel->xdg_scene_tree->node, xdg_ox, xdg_oy);
	decor_clip_xdg_surface(toplevel, clip_ptr);
	/* surface_reconfigure overwrites source and dest when the clip changes.
	 * Program the scaled crop after that, or the box is a 1:1 top-left crop. */
	if (pop) {
		sf_decor_pop_set_dest_size(toplevel, present_w, present_h);
	}

	if (toplevel->blur != NULL) {
		wlr_scene_blur_set_size(toplevel->blur, present_w, present_h + ssd_h);
		wlr_scene_node_set_position(&toplevel->blur->node, 0, -ssd_h);
		wlr_scene_blur_set_corner_radii(toplevel->blur, inner_corners);
	}

	int border_w = present_w + 2 * border;
	int border_h = present_h + ssd_h + 2 * border;
	if (toplevel->border != NULL) {
		/* Inset hole by overlap so the rim covers client AA / scale hairlines.
		 * Hole punches content + SSD so the rim can sit above the titlebar
		 * without painting solid border color over it (ssd_update raises
		 * the border node above the SSD buffer). */
		int ov = (chrome && border > 0) ? border_overlap_px : 0;
		int hole_h = present_h + ssd_h;
		if (ov * 2 >= present_w || ov * 2 >= hole_h) {
			ov = 0;
		}
		struct clipped_region hole = {
			.area =
				{
					.x = border + ov,
					.y = border + ov,
					.width = present_w - 2 * ov,
					.height = hole_h - 2 * ov,
				},
			.corners = ov > 0 ? corner_radii_all(inner > ov ? inner - ov : 0)
					  : inner_corners,
		};
		wlr_scene_rect_set_size(toplevel->border, border_w, border_h);
		wlr_scene_node_set_position(&toplevel->border->node, -border, -ssd_h - border);
		wlr_scene_rect_set_corner_radii(toplevel->border, outer_corners);
		wlr_scene_rect_set_clipped_region(toplevel->border, hole);
	}

	if (toplevel->shadow != NULL) {
		int sw = border_w + 2 * (int)sigma;
		int sh = border_h + 2 * (int)sigma;
		wlr_scene_shadow_set_size(toplevel->shadow, sw, sh);
		wlr_scene_node_set_position(&toplevel->shadow->node, -border - (int)sigma,
					    -ssd_h - border - (int)sigma);
		/* Clip follows the outer border silhouette. */
		wlr_scene_shadow_set_clipped_region(toplevel->shadow,
						    (struct clipped_region){
							    .area =
								    {
									    .x = (int)sigma,
									    .y = (int)sigma,
									    .width = border_w,
									    .height = border_h,
								    },
							    .corners = outer_corners,
						    });
	}

	sf_decor_apply_buffers(toplevel);
	sf_ssd_update(toplevel);
	sf_decor_refresh_close_snap(toplevel);
}

static const struct sf_rgba *border_color_for(const struct sf_toplevel *toplevel, bool focused,
					      const struct sf_config *cfg) {
	if (toplevel->bell_flash || toplevel->urgent) {
		return &cfg->border_urgent;
	}
	if (toplevel->arrange_floating) {
		return focused ? &cfg->border_float_active : &cfg->border_float_inactive;
	}
	return focused ? &cfg->border_active : &cfg->border_inactive;
}

void sf_decor_refresh_border(struct sf_toplevel *toplevel) {
	bool focused = sf_focused_toplevel(toplevel->server) == toplevel;
	const struct sf_config *cfg = sf_cfg(toplevel->server);
	float opacity = toplevel_chrome_opacity(toplevel, focused);
	apply_border_color(toplevel, border_color_for(toplevel, focused, cfg), opacity);
	apply_shadow_opacity(toplevel, opacity);
}

void sf_decor_set_focused(struct sf_toplevel *toplevel, bool focused) {
	if (focused) {
		toplevel->urgent = false;
	}
	sf_ssd_set_focused(toplevel, focused);
	/* Use the caller-supplied focus bit: seat focus may not have moved yet. */
	const struct sf_config *cfg = sf_cfg(toplevel->server);
	float opacity = toplevel_chrome_opacity(toplevel, focused);
	apply_border_color(toplevel, border_color_for(toplevel, focused, cfg), opacity);
	apply_shadow_opacity(toplevel, opacity);
	decor_apply_buffers_focused(toplevel, focused);
}

void sf_decor_set_urgent(struct sf_toplevel *toplevel, bool urgent) {
	if (sf_focused_toplevel(toplevel->server) == toplevel) {
		toplevel->urgent = false;
		sf_decor_refresh_border(toplevel);
		return;
	}
	toplevel->urgent = urgent;
	sf_decor_refresh_border(toplevel);
}

static int bell_flash_done(void *data) {
	struct sf_toplevel *toplevel = data;
	toplevel->bell_timer = NULL;
	toplevel->bell_flash = false;
	sf_decor_refresh_border(toplevel);
	return 0;
}

void sf_decor_ring_bell(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->border == NULL) {
		return;
	}
	toplevel->bell_flash = true;
	sf_decor_refresh_border(toplevel);

	int flash_ms = sf_cfg(toplevel->server)->bell_flash_ms;
	struct wl_event_loop *loop = wl_display_get_event_loop(toplevel->server->wl_display);
	if (toplevel->bell_timer != NULL) {
		wl_event_source_timer_update(toplevel->bell_timer, flash_ms);
		return;
	}
	toplevel->bell_timer = wl_event_loop_add_timer(loop, bell_flash_done, toplevel);
	if (toplevel->bell_timer != NULL) {
		wl_event_source_timer_update(toplevel->bell_timer, flash_ms);
	}
}

void sf_decor_finish_toplevel(struct sf_toplevel *toplevel) {
	sf_decor_pop_cancel_reapply(toplevel);
	if (toplevel->bell_timer != NULL) {
		wl_event_source_remove(toplevel->bell_timer);
		toplevel->bell_timer = NULL;
	}
	toplevel->bell_flash = false;
	sf_decor_clear_close_snap(toplevel);

	/* Wrapper owns blur/shadow/border (+ xdg child if still linked). */
	if (toplevel->scene_tree != NULL) {
		wlr_scene_node_destroy(&toplevel->scene_tree->node);
		toplevel->scene_tree = NULL;
	}
	toplevel->xdg_scene_tree = NULL;
	toplevel->blur = NULL;
	toplevel->shadow = NULL;
	toplevel->border = NULL;
	toplevel->fs_backdrop = NULL;
}

/* ---- close ghost (FORK.md last-frame freeze) ---- */

struct ghost_pick {
	struct wlr_buffer *buffer;
	int width, height;
};

static void buffer_dims(struct wlr_scene_buffer *buffer, int *w, int *h) {
	*w = buffer->dst_width;
	*h = buffer->dst_height;
	if (*w > 0 && *h > 0) {
		return;
	}
	struct wlr_scene_surface *ss = wlr_scene_surface_try_from_buffer(buffer);
	if (ss == NULL) {
		return;
	}
	if (*w <= 0) {
		*w = ss->surface->current.width;
	}
	if (*h <= 0) {
		*h = ss->surface->current.height;
	}
}

/* Prefer the root xdg-toplevel buffer (not a subsurface). */
static void iter_pick_toplevel(struct wlr_scene_buffer *buffer, int sx, int sy, void *user_data) {
	(void)sx;
	(void)sy;
	struct ghost_pick *pick = user_data;
	if (pick->buffer != NULL || buffer->buffer == NULL) {
		return;
	}
	struct wlr_scene_surface *ss = wlr_scene_surface_try_from_buffer(buffer);
	if (ss == NULL) {
		return;
	}
	struct wlr_xdg_surface *xdg = wlr_xdg_surface_try_from_wlr_surface(ss->surface);
	if (xdg == NULL || xdg->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
		return;
	}
	if (wlr_subsurface_try_from_wlr_surface(ss->surface) != NULL) {
		return;
	}
	int w = 0, h = 0;
	buffer_dims(buffer, &w, &h);
	if (w <= 0 || h <= 0) {
		return;
	}
	pick->buffer = buffer->buffer;
	pick->width = w;
	pick->height = h;
}

/* Fallback: largest buffer in the xdg tree (subsurface-heavy CSDs). */
static void iter_pick_largest(struct wlr_scene_buffer *buffer, int sx, int sy, void *user_data) {
	(void)sx;
	(void)sy;
	struct ghost_pick *pick = user_data;
	if (buffer->buffer == NULL) {
		return;
	}
	int w = 0, h = 0;
	buffer_dims(buffer, &w, &h);
	if (w <= 0 || h <= 0) {
		return;
	}
	if (pick->buffer != NULL &&
	    (int64_t)w * (int64_t)h <= (int64_t)pick->width * (int64_t)pick->height) {
		return;
	}
	pick->buffer = buffer->buffer;
	pick->width = w;
	pick->height = h;
}

static void ghost_destroy(struct sf_ghost *ghost) {
	if (ghost->locked != NULL) {
		wlr_buffer_unlock(ghost->locked);
		ghost->locked = NULL;
	}
	if (ghost->tree != NULL) {
		wlr_scene_node_destroy(&ghost->tree->node);
		ghost->tree = NULL;
	}
	wl_list_remove(&ghost->link);
	free(ghost);
}

bool sf_decor_lock_snapshot(struct sf_toplevel *toplevel, struct wlr_buffer **out_locked,
			    int *out_w, int *out_h) {
	if (toplevel == NULL || toplevel->xdg_scene_tree == NULL || out_locked == NULL ||
	    out_w == NULL || out_h == NULL) {
		return false;
	}
	struct ghost_pick pick = {0};
	wlr_scene_node_for_each_buffer(&toplevel->xdg_scene_tree->node, iter_pick_toplevel, &pick);
	if (pick.buffer == NULL) {
		wlr_scene_node_for_each_buffer(&toplevel->xdg_scene_tree->node, iter_pick_largest,
					       &pick);
	}
	if (pick.buffer == NULL || pick.width <= 0 || pick.height <= 0) {
		return false;
	}
	struct wlr_buffer *locked = wlr_buffer_lock(pick.buffer);
	if (locked == NULL) {
		return false;
	}
	*out_locked = locked;
	*out_w = pick.width;
	*out_h = pick.height;
	return true;
}

void sf_decor_clear_close_snap(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->close_snap == NULL) {
		return;
	}
	wlr_buffer_unlock(toplevel->close_snap);
	toplevel->close_snap = NULL;
	toplevel->close_snap_w = 0;
	toplevel->close_snap_h = 0;
}

void sf_decor_refresh_close_snap(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->xdg_toplevel == NULL ||
	    toplevel->xdg_toplevel->base == NULL || toplevel->xdg_toplevel->base->surface == NULL) {
		return;
	}
	/* Only cache while mapped; keep the prior frame across the NULL commit
	 * that precedes client unmap (GTK/gedit, many terminals). */
	if (!toplevel->xdg_toplevel->base->surface->mapped) {
		return;
	}
	struct wlr_buffer *locked = NULL;
	int w = 0, h = 0;
	if (!sf_decor_lock_snapshot(toplevel, &locked, &w, &h)) {
		return;
	}
	if (toplevel->close_snap != NULL) {
		wlr_buffer_unlock(toplevel->close_snap);
	}
	toplevel->close_snap = locked;
	toplevel->close_snap_w = w;
	toplevel->close_snap_h = h;
}

void sf_ghost_try_spawn(struct sf_toplevel *toplevel) {
	if (toplevel->minimized || toplevel->anim_hide) {
		return;
	}
	if (toplevel->scene_tree == NULL) {
		return;
	}

	/* Prefer the cached last frame — live scene buffers are often already
	 * NULL by unmap (client attached a null buffer). */
	struct wlr_buffer *locked = NULL;
	int pick_w = 0, pick_h = 0;
	if (toplevel->close_snap != NULL) {
		locked = toplevel->close_snap;
		pick_w = toplevel->close_snap_w;
		pick_h = toplevel->close_snap_h;
		toplevel->close_snap = NULL;
		toplevel->close_snap_w = 0;
		toplevel->close_snap_h = 0;
	} else if (!sf_decor_lock_snapshot(toplevel, &locked, &pick_w, &pick_h)) {
		return;
	}

	struct sf_ghost *ghost = calloc(1, sizeof(*ghost));
	if (ghost == NULL) {
		wlr_buffer_unlock(locked);
		return;
	}

	ghost->server = toplevel->server;
	ghost->locked = locked;
	ghost->width = pick_w;
	ghost->height = pick_h;

	/* xdg tree sits at (-geo.x,-geo.y) under the wrapper so geometry TL is
	 * at wrapper (0,0). Place the full-buffer ghost the same way. */
	struct wlr_box *geo = &toplevel->xdg_toplevel->base->geometry;
	int geo_x = (geo->width > 0) ? geo->x : 0;
	int geo_y = (geo->height > 0) ? geo->y : 0;
	ghost->resting_x = toplevel->scene_tree->node.x - geo_x;
	ghost->resting_y = toplevel->scene_tree->node.y - geo_y;

	const struct sf_config *cfg = sf_cfg(toplevel->server);
	ghost->tree = wlr_scene_tree_create(toplevel->server->layers.toplevel);
	if (ghost->tree == NULL) {
		wlr_buffer_unlock(locked);
		free(ghost);
		return;
	}
	ghost->buffer = wlr_scene_buffer_create(ghost->tree, locked);
	if (ghost->buffer == NULL) {
		wlr_scene_node_destroy(&ghost->tree->node);
		wlr_buffer_unlock(locked);
		free(ghost);
		return;
	}
	wlr_scene_buffer_set_dest_size(ghost->buffer, pick_w, pick_h);
	wlr_scene_buffer_set_opacity(ghost->buffer, cfg->client_opacity);
	wlr_scene_buffer_set_corner_radii(ghost->buffer, corner_radii_all(cfg->corner_radius));
	wlr_scene_node_set_position(&ghost->tree->node, ghost->resting_x, ghost->resting_y);
	wlr_scene_node_raise_to_top(&ghost->tree->node);

	/* Capture before unmap unlinks the column (caller order). */
	bool strip = toplevel->arrange_managed || toplevel->column != NULL ||
		     toplevel->anim_slide_column;
	ghost->base_opacity = cfg->client_opacity;
	ghost->anim_pop = strip;

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	ghost->anim_start = now;
	/* FIXME: per-op duration → sf.opt.anim; reuse slide for now. */
	ghost->anim_dur_ns = (int64_t)cfg->slide_duration_ms * 1000000LL;
	ghost->anim_active = true;
	ghost->anim_hide = true;
	ghost->anim_from_x = ghost->resting_x;
	ghost->anim_from_y = ghost->resting_y;
	ghost->anim_from_w = pick_w;
	ghost->anim_from_h = pick_h;
	ghost->anim_fade_from = 1.0f;
	ghost->anim_fade_to = 0.0f;

	if (strip) {
		/* FIXME: sf.opt.anim.windows_out.scale (Umbriel default 0.8). */
		const double scale = 0.8;
		int small_w = (int)((double)pick_w * scale);
		int small_h = (int)((double)pick_h * scale);
		if (small_w < 1) {
			small_w = 1;
		}
		if (small_h < 1) {
			small_h = 1;
		}
		ghost->anim_to_w = small_w;
		ghost->anim_to_h = small_h;
		ghost->anim_to_x = ghost->resting_x + (pick_w - small_w) / 2;
		ghost->anim_to_y = ghost->resting_y + (pick_h - small_h) / 2;
	} else {
		int to_x, to_y;
		sf_anim_offscreen_for_box(ghost->server, ghost->resting_x, ghost->resting_y,
					  ghost->width, ghost->height, false, &to_x, &to_y);
		ghost->anim_to_x = to_x;
		ghost->anim_to_y = to_y;
		ghost->anim_to_w = pick_w;
		ghost->anim_to_h = pick_h;
	}

	wl_list_insert(&toplevel->server->ghosts, &ghost->link);
	sf_anim_schedule(toplevel->server);
	SF_LOGI("close ghost", sf_i64("id", (int64_t)toplevel->id), sf_i64("w", (int64_t)pick_w),
		sf_i64("h", (int64_t)pick_h));
}

void sf_ghost_tick(struct sf_server *server, const struct timespec *now) {
	struct sf_ghost *ghost, *tmp;
	wl_list_for_each_safe(ghost, tmp, &server->ghosts, link) {
		if (!ghost->anim_active) {
			ghost_destroy(ghost);
			continue;
		}
		wlr_scene_node_raise_to_top(&ghost->tree->node);
		int64_t elapsed = sf_timespec_ns(now) - sf_timespec_ns(&ghost->anim_start);
		double t = 1.0;
		if (ghost->anim_dur_ns > 0) {
			t = (double)elapsed / (double)ghost->anim_dur_ns;
		}
		double p = sf_ease_out_cubic(t);
		int x = sf_lerp_i(ghost->anim_from_x, ghost->anim_to_x, p);
		int y = sf_lerp_i(ghost->anim_from_y, ghost->anim_to_y, p);
		wlr_scene_node_set_position(&ghost->tree->node, x, y);

		if (ghost->anim_pop) {
			int w = sf_lerp_i(ghost->anim_from_w, ghost->anim_to_w, p);
			int h = sf_lerp_i(ghost->anim_from_h, ghost->anim_to_h, p);
			if (w < 1) {
				w = 1;
			}
			if (h < 1) {
				h = 1;
			}
			float fade = ghost->anim_fade_from +
				     (ghost->anim_fade_to - ghost->anim_fade_from) * (float)p;
			if (fade < 0.0f) {
				fade = 0.0f;
			}
			if (fade > 1.0f) {
				fade = 1.0f;
			}
			wlr_scene_buffer_set_dest_size(ghost->buffer, w, h);
			wlr_scene_buffer_set_opacity(ghost->buffer, ghost->base_opacity * fade);
			if (t >= 1.0) {
				ghost->anim_active = false;
				ghost_destroy(ghost);
			}
			continue;
		}

		/* Drop as soon as the buffer clears usable — ease-out can crawl
		 * with a sliver on-screen if frames stall near t=1. */
		struct wlr_box usable;
		bool clear = false;
		if (sf_layout_usable_at(server, (double)x + (double)ghost->width / 2.0,
					(double)y + (double)ghost->height / 2.0, &usable) ||
		    sf_layout_usable_at(server, server->cursor->x, server->cursor->y, &usable)) {
			clear = x + ghost->width <= usable.x || x >= usable.x + usable.width ||
				y + ghost->height <= usable.y || y >= usable.y + usable.height;
		}
		if (t >= 1.0 || clear) {
			ghost->anim_active = false;
			ghost_destroy(ghost);
		}
	}
}

void sf_ghost_finish_all(struct sf_server *server) {
	struct sf_ghost *ghost, *tmp;
	wl_list_for_each_safe(ghost, tmp, &server->ghosts, link) {
		ghost_destroy(ghost);
	}
}
