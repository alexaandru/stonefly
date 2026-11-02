#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdlib.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <scenefx/types/fx/clipped_region.h>
#include <scenefx/types/wlr_scene.h>

#include "anim/anim.h"
#include "config/hardcoded.h"
#include "decor/decor.h"
#include "output/output.h"
#include "server/server.h"
#include "wm/wm.h"
#include "xdg/xdg.h"

enum stonefly_slide_dir {
	STONEFLY_SLIDE_DOWN = 0,
	STONEFLY_SLIDE_UP,
	STONEFLY_SLIDE_LEFT,
	STONEFLY_SLIDE_RIGHT,
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

/* Peeble EaseOutCubic. */
static double ease_out_cubic(double t) {
	t = clamp01(t);
	double u = 1.0 - t;
	return 1.0 - u * u * u;
}

static int lerp_i(int a, int b, double t) {
	t = clamp01(t);
	return (int)lround((double)a + (double)(b - a) * t);
}

static int64_t timespec_ns(const struct timespec *ts) {
	return (int64_t)ts->tv_sec * 1000000000LL + (int64_t)ts->tv_nsec;
}

/* Peeble ClosestDir: enter from the nearest usable edge. */
static enum stonefly_slide_dir closest_dir(int x, int y, int w, int h,
		const struct wlr_box *area) {
	if (area == NULL || area->width <= 0 || area->height <= 0) {
		return STONEFLY_SLIDE_DOWN;
	}

	int64_t top = (int64_t)y - (int64_t)area->y;
	if (top < 0) {
		top = 0;
	}
	int64_t left = (int64_t)x - (int64_t)area->x;
	if (left < 0) {
		left = 0;
	}
	int64_t bottom = (int64_t)area->y + (int64_t)area->height
		- (int64_t)y - (int64_t)h;
	if (bottom < 0) {
		bottom = 0;
	}
	int64_t right = (int64_t)area->x + (int64_t)area->width
		- (int64_t)x - (int64_t)w;
	if (right < 0) {
		right = 0;
	}

	enum stonefly_slide_dir dir = STONEFLY_SLIDE_DOWN;
	int64_t best = top;
	if (left < best) {
		best = left;
		dir = STONEFLY_SLIDE_RIGHT; /* enter from left */
	}
	if (bottom < best) {
		best = bottom;
		dir = STONEFLY_SLIDE_UP; /* enter from bottom */
	}
	if (right < best) {
		dir = STONEFLY_SLIDE_LEFT; /* enter from right */
	}
	return dir;
}

static void resolve_dir(struct stonefly_toplevel *toplevel,
		int resting_x, int resting_y, int w, int h,
		enum stonefly_slide_dir *out) {
#if STONEFLY_SLIDE_DIR_CLOSEST
	struct wlr_box area;
	if (stonefly_toplevel_output_box(toplevel, &area)) {
		*out = closest_dir(resting_x, resting_y, w, h, &area);
		return;
	}
#endif
	(void)toplevel;
	(void)resting_x;
	(void)resting_y;
	(void)w;
	(void)h;
	*out = STONEFLY_SLIDE_DOWN;
}

/* Peeble OffscreenPos: clear past the usable area along dir. */
static void offscreen_pos(enum stonefly_slide_dir dir,
		int resting_x, int resting_y, int w, int h,
		const struct wlr_box *area, int *ox, int *oy) {
	*ox = resting_x;
	*oy = resting_y;

	if (area == NULL || area->width <= 0 || area->height <= 0) {
		switch (dir) {
		case STONEFLY_SLIDE_DOWN:
			*oy = resting_y - h;
			break;
		case STONEFLY_SLIDE_UP:
			*oy = resting_y + h;
			break;
		case STONEFLY_SLIDE_LEFT:
			*ox = resting_x + w;
			break;
		case STONEFLY_SLIDE_RIGHT:
			*ox = resting_x - w;
			break;
		}
		return;
	}

	switch (dir) {
	case STONEFLY_SLIDE_DOWN: {
		int y = area->y - h;
		int alt = resting_y - h;
		*oy = alt < y ? alt : y;
		break;
	}
	case STONEFLY_SLIDE_UP: {
		int y = area->y + area->height;
		int alt = resting_y + h;
		*oy = alt > y ? alt : y;
		break;
	}
	case STONEFLY_SLIDE_LEFT: {
		int x = area->x + area->width;
		int alt = resting_x + w;
		*ox = alt > x ? alt : x;
		break;
	}
	case STONEFLY_SLIDE_RIGHT: {
		int x = area->x - w;
		int alt = resting_x - w;
		*ox = alt < x ? alt : x;
		break;
	}
	}
}

static void toplevel_size(struct stonefly_toplevel *toplevel, int *w, int *h) {
	struct wlr_box geom;
	stonefly_toplevel_layout_geom(toplevel, &geom);
	*w = geom.width;
	*h = geom.height;
}

static void set_visual_pos(struct stonefly_toplevel *toplevel, int x, int y) {
	wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y);
}

