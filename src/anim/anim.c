#include <stdlib.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <scenefx/types/wlr_scene.h>

#include "anim/anim.h"
#include "config/config.h"
#include "decor/decor.h"
#include "layer/layer.h"
#include "output/output.h"
#include "server/server.h"
#include "toplevel/toplevel.h"
#include "util/math.h"
#include "util/time.h"
#include "wm/wm.h"
#include "wm/workspace.h"

enum sf_slide_dir {
	SF_SLIDE_DOWN = 0,
	SF_SLIDE_UP,
	SF_SLIDE_LEFT,
	SF_SLIDE_RIGHT,
};

/*
 * Enter from the nearest usable edge. When several edges sit near the
 * minimum gap (snap / gap-max; 2px slack for fractional-scale fudge),
 * prefer the shorter window axis: tall → left/right, wide → top/bottom.
 * Among that axis, pick the closer edge (strict <; top then left then
 * bottom then right on equal distance).
 */
static enum sf_slide_dir closest_dir(int x, int y, int w, int h, const struct wlr_box *area) {
	if (area == NULL || area->width <= 0 || area->height <= 0) {
		return SF_SLIDE_DOWN;
	}

	int64_t top = (int64_t)y - (int64_t)area->y;
	if (top < 0) {
		top = 0;
	}
	int64_t left = (int64_t)x - (int64_t)area->x;
	if (left < 0) {
		left = 0;
	}
	int64_t bottom = (int64_t)area->y + (int64_t)area->height - (int64_t)y - (int64_t)h;
	if (bottom < 0) {
		bottom = 0;
	}
	int64_t right = (int64_t)area->x + (int64_t)area->width - (int64_t)x - (int64_t)w;
	if (right < 0) {
		right = 0;
	}

	const int64_t slack = 2;
	int64_t min_d = top;
	if (left < min_d) {
		min_d = left;
	}
	if (bottom < min_d) {
		min_d = bottom;
	}
	if (right < min_d) {
		min_d = right;
	}

	bool near_top = top <= min_d + slack;
	bool near_left = left <= min_d + slack;
	bool near_bottom = bottom <= min_d + slack;
	bool near_right = right <= min_d + slack;
	bool near_v = near_top || near_bottom;
	bool near_h = near_left || near_right;

	if (near_v && near_h) {
		if (h > w) {
			/* Tall: slide sideways. */
			near_top = false;
			near_bottom = false;
		} else if (w > h) {
			/* Wide: slide to top/bottom. */
			near_left = false;
			near_right = false;
		}
		/* Square: keep all near edges; pure closest among them. */
	}

	enum sf_slide_dir dir = SF_SLIDE_DOWN;
	int64_t best = -1;
	if (near_top) {
		best = top;
		dir = SF_SLIDE_DOWN;
	}
	if (near_left && (best < 0 || left < best)) {
		best = left;
		dir = SF_SLIDE_RIGHT; /* enter from left */
	}
	if (near_bottom && (best < 0 || bottom < best)) {
		best = bottom;
		dir = SF_SLIDE_UP; /* enter from bottom */
	}
	if (near_right && (best < 0 || right < best)) {
		dir = SF_SLIDE_LEFT; /* enter from right */
	}
	return dir;
}

/* Peeble OffscreenPos: clear past the usable area along dir. */
static void offscreen_pos(enum sf_slide_dir dir, int resting_x, int resting_y, int w, int h,
			  const struct wlr_box *area, int *ox, int *oy) {
	*ox = resting_x;
	*oy = resting_y;

	if (area == NULL || area->width <= 0 || area->height <= 0) {
		switch (dir) {
		case SF_SLIDE_DOWN:
			*oy = resting_y - h;
			break;
		case SF_SLIDE_UP:
			*oy = resting_y + h;
			break;
		case SF_SLIDE_LEFT:
			*ox = resting_x + w;
			break;
		case SF_SLIDE_RIGHT:
			*ox = resting_x - w;
			break;
		}
		return;
	}

	switch (dir) {
	case SF_SLIDE_DOWN: {
		int y = area->y - h;
		int alt = resting_y - h;
		*oy = alt < y ? alt : y;
		break;
	}
	case SF_SLIDE_UP: {
		int y = area->y + area->height;
		int alt = resting_y + h;
		*oy = alt > y ? alt : y;
		break;
	}
	case SF_SLIDE_LEFT: {
		int x = area->x + area->width;
		int alt = resting_x + w;
		*ox = alt > x ? alt : x;
		break;
	}
	case SF_SLIDE_RIGHT: {
		int x = area->x - w;
		int alt = resting_x - w;
		*ox = alt < x ? alt : x;
		break;
	}
	}
}

