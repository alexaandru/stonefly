#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>
#include <wlr/util/edges.h>

#include "anim/anim.h"
#include "decor/decor.h"
#include "layer/layer.h"
#include "log/log.h"
#include "output/output.h"
#include "session/session.h"
#include "ssd/ssd.h"
#include "toplevel/foreign.h"
#include "wm/rules.h"
#include "wm/scratchpad.h"
#include "wm/wm.h"
#include "wm/workspace.h"

void stonefly_toplevel_layout_geom(struct stonefly_toplevel *toplevel, struct wlr_box *box) {
	struct wlr_box *geo = &toplevel->xdg_toplevel->base->geometry;
	box->x = toplevel->scene_tree->node.x;
	box->y = toplevel->scene_tree->node.y;
	box->width = geo->width;
	box->height = geo->height;
	if (box->width <= 0 || box->height <= 0) {
		box->width = toplevel->xdg_toplevel->base->surface->current.width;
		box->height = toplevel->xdg_toplevel->base->surface->current.height;
	}
}

static void stonefly_toplevel_save_geom(struct stonefly_toplevel *toplevel) {
	if (toplevel->has_saved_geom || toplevel->maximized_edges || toplevel->maximized_gaps ||
	    toplevel->fullscreen || toplevel->snapped != STONEFLY_SNAP_NONE) {
		return;
	}
	struct wlr_box geom;
	stonefly_toplevel_layout_geom(toplevel, &geom);
	/* Never remember a zero-sized box — restore would make the window vanish. */
	if (geom.width <= 0 || geom.height <= 0) {
		return;
	}
	toplevel->saved_geom = geom;
	toplevel->has_saved_geom = true;
}

bool stonefly_toplevel_output_box(struct stonefly_toplevel *toplevel, struct wlr_box *box) {
	struct stonefly_server *server = toplevel->server;
	struct wlr_box geom;
	stonefly_toplevel_layout_geom(toplevel, &geom);
	double cx = geom.x + geom.width / 2.0;
	double cy = geom.y + geom.height / 2.0;
	if (stonefly_layout_usable_at(server, cx, cy, box)) {
		return true;
	}
	return stonefly_layout_usable_at(server, server->cursor->x, server->cursor->y, box);
}

void stonefly_box_inset_gap(struct wlr_box *box, int gap) {
	const int g = gap;
	if (box->width > 2 * g) {
		box->x += g;
		box->width -= 2 * g;
	}
	if (box->height > 2 * g) {
		box->y += g;
		box->height -= 2 * g;
	}
}

/*
 * Content origin is the scene_tree; SSD is drawn above it. Gap-max / snap fill
 * usable as content — reserve titlebar height so it stays below exclusive
 * zones (Noctalia bar), matching cascade / ensure-on-screen.
 */
static void box_inset_ssd_top(struct stonefly_toplevel *toplevel, struct wlr_box *box) {
	int inset = stonefly_ssd_top_inset(toplevel);
	if (inset <= 0 || box->height <= inset) {
		return;
	}
	box->y += inset;
	box->height -= inset;
}

static void stonefly_cover_fractional_gap(struct stonefly_server *server, struct wlr_box *box) {
	/* Same class of gap as peeble at non-integer scale: grow 1 logical px. */
	float scale = stonefly_output_scale_factor(server);
	if (scale != (float)(int)scale) {
		box->width += 1;
		box->height += 1;
	}
}

/* wlroots 0.20 asserts schedule_configure only after the first commit. */
static bool xdg_surface_ready(struct stonefly_toplevel *toplevel) {
	return toplevel->xdg_toplevel->base->initialized;
}

/* Peeble tiledEdgesAll / tiledEdgesFor — CSD drops shadows/resizers on edges. */
static uint32_t tiled_edges_all(void) {
	return (uint32_t)WLR_EDGE_TOP | (uint32_t)WLR_EDGE_BOTTOM | (uint32_t)WLR_EDGE_LEFT |
	       (uint32_t)WLR_EDGE_RIGHT;
}

static uint32_t tiled_edges_for(enum stonefly_snap edge) {
	switch (edge) {
	case STONEFLY_SNAP_LEFT:
		return (uint32_t)WLR_EDGE_LEFT | (uint32_t)WLR_EDGE_TOP | (uint32_t)WLR_EDGE_BOTTOM;
	case STONEFLY_SNAP_RIGHT:
		return (uint32_t)WLR_EDGE_RIGHT | (uint32_t)WLR_EDGE_TOP |
		       (uint32_t)WLR_EDGE_BOTTOM;
	case STONEFLY_SNAP_TOP:
		return (uint32_t)WLR_EDGE_TOP | (uint32_t)WLR_EDGE_LEFT | (uint32_t)WLR_EDGE_RIGHT;
	case STONEFLY_SNAP_BOTTOM:
		return (uint32_t)WLR_EDGE_BOTTOM | (uint32_t)WLR_EDGE_LEFT |
		       (uint32_t)WLR_EDGE_RIGHT;
	case STONEFLY_SNAP_NONE:
		break;
	}
	return 0;
}

static void toplevel_set_tiled(struct stonefly_toplevel *toplevel, uint32_t edges) {
	if (!xdg_surface_ready(toplevel)) {
		return;
	}
	wlr_xdg_toplevel_set_tiled(toplevel->xdg_toplevel, edges);
}

static void stonefly_toplevel_apply_box(struct stonefly_toplevel *toplevel,
					const struct wlr_box *box) {
	stonefly_anim_cancel(toplevel, false);
	wlr_scene_node_set_position(&toplevel->scene_tree->node, box->x, box->y);
	toplevel->resting_x = box->x;
	toplevel->resting_y = box->y;
	if (!xdg_surface_ready(toplevel)) {
		return;
	}
	wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, box->width, box->height);
}

