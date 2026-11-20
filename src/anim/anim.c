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
#include "wm/arrange.h"
#include "wm/overview.h"
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

/* Border + shadow sit past the content TL; pad so a slide-to-edge cannot leave
 * a chrome/content strip inside usable (Super+q hide finish). */
static int slide_chrome_pad(struct sf_server *server) {
	const struct sf_config *cfg = sf_cfg(server);
	int pad = cfg->border_thickness;
	int sigma = (int)(cfg->shadow_blur_sigma + 0.5f);
	if (sigma > 0) {
		pad += sigma;
	}
	return pad;
}

/* Peeble OffscreenPos: clear past the usable area along dir. */
static void offscreen_pos(struct sf_server *server, enum sf_slide_dir dir, int resting_x,
			  int resting_y, int w, int h, const struct wlr_box *area, int *ox,
			  int *oy) {
	*ox = resting_x;
	*oy = resting_y;
	int pad = slide_chrome_pad(server);

	if (area == NULL || area->width <= 0 || area->height <= 0) {
		switch (dir) {
		case SF_SLIDE_DOWN:
			*oy = resting_y - h - pad;
			break;
		case SF_SLIDE_UP:
			*oy = resting_y + h + pad;
			break;
		case SF_SLIDE_LEFT:
			*ox = resting_x + w + pad;
			break;
		case SF_SLIDE_RIGHT:
			*ox = resting_x - w - pad;
			break;
		}
		return;
	}

	switch (dir) {
	case SF_SLIDE_DOWN: {
		int y = area->y - h - pad;
		int alt = resting_y - h - pad;
		*oy = alt < y ? alt : y;
		break;
	}
	case SF_SLIDE_UP: {
		int y = area->y + area->height + pad;
		int alt = resting_y + h + pad;
		*oy = alt > y ? alt : y;
		break;
	}
	case SF_SLIDE_LEFT: {
		int x = area->x + area->width + pad;
		int alt = resting_x + w + pad;
		*ox = alt > x ? alt : x;
		break;
	}
	case SF_SLIDE_RIGHT: {
		int x = area->x - w - pad;
		int alt = resting_x - w - pad;
		*ox = alt < x ? alt : x;
		break;
	}
	}
}

/*
 * Edge slide off-screen target. strip_column keeps the legacy right inter-gap
 * peel for callers that still pass it; tiled appear/hide use popin/popout.
 */
static void slide_offscreen(struct sf_server *server, bool strip_column, int resting_x,
			    int resting_y, int w, int h, const struct wlr_box *area, int *ox,
			    int *oy) {
	if (strip_column) {
		/* Right inter-gap peel, but never short of clearing usable —
		 * left-column Super+q used to stop mid-screen with a remnant. */
		*ox = resting_x + w;
		*oy = resting_y;
		int pad = slide_chrome_pad(server);
		if (area != NULL && area->width > 0) {
			int clear = area->x + area->width + pad;
			if (clear > *ox) {
				*ox = clear;
			}
		} else {
			*ox += pad;
		}
		return;
	}
	enum sf_slide_dir dir = SF_SLIDE_DOWN;
	if (sf_cfg(server)->slide_dir_closest && area != NULL && area->width > 0 &&
	    area->height > 0) {
		dir = closest_dir(resting_x, resting_y, w, h, area);
	}
	offscreen_pos(server, dir, resting_x, resting_y, w, h, area, ox, oy);
}

static bool toplevel_strip_column(const struct sf_toplevel *toplevel) {
	return toplevel != NULL && (toplevel->arrange_managed || toplevel->column != NULL ||
				    toplevel->anim_slide_column);
}