static void morph_destroy_overlay(struct stonefly_toplevel *toplevel) {
	if (toplevel->anim.morph_locked != NULL) {
		wlr_buffer_unlock(toplevel->anim.morph_locked);
		toplevel->anim.morph_locked = NULL;
	}
	if (toplevel->anim.morph_tree != NULL) {
		wlr_scene_node_destroy(&toplevel->anim.morph_tree->node);
		toplevel->anim.morph_tree = NULL;
	}
	toplevel->anim.morph_buffer = NULL;
	toplevel->anim.morph_border = NULL;
	toplevel->anim.morph_shadow = NULL;
}

/* Place snapshot + SceneFX chrome so geometry TL is at (box_x, box_y). */
static void morph_apply_visual(struct stonefly_toplevel *toplevel,
		int box_x, int box_y, int box_w, int box_h) {
	if (toplevel->anim.morph_tree == NULL ||
			toplevel->anim.capture_w <= 0 || toplevel->anim.capture_h <= 0) {
		return;
	}

	double sx = (double)box_w / (double)toplevel->anim.capture_w;
	double sy = (double)box_h / (double)toplevel->anim.capture_h;
	int dest_w = (int)lround((double)toplevel->anim.buf_w * sx);
	int dest_h = (int)lround((double)toplevel->anim.buf_h * sy);
	if (dest_w < 1) {
		dest_w = 1;
	}
	if (dest_h < 1) {
		dest_h = 1;
	}
	int off_x = (int)lround((double)toplevel->anim.geo_x * sx);
	int off_y = (int)lround((double)toplevel->anim.geo_y * sy);

	wlr_scene_node_set_position(&toplevel->anim.morph_tree->node, box_x, box_y);
	if (toplevel->anim.morph_buffer != NULL) {
		wlr_scene_buffer_set_dest_size(toplevel->anim.morph_buffer,
			dest_w, dest_h);
		wlr_scene_node_set_position(&toplevel->anim.morph_buffer->node,
			-off_x, -off_y);
	}

	const int border = STONEFLY_BORDER_THICKNESS;
	const int radius = STONEFLY_CORNER_RADIUS;
	const float sigma = STONEFLY_SHADOW_BLUR_SIGMA;
	struct fx_corner_radii corners = corner_radii_all(radius);

	if (toplevel->anim.morph_border != NULL) {
		int bw = box_w + 2 * border;
		int bh = box_h + 2 * border;
		wlr_scene_rect_set_size(toplevel->anim.morph_border, bw, bh);
		wlr_scene_node_set_position(&toplevel->anim.morph_border->node,
			-border, -border);
		wlr_scene_rect_set_corner_radii(toplevel->anim.morph_border, corners);
		wlr_scene_rect_set_clipped_region(toplevel->anim.morph_border,
			(struct clipped_region){
				.area = {
					.x = border,
					.y = border,
					.width = box_w,
					.height = box_h,
				},
				.corners = corners,
			});
	}

	if (toplevel->anim.morph_shadow != NULL) {
		int sw = box_w + 2 * border + 2 * (int)sigma;
		int sh = box_h + 2 * border + 2 * (int)sigma;
		wlr_scene_shadow_set_size(toplevel->anim.morph_shadow, sw, sh);
		wlr_scene_node_set_position(&toplevel->anim.morph_shadow->node,
			-border - (int)sigma, -border - (int)sigma);
		wlr_scene_shadow_set_clipped_region(toplevel->anim.morph_shadow,
			(struct clipped_region){
				.area = {
					.x = (int)sigma,
					.y = (int)sigma,
					.width = box_w + 2 * border,
					.height = box_h + 2 * border,
				},
				.corners = corners,
			});
	}

	wlr_scene_node_raise_to_top(&toplevel->anim.morph_tree->node);
}