/* Snap / gap-max / edge-max share peeble ApplyManage morph. Fullscreen stays
 * instant (chrome policy). Falls back to apply_box when morph is disabled. */
static bool toplevel_try_morph_to(struct stonefly_toplevel *toplevel, const struct wlr_box *to) {
	struct wlr_box from;
	stonefly_toplevel_layout_geom(toplevel, &from);
	if (from.width > 0 && from.height > 0 && stonefly_anim_start_morph(toplevel, &from, to)) {
		return true;
	}
	stonefly_toplevel_apply_box(toplevel, to);
	return false;
}

void stonefly_toplevel_restore_saved(struct stonefly_toplevel *toplevel) {
	if (!toplevel->has_saved_geom) {
		return;
	}
	if (xdg_surface_ready(toplevel)) {
		wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, false);
		wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, false);
	}
	struct wlr_box to = toplevel->saved_geom;
	toplevel->has_saved_geom = false;
	bool was_snapped = toplevel->snapped != STONEFLY_SNAP_NONE;
	bool was_max = toplevel->maximized_edges || toplevel->maximized_gaps;
	bool was_fullscreen = toplevel->fullscreen;
	toplevel->maximized_edges = false;
	toplevel->maximized_gaps = false;
	toplevel->fullscreen = false;
	toplevel->snapped = STONEFLY_SNAP_NONE;
	stonefly_toplevel_clear_auto_place(toplevel);
	toplevel_set_tiled(toplevel, 0);
	/* Morph snap and max restores; fullscreen leave stays instant. */
	if (!was_fullscreen && (was_snapped || was_max)) {
		toplevel_try_morph_to(toplevel, &to);
	} else {
		stonefly_toplevel_apply_box(toplevel, &to);
	}
	stonefly_decor_update_geom(toplevel);
}

void stonefly_toplevel_capture_geom(struct stonefly_toplevel *toplevel, const struct wlr_box *box) {
	if (toplevel->has_saved_geom || box == NULL || box->width <= 0 || box->height <= 0) {
		return;
	}
	toplevel->saved_geom = *box;
	toplevel->has_saved_geom = true;
}

void stonefly_toplevel_apply_saved_at(struct stonefly_toplevel *toplevel, int x, int y) {
	if (!toplevel->has_saved_geom) {
		return;
	}
	struct wlr_box box = toplevel->saved_geom;
	box.x = x;
	box.y = y;
	if (xdg_surface_ready(toplevel)) {
		wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, false);
	}
	stonefly_toplevel_apply_box(toplevel, &box);
	toplevel->maximized_edges = false;
	toplevel->maximized_gaps = false;
	toplevel->snapped = STONEFLY_SNAP_NONE;
	toplevel_set_tiled(toplevel, 0);
	stonefly_decor_update_geom(toplevel);
}

void stonefly_toplevel_float_for_resize(struct stonefly_toplevel *toplevel) {
	if (toplevel == NULL) {
		return;
	}
	bool was_edges = toplevel->maximized_edges;
	bool was_gaps = toplevel->maximized_gaps;
	bool was_snap = toplevel->snapped != STONEFLY_SNAP_NONE;
	if (!was_edges && !was_gaps && !was_snap) {
		return;
	}
	if (was_edges && xdg_surface_ready(toplevel)) {
		wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, false);
	}
	toplevel->maximized_edges = false;
	toplevel->maximized_gaps = false;
	toplevel->snapped = STONEFLY_SNAP_NONE;
	toplevel->has_saved_geom = false;
	stonefly_toplevel_clear_auto_place(toplevel);
	toplevel_set_tiled(toplevel, 0);
	stonefly_decor_update_geom(toplevel);
	stonefly_foreign_toplevel_on_state_change(toplevel);
}

void stonefly_toplevel_set_max_edges(struct stonefly_toplevel *toplevel, bool want) {
	if (!xdg_surface_ready(toplevel)) {
		return;
	}
	if (want == toplevel->maximized_edges) {
		if (want) {
			/* Still send a configure so the client is not left hanging. */
			wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
		}
		return;
	}
	if (!want) {
		if (toplevel->has_saved_geom) {
			stonefly_toplevel_restore_saved(toplevel);
		} else {
			wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, false);
			toplevel->maximized_edges = false;
			toplevel->maximized_gaps = false;
			toplevel_set_tiled(toplevel, 0);
			wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
			stonefly_decor_update_geom(toplevel);
		}
		stonefly_foreign_toplevel_on_state_change(toplevel);
		return;
	}
	if (toplevel->fullscreen) {
		wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, false);
		toplevel->fullscreen = false;
	}
	struct wlr_box out;
	if (!stonefly_toplevel_output_box(toplevel, &out)) {
		wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
		return;
	}
	stonefly_toplevel_save_geom(toplevel);
	toplevel->snapped = STONEFLY_SNAP_NONE;
	toplevel->maximized_gaps = false;
	stonefly_toplevel_clear_auto_place(toplevel);
	stonefly_cover_fractional_gap(toplevel->server, &out);
	wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, true);
	toplevel->maximized_edges = true;
	toplevel_set_tiled(toplevel, tiled_edges_all());
	toplevel_try_morph_to(toplevel, &out);
	/* Drop border/shadow/corners immediately (commit may lag). Blur stays
	 * on for edge-max so quick terminal still gets backdrop blur. */
	stonefly_decor_update_geom(toplevel);
	stonefly_foreign_toplevel_on_state_change(toplevel);
	stonefly_foreign_toplevel_on_outputs_change(toplevel);
}

