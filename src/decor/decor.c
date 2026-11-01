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
#include "config/hardcoded.h"
#include "decor/decor.h"
#include "layer/layer.h"
#include "log/log.h"
#include "output/output.h"
#include "server/server.h"
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

void stonefly_decor_update_blur_size(struct stonefly_server *server) {
	if (server->blur_layer == NULL) {
		return;
	}
	struct wlr_box layout = {0};
	wlr_output_layout_get_box(server->output_layout, NULL, &layout);
	if (layout.width <= 0 || layout.height <= 0) {
		return;
	}
	wlr_scene_optimized_blur_set_size(server->blur_layer,
		layout.width, layout.height);
}

/* Edge-maximize (Super+f): no border/blur/shadow/corners — client only. */
static bool decor_chrome_on(const struct stonefly_toplevel *toplevel) {
	return !toplevel->maximized_edges;
}

static void decor_set_chrome_enabled(struct stonefly_toplevel *toplevel,
		bool on) {
	if (toplevel->blur != NULL) {
		wlr_scene_node_set_enabled(&toplevel->blur->node, on);
		if (!on) {
			wlr_scene_blur_set_transparency_mask_source(toplevel->blur, NULL);
		}
	}
	if (toplevel->border != NULL) {
		wlr_scene_node_set_enabled(&toplevel->border->node, on);
	}
	if (toplevel->shadow != NULL) {
		wlr_scene_node_set_enabled(&toplevel->shadow->node, on);
	}
}

static void iter_apply_buffers(struct wlr_scene_buffer *buffer,
		int sx, int sy, void *user_data) {
	(void)sx;
	(void)sy;
	struct stonefly_toplevel *toplevel = user_data;

	struct wlr_scene_surface *scene_surface =
		wlr_scene_surface_try_from_buffer(buffer);
	if (scene_surface == NULL) {
		return;
	}

	struct wlr_xdg_surface *xdg_surface =
		wlr_xdg_surface_try_from_wlr_surface(scene_surface->surface);
	if (xdg_surface == NULL ||
			xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
		return;
	}

	bool chrome = decor_chrome_on(toplevel);
	wlr_scene_buffer_set_opacity(buffer,
		chrome ? STONEFLY_CLIENT_OPACITY : 1.0f);

	if (wlr_subsurface_try_from_wlr_surface(xdg_surface->surface) == NULL) {
		wlr_scene_buffer_set_corner_radii(buffer,
			corner_radii_all(chrome ? STONEFLY_CORNER_RADIUS : 0));
		if (chrome && toplevel->blur != NULL) {
			wlr_scene_blur_set_transparency_mask_source(
				toplevel->blur, buffer);
		}
	}
}

void stonefly_decor_apply_buffers(struct stonefly_toplevel *toplevel) {
	if (toplevel->xdg_scene_tree == NULL) {
		return;
	}
	decor_set_chrome_enabled(toplevel, decor_chrome_on(toplevel));
	wlr_scene_node_for_each_buffer(&toplevel->xdg_scene_tree->node,
		iter_apply_buffers, toplevel);
}

/* Border rects stay opaque in the shader unless alpha is scaled to match
 * client opacity — otherwise the ring looks solid while the window is 0.90. */
static void border_rgba(const float src[4], float out[4]) {
	out[0] = src[0];
	out[1] = src[1];
	out[2] = src[2];
	out[3] = src[3] * STONEFLY_CLIENT_OPACITY;
}

static void apply_border_color(struct stonefly_toplevel *toplevel,
		const float *src) {
	if (toplevel->border == NULL) {
		return;
	}
	float rgba[4];
	border_rgba(src, rgba);
	wlr_scene_rect_set_color(toplevel->border, rgba);
}

