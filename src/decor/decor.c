#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
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
#include "wm/wm.h"
#include "xdg/xdg.h"

#include <wlr/types/wlr_buffer.h>

struct stonefly_ghost {
	struct wl_list link;
	struct stonefly_server *server;
	struct wlr_scene_tree *tree;
	struct wlr_scene_buffer *buffer;
	struct wlr_buffer *locked;
	int width, height;
	int resting_x, resting_y;
	bool anim_active;
	bool anim_hide;
	struct timespec anim_start;
	int64_t anim_dur_ns;
	int anim_from_x, anim_from_y;
	int anim_to_x, anim_to_y;
};

static double clamp01(double t) {
	if (t < 0.0) {
		return 0.0;
	}
	if (t > 1.0) {
		return 1.0;
	}
	return t;
}

static double ease_out_cubic(double t) {
	t = clamp01(t);
	double u = 1.0 - t;
	return 1.0 - u * u * u;
}

static int lerp_i(int a, int b, double t) {
	return (int)((double)a + (double)(b - a) * clamp01(t) + (t >= 0 ? 0.5 : -0.5));
}

static int64_t timespec_ns(const struct timespec *ts) {
	return (int64_t)ts->tv_sec * 1000000000LL + (int64_t)ts->tv_nsec;
}

void stonefly_decor_setup_scene(struct stonefly_server *server) {
	/* Blur node is created in stonefly_layer_setup (z-order). */
	(void)server;
}