void stonefly_toplevel_toggle_max_edges(struct stonefly_toplevel *toplevel) {
	stonefly_toplevel_set_max_edges(toplevel, !toplevel->maximized_edges);
}

static bool toplevel_full_output_box(struct stonefly_toplevel *toplevel, struct wlr_output *hint,
				     struct wlr_box *box) {
	struct stonefly_server *server = toplevel->server;
	struct wlr_output *wlr_output = hint;
	if (wlr_output == NULL) {
		struct wlr_box geom;
		stonefly_toplevel_layout_geom(toplevel, &geom);
		double cx = geom.x + geom.width / 2.0;
		double cy = geom.y + geom.height / 2.0;
		wlr_output = wlr_output_layout_output_at(server->output_layout, cx, cy);
		if (wlr_output == NULL) {
			wlr_output = wlr_output_layout_output_at(
				server->output_layout, server->cursor->x, server->cursor->y);
		}
	}
	if (wlr_output == NULL) {
		return false;
	}
	wlr_output_layout_get_box(server->output_layout, wlr_output, box);
	return box->width > 0 && box->height > 0;
}

void stonefly_toplevel_set_fullscreen(struct stonefly_toplevel *toplevel, bool want,
				      struct wlr_output *hint) {
	if (!xdg_surface_ready(toplevel)) {
		return;
	}
	if (want == toplevel->fullscreen) {
		if (want) {
			wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
		}
		return;
	}
	if (!want) {
		wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, false);
		toplevel->fullscreen = false;
		if (toplevel->has_saved_geom) {
			stonefly_toplevel_restore_saved(toplevel);
		} else {
			wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
			stonefly_decor_update_geom(toplevel);
		}
		stonefly_foreign_toplevel_on_state_change(toplevel);
		stonefly_foreign_toplevel_on_outputs_change(toplevel);
		return;
	}

	if (toplevel->maximized_edges) {
		wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, false);
		toplevel->maximized_edges = false;
	}
	toplevel->maximized_gaps = false;
	toplevel->snapped = STONEFLY_SNAP_NONE;
	stonefly_toplevel_clear_auto_place(toplevel);
	toplevel_set_tiled(toplevel, 0);

	struct wlr_box out;
	if (!toplevel_full_output_box(toplevel, hint, &out)) {
		wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
		return;
	}
	stonefly_toplevel_save_geom(toplevel);
	wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, true);
	stonefly_toplevel_apply_box(toplevel, &out);
	toplevel->fullscreen = true;
	stonefly_decor_update_geom(toplevel);
	stonefly_foreign_toplevel_on_state_change(toplevel);
	stonefly_foreign_toplevel_on_outputs_change(toplevel);
}

void stonefly_toplevel_set_max_gaps(struct stonefly_toplevel *toplevel, bool want) {
	if (!xdg_surface_ready(toplevel)) {
		return;
	}
	if (want == toplevel->maximized_gaps) {
		if (want) {
			wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
		}
		return;
	}
	if (!want) {
		if (toplevel->has_saved_geom) {
			stonefly_toplevel_restore_saved(toplevel);
		} else {
			toplevel->maximized_gaps = false;
			wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
		}
		stonefly_foreign_toplevel_on_state_change(toplevel);
		return;
	}
	struct wlr_box out;
	if (!stonefly_toplevel_output_box(toplevel, &out)) {
		wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
		return;
	}
	stonefly_toplevel_save_geom(toplevel);
	toplevel->snapped = STONEFLY_SNAP_NONE;
	if (toplevel->maximized_edges) {
		wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, false);
		toplevel->maximized_edges = false;
	}
	/* Gap-max: WM-only — do not inform maximized / tiled (peeble). */
	toplevel_set_tiled(toplevel, 0);
	stonefly_box_inset_gap(&out, stonefly_cfg(toplevel->server)->gap_size);
	box_inset_ssd_top(toplevel, &out);
	stonefly_toplevel_clear_auto_place(toplevel);
	toplevel->maximized_gaps = true;
	toplevel_try_morph_to(toplevel, &out);
	stonefly_decor_update_geom(toplevel);
	stonefly_foreign_toplevel_on_state_change(toplevel);
	stonefly_foreign_toplevel_on_outputs_change(toplevel);
}

void stonefly_toplevel_toggle_max_gaps(struct stonefly_toplevel *toplevel) {
	stonefly_toplevel_set_max_gaps(toplevel, !toplevel->maximized_gaps);
}

/* Peeble snapBorderBox: gap between two half-snaps is exactly GapSize. */
struct wlr_box stonefly_wm_snap_box(enum stonefly_snap edge, const struct wlr_box *usable,
				    int gap) {
	const int g = gap;
	struct wlr_box u = *usable;
	int half_w = (u.width - 3 * g) / 2;
	int half_h = (u.height - 3 * g) / 2;
	int full_w = u.width - 2 * g;
	int full_h = u.height - 2 * g;
	struct wlr_box box = {0};

	switch (edge) {
	case STONEFLY_SNAP_LEFT:
		box.x = u.x + g;
		box.y = u.y + g;
		box.width = half_w;
		box.height = full_h;
		break;
	case STONEFLY_SNAP_RIGHT:
		box.x = u.x + 2 * g + half_w;
		box.y = u.y + g;
		box.width = half_w;
		box.height = full_h;
		break;
	case STONEFLY_SNAP_TOP:
		box.x = u.x + g;
		box.y = u.y + g;
		box.width = full_w;
		box.height = half_h;
		break;
	case STONEFLY_SNAP_BOTTOM:
		box.x = u.x + g;
		box.y = u.y + 2 * g + half_h;
		box.width = full_w;
		box.height = half_h;
		break;
	case STONEFLY_SNAP_NONE:
		break;
	}
	return box;
}