static void morph_finish(struct stonefly_toplevel *toplevel, bool show_real) {
	morph_destroy_overlay(toplevel);
	toplevel->anim.morph = false;
	toplevel->anim.active = false;
	toplevel->anim.on_done = NULL;

	if (!show_real || toplevel->scene_tree == NULL) {
		return;
	}

	toplevel->resting_x = toplevel->anim.to_x;
	toplevel->resting_y = toplevel->anim.to_y;
	set_visual_pos(toplevel, toplevel->anim.to_x, toplevel->anim.to_y);
	wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
	if (toplevel->xdg_toplevel != NULL) {
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
			toplevel->anim.to_w, toplevel->anim.to_h);
	}
	stonefly_decor_update_geom(toplevel);
	stonefly_decor_apply_buffers(toplevel);
}

void stonefly_anim_schedule(struct stonefly_server *server) {
	struct stonefly_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		wlr_output_schedule_frame(output->wlr_output);
	}
}

void stonefly_anim_cancel(struct stonefly_toplevel *toplevel, bool commit_visual) {
	if (toplevel == NULL || !toplevel->anim.active) {
		return;
	}
	if (toplevel->anim.morph) {
		if (commit_visual) {
			/* Keep whatever frame the overlay last showed as resting. */
			toplevel->resting_x = toplevel->anim.morph_tree != NULL
				? toplevel->anim.morph_tree->node.x : toplevel->anim.to_x;
			toplevel->resting_y = toplevel->anim.morph_tree != NULL
				? toplevel->anim.morph_tree->node.y : toplevel->anim.to_y;
			/* Jump real window to destination size/pos (overlay goes away). */
			toplevel->anim.to_x = toplevel->resting_x;
			toplevel->anim.to_y = toplevel->resting_y;
		}
		morph_finish(toplevel, true);
		toplevel->anim_hide = false;
		return;
	}
	if (commit_visual) {
		toplevel->resting_x = toplevel->scene_tree->node.x;
		toplevel->resting_y = toplevel->scene_tree->node.y;
	} else {
		set_visual_pos(toplevel, toplevel->resting_x, toplevel->resting_y);
	}
	toplevel->anim.active = false;
	toplevel->anim.on_done = NULL;
	toplevel->anim_hide = false;
}

bool stonefly_anim_active(const struct stonefly_toplevel *toplevel) {
	return toplevel != NULL && toplevel->anim.active;
}

bool stonefly_anim_busy(struct stonefly_server *server) {
	struct stonefly_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel->anim.active) {
			return true;
		}
	}
	return false;
}

static void begin_slide(struct stonefly_toplevel *toplevel,
		int from_x, int from_y, int to_x, int to_y,
		int resting_x, int resting_y, bool hide,
		stonefly_anim_done_fn on_done) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	int64_t dur_ns = (int64_t)STONEFLY_SLIDE_DURATION_MS * 1000000LL;

	/* Reverse mid-flight: keep remaining time (peeble Animator.beginSlide). */
	if (toplevel->anim.active) {
		if (toplevel->anim.morph) {
			morph_finish(toplevel, true);
		} else {
			int64_t elapsed = timespec_ns(&now) - timespec_ns(&toplevel->anim.start);
			int64_t remain = dur_ns - elapsed;
			if (remain > 0 && remain < dur_ns) {
				dur_ns = remain;
			}
			from_x = toplevel->scene_tree->node.x;
			from_y = toplevel->scene_tree->node.y;
		}
	}

	toplevel->resting_x = resting_x;
	toplevel->resting_y = resting_y;
	toplevel->anim.from_x = from_x;
	toplevel->anim.from_y = from_y;
	toplevel->anim.to_x = to_x;
	toplevel->anim.to_y = to_y;
	toplevel->anim.from_w = 0;
	toplevel->anim.from_h = 0;
	toplevel->anim.to_w = 0;
	toplevel->anim.to_h = 0;
	toplevel->anim.start = now;
	toplevel->anim.dur_ns = dur_ns;
	toplevel->anim.on_done = on_done;
	toplevel->anim.active = true;
	toplevel->anim.morph = false;
	toplevel->anim_hide = hide;

	set_visual_pos(toplevel, from_x, from_y);
	if (hide) {
		wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
	}
	stonefly_anim_schedule(toplevel->server);
}