void stonefly_decor_apply_blur_params(struct stonefly_server *server) {
	if (server == NULL || server->scene == NULL) {
		return;
	}
	const struct stonefly_config *cfg = stonefly_cfg(server);
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

void stonefly_decor_update_blur_size(struct stonefly_server *server) {
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

void stonefly_decor_mark_blur_dirty(struct stonefly_server *server) {
	if (server == NULL || server->blur_layer == NULL) {
		return;
	}
	wlr_scene_optimized_blur_mark_dirty(server->blur_layer);
}

/* Border/shadow/corners off for edge-max and fullscreen. Blur is separate
 * (stays on for edge-max / quick terminal; off only for true fullscreen). */
static bool decor_chrome_on(const struct stonefly_toplevel *toplevel) {
	return !toplevel->maximized_edges && !toplevel->fullscreen;
}

static bool decor_blur_on(const struct stonefly_toplevel *toplevel) {
	const struct stonefly_config *cfg = stonefly_cfg(toplevel->server);
	return (cfg->window_blur || toplevel->client_blur) && !toplevel->fullscreen;
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

void stonefly_decor_refresh_blur(struct stonefly_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->blur == NULL) {
		return;
	}
	wlr_scene_node_set_enabled(&toplevel->blur->node, decor_blur_on(toplevel));
}

static void decor_set_chrome_enabled(struct stonefly_toplevel *toplevel, bool on) {
	stonefly_decor_refresh_blur(toplevel);
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
static float toplevel_chrome_opacity(struct stonefly_toplevel *toplevel, bool focused) {
	if (toplevel->fullscreen) {
		return 1.0f;
	}
	const struct stonefly_config *cfg = stonefly_cfg(toplevel->server);
	return focused ? cfg->client_opacity : cfg->inactive_opacity;
}

struct apply_buffers_ctx {
	struct stonefly_toplevel *toplevel;
	float opacity;
	bool chrome;
	int radius;
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
	bool round_top = !stonefly_ssd_should_show(ctx->toplevel);
	wlr_scene_buffer_set_corner_radii(
		buffer, corners_for_buffer(sx, sy, bw, bh, geo, radius, round_top));
}

static void decor_apply_buffers_focused(struct stonefly_toplevel *toplevel, bool focused) {
	if (toplevel->xdg_scene_tree == NULL) {
		return;
	}
	bool chrome = decor_chrome_on(toplevel);
	decor_set_chrome_enabled(toplevel, chrome);
	const struct stonefly_config *cfg = stonefly_cfg(toplevel->server);
	struct apply_buffers_ctx ctx = {
		.toplevel = toplevel,
		.opacity = toplevel_chrome_opacity(toplevel, focused),
		.chrome = chrome,
		.radius = cfg->corner_radius,
	};
	wlr_scene_node_for_each_buffer(&toplevel->xdg_scene_tree->node, iter_apply_buffers, &ctx);
}

void stonefly_decor_apply_buffers(struct stonefly_toplevel *toplevel) {
	bool focused = stonefly_focused_toplevel(toplevel->server) == toplevel;
	decor_apply_buffers_focused(toplevel, focused);
}

/* SceneFX rect colors are premultiplied (wlroots/scenefx contract). */
static void border_rgba_premul(const float src[4], float opacity, float out[4]) {
	float a = src[3] * opacity;
	out[0] = src[0] * a;
	out[1] = src[1] * a;
	out[2] = src[2] * a;
	out[3] = a;
}

static void apply_border_color(struct stonefly_toplevel *toplevel, const float *src,
			       float opacity) {
	if (toplevel->border == NULL) {
		return;
	}
	float rgba[4];
	border_rgba_premul(src, opacity, rgba);
	wlr_scene_rect_set_color(toplevel->border, rgba);
}

static void apply_shadow_opacity(struct stonefly_toplevel *toplevel, float opacity) {
	if (toplevel->shadow == NULL) {
		return;
	}
	const struct stonefly_config *cfg = stonefly_cfg(toplevel->server);
	float shadow_a = cfg->shadow_color[3] * opacity;
	float shadow[4] = {
		cfg->shadow_color[0] * shadow_a,
		cfg->shadow_color[1] * shadow_a,
		cfg->shadow_color[2] * shadow_a,
		shadow_a,
	};
	wlr_scene_shadow_set_color(toplevel->shadow, shadow);
}

void stonefly_decor_init_toplevel(struct stonefly_toplevel *toplevel) {
	const struct stonefly_config *cfg = stonefly_cfg(toplevel->server);
	const int inner = cfg->corner_radius;
	const int border = cfg->border_thickness;
	const int outer = outer_corner_radius(inner, border);
	const float sigma = cfg->shadow_blur_sigma;

	toplevel->blur = wlr_scene_blur_create(toplevel->scene_tree, 0, 0);
	/* blur_xray: sample optimized wallpaper only. Off: blur real backdrop. */
	wlr_scene_blur_set_should_only_blur_bottom_layer(toplevel->blur, cfg->blur_xray != 0);
	wlr_scene_blur_set_corner_radii(toplevel->blur, corner_radii_all(inner));
	stonefly_decor_refresh_blur(toplevel);

	/* New windows start unfocused until focus_toplevel runs. */
	float start_opacity = cfg->inactive_opacity;
	float inactive[4];
	border_rgba_premul(cfg->border_inactive, start_opacity, inactive);
	toplevel->border = wlr_scene_rect_create(toplevel->scene_tree, 0, 0, inactive);
	/* Hole/client = corner_radius; outer rim = inner + border. */
	wlr_scene_rect_set_corner_radii(toplevel->border, corner_radii_all(outer));
	wlr_scene_node_set_position(&toplevel->border->node, -border, -border);
	wlr_scene_rect_set_clipped_region(
		toplevel->border,
		(struct clipped_region){
			.area = {.x = border, .y = border, .width = 0, .height = 0},
			.corners = corner_radii_all(inner),
		});

	float shadow_a = cfg->shadow_color[3] * start_opacity;
	float shadow[4] = {
		cfg->shadow_color[0] * shadow_a,
		cfg->shadow_color[1] * shadow_a,
		cfg->shadow_color[2] * shadow_a,
		shadow_a,
	};
	toplevel->shadow =
		wlr_scene_shadow_create(toplevel->scene_tree, 0, 0, outer, sigma, shadow);
	wlr_scene_node_set_position(&toplevel->shadow->node, -border - (int)sigma,
				    -border - (int)sigma);

	/* xdg content on top; blur < shadow < border < xdg. */
	wlr_scene_node_lower_to_bottom(&toplevel->border->node);
	wlr_scene_node_lower_to_bottom(&toplevel->shadow->node);
	wlr_scene_node_lower_to_bottom(&toplevel->blur->node);
}

/* Clip the subsurface tree inside wlr_scene_xdg_surface_create's wrapper.
 * set_clip on the outer tree is a no-op — only the subsurface tree accepts it. */
static void decor_clip_xdg_surface(struct stonefly_toplevel *toplevel, const struct wlr_box *geo) {
	struct wlr_scene_node *child;
	wl_list_for_each(child, &toplevel->xdg_scene_tree->children, link) {
		wlr_scene_subsurface_tree_set_clip(child, geo);
		return;
	}
	SF_LOGW("decor clip: no subsurface child", sf_i64("id", (int64_t)toplevel->id));
}

void stonefly_decor_update_geom(struct stonefly_toplevel *toplevel) {
	struct wlr_box *geo = &toplevel->xdg_toplevel->base->geometry;
	if (geo->width <= 0 || geo->height <= 0) {
		return;
	}

	const bool chrome = decor_chrome_on(toplevel);
	const struct stonefly_config *cfg = stonefly_cfg(toplevel->server);
	const int border = chrome ? cfg->border_thickness : 0;
	const int inner = chrome ? cfg->corner_radius : 0;
	const int outer = chrome ? outer_corner_radius(inner, border) : 0;
	const float sigma = chrome ? cfg->shadow_blur_sigma : 0.0f;
	/* Titlebar sits above content; border/shadow/blur wrap both. */
	const int ssd_h = chrome ? stonefly_ssd_top_inset(toplevel) : 0;
	struct fx_corner_radii outer_corners = corner_radii_all(outer);
	struct fx_corner_radii inner_corners = corner_radii_all(inner);

	/*
	 * Do NOT shift xdg_scene_tree by -geo. wlr_scene_xdg_surface_create
	 * already places its subsurface child at (-geo.x,-geo.y) on every
	 * commit so geometry TL sits at the xdg tree origin.
	 */
	wlr_scene_node_set_position(&toplevel->xdg_scene_tree->node, 0, 0);
	decor_clip_xdg_surface(toplevel, geo);

	if (toplevel->blur != NULL) {
		wlr_scene_blur_set_size(toplevel->blur, geo->width, geo->height + ssd_h);
		wlr_scene_node_set_position(&toplevel->blur->node, 0, -ssd_h);
		wlr_scene_blur_set_corner_radii(toplevel->blur, inner_corners);
	}

	int border_w = geo->width + 2 * border;
	int border_h = geo->height + ssd_h + 2 * border;
	if (toplevel->border != NULL) {
		/* Inset hole by overlap so the rim covers client AA / scale hairlines.
		 * Hole covers client content only; SSD titlebar fills the top band. */
		int ov = (chrome && border > 0) ? border_overlap_px : 0;
		if (ov * 2 >= geo->width || ov * 2 >= geo->height) {
			ov = 0;
		}
		struct clipped_region hole = {
			.area =
				{
					.x = border + ov,
					.y = border + ssd_h + ov,
					.width = geo->width - 2 * ov,
					.height = geo->height - 2 * ov,
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

	stonefly_decor_apply_buffers(toplevel);
	stonefly_ssd_update(toplevel);
}

void stonefly_decor_refresh_border(struct stonefly_toplevel *toplevel) {
	bool focused = stonefly_focused_toplevel(toplevel->server) == toplevel;
	const struct stonefly_config *cfg = stonefly_cfg(toplevel->server);
	float opacity = toplevel_chrome_opacity(toplevel, focused);
	const float *color = cfg->border_inactive;
	if (toplevel->bell_flash || toplevel->urgent) {
		color = cfg->border_urgent;
	} else if (focused) {
		color = cfg->border_active;
	}
	apply_border_color(toplevel, color, opacity);
	apply_shadow_opacity(toplevel, opacity);
}

void stonefly_decor_set_focused(struct stonefly_toplevel *toplevel, bool focused) {
	if (focused) {
		toplevel->urgent = false;
	}
	stonefly_ssd_set_focused(toplevel, focused);
	/* Use the caller-supplied focus bit: seat focus may not have moved yet. */
	const struct stonefly_config *cfg = stonefly_cfg(toplevel->server);
	float opacity = toplevel_chrome_opacity(toplevel, focused);
	const float *color = cfg->border_inactive;
	if (toplevel->bell_flash || toplevel->urgent) {
		color = cfg->border_urgent;
	} else if (focused) {
		color = cfg->border_active;
	}
	apply_border_color(toplevel, color, opacity);
	apply_shadow_opacity(toplevel, opacity);
	decor_apply_buffers_focused(toplevel, focused);
}

void stonefly_decor_set_urgent(struct stonefly_toplevel *toplevel, bool urgent) {
	if (stonefly_focused_toplevel(toplevel->server) == toplevel) {
		toplevel->urgent = false;
		stonefly_decor_refresh_border(toplevel);
		return;
	}
	toplevel->urgent = urgent;
	stonefly_decor_refresh_border(toplevel);
}

static int bell_flash_done(void *data) {
	struct stonefly_toplevel *toplevel = data;
	toplevel->bell_timer = NULL;
	toplevel->bell_flash = false;
	stonefly_decor_refresh_border(toplevel);
	return 0;
}

void stonefly_decor_ring_bell(struct stonefly_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->border == NULL) {
		return;
	}
	toplevel->bell_flash = true;
	stonefly_decor_refresh_border(toplevel);

	int flash_ms = stonefly_cfg(toplevel->server)->bell_flash_ms;
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

void stonefly_decor_finish_toplevel(struct stonefly_toplevel *toplevel) {
	if (toplevel->bell_timer != NULL) {
		wl_event_source_remove(toplevel->bell_timer);
		toplevel->bell_timer = NULL;
	}
	toplevel->bell_flash = false;

	/* Wrapper owns blur/shadow/border (+ xdg child if still linked). */
	if (toplevel->scene_tree != NULL) {
		wlr_scene_node_destroy(&toplevel->scene_tree->node);
		toplevel->scene_tree = NULL;
	}
	toplevel->xdg_scene_tree = NULL;
	toplevel->blur = NULL;
	toplevel->shadow = NULL;
	toplevel->border = NULL;
}

/* ---- close ghost (FORK.md last-frame freeze) ---- */

struct ghost_pick {
	struct wlr_scene_buffer *scene_buffer;
	struct wlr_buffer *buffer;
	int width, height;
};

static void iter_pick_buffer(struct wlr_scene_buffer *buffer, int sx, int sy, void *user_data) {
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
	pick->scene_buffer = buffer;
	pick->buffer = buffer->buffer;
	pick->width = buffer->dst_width > 0 ? buffer->dst_width : ss->surface->current.width;
	pick->height = buffer->dst_height > 0 ? buffer->dst_height : ss->surface->current.height;
}

static void ghost_destroy(struct stonefly_ghost *ghost) {
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

static void ghost_offscreen(struct stonefly_ghost *ghost, int *ox, int *oy) {
	/* Same closest-down fallback as anim: slide up off the usable area. */
	struct wlr_box area = {0};
	struct stonefly_output *output;
	wl_list_for_each(output, &ghost->server->outputs, link) {
		if (stonefly_output_usable_box(output, &area)) {
			break;
		}
	}
	*ox = ghost->resting_x;
	if (area.height > 0) {
		int y = area.y - ghost->height;
		int alt = ghost->resting_y - ghost->height;
		*oy = alt < y ? alt : y;
	} else {
		*oy = ghost->resting_y - ghost->height;
	}
}

bool stonefly_decor_lock_snapshot(struct stonefly_toplevel *toplevel,
				  struct wlr_buffer **out_locked, int *out_w, int *out_h) {
	if (toplevel == NULL || toplevel->xdg_scene_tree == NULL || out_locked == NULL ||
	    out_w == NULL || out_h == NULL) {
		return false;
	}
	struct ghost_pick pick = {0};
	wlr_scene_node_for_each_buffer(&toplevel->xdg_scene_tree->node, iter_pick_buffer, &pick);
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

void stonefly_ghost_try_spawn(struct stonefly_toplevel *toplevel) {
	if (toplevel->minimized || toplevel->anim_hide) {
		return;
	}
	if (toplevel->xdg_scene_tree == NULL) {
		return;
	}

	struct wlr_buffer *locked = NULL;
	int pick_w = 0, pick_h = 0;
	if (!stonefly_decor_lock_snapshot(toplevel, &locked, &pick_w, &pick_h)) {
		return;
	}

	struct stonefly_ghost *ghost = calloc(1, sizeof(*ghost));
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

	const struct stonefly_config *cfg = stonefly_cfg(toplevel->server);
	ghost->tree = wlr_scene_tree_create(toplevel->server->layers.toplevel);
	ghost->buffer = wlr_scene_buffer_create(ghost->tree, locked);
	wlr_scene_buffer_set_dest_size(ghost->buffer, pick_w, pick_h);
	wlr_scene_buffer_set_opacity(ghost->buffer, cfg->client_opacity);
	wlr_scene_buffer_set_corner_radii(ghost->buffer, corner_radii_all(cfg->corner_radius));
	wlr_scene_node_set_position(&ghost->tree->node, ghost->resting_x, ghost->resting_y);
	wlr_scene_node_raise_to_top(&ghost->tree->node);

	int to_x, to_y;
	ghost_offscreen(ghost, &to_x, &to_y);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	ghost->anim_from_x = ghost->resting_x;
	ghost->anim_from_y = ghost->resting_y;
	ghost->anim_to_x = to_x;
	ghost->anim_to_y = to_y;
	ghost->anim_start = now;
	ghost->anim_dur_ns = (int64_t)cfg->slide_duration_ms * 1000000LL;
	ghost->anim_active = true;
	ghost->anim_hide = true;

	wl_list_insert(&toplevel->server->ghosts, &ghost->link);
	stonefly_anim_schedule(toplevel->server);
}

void stonefly_ghost_tick(struct stonefly_server *server, const struct timespec *now) {
	struct stonefly_ghost *ghost, *tmp;
	wl_list_for_each_safe(ghost, tmp, &server->ghosts, link) {
		if (!ghost->anim_active) {
			ghost_destroy(ghost);
			continue;
		}
		wlr_scene_node_raise_to_top(&ghost->tree->node);
		int64_t elapsed = timespec_ns(now) - timespec_ns(&ghost->anim_start);
		double t = 1.0;
		if (ghost->anim_dur_ns > 0) {
			t = (double)elapsed / (double)ghost->anim_dur_ns;
		}
		double p = ease_out_cubic(t);
		int x = lerp_i(ghost->anim_from_x, ghost->anim_to_x, p);
		int y = lerp_i(ghost->anim_from_y, ghost->anim_to_y, p);
		wlr_scene_node_set_position(&ghost->tree->node, x, y);
		if (t >= 1.0) {
			ghost->anim_active = false;
			ghost_destroy(ghost);
		}
	}
}

void stonefly_ghost_finish_all(struct stonefly_server *server) {
	struct stonefly_ghost *ghost, *tmp;
	wl_list_for_each_safe(ghost, tmp, &server->ghosts, link) {
		ghost_destroy(ghost);
	}
}