void stonefly_decor_init_toplevel(struct stonefly_toplevel *toplevel) {
	const int radius = STONEFLY_CORNER_RADIUS;
	const int border = STONEFLY_BORDER_THICKNESS;
	const float sigma = STONEFLY_SHADOW_BLUR_SIGMA;

	toplevel->blur = wlr_scene_blur_create(toplevel->scene_tree, 0, 0);
	wlr_scene_blur_set_should_only_blur_bottom_layer(toplevel->blur, true);
	wlr_scene_blur_set_corner_radii(toplevel->blur, corner_radii_all(radius));

	float inactive[4];
	border_rgba(STONEFLY_BORDER_INACTIVE, inactive);
	toplevel->border = wlr_scene_rect_create(toplevel->scene_tree, 0, 0,
		inactive);
	wlr_scene_rect_set_corner_radii(toplevel->border,
		corner_radii_all(radius + border));
	wlr_scene_node_set_position(&toplevel->border->node, -border, -border);

	float shadow[4] = {
		STONEFLY_SHADOW_COLOR[0],
		STONEFLY_SHADOW_COLOR[1],
		STONEFLY_SHADOW_COLOR[2],
		STONEFLY_SHADOW_COLOR[3] * STONEFLY_CLIENT_OPACITY,
	};
	toplevel->shadow = wlr_scene_shadow_create(toplevel->scene_tree,
		0, 0, radius, sigma, shadow);
	wlr_scene_node_set_position(&toplevel->shadow->node,
		-border - (int)sigma, -border - (int)sigma);

	/* xdg content on top; blur < shadow < border < xdg. */
	wlr_scene_node_lower_to_bottom(&toplevel->border->node);
	wlr_scene_node_lower_to_bottom(&toplevel->shadow->node);
	wlr_scene_node_lower_to_bottom(&toplevel->blur->node);
}

/* Clip the subsurface tree inside wlr_scene_xdg_surface_create's wrapper.
 * set_clip on the outer tree is a no-op — only the subsurface tree accepts it. */
static void decor_clip_xdg_surface(struct stonefly_toplevel *toplevel,
		const struct wlr_box *geo) {
	struct wlr_scene_node *child;
	wl_list_for_each(child, &toplevel->xdg_scene_tree->children, link) {
		wlr_scene_subsurface_tree_set_clip(child, geo);
		SF_LOGD("decor clip",
			sf_i64("id", (int64_t)toplevel->id),
			sf_i64("child_x", child->x),
			sf_i64("child_y", child->y),
			sf_i64("geo_x", geo->x),
			sf_i64("geo_y", geo->y),
			sf_i64("geo_w", geo->width),
			sf_i64("geo_h", geo->height));
		return;
	}
	SF_LOGW("decor clip: no subsurface child",
		sf_i64("id", (int64_t)toplevel->id));
}