bool stonefly_anim_start_show(struct stonefly_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->scene_tree == NULL) {
		return false;
	}
	if (STONEFLY_SLIDE_DURATION_MS <= 0) {
		return false;
	}

	int w, h;
	toplevel_size(toplevel, &w, &h);
	if (w <= 0 || h <= 0) {
		return false;
	}

	int resting_x = toplevel->anim.active
		? toplevel->resting_x : toplevel->scene_tree->node.x;
	int resting_y = toplevel->anim.active
		? toplevel->resting_y : toplevel->scene_tree->node.y;

	enum stonefly_slide_dir dir;
	resolve_dir(toplevel, resting_x, resting_y, w, h, &dir);

	struct wlr_box area;
	const struct wlr_box *area_p = NULL;
	if (stonefly_toplevel_output_box(toplevel, &area)) {
		area_p = &area;
	}

	int from_x, from_y;
	offscreen_pos(dir, resting_x, resting_y, w, h, area_p, &from_x, &from_y);

	begin_slide(toplevel, from_x, from_y, resting_x, resting_y,
		resting_x, resting_y, false, NULL);
	return true;
}

bool stonefly_anim_start_hide(struct stonefly_toplevel *toplevel,
		stonefly_anim_done_fn on_done) {
	if (toplevel == NULL || toplevel->scene_tree == NULL) {
		return false;
	}
	if (STONEFLY_SLIDE_DURATION_MS <= 0) {
		return false;
	}

	int w, h;
	toplevel_size(toplevel, &w, &h);
	if (w <= 0 || h <= 0) {
		return false;
	}

	int resting_x = toplevel->anim.active
		? toplevel->resting_x : toplevel->scene_tree->node.x;
	int resting_y = toplevel->anim.active
		? toplevel->resting_y : toplevel->scene_tree->node.y;
	int from_x = toplevel->scene_tree->node.x;
	int from_y = toplevel->scene_tree->node.y;

	enum stonefly_slide_dir show_dir;
	resolve_dir(toplevel, resting_x, resting_y, w, h, &show_dir);
	/* Hide uses the same off-screen target as show start (peeble). */
	enum stonefly_slide_dir dir = show_dir;

	struct wlr_box area;
	const struct wlr_box *area_p = NULL;
	if (stonefly_toplevel_output_box(toplevel, &area)) {
		area_p = &area;
	}

	int to_x, to_y;
	offscreen_pos(dir, resting_x, resting_y, w, h, area_p, &to_x, &to_y);

	begin_slide(toplevel, from_x, from_y, to_x, to_y,
		resting_x, resting_y, true, on_done);
	return true;
}