void stonefly_toplevel_snap_in(struct stonefly_toplevel *toplevel, enum stonefly_snap edge,
			       const struct wlr_box *usable) {
	if (edge == STONEFLY_SNAP_NONE || usable == NULL || !xdg_surface_ready(toplevel)) {
		return;
	}
	/* save_geom no-ops when has_saved_geom (drag origin / PreSnap kept). */
	stonefly_toplevel_save_geom(toplevel);
	if (toplevel->maximized_edges) {
		wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, false);
		toplevel->maximized_edges = false;
	}
	toplevel->maximized_gaps = false;
	struct wlr_box box =
		stonefly_wm_snap_box(edge, usable, stonefly_cfg(toplevel->server)->gap_size);
	if (box.width <= 0 || box.height <= 0) {
		return;
	}
	box_inset_ssd_top(toplevel, &box);
	if (box.width <= 0 || box.height <= 0) {
		return;
	}
	toplevel->snapped = edge;
	stonefly_toplevel_clear_auto_place(toplevel);
	toplevel_set_tiled(toplevel, tiled_edges_for(edge));
	/* start_morph retargets from the current live rect when already morphing. */
	toplevel_try_morph_to(toplevel, &box);
}

void stonefly_toplevel_snap(struct stonefly_toplevel *toplevel, enum stonefly_snap edge) {
	struct wlr_box out;
	if (!stonefly_toplevel_output_box(toplevel, &out)) {
		return;
	}
	stonefly_toplevel_snap_in(toplevel, edge, &out);
}

static bool toplevel_on_output(struct stonefly_toplevel *toplevel, struct stonefly_output *output) {
	struct wlr_box geom;
	stonefly_toplevel_layout_geom(toplevel, &geom);
	double cx = geom.x + geom.width / 2.0;
	double cy = geom.y + geom.height / 2.0;
	struct wlr_output *at =
		wlr_output_layout_output_at(toplevel->server->output_layout, cx, cy);
	return at == output->wlr_output;
}

static void toplevel_set_resting(struct stonefly_toplevel *toplevel, int x, int y) {
	toplevel->resting_x = x;
	toplevel->resting_y = y;
	if (toplevel->anim.active && !toplevel->anim_hide) {
		/* Retarget an in-flight appear slide (peeble RetargetShowResting). */
		toplevel->anim.to_x = x;
		toplevel->anim.to_y = y;
		return;
	}
	if (!toplevel->anim.active) {
		wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y);
	}
}

void stonefly_toplevel_ensure_on_screen(struct stonefly_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->minimized || toplevel->anim_hide) {
		return;
	}
	if (toplevel->maximized_edges || toplevel->maximized_gaps ||
	    toplevel->snapped != STONEFLY_SNAP_NONE) {
		return;
	}
	if (toplevel->server->grabbed_toplevel == toplevel) {
		return;
	}

	struct wlr_box usable;
	if (!stonefly_toplevel_output_box(toplevel, &usable)) {
		return;
	}
	struct wlr_box area = usable;
	stonefly_box_inset_gap(&area, stonefly_cfg(toplevel->server)->gap_size);
	if (area.width <= 0 || area.height <= 0) {
		return;
	}

	struct wlr_box geom;
	stonefly_toplevel_layout_geom(toplevel, &geom);
	int top_inset = stonefly_ssd_top_inset(toplevel);
	if (geom.width <= 0 || geom.height <= 0) {
		/* Size unknown: at least park the cascade origin inside usable. */
		int x = toplevel->resting_x;
		int y = toplevel->resting_y;
		if (x < area.x) {
			x = area.x;
		}
		if (y < area.y + top_inset) {
			y = area.y + top_inset;
		}
		if (x != toplevel->resting_x || y != toplevel->resting_y) {
			toplevel_set_resting(toplevel, x, y);
		}
		return;
	}

	if (geom.width > area.width || geom.height + top_inset > area.height) {
		stonefly_toplevel_set_max_gaps(toplevel, true);
		return;
	}
	int x = geom.x;
	int y = geom.y;
	if (x + geom.width > area.x + area.width) {
		x = area.x + area.width - geom.width;
	}
	if (y + geom.height > area.y + area.height) {
		y = area.y + area.height - geom.height;
	}
	if (x < area.x) {
		x = area.x;
	}
	if (y < area.y + top_inset) {
		y = area.y + top_inset;
	}
	if (x == geom.x && y == geom.y) {
		return;
	}
	toplevel_set_resting(toplevel, x, y);
}

void stonefly_wm_relayout_output(struct stonefly_output *output) {
	if (output == NULL) {
		return;
	}
	struct stonefly_server *server = output->server;
	struct stonefly_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel->minimized || !toplevel_on_output(toplevel, output)) {
			continue;
		}
		struct wlr_box out;
		if (!stonefly_output_usable_box(output, &out)) {
			continue;
		}
		if (toplevel->maximized_edges) {
			stonefly_cover_fractional_gap(toplevel->server, &out);
			stonefly_toplevel_apply_box(toplevel, &out);
		} else if (toplevel->maximized_gaps) {
			stonefly_box_inset_gap(&out, stonefly_cfg(server)->gap_size);
			box_inset_ssd_top(toplevel, &out);
			stonefly_toplevel_apply_box(toplevel, &out);
		} else if (toplevel->snapped != STONEFLY_SNAP_NONE) {
			struct wlr_box box = stonefly_wm_snap_box(toplevel->snapped, &out,
								  stonefly_cfg(server)->gap_size);
			box_inset_ssd_top(toplevel, &box);
			if (box.width > 0 && box.height > 0) {
				stonefly_toplevel_apply_box(toplevel, &box);
			}
		} else {
			/* Floaters: exclusive zone often arrives after PlaceNew. */
			stonefly_toplevel_ensure_on_screen(toplevel);
		}
	}
}