/*
 * Tiled columns: always enter from / exit to the right, and travel only from
 * the column's right edge (inter-column gap), not the usable/screen edge.
 * Umbriel defaults windows_in to popin; we keep slide but pin the seam.
 */
static void slide_offscreen(struct sf_server *server, struct sf_toplevel *toplevel, int resting_x,
			    int resting_y, int w, int h, const struct wlr_box *area, int *ox,
			    int *oy) {
	bool tiled = toplevel != NULL && (toplevel->arrange_managed || toplevel->column != NULL);
	if (tiled) {
		/* Fully past the resting right edge = the gap / next column. */
		*ox = resting_x + w;
		*oy = resting_y;
		return;
	}
	enum sf_slide_dir dir = SF_SLIDE_DOWN;
	if (sf_cfg(server)->slide_dir_closest && area != NULL && area->width > 0 &&
	    area->height > 0) {
		dir = closest_dir(resting_x, resting_y, w, h, area);
	}
	offscreen_pos(dir, resting_x, resting_y, w, h, area, ox, oy);
}

void sf_anim_offscreen_for_box(struct sf_server *server, int resting_x, int resting_y, int w, int h,
			       int *ox, int *oy) {
	struct wlr_box area;
	const struct wlr_box *area_p = NULL;
	double cx = (double)resting_x + (double)w / 2.0;
	double cy = (double)resting_y + (double)h / 2.0;
	if (sf_layout_usable_at(server, cx, cy, &area) ||
	    sf_layout_usable_at(server, server->cursor->x, server->cursor->y, &area)) {
		area_p = &area;
	}
	/* Ghosts / box-only: closest-edge (no toplevel strip context). */
	slide_offscreen(server, NULL, resting_x, resting_y, w, h, area_p, ox, oy);
}

static void toplevel_size(struct sf_toplevel *toplevel, int *w, int *h) {
	struct wlr_box geom;
	sf_toplevel_layout_geom(toplevel, &geom);
	*w = geom.width;
	*h = geom.height;
}

static void set_visual_pos(struct sf_toplevel *toplevel, int x, int y) {
	wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y);
}

/* Peeble applyMorphRect: live position + propose_dimensions. */
static void morph_apply_live(struct sf_toplevel *toplevel, int x, int y, int w, int h) {
	if (w < 1) {
		w = 1;
	}
	if (h < 1) {
		h = 1;
	}
	set_visual_pos(toplevel, x, y);
	if (toplevel->xdg_toplevel != NULL && toplevel->xdg_toplevel->base->initialized) {
		if (toplevel->anim.prop_w != w || toplevel->anim.prop_h != h) {
			wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, w, h);
			toplevel->anim.prop_w = w;
			toplevel->anim.prop_h = h;
		}
	}
}

static void morph_finish(struct sf_toplevel *toplevel, bool commit) {
	toplevel->anim.morph = false;
	toplevel->anim.active = false;
	toplevel->anim.on_done = NULL;
	toplevel->anim.prop_w = 0;
	toplevel->anim.prop_h = 0;

	if (!commit || toplevel->scene_tree == NULL) {
		return;
	}

	toplevel->resting_x = toplevel->anim.to_x;
	toplevel->resting_y = toplevel->anim.to_y;
	set_visual_pos(toplevel, toplevel->anim.to_x, toplevel->anim.to_y);
	wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
	if (toplevel->xdg_toplevel != NULL && toplevel->xdg_toplevel->base->initialized) {
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, toplevel->anim.to_w,
					  toplevel->anim.to_h);
	}
	sf_decor_update_geom(toplevel);
	sf_decor_apply_buffers(toplevel);
}

void sf_anim_schedule(struct sf_server *server) {
	struct sf_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		wlr_output_schedule_frame(output->wlr_output);
	}
}