bool stonefly_anim_start_morph(struct stonefly_toplevel *toplevel,
		const struct wlr_box *from, const struct wlr_box *to) {
	if (toplevel == NULL || toplevel->scene_tree == NULL ||
			from == NULL || to == NULL) {
		return false;
	}
	if (STONEFLY_MORPH_DURATION_MS <= 0) {
		return false;
	}
	if (from->width <= 0 || from->height <= 0 ||
			to->width <= 0 || to->height <= 0) {
		return false;
	}
	if (from->x == to->x && from->y == to->y &&
			from->width == to->width && from->height == to->height) {
		return false;
	}

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	int64_t dur_ns = (int64_t)STONEFLY_MORPH_DURATION_MS * 1000000LL;

	struct wlr_box start = *from;
	if (toplevel->anim.active && toplevel->anim.morph &&
			toplevel->anim.morph_tree != NULL) {
		/* Retarget mid-flight: keep snapshot, shorten remaining time. */
		int64_t elapsed = timespec_ns(&now) - timespec_ns(&toplevel->anim.start);
		int64_t remain = dur_ns - elapsed;
		if (remain > 0 && remain < dur_ns) {
			dur_ns = remain;
		}
		start.x = toplevel->anim.morph_tree->node.x;
		start.y = toplevel->anim.morph_tree->node.y;
		/* Approximate current size from last lerp progress. */
		double t = 1.0;
		if (toplevel->anim.dur_ns > 0) {
			t = (double)elapsed / (double)toplevel->anim.dur_ns;
		}
		double p = ease_out_cubic(t);
		start.width = lerp_i(toplevel->anim.from_w, toplevel->anim.to_w, p);
		start.height = lerp_i(toplevel->anim.from_h, toplevel->anim.to_h, p);
		if (start.width < 1) {
			start.width = 1;
		}
		if (start.height < 1) {
			start.height = 1;
		}

		toplevel->anim.from_x = start.x;
		toplevel->anim.from_y = start.y;
		toplevel->anim.from_w = start.width;
		toplevel->anim.from_h = start.height;
		toplevel->anim.to_x = to->x;
		toplevel->anim.to_y = to->y;
		toplevel->anim.to_w = to->width;
		toplevel->anim.to_h = to->height;
		toplevel->anim.start = now;
		toplevel->anim.dur_ns = dur_ns;
		toplevel->resting_x = to->x;
		toplevel->resting_y = to->y;
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, to->width, to->height);
		morph_apply_visual(toplevel, start.x, start.y, start.width, start.height);
		stonefly_anim_schedule(toplevel->server);
		return true;
	}

	if (toplevel->anim.active) {
		stonefly_anim_cancel(toplevel, true);
	}

	struct wlr_buffer *locked = NULL;
	int buf_w = 0, buf_h = 0;
	if (!stonefly_decor_lock_snapshot(toplevel, &locked, &buf_w, &buf_h)) {
		return false;
	}

	struct wlr_box *geo = &toplevel->xdg_toplevel->base->geometry;
	int geo_x = (geo->width > 0) ? geo->x : 0;
	int geo_y = (geo->height > 0) ? geo->y : 0;

	struct wlr_scene_tree *tree =
		wlr_scene_tree_create(toplevel->server->layers.toplevel);
	if (tree == NULL) {
		wlr_buffer_unlock(locked);
		return false;
	}

	struct wlr_scene_buffer *buffer = wlr_scene_buffer_create(tree, locked);
	if (buffer == NULL) {
		wlr_scene_node_destroy(&tree->node);
		wlr_buffer_unlock(locked);
		return false;
	}
	wlr_scene_buffer_set_opacity(buffer, STONEFLY_CLIENT_OPACITY);
	wlr_scene_buffer_set_corner_radii(buffer,
		corner_radii_all(STONEFLY_CORNER_RADIUS));

	float border_rgba[4];
	const float *border_src = STONEFLY_BORDER_ACTIVE;
	float a = border_src[3] * STONEFLY_CLIENT_OPACITY;
	border_rgba[0] = border_src[0] * a;
	border_rgba[1] = border_src[1] * a;
	border_rgba[2] = border_src[2] * a;
	border_rgba[3] = a;
	struct wlr_scene_rect *border = wlr_scene_rect_create(tree, 0, 0, border_rgba);

	float shadow_a = STONEFLY_SHADOW_COLOR[3] * STONEFLY_CLIENT_OPACITY;
	float shadow_rgba[4] = {
		STONEFLY_SHADOW_COLOR[0] * shadow_a,
		STONEFLY_SHADOW_COLOR[1] * shadow_a,
		STONEFLY_SHADOW_COLOR[2] * shadow_a,
		shadow_a,
	};
	struct wlr_scene_shadow *shadow = wlr_scene_shadow_create(tree,
		0, 0, STONEFLY_CORNER_RADIUS, STONEFLY_SHADOW_BLUR_SIGMA, shadow_rgba);

	/* Content above chrome. */
	if (shadow != NULL) {
		wlr_scene_node_lower_to_bottom(&shadow->node);
	}
	if (border != NULL) {
		wlr_scene_node_lower_to_bottom(&border->node);
		if (shadow != NULL) {
			wlr_scene_node_lower_to_bottom(&shadow->node);
		}
	}

	toplevel->anim.morph_tree = tree;
	toplevel->anim.morph_buffer = buffer;
	toplevel->anim.morph_border = border;
	toplevel->anim.morph_shadow = shadow;
	toplevel->anim.morph_locked = locked;
	toplevel->anim.capture_w = from->width;
	toplevel->anim.capture_h = from->height;
	toplevel->anim.buf_w = buf_w;
	toplevel->anim.buf_h = buf_h;
	toplevel->anim.geo_x = geo_x;
	toplevel->anim.geo_y = geo_y;
	toplevel->anim.from_x = start.x;
	toplevel->anim.from_y = start.y;
	toplevel->anim.from_w = start.width;
	toplevel->anim.from_h = start.height;
	toplevel->anim.to_x = to->x;
	toplevel->anim.to_y = to->y;
	toplevel->anim.to_w = to->width;
	toplevel->anim.to_h = to->height;
	toplevel->anim.start = now;
	toplevel->anim.dur_ns = dur_ns;
	toplevel->anim.on_done = NULL;
	toplevel->anim.active = true;
	toplevel->anim.morph = true;
	toplevel->anim_hide = false;
	toplevel->resting_x = to->x;
	toplevel->resting_y = to->y;

	/* Hide the live window; client resizes toward the target off-screen. */
	wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
	wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, to->width, to->height);

	morph_apply_visual(toplevel, start.x, start.y, start.width, start.height);
	stonefly_anim_schedule(toplevel->server);
	return true;
}

