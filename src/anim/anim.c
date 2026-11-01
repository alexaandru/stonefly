#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdlib.h>
#include <wlr/types/wlr_output.h>
#include <scenefx/types/wlr_scene.h>

#include "anim/anim.h"
#include "config/hardcoded.h"
#include "decor/decor.h"
#include "output/output.h"
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
		int64_t elapsed = timespec_ns(&now) - timespec_ns(&toplevel->anim.start);
		int64_t remain = dur_ns - elapsed;
		if (remain > 0 && remain < dur_ns) {
			dur_ns = remain;
		}
		from_x = toplevel->scene_tree->node.x;
		from_y = toplevel->scene_tree->node.y;
	}

	toplevel->resting_x = resting_x;
	toplevel->resting_y = resting_y;
	toplevel->anim.from_x = from_x;
	toplevel->anim.from_y = from_y;
	toplevel->anim.to_x = to_x;
	toplevel->anim.to_y = to_y;
	toplevel->anim.start = now;
	toplevel->anim.dur_ns = dur_ns;
	toplevel->anim.on_done = on_done;
	toplevel->anim.active = true;
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

void stonefly_anim_tick(struct stonefly_server *server,
		const struct timespec *now) {
	struct stonefly_toplevel *toplevel, *tmp;
	struct stonefly_toplevel *done[32];
	size_t ndone = 0;

	wl_list_for_each_safe(toplevel, tmp, &server->toplevels, link) {
		if (!toplevel->anim.active) {
			continue;
		}
		if (toplevel->anim_hide) {
			wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
		}

		int64_t elapsed = timespec_ns(now) - timespec_ns(&toplevel->anim.start);
		double t = 1.0;
		if (toplevel->anim.dur_ns > 0) {
			t = (double)elapsed / (double)toplevel->anim.dur_ns;
		}
		double p = ease_out_cubic(t);
		int x = lerp_i(toplevel->anim.from_x, toplevel->anim.to_x, p);
		int y = lerp_i(toplevel->anim.from_y, toplevel->anim.to_y, p);
		set_visual_pos(toplevel, x, y);

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
		t->anim.on_done = NULL;
		t->anim_hide = false;
		if (!was_hide) {
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