void sf_anim_cancel(struct sf_toplevel *toplevel, bool commit_visual) {
	if (toplevel == NULL || !toplevel->anim.active) {
		return;
	}
	if (toplevel->anim.morph) {
		if (commit_visual) {
			/* Keep whatever ApplyManage last wrote (peeble Cancel). */
			toplevel->anim.to_x = toplevel->scene_tree->node.x;
			toplevel->anim.to_y = toplevel->scene_tree->node.y;
			if (toplevel->anim.prop_w > 0 && toplevel->anim.prop_h > 0) {
				toplevel->anim.to_w = toplevel->anim.prop_w;
				toplevel->anim.to_h = toplevel->anim.prop_h;
			}
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

bool sf_anim_active(const struct sf_toplevel *toplevel) {
	return toplevel != NULL && toplevel->anim.active;
}

bool sf_anim_busy(struct sf_server *server) {
	struct sf_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel->anim.active) {
			return true;
		}
	}
	return sf_workspace_anim_busy(server);
}

static void begin_slide(struct sf_toplevel *toplevel, int from_x, int from_y, int to_x, int to_y,
			int resting_x, int resting_y, bool hide, sf_anim_done_fn on_done) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	int64_t dur_ns = (int64_t)sf_cfg(toplevel->server)->slide_duration_ms * 1000000LL;

	/* Reverse mid-flight: keep remaining time (peeble Animator.beginSlide). */
	if (toplevel->anim.active) {
		if (toplevel->anim.morph) {
			morph_finish(toplevel, true);
		} else {
			int64_t elapsed =
				sf_timespec_ns(&now) - sf_timespec_ns(&toplevel->anim.start);
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
	toplevel->anim.prop_w = 0;
	toplevel->anim.prop_h = 0;
	toplevel->anim.start = now;
	toplevel->anim.dur_ns = dur_ns;
	toplevel->anim.on_done = on_done;
	toplevel->anim.active = true;
	toplevel->anim.morph = false;
	toplevel->anim_hide = hide;

	set_visual_pos(toplevel, from_x, from_y);
	if (hide) {
		sf_toplevel_raise(toplevel);
	}
	sf_anim_schedule(toplevel->server);
}

bool sf_anim_start_show(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->scene_tree == NULL) {
		return false;
	}
	if (sf_cfg(toplevel->server)->slide_duration_ms <= 0) {
		return false;
	}

	int w, h;
	toplevel_size(toplevel, &w, &h);
	if (w <= 0 || h <= 0) {
		return false;
	}

	int resting_x = toplevel->anim.active ? toplevel->resting_x : toplevel->scene_tree->node.x;
	int resting_y = toplevel->anim.active ? toplevel->resting_y : toplevel->scene_tree->node.y;

	struct wlr_box area;
	const struct wlr_box *area_p = NULL;
	if (sf_toplevel_output_box(toplevel, &area)) {
		area_p = &area;
	}

	int from_x, from_y;
	slide_offscreen(toplevel->server, toplevel, resting_x, resting_y, w, h, area_p, &from_x,
			&from_y);

	begin_slide(toplevel, from_x, from_y, resting_x, resting_y, resting_x, resting_y, false,
		    NULL);
	return true;
}

bool sf_anim_start_hide(struct sf_toplevel *toplevel, sf_anim_done_fn on_done) {
	if (toplevel == NULL || toplevel->scene_tree == NULL) {
		return false;
	}
	if (sf_cfg(toplevel->server)->slide_duration_ms <= 0) {
		return false;
	}

	int w, h;
	toplevel_size(toplevel, &w, &h);
	if (w <= 0 || h <= 0) {
		return false;
	}

	int resting_x = toplevel->anim.active ? toplevel->resting_x : toplevel->scene_tree->node.x;
	int resting_y = toplevel->anim.active ? toplevel->resting_y : toplevel->scene_tree->node.y;
	int from_x = toplevel->scene_tree->node.x;
	int from_y = toplevel->scene_tree->node.y;

	struct wlr_box area;
	const struct wlr_box *area_p = NULL;
	if (sf_toplevel_output_box(toplevel, &area)) {
		area_p = &area;
	}

	/* Same off-screen target as show start (peeble). */
	int to_x, to_y;
	slide_offscreen(toplevel->server, toplevel, resting_x, resting_y, w, h, area_p, &to_x,
			&to_y);

	begin_slide(toplevel, from_x, from_y, to_x, to_y, resting_x, resting_y, true, on_done);
	return true;
}

bool sf_anim_start_morph(struct sf_toplevel *toplevel, const struct wlr_box *from,
			 const struct wlr_box *to) {
	if (toplevel == NULL || toplevel->scene_tree == NULL || from == NULL || to == NULL) {
		return false;
	}
	if (sf_cfg(toplevel->server)->morph_duration_ms <= 0) {
		return false;
	}
	if (from->width <= 0 || from->height <= 0 || to->width <= 0 || to->height <= 0) {
		return false;
	}
	if (from->x == to->x && from->y == to->y && from->width == to->width &&
	    from->height == to->height) {
		return false;
	}
	if (toplevel->xdg_toplevel == NULL || !toplevel->xdg_toplevel->base->initialized) {
		return false;
	}

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	int64_t dur_ns = (int64_t)sf_cfg(toplevel->server)->morph_duration_ms * 1000000LL;

	struct wlr_box start = *from;
	if (toplevel->anim.active && toplevel->anim.morph) {
		/* Retarget mid-flight: keep remaining time (peeble beginMorph). */
		int64_t elapsed = sf_timespec_ns(&now) - sf_timespec_ns(&toplevel->anim.start);
		int64_t remain = dur_ns - elapsed;
		if (remain > 0 && remain < dur_ns) {
			dur_ns = remain;
		}
		start.x = toplevel->scene_tree->node.x;
		start.y = toplevel->scene_tree->node.y;
		if (toplevel->anim.prop_w > 0 && toplevel->anim.prop_h > 0) {
			start.width = toplevel->anim.prop_w;
			start.height = toplevel->anim.prop_h;
		} else {
			double t = 1.0;
			if (toplevel->anim.dur_ns > 0) {
				t = (double)elapsed / (double)toplevel->anim.dur_ns;
			}
			double p = sf_ease_out_cubic(t);
			start.width = sf_lerp_i(toplevel->anim.from_w, toplevel->anim.to_w, p);
			start.height = sf_lerp_i(toplevel->anim.from_h, toplevel->anim.to_h, p);
		}
		if (start.width < 1) {
			start.width = 1;
		}
		if (start.height < 1) {
			start.height = 1;
		}
	} else if (toplevel->anim.active) {
		sf_anim_cancel(toplevel, true);
	}

	toplevel->anim.from_x = start.x;
	toplevel->anim.from_y = start.y;
	toplevel->anim.from_w = start.width;
	toplevel->anim.from_h = start.height;
	toplevel->anim.to_x = to->x;
	toplevel->anim.to_y = to->y;
	toplevel->anim.to_w = to->width;
	toplevel->anim.to_h = to->height;
	toplevel->anim.prop_w = 0;
	toplevel->anim.prop_h = 0;
	toplevel->anim.start = now;
	toplevel->anim.dur_ns = dur_ns;
	toplevel->anim.on_done = NULL;
	toplevel->anim.active = true;
	toplevel->anim.morph = true;
	toplevel->anim_hide = false;
	toplevel->resting_x = to->x;
	toplevel->resting_y = to->y;

	wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
	morph_apply_live(toplevel, start.x, start.y, start.width, start.height);
	sf_anim_schedule(toplevel->server);
	return true;
}

void sf_anim_tick(struct sf_server *server, const struct timespec *now) {
	struct sf_toplevel *toplevel, *tmp;
	struct sf_toplevel *done[32];
	size_t ndone = 0;

	wl_list_for_each_safe(toplevel, tmp, &server->toplevels, link) {
		if (!toplevel->anim.active) {
			continue;
		}

		int64_t elapsed = sf_timespec_ns(now) - sf_timespec_ns(&toplevel->anim.start);
		double t = 1.0;
		if (toplevel->anim.dur_ns > 0) {
			t = (double)elapsed / (double)toplevel->anim.dur_ns;
		}
		double p = sf_ease_out_cubic(t);

		if (toplevel->anim.morph) {
			int x = sf_lerp_i(toplevel->anim.from_x, toplevel->anim.to_x, p);
			int y = sf_lerp_i(toplevel->anim.from_y, toplevel->anim.to_y, p);
			int w = sf_lerp_i(toplevel->anim.from_w, toplevel->anim.to_w, p);
			int h = sf_lerp_i(toplevel->anim.from_h, toplevel->anim.to_h, p);
			morph_apply_live(toplevel, x, y, w, h);
		} else {
			if (toplevel->anim_hide) {
				sf_toplevel_raise(toplevel);
			}
			int x = sf_lerp_i(toplevel->anim.from_x, toplevel->anim.to_x, p);
			int y = sf_lerp_i(toplevel->anim.from_y, toplevel->anim.to_y, p);
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
		struct sf_toplevel *t = done[i];
		sf_anim_done_fn on_done = t->anim.on_done;
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

	sf_ghost_tick(server, now);
	sf_workspace_anim_tick(server, now);

	if (sf_anim_busy(server) || !wl_list_empty(&server->ghosts)) {
		sf_anim_schedule(server);
	}
}