void sf_anim_offscreen_for_box(struct sf_server *server, int resting_x, int resting_y, int w, int h,
			       bool strip_column, int *ox, int *oy) {
	struct wlr_box area;
	const struct wlr_box *area_p = NULL;
	double cx = (double)resting_x + (double)w / 2.0;
	double cy = (double)resting_y + (double)h / 2.0;
	if (sf_layout_usable_at(server, cx, cy, &area) ||
	    sf_layout_usable_at(server, server->cursor->x, server->cursor->y, &area)) {
		area_p = &area;
	}
	slide_offscreen(server, strip_column, resting_x, resting_y, w, h, area_p, ox, oy);
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

/* FIXME: move to sf.opt.anim.windows_in.scale / windows_out.scale */
#define SF_ANIM_POPIN_SCALE 0.5
#define SF_ANIM_POPOUT_SCALE 0.8

static void pop_finish(struct sf_toplevel *toplevel, bool commit) {
	bool was_hide = toplevel->anim_hide;
	sf_decor_pop_cancel_reapply(toplevel);

	/* Drop pop before clear so update_geom cannot re-stretch. Prefer the
	 * committed size when it already matches the cell (±3px) so chrome and
	 * content agree on the first settled frame. */
	if (!was_hide && toplevel->xdg_toplevel != NULL) {
		struct wlr_box geo = {0};
		sf_toplevel_layout_geom(toplevel, &geo);
		const int tol = 3;
		int dw = geo.width - toplevel->anim.to_w;
		int dh = geo.height - toplevel->anim.to_h;
		if (dw < 0) {
			dw = -dw;
		}
		if (dh < 0) {
			dh = -dh;
		}
		if (geo.width > 0 && geo.height > 0 && dw <= tol && dh <= tol) {
			toplevel->anim.to_w = geo.width;
			toplevel->anim.to_h = geo.height;
		}
	}

	toplevel->anim.pop = false;
	toplevel->anim.active = false;
	toplevel->anim.on_done = NULL;
	toplevel->anim.prop_w = 0;
	toplevel->anim.prop_h = 0;
	/* Hide: drop the tree before fade/clip restore. Leaving it enabled
	 * paints a full-opacity box at the 0.8 pose (down-right of the cell)
	 * for a frame, then unmap spawns a second close-ghost. */
	if (was_hide && toplevel->scene_tree != NULL) {
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
	}
	toplevel->anim.fade = 1.0f;
	toplevel->anim.fade_from = 1.0f;
	toplevel->anim.fade_to = 1.0f;
	/* Leave present_stretch set. update_geom nulls the clip, then installs
	 * the resting clip. Writing dest_size here parks a full-surface buffer
	 * at (geometry.x, geometry.y) — content shifts by that margin until
	 * the next commit (wlroots 0.20 set_clip early-out). */

	if (!commit || toplevel->scene_tree == NULL) {
		return;
	}

	if (was_hide) {
		/* Tree is already disabled. Still drop the pop clip/dest so show
		 * does not come back with the 0.8 buffer covering the rim. */
		sf_decor_update_geom(toplevel);
		return;
	}

	toplevel->resting_x = toplevel->anim.to_x;
	toplevel->resting_y = toplevel->anim.to_y;
	set_visual_pos(toplevel, toplevel->anim.to_x, toplevel->anim.to_y);
	wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
	sf_decor_update_geom(toplevel);
	sf_decor_apply_buffers(toplevel);
}

static void pop_apply_live(struct sf_toplevel *toplevel, int x, int y, int w, int h, float fade) {
	if (w < 1) {
		w = 1;
	}
	if (h < 1) {
		h = 1;
	}
	if (fade < 0.0f) {
		fade = 0.0f;
	}
	if (fade > 1.0f) {
		fade = 1.0f;
	}
	set_visual_pos(toplevel, x, y);
	toplevel->anim.prop_w = w;
	toplevel->anim.prop_h = h;
	toplevel->anim.fade = fade;
	sf_decor_update_geom(toplevel);
	sf_decor_apply_buffers(toplevel);
}

static bool begin_pop(struct sf_toplevel *toplevel, bool hide, sf_anim_done_fn on_done) {
	int w, h;
	/* Tiled appear must target the packed cell, not the still-catching-up
	 * commit (Ghostty/kitty: 70% height then a second “pop into place”). */
	if (!hide && toplevel->arrange_managed && toplevel->arrange_prop_w > 0 &&
	    toplevel->arrange_prop_h > 0) {
		w = toplevel->arrange_prop_w;
		h = toplevel->arrange_prop_h;
	} else {
		toplevel_size(toplevel, &w, &h);
	}
	if (w <= 0 || h <= 0) {
		return false;
	}

	/* FIXME: per-op duration belongs in sf.opt.anim; reuse slide for now. */
	int64_t dur_ns = (int64_t)sf_cfg(toplevel->server)->slide_duration_ms * 1000000LL;
	if (dur_ns <= 0) {
		return false;
	}

	int resting_x = toplevel->resting_x;
	int resting_y = toplevel->resting_y;
	double scale = hide ? SF_ANIM_POPOUT_SCALE : SF_ANIM_POPIN_SCALE;
	if (scale < 0.05) {
		scale = 0.05;
	}
	if (scale > 1.0) {
		scale = 1.0;
	}
	int small_w = (int)((double)w * scale);
	int small_h = (int)((double)h * scale);
	if (small_w < 1) {
		small_w = 1;
	}
	if (small_h < 1) {
		small_h = 1;
	}
	int small_x = resting_x + (w - small_w) / 2;
	int small_y = resting_y + (h - small_h) / 2;

	int from_x = hide ? resting_x : small_x;
	int from_y = hide ? resting_y : small_y;
	int from_w = hide ? w : small_w;
	int from_h = hide ? h : small_h;
	float fade_from = hide ? 1.0f : 0.0f;

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	if (toplevel->anim.active) {
		if (toplevel->anim.morph) {
			morph_finish(toplevel, true);
		} else if (toplevel->anim.pop) {
			/* Reverse mid-flight: keep remaining time + current pose. */
			int64_t elapsed =
				sf_timespec_ns(&now) - sf_timespec_ns(&toplevel->anim.start);
			int64_t remain = dur_ns - elapsed;
			if (remain > 0 && remain < dur_ns) {
				dur_ns = remain;
			}
			from_x = toplevel->scene_tree->node.x;
			from_y = toplevel->scene_tree->node.y;
			if (toplevel->anim.prop_w > 0 && toplevel->anim.prop_h > 0) {
				from_w = toplevel->anim.prop_w;
				from_h = toplevel->anim.prop_h;
			}
			fade_from = toplevel->anim.fade;
		} else {
			sf_anim_cancel(toplevel, true);
		}
	}

	toplevel->resting_x = resting_x;
	toplevel->resting_y = resting_y;
	toplevel->anim.pop = true;
	toplevel->anim.morph = false;
	toplevel->anim.active = true;
	toplevel->anim_hide = hide;
	toplevel->anim_slide_column = true;
	toplevel->anim.start = now;
	toplevel->anim.dur_ns = dur_ns;
	toplevel->anim.on_done = on_done;
	toplevel->anim.from_x = from_x;
	toplevel->anim.from_y = from_y;
	toplevel->anim.from_w = from_w;
	toplevel->anim.from_h = from_h;
	toplevel->anim.to_x = hide ? small_x : resting_x;
	toplevel->anim.to_y = hide ? small_y : resting_y;
	toplevel->anim.to_w = hide ? small_w : w;
	toplevel->anim.to_h = hide ? small_h : h;
	toplevel->anim.fade_from = fade_from;
	toplevel->anim.fade_to = hide ? 0.0f : 1.0f;
	toplevel->anim.fade = fade_from;
	toplevel->anim.prop_w = 0;
	toplevel->anim.prop_h = 0;

	if (hide) {
		sf_toplevel_raise(toplevel);
	}
	wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
	pop_apply_live(toplevel, from_x, from_y, from_w, from_h, fade_from);
	sf_anim_schedule(toplevel->server);
	return true;
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
	if (toplevel->anim.pop) {
		if (commit_visual && !toplevel->anim_hide) {
			toplevel->anim.to_x = toplevel->scene_tree->node.x;
			toplevel->anim.to_y = toplevel->scene_tree->node.y;
			if (toplevel->anim.prop_w > 0 && toplevel->anim.prop_h > 0) {
				toplevel->anim.to_w = toplevel->anim.prop_w;
				toplevel->anim.to_h = toplevel->anim.prop_h;
			}
		} else if (!commit_visual) {
			toplevel->anim.to_x = toplevel->resting_x;
			toplevel->anim.to_y = toplevel->resting_y;
		}
		pop_finish(toplevel, true);
		toplevel->anim_hide = false;
		toplevel->anim_slide_column = false;
		if (!commit_visual) {
			toplevel->arrange_appear_pending = false;
		}
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
	toplevel->anim_slide_column = false;
	if (!commit_visual) {
		/* Cancelled without finishing the appear slide (unmap / apply). */
		toplevel->arrange_appear_pending = false;
	}
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
	toplevel->anim.pop = false;
	toplevel->anim.fade = 1.0f;
	toplevel->anim_hide = hide;

	/* Park off-screen before any enable so chrome cannot flash at rest. */
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

	/* Tiled columns: umbriel popin at the packed cell. Floats / scratch
	 * keep the edge slide. */
	if (toplevel_strip_column(toplevel)) {
		return begin_pop(toplevel, false, NULL);
	}

	/* Logical resting — not scene node. Hide finish leaves the node parked
	 * off-screen while resting_* stays on-screen; reading node.x here made
	 * unminimize / taskbar recall slide back to the off-screen pose. */
	int resting_x = toplevel->resting_x;
	int resting_y = toplevel->resting_y;

	struct wlr_box area;
	const struct wlr_box *area_p = NULL;
	if (sf_toplevel_output_box(toplevel, &area)) {
		area_p = &area;
	}

	toplevel->anim_slide_column = false;

	int from_x, from_y;
	slide_offscreen(toplevel->server, false, resting_x, resting_y, w, h, area_p, &from_x,
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

	if (toplevel_strip_column(toplevel)) {
		return begin_pop(toplevel, true, on_done);
	}

	int resting_x = toplevel->resting_x;
	int resting_y = toplevel->resting_y;
	int from_x = toplevel->scene_tree->node.x;
	int from_y = toplevel->scene_tree->node.y;

	struct wlr_box area;
	const struct wlr_box *area_p = NULL;
	if (sf_toplevel_output_box(toplevel, &area)) {
		area_p = &area;
	}

	toplevel->anim_slide_column = false;

	int to_x, to_y;
	slide_offscreen(toplevel->server, false, resting_x, resting_y, w, h, area_p, &to_x, &to_y);

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
	toplevel->anim.pop = false;
	toplevel->anim.fade = 1.0f;
	toplevel->anim_hide = false;
	toplevel->resting_x = to->x;
	toplevel->resting_y = to->y;

	wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
	morph_apply_live(toplevel, start.x, start.y, start.width, start.height);
	sf_anim_schedule(toplevel->server);
	return true;
}

void sf_anim_sync_clock(struct sf_toplevel *toplevel, const struct sf_toplevel *leader) {
	if (toplevel == NULL || leader == NULL || !toplevel->anim.active || !leader->anim.active) {
		return;
	}
	toplevel->anim.start = leader->anim.start;
	toplevel->anim.dur_ns = leader->anim.dur_ns;
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
		} else if (toplevel->anim.pop) {
			if (toplevel->anim_hide) {
				sf_toplevel_raise(toplevel);
			}
			int x = sf_lerp_i(toplevel->anim.from_x, toplevel->anim.to_x, p);
			int y = sf_lerp_i(toplevel->anim.from_y, toplevel->anim.to_y, p);
			int w = sf_lerp_i(toplevel->anim.from_w, toplevel->anim.to_w, p);
			int h = sf_lerp_i(toplevel->anim.from_h, toplevel->anim.to_h, p);
			float fade = toplevel->anim.fade_from +
				     (toplevel->anim.fade_to - toplevel->anim.fade_from) * (float)p;
			pop_apply_live(toplevel, x, y, w, h, fade);
		} else {
			if (toplevel->anim_hide) {
				sf_toplevel_raise(toplevel);
			}
			int x = sf_lerp_i(toplevel->anim.from_x, toplevel->anim.to_x, p);
			int y = sf_lerp_i(toplevel->anim.from_y, toplevel->anim.to_y, p);
			set_visual_pos(toplevel, x, y);
		}

		/* Overflow stays active and completes on the next scheduled frame. */
		if (t >= 1.0 && ndone < sizeof(done) / sizeof(done[0])) {
			done[ndone++] = toplevel;
			toplevel->anim.active = false;
		}
	}

	for (size_t i = 0; i < ndone; i++) {
		struct sf_toplevel *t = done[i];
		sf_anim_done_fn on_done = t->anim.on_done;
		bool was_hide = t->anim_hide;
		bool was_morph = t->anim.morph;
		bool was_pop = t->anim.pop;
		t->anim.on_done = NULL;
		if (!was_morph && !was_hide) {
			t->anim_slide_column = false;
			t->arrange_appear_pending = false;
			/* Mid-pop consume may have stashed neighbor boxes. */
			if (t->workspace != NULL) {
				sf_arrange_release_deferred(t->workspace);
			}
		}
		if (was_morph) {
			morph_finish(t, true);
		} else if (was_pop) {
			/* pop_finish reads anim_hide for resting restore. */
			pop_finish(t, true);
		} else if (!was_hide) {
			set_visual_pos(t, t->resting_x, t->resting_y);
		} else if (t->scene_tree != NULL) {
			/* Hide done: disable until unmap. Offscreen TL alone can
			 * leave border/shadow/blur past usable; client close is
			 * async after close_after_hide. */
			wlr_scene_node_set_enabled(&t->scene_tree->node, false);
		}
		/* Keep anim_hide through on_done so close_after_hide → unmap
		 * skips the close-ghost (WM popout already ran). */
		if (on_done != NULL) {
			on_done(t);
		}
		t->anim_hide = false;
		if (was_hide) {
			t->anim_slide_column = false;
		}
	}

	sf_ghost_tick(server, now);
	sf_workspace_anim_tick(server, now);
	sf_overview_tick(server, now);

	if (sf_anim_busy(server) || !wl_list_empty(&server->ghosts) ||
	    sf_overview_anim_busy(server)) {
		sf_anim_schedule(server);
	}
}