void stonefly_toplevel_unsnap(struct stonefly_toplevel *toplevel) {
	if (toplevel->snapped == STONEFLY_SNAP_NONE && !toplevel->maximized_edges &&
	    !toplevel->maximized_gaps) {
		return;
	}
	stonefly_toplevel_restore_saved(toplevel);
}

void stonefly_toplevel_clear_auto_place(struct stonefly_toplevel *toplevel) {
	if (toplevel != NULL) {
		toplevel->auto_place = false;
		toplevel->soft_place = false;
	}
}

void stonefly_toplevel_place_cascade(struct stonefly_toplevel *toplevel) {
	struct stonefly_server *server = toplevel->server;
	struct wlr_box out = {0};
	/* Prefer the usable area under the cursor for brand-new maps. */
	if (!stonefly_layout_usable_at(server, server->cursor->x, server->cursor->y, &out)) {
		if (!stonefly_toplevel_output_box(toplevel, &out)) {
			return;
		}
	}
	const struct stonefly_config *cfg = stonefly_cfg(server);
	stonefly_box_inset_gap(&out, cfg->gap_size);

	/* Count already-mapped siblings (caller inserts after place). */
	size_t n = 0;
	struct stonefly_toplevel *t;
	wl_list_for_each(t, &toplevel->server->toplevels, link) {
		if (t != toplevel && !t->minimized) {
			n++;
		}
	}
	int wrap = cfg->cascade_wrap > 0 ? cfg->cascade_wrap : 1;
	int offset = (int)(n % (size_t)wrap) * cfg->cascade_step;
	int top_inset = stonefly_ssd_top_inset(toplevel);
	int x = out.x + offset;
	int y = out.y + top_inset + offset;
	toplevel_set_resting(toplevel, x, y);
}

static struct stonefly_toplevel *toplevel_from_xdg_parent(struct wlr_xdg_toplevel *xdg) {
	if (xdg == NULL || xdg->base == NULL || xdg->base->data == NULL) {
		return NULL;
	}
	struct wlr_scene_tree *tree = xdg->base->data;
	if (tree->node.data == NULL) {
		return NULL;
	}
	struct stonefly_toplevel *t = tree->node.data;
	if (t->kind != STONEFLY_NODE_TOPLEVEL || t->xdg_toplevel != xdg) {
		return NULL;
	}
	return t;
}

/* Visible parent suitable as a centering anchor (mapped, not hidden). */
static struct stonefly_toplevel *visible_parent(struct stonefly_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->xdg_toplevel == NULL) {
		return NULL;
	}
	struct stonefly_toplevel *parent = toplevel_from_xdg_parent(toplevel->xdg_toplevel->parent);
	if (parent == NULL || parent->minimized || parent->anim_hide) {
		return NULL;
	}
	if (parent->scene_tree == NULL || !parent->scene_tree->node.enabled) {
		return NULL;
	}
	return parent;
}

static void placement_size(struct stonefly_toplevel *toplevel, int *w, int *h) {
	struct wlr_box geom;
	stonefly_toplevel_layout_geom(toplevel, &geom);
	*w = geom.width;
	*h = geom.height;
	if (*w > 0 && *h > 0) {
		return;
	}
	/* Prefer client min hints over 1×1 so late commits re-center cleanly. */
	if (toplevel->xdg_toplevel != NULL) {
		int32_t mw = toplevel->xdg_toplevel->current.min_width;
		int32_t mh = toplevel->xdg_toplevel->current.min_height;
		if (*w <= 0 && mw > 0) {
			*w = mw;
		}
		if (*h <= 0 && mh > 0) {
			*h = mh;
		}
	}
	if (*w <= 0) {
		*w = 1;
	}
	if (*h <= 0) {
		*h = 1;
	}
}

static void clamp_pos_to_area(int *x, int *y, int w, int h, const struct wlr_box *area,
			      int top_inset) {
	if (area == NULL || area->width <= 0 || area->height <= 0) {
		return;
	}
	int min_y = area->y + top_inset;
	if (*x + w > area->x + area->width) {
		*x = area->x + area->width - w;
	}
	if (*y + h > area->y + area->height) {
		*y = area->y + area->height - h;
	}
	if (*x < area->x) {
		*x = area->x;
	}
	if (*y < min_y) {
		*y = min_y;
	}
}

static bool place_on_parent(struct stonefly_toplevel *toplevel) {
	struct stonefly_toplevel *parent = visible_parent(toplevel);
	if (parent == NULL) {
		return false;
	}

	struct wlr_box parent_geom;
	stonefly_toplevel_layout_geom(parent, &parent_geom);

	struct wlr_box area = {0};
	double pcx = parent_geom.x + parent_geom.width / 2.0;
	double pcy = parent_geom.y + parent_geom.height / 2.0;
	if (!stonefly_layout_usable_at(toplevel->server, pcx, pcy, &area)) {
		if (!stonefly_toplevel_output_box(toplevel, &area)) {
			return false;
		}
	}
	stonefly_box_inset_gap(&area, stonefly_cfg(toplevel->server)->gap_size);

	struct wlr_box anchor = parent_geom;
	if (anchor.width <= 0 || anchor.height <= 0) {
		anchor = area;
	}

	int w, h;
	placement_size(toplevel, &w, &h);
	int top_inset = stonefly_ssd_top_inset(toplevel);
	int x = anchor.x + (anchor.width - w) / 2;
	int y = anchor.y + (anchor.height - h) / 2;
	clamp_pos_to_area(&x, &y, w, h, &area, top_inset);
	toplevel_set_resting(toplevel, x, y);

	/* Stack above the parent in the same scene tree when possible. */
	if (toplevel->scene_tree != NULL && parent->scene_tree != NULL &&
	    toplevel->scene_tree->node.parent == parent->scene_tree->node.parent) {
		wlr_scene_node_place_above(&toplevel->scene_tree->node, &parent->scene_tree->node);
	} else {
		wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
	}
	return true;
}