void stonefly_decor_update_geom(struct stonefly_toplevel *toplevel) {
	struct wlr_box *geo = &toplevel->xdg_toplevel->base->geometry;
	if (geo->width <= 0 || geo->height <= 0) {
		return;
	}

	const bool chrome = decor_chrome_on(toplevel);
	const int border = chrome ? STONEFLY_BORDER_THICKNESS : 0;
	const int radius = chrome ? STONEFLY_CORNER_RADIUS : 0;
	const float sigma = chrome ? STONEFLY_SHADOW_BLUR_SIGMA : 0.0f;

	/*
	 * Do NOT shift xdg_scene_tree by -geo. wlr_scene_xdg_surface_create
	 * already places its subsurface child at (-geo.x,-geo.y) on every
	 * commit so geometry TL sits at the xdg tree origin. Shifting the
	 * outer tree again double-applied the inset (border appeared ~geo
	 * px down-right of the client — often 15–20px on GTK CSDs).
	 */
	wlr_scene_node_set_position(&toplevel->xdg_scene_tree->node, 0, 0);
	decor_clip_xdg_surface(toplevel, geo);

	if (toplevel->blur != NULL) {
		wlr_scene_blur_set_size(toplevel->blur, geo->width, geo->height);
		wlr_scene_node_set_position(&toplevel->blur->node, 0, 0);
		wlr_scene_blur_set_corner_radii(toplevel->blur,
			corner_radii_all(radius));
	}

	int border_w = geo->width + 2 * border;
	int border_h = geo->height + 2 * border;
	if (toplevel->border != NULL) {
		wlr_scene_rect_set_size(toplevel->border, border_w, border_h);
		wlr_scene_node_set_position(&toplevel->border->node,
			-border, -border);
		wlr_scene_rect_set_corner_radii(toplevel->border,
			corner_radii_all(radius + border));
		wlr_scene_rect_set_clipped_region(toplevel->border,
			(struct clipped_region){
				.area = {
					.x = border,
					.y = border,
					.width = geo->width,
					.height = geo->height,
				},
				.corners = corner_radii_all(radius),
			});
	}

	if (toplevel->shadow != NULL) {
		int sw = border_w + 2 * (int)sigma;
		int sh = border_h + 2 * (int)sigma;
		wlr_scene_shadow_set_size(toplevel->shadow, sw, sh);
		wlr_scene_node_set_position(&toplevel->shadow->node,
			-border - (int)sigma, -border - (int)sigma);
		wlr_scene_shadow_set_clipped_region(toplevel->shadow,
			(struct clipped_region){
				.area = {
					.x = (int)sigma,
					.y = (int)sigma,
					.width = border_w,
					.height = border_h,
				},
				.corners = corner_radii_all(radius + border),
			});
	}

	struct wlr_surface *surf = toplevel->xdg_toplevel->base->surface;
	/* DEBUG: main sets SF_LOG_DEBUG — grep wm.log for "decor geom" / "decor clip". */
	SF_LOGD("decor geom",
		sf_i64("id", (int64_t)toplevel->id),
		sf_str("app_id",
			toplevel->xdg_toplevel->app_id != NULL
				? toplevel->xdg_toplevel->app_id : ""),
		sf_bool("chrome", chrome),
		sf_i64("wrap_x", toplevel->scene_tree->node.x),
		sf_i64("wrap_y", toplevel->scene_tree->node.y),
		sf_i64("xdg_x", toplevel->xdg_scene_tree->node.x),
		sf_i64("xdg_y", toplevel->xdg_scene_tree->node.y),
		sf_i64("geo_x", geo->x),
		sf_i64("geo_y", geo->y),
		sf_i64("geo_w", geo->width),
		sf_i64("geo_h", geo->height),
		sf_i64("surf_w", surf != NULL ? surf->current.width : 0),
		sf_i64("surf_h", surf != NULL ? surf->current.height : 0),
		sf_i64("border", border),
		sf_i64("border_x", toplevel->border != NULL
			? toplevel->border->node.x : 0),
		sf_i64("border_y", toplevel->border != NULL
			? toplevel->border->node.y : 0));

	stonefly_decor_apply_buffers(toplevel);
}

void stonefly_decor_refresh_border(struct stonefly_toplevel *toplevel) {
	bool focused = stonefly_focused_toplevel(toplevel->server) == toplevel;
	const float *color = (const float *)STONEFLY_BORDER_INACTIVE;
	if (toplevel->bell_flash || toplevel->urgent) {
		color = (const float *)STONEFLY_BORDER_URGENT;
	} else if (focused) {
		color = (const float *)STONEFLY_BORDER_ACTIVE;
	}
	apply_border_color(toplevel, color);
}

void stonefly_decor_set_focused(struct stonefly_toplevel *toplevel, bool focused) {
	if (focused) {
		toplevel->urgent = false;
	}
	/* Use the caller-supplied focus bit: seat focus may not have moved yet. */
	const float *color = (const float *)STONEFLY_BORDER_INACTIVE;
	if (toplevel->bell_flash || toplevel->urgent) {
		color = (const float *)STONEFLY_BORDER_URGENT;
	} else if (focused) {
		color = (const float *)STONEFLY_BORDER_ACTIVE;
	}
	apply_border_color(toplevel, color);
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

	struct wl_event_loop *loop =
		wl_display_get_event_loop(toplevel->server->wl_display);
	if (toplevel->bell_timer != NULL) {
		wl_event_source_timer_update(toplevel->bell_timer,
			STONEFLY_BELL_FLASH_MS);
		return;
	}
	toplevel->bell_timer = wl_event_loop_add_timer(loop, bell_flash_done,
		toplevel);
	if (toplevel->bell_timer != NULL) {
		wl_event_source_timer_update(toplevel->bell_timer,
			STONEFLY_BELL_FLASH_MS);
	}
}