void stonefly_anim_tick(struct stonefly_server *server,
		const struct timespec *now) {
	struct stonefly_toplevel *toplevel, *tmp;
	struct stonefly_toplevel *done[32];
	size_t ndone = 0;

	wl_list_for_each_safe(toplevel, tmp, &server->toplevels, link) {
		if (!toplevel->anim.active) {
			continue;
		}

		int64_t elapsed = timespec_ns(now) - timespec_ns(&toplevel->anim.start);
		double t = 1.0;
		if (toplevel->anim.dur_ns > 0) {
			t = (double)elapsed / (double)toplevel->anim.dur_ns;
		}
		double p = ease_out_cubic(t);

		if (toplevel->anim.morph) {
			int x = lerp_i(toplevel->anim.from_x, toplevel->anim.to_x, p);
			int y = lerp_i(toplevel->anim.from_y, toplevel->anim.to_y, p);
			int w = lerp_i(toplevel->anim.from_w, toplevel->anim.to_w, p);
			int h = lerp_i(toplevel->anim.from_h, toplevel->anim.to_h, p);
			if (w < 1) {
				w = 1;
			}
			if (h < 1) {
				h = 1;
			}
			morph_apply_visual(toplevel, x, y, w, h);
		} else {
			if (toplevel->anim_hide) {
				wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
			}
			int x = lerp_i(toplevel->anim.from_x, toplevel->anim.to_x, p);
			int y = lerp_i(toplevel->anim.from_y, toplevel->anim.to_y, p);
			set_visual_pos(toplevel, x, y);
		}

		if (t >= 1.0) {
			if (ndone < sizeof(done) / sizeof(done[0])) {
				done[ndone++] = toplevel;
			}
			toplevel->anim.active = false;
		}
	}

	for (size_t i = 0; i < ndone; i++) {
		struct stonefly_toplevel *t = done[i];
		stonefly_anim_done_fn on_done = t->anim.on_done;
		bool was_hide = t->anim_hide;
		bool was_morph = t->anim.morph;
		t->anim.on_done = NULL;
		t->anim_hide = false;
		if (was_morph) {
			morph_finish(t, true);
		} else if (!was_hide) {
			set_visual_pos(t, t->resting_x, t->resting_y);
		}
		if (on_done != NULL) {
			on_done(t);
		}
	}

	stonefly_ghost_tick(server, now);

	if (stonefly_anim_busy(server) || !wl_list_empty(&server->ghosts)) {
		stonefly_anim_schedule(server);
	}
}