static bool place_on_usable_center(struct stonefly_toplevel *toplevel) {
	struct wlr_box area = {0};
	struct stonefly_server *server = toplevel->server;
	if (!stonefly_layout_usable_at(server, server->cursor->x, server->cursor->y, &area)) {
		if (!stonefly_toplevel_output_box(toplevel, &area)) {
			return false;
		}
	}
	stonefly_box_inset_gap(&area, stonefly_cfg(server)->gap_size);
	if (area.width <= 0 || area.height <= 0) {
		return false;
	}
	int w, h;
	placement_size(toplevel, &w, &h);
	int top_inset = stonefly_ssd_top_inset(toplevel);
	int x = area.x + (area.width - w) / 2;
	int y = area.y + (area.height - h) / 2;
	clamp_pos_to_area(&x, &y, w, h, &area, top_inset);
	toplevel_set_resting(toplevel, x, y);
	return true;
}

void stonefly_toplevel_place_transient(struct stonefly_toplevel *toplevel) {
	if (toplevel == NULL || !toplevel->auto_place || toplevel->scene_tree == NULL) {
		return;
	}
	if (toplevel->maximized_edges || toplevel->maximized_gaps ||
	    toplevel->snapped != STONEFLY_SNAP_NONE || toplevel->fullscreen) {
		return;
	}
	if (place_on_parent(toplevel)) {
		stonefly_toplevel_ensure_on_screen(toplevel);
		return;
	}
	if (stonefly_rules_wants_center(toplevel) && place_on_usable_center(toplevel)) {
		stonefly_toplevel_ensure_on_screen(toplevel);
	}
}

void stonefly_toplevel_place_new(struct stonefly_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->scene_tree == NULL) {
		return;
	}
	toplevel->auto_place = true;
	toplevel->soft_place = true;
	stonefly_rules_apply(toplevel);
	if (place_on_parent(toplevel)) {
		stonefly_toplevel_ensure_on_screen(toplevel);
		return;
	}
	if (stonefly_rules_wants_center(toplevel) && place_on_usable_center(toplevel)) {
		stonefly_toplevel_ensure_on_screen(toplevel);
		return;
	}
	stonefly_toplevel_place_cascade(toplevel);
}

/* minimize_link is either self-linked (off stack) or in server->minimized. */
static void minimize_link_detach(struct stonefly_toplevel *toplevel) {
	if (toplevel->minimize_link.next != &toplevel->minimize_link) {
		wl_list_remove(&toplevel->minimize_link);
	}
	wl_list_init(&toplevel->minimize_link);
}

static bool toplevel_focusable(const struct stonefly_toplevel *toplevel) {
	/* anim_hide: sliding out but minimized not set yet — do not re-pick. */
	return !toplevel->minimized && !toplevel->anim_hide &&
	       stonefly_toplevel_workspace_visible(toplevel);
}

void stonefly_toplevel_apply_hidden(struct stonefly_toplevel *toplevel,
				    enum stonefly_hide_kind kind) {
	struct stonefly_server *server = toplevel->server;
	const bool was_hidden = toplevel->minimized;
	const bool was_focused = stonefly_focused_toplevel(server) == toplevel;

	minimize_link_detach(toplevel);
	if (kind == STONEFLY_HIDE_LIFO) {
		wl_list_insert(&server->minimized, &toplevel->minimize_link);
	}

	if (!was_hidden) {
		toplevel->minimized = true;
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
	}

	if (was_focused) {
		if (xdg_surface_ready(toplevel)) {
			wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, false);
		}
		stonefly_decor_set_focused(toplevel, false);
		stonefly_focus_top(server);
	}
	stonefly_foreign_toplevel_on_state_change(toplevel);
}

void stonefly_toplevel_apply_shown(struct stonefly_toplevel *toplevel, bool activate) {
	if (toplevel->minimized) {
		/* LIFO unminimize lands on the output's current active workspace. */
		if (!stonefly_toplevel_is_scratchpad(toplevel)) {
			struct stonefly_output *out = NULL;
			if (toplevel->workspace != NULL) {
				out = toplevel->workspace->output;
			}
			if (out == NULL) {
				out = stonefly_output_for_toplevel(toplevel);
			}
			if (out != NULL && out->active != NULL &&
			    toplevel->workspace != out->active) {
				stonefly_workspace_assign(toplevel, out->active);
			}
		}
		toplevel->minimized = false;
		minimize_link_detach(toplevel);
		/* Enable at resting, then slide in from off-screen. */
		wlr_scene_node_set_position(&toplevel->scene_tree->node, toplevel->resting_x,
					    toplevel->resting_y);
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
		stonefly_anim_start_show(toplevel);
		stonefly_foreign_toplevel_on_state_change(toplevel);
	}
	if (activate) {
		stonefly_focus_toplevel(toplevel);
	}
}

static void minimize_now(struct stonefly_toplevel *toplevel) {
	stonefly_toplevel_apply_hidden(toplevel, STONEFLY_HIDE_LIFO);
}

void stonefly_toplevel_unminimize(struct stonefly_toplevel *toplevel) {
	stonefly_toplevel_apply_shown(toplevel, false);
}