void stonefly_decor_finish_toplevel(struct stonefly_toplevel *toplevel) {
	if (toplevel->bell_timer != NULL) {
		wl_event_source_remove(toplevel->bell_timer);
		toplevel->bell_timer = NULL;
	}
	toplevel->bell_flash = false;

	/* Wrapper owns blur/shadow/border (+ xdg child if still linked).
	 * Without this, closing a client leaves an opaque border rect behind. */
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

static void iter_pick_buffer(struct wlr_scene_buffer *buffer,
		int sx, int sy, void *user_data) {
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
	struct wlr_xdg_surface *xdg =
		wlr_xdg_surface_try_from_wlr_surface(ss->surface);
	if (xdg == NULL || xdg->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
		return;
	}
	if (wlr_subsurface_try_from_wlr_surface(ss->surface) != NULL) {
		return;
	}
	pick->scene_buffer = buffer;
	pick->buffer = buffer->buffer;
	pick->width = buffer->dst_width > 0
		? buffer->dst_width : ss->surface->current.width;
	pick->height = buffer->dst_height > 0
		? buffer->dst_height : ss->surface->current.height;
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

static void ghost_offscreen(struct stonefly_ghost *ghost,
		int *ox, int *oy) {
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

void stonefly_ghost_try_spawn(struct stonefly_toplevel *toplevel) {
	if (toplevel->minimized || toplevel->anim_hide) {
		return;
	}
	if (toplevel->xdg_scene_tree == NULL) {
		return;
	}

	struct ghost_pick pick = {0};
	wlr_scene_node_for_each_buffer(&toplevel->xdg_scene_tree->node,
		iter_pick_buffer, &pick);
	if (pick.buffer == NULL || pick.width <= 0 || pick.height <= 0) {
		return;
	}

	struct wlr_buffer *locked = wlr_buffer_lock(pick.buffer);
	if (locked == NULL) {
		return;
	}

	struct stonefly_ghost *ghost = calloc(1, sizeof(*ghost));
	if (ghost == NULL) {
		wlr_buffer_unlock(locked);
		return;
	}

	ghost->server = toplevel->server;
	ghost->locked = locked;
	ghost->width = pick.width;
	ghost->height = pick.height;

	/* xdg tree sits at (-geo.x,-geo.y) under the wrapper so geometry TL is
	 * at wrapper (0,0). Place the full-buffer ghost the same way. */
	struct wlr_box *geo = &toplevel->xdg_toplevel->base->geometry;
	int geo_x = (geo->width > 0) ? geo->x : 0;
	int geo_y = (geo->height > 0) ? geo->y : 0;
	ghost->resting_x = toplevel->scene_tree->node.x - geo_x;
	ghost->resting_y = toplevel->scene_tree->node.y - geo_y;

	ghost->tree = wlr_scene_tree_create(toplevel->server->layers.toplevel);
	ghost->buffer = wlr_scene_buffer_create(ghost->tree, locked);
	wlr_scene_buffer_set_dest_size(ghost->buffer, pick.width, pick.height);
	wlr_scene_buffer_set_opacity(ghost->buffer, STONEFLY_CLIENT_OPACITY);
	wlr_scene_buffer_set_corner_radii(ghost->buffer,
		corner_radii_all(STONEFLY_CORNER_RADIUS));
	wlr_scene_node_set_position(&ghost->tree->node,
		ghost->resting_x, ghost->resting_y);
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
	ghost->anim_dur_ns = (int64_t)STONEFLY_SLIDE_DURATION_MS * 1000000LL;
	ghost->anim_active = true;
	ghost->anim_hide = true;

	wl_list_insert(&toplevel->server->ghosts, &ghost->link);
	stonefly_anim_schedule(toplevel->server);
}

void stonefly_ghost_tick(struct stonefly_server *server,
		const struct timespec *now) {
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