void stonefly_focus_toplevel(struct stonefly_toplevel *toplevel) {
	/* Note: this function only deals with keyboard focus. */
	if (toplevel == NULL) {
		return;
	}
	struct stonefly_server *server = toplevel->server;
	if (stonefly_session_locked(server)) {
		return;
	}
	/* Show before reveal so LIFO unminimize can reassign to the active
	 * workspace first (otherwise reveal would switch to the old strip). */
	if (toplevel->minimized) {
		stonefly_toplevel_apply_shown(toplevel, false);
	}
	stonefly_workspace_reveal_toplevel(toplevel);
	server->exclusive_layer = NULL;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
	struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
	if (prev_surface == surface) {
		/* Already focused (e.g. taskbar click on the active window before
		 * minimize). Still raise so the request is visible. */
		wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
		wl_list_remove(&toplevel->link);
		wl_list_insert(&server->toplevels, &toplevel->link);
		return;
	}
	struct stonefly_toplevel *prev_toplevel = NULL;
	if (prev_surface) {
		/*
		 * Deactivate the previously focused surface. This lets the client know
		 * it no longer has focus and the client will repaint accordingly, e.g.
		 * stop displaying a caret.
		 */
		struct wlr_xdg_toplevel *prev_xdg =
			wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
		if (prev_xdg != NULL) {
			wlr_xdg_toplevel_set_activated(prev_xdg, false);
			struct wlr_surface *root = wlr_surface_get_root_surface(prev_surface);
			struct stonefly_toplevel *t;
			wl_list_for_each(t, &server->toplevels, link) {
				if (t->xdg_toplevel->base->surface == root) {
					stonefly_decor_set_focused(t, false);
					prev_toplevel = t;
					break;
				}
			}
		}
	}
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	/* Move the toplevel to the front */
	wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
	wl_list_remove(&toplevel->link);
	wl_list_insert(&server->toplevels, &toplevel->link);
	/* Activate the new surface */
	wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
	stonefly_decor_set_focused(toplevel, true);
	/*
	 * Tell the seat to have the keyboard enter this surface. wlroots will keep
	 * track of this and automatically send key events to the appropriate
	 * clients without additional work on your part.
	 */
	if (keyboard != NULL) {
		wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes,
					       keyboard->num_keycodes, &keyboard->modifiers);
	}
	if (prev_toplevel != NULL) {
		stonefly_foreign_toplevel_on_state_change(prev_toplevel);
	}
	stonefly_foreign_toplevel_on_state_change(toplevel);
	/* After the new window owns focus so auto_hide cannot focus_top-steal. */
	stonefly_scratchpad_on_focus_change(toplevel);
}

struct stonefly_toplevel *stonefly_focused_toplevel(struct stonefly_server *server) {
	struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
	if (focused == NULL) {
		return NULL;
	}
	struct wlr_surface *root = wlr_surface_get_root_surface(focused);
	struct stonefly_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel->xdg_toplevel->base->surface == root) {
			return toplevel;
		}
	}
	return NULL;
}

void stonefly_focus_top(struct stonefly_server *server) {
	if (stonefly_session_locked(server)) {
		return;
	}
	struct stonefly_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel_focusable(toplevel) && !stonefly_toplevel_is_scratchpad(toplevel)) {
			stonefly_focus_toplevel(toplevel);
			return;
		}
	}
	/* Fall back to a visible scratchpad if nothing else remains. */
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel_focusable(toplevel)) {
			stonefly_focus_toplevel(toplevel);
			return;
		}
	}
	wlr_seat_keyboard_notify_clear_focus(server->seat);
}

void stonefly_toplevel_minimize(struct stonefly_toplevel *toplevel) {
	if (toplevel->minimized) {
		return;
	}
	/* Mid-show reverses into a hide (peeble beginSlide). */
	if (stonefly_anim_start_hide(toplevel, minimize_now)) {
		return;
	}
	minimize_now(toplevel);
}

static void stonefly_minimize_focused(struct stonefly_server *server) {
	struct stonefly_toplevel *toplevel = stonefly_focused_toplevel(server);
	if (toplevel != NULL) {
		stonefly_toplevel_minimize(toplevel);
	}
}

static void stonefly_unminimize_most_recent(struct stonefly_server *server) {
	if (wl_list_empty(&server->minimized)) {
		return;
	}
	struct stonefly_toplevel *toplevel =
		wl_container_of(server->minimized.next, toplevel, minimize_link);
	stonefly_focus_toplevel(toplevel);
}

static void close_after_hide(struct stonefly_toplevel *toplevel) {
	wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);
}

static void stonefly_close_focused(struct stonefly_server *server) {
	struct stonefly_toplevel *toplevel = stonefly_focused_toplevel(server);
	if (toplevel == NULL) {
		return;
	}
	if (stonefly_anim_start_hide(toplevel, close_after_hide)) {
		return;
	}
	wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);
}

static void stonefly_spawn_cmd(const char *cmd) {
	if (fork() == 0) {
		execl("/bin/sh", "/bin/sh", "-c", cmd, (void *)NULL);
		_exit(1);
	}
}

static bool focus_cycle_eligible(struct stonefly_toplevel *t, bool include_minimized) {
	if (stonefly_toplevel_is_scratchpad(t) || t->anim_hide) {
		return false;
	}
	if (!include_minimized && !stonefly_toplevel_workspace_visible(t)) {
		return false;
	}
	return include_minimized || !t->minimized;
}

static void stonefly_focus_cycle(struct stonefly_server *server, bool backward,
				 bool include_minimized) {
	if (wl_list_empty(&server->toplevels)) {
		return;
	}
	struct stonefly_toplevel *current = stonefly_focused_toplevel(server);
	struct stonefly_toplevel *candidate = NULL;

	if (current == NULL) {
		wl_list_for_each(candidate, &server->toplevels, link) {
			if (focus_cycle_eligible(candidate, include_minimized)) {
				stonefly_focus_toplevel(candidate);
				return;
			}
		}
		return;
	}

	struct stonefly_toplevel *t = current;
	do {
		if (backward) {
			t = wl_container_of(t->link.next, t, link);
			if (&t->link == &server->toplevels) {
				t = wl_container_of(server->toplevels.next, t, link);
			}
		} else {
			/* Top of list is most recently focused; next is previous link. */
			t = wl_container_of(t->link.prev, t, link);
			if (&t->link == &server->toplevels) {
				t = wl_container_of(server->toplevels.prev, t, link);
			}
		}
		if (focus_cycle_eligible(t, include_minimized)) {
			stonefly_focus_toplevel(t);
			return;
		}
	} while (t != current);
}

void stonefly_run_action(struct stonefly_server *server, enum stonefly_action action,
			 const char *cmd) {
	struct stonefly_toplevel *focused = stonefly_focused_toplevel(server);
	switch (action) {
	case STONEFLY_ACTION_NONE:
		break;
	case STONEFLY_ACTION_QUIT:
		wl_display_terminate(server->wl_display);
		break;
	case STONEFLY_ACTION_CLOSE:
		stonefly_close_focused(server);
		break;
	case STONEFLY_ACTION_FOCUS_NEXT:
		stonefly_focus_cycle(server, false, false);
		break;
	case STONEFLY_ACTION_FOCUS_PREV:
		stonefly_focus_cycle(server, true, false);
		break;
	case STONEFLY_ACTION_FOCUS_NEXT_ALL:
		stonefly_focus_cycle(server, false, true);
		break;
	case STONEFLY_ACTION_FOCUS_PREV_ALL:
		stonefly_focus_cycle(server, true, true);
		break;
	case STONEFLY_ACTION_TOGGLE_MAX_EDGES:
		if (focused != NULL) {
			stonefly_toplevel_toggle_max_edges(focused);
		}
		break;
	case STONEFLY_ACTION_TOGGLE_MAX_GAPS:
		if (focused != NULL) {
			stonefly_toplevel_toggle_max_gaps(focused);
		}
		break;
	case STONEFLY_ACTION_MINIMIZE:
		stonefly_minimize_focused(server);
		break;
	case STONEFLY_ACTION_UNMINIMIZE:
		stonefly_unminimize_most_recent(server);
		break;
	case STONEFLY_ACTION_SNAP_LEFT:
		if (focused != NULL) {
			stonefly_toplevel_snap(focused, STONEFLY_SNAP_LEFT);
		}
		break;
	case STONEFLY_ACTION_SNAP_RIGHT:
		if (focused != NULL) {
			stonefly_toplevel_snap(focused, STONEFLY_SNAP_RIGHT);
		}
		break;
	case STONEFLY_ACTION_SNAP_TOP:
		if (focused != NULL) {
			stonefly_toplevel_snap(focused, STONEFLY_SNAP_TOP);
		}
		break;
	case STONEFLY_ACTION_SNAP_BOTTOM:
		if (focused != NULL) {
			stonefly_toplevel_snap(focused, STONEFLY_SNAP_BOTTOM);
		}
		break;
	case STONEFLY_ACTION_UNSNAP:
		if (focused != NULL) {
			stonefly_toplevel_unsnap(focused);
		}
		break;
	case STONEFLY_ACTION_EXEC:
		if (cmd != NULL && cmd[0] != '\0') {
			SF_LOGD("exec", sf_str("cmd", cmd));
			stonefly_spawn_cmd(cmd);
		} else {
			SF_LOGW("exec action with empty cmd");
		}
		break;
	case STONEFLY_ACTION_SCRATCH_TOGGLE:
		stonefly_scratchpad_toggle(server, (cmd != NULL && cmd[0] != '\0') ? cmd : "term");
		break;
	case STONEFLY_ACTION_SCRATCH_CLAIM:
		stonefly_scratchpad_claim_focused(server,
						  (cmd != NULL && cmd[0] != '\0') ? cmd : "term");
		break;
	case STONEFLY_ACTION_WORKSPACE_NEXT:
	case STONEFLY_ACTION_WORKSPACE_PREV: {
		struct stonefly_output *output = stonefly_output_at_cursor(server);
		if (output != NULL) {
			stonefly_workspace_switch_offset(
				output, action == STONEFLY_ACTION_WORKSPACE_NEXT ? 1 : -1);
		}
		break;
	}
	case STONEFLY_ACTION_WORKSPACE_SWITCH: {
		struct stonefly_output *output = stonefly_output_at_cursor(server);
		long v = (cmd != NULL) ? strtol(cmd, NULL, 10) : 0;
		int idx = (v > 0 && v <= (long)INT_MAX) ? (int)v : 0;
		if (output != NULL && idx > 0) {
			stonefly_workspace_switch_index(output, idx);
		}
		break;
	}
	case STONEFLY_ACTION_MOVE_TO_WORKSPACE_NEXT:
	case STONEFLY_ACTION_MOVE_TO_WORKSPACE_PREV:
		if (focused != NULL) {
			stonefly_workspace_move_offset(
				focused, action == STONEFLY_ACTION_MOVE_TO_WORKSPACE_NEXT ? 1 : -1,
				true);
		}
		break;
	case STONEFLY_ACTION_MOVE_TO_WORKSPACE: {
		long v = (cmd != NULL) ? strtol(cmd, NULL, 10) : 0;
		int idx = (v > 0 && v <= (long)INT_MAX) ? (int)v : 0;
		if (focused != NULL && idx > 0) {
			stonefly_workspace_move_index(focused, idx, true);
		}
		break;
	}
	}
}
